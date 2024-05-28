/****************************************************************************
 * mm/mempool/mempool.c
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <syslog.h>

#include <nuttx/kmalloc.h>
#include <nuttx/mm/kasan.h>
#include <nuttx/mm/mempool.h>
#include <nuttx/sched.h>

#undef  ALIGN_UP
#define ALIGN_UP(x, a) (((x) + ((a) - 1)) & (~((a) - 1)))

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static inline FAR sq_entry_t *
mempool_remove_queue(FAR struct mempool_s *pool, FAR sq_queue_t *queue)
{
  FAR sq_entry_t *ret = queue->head;

  if (ret)
    {
      queue->head = ret->flink;
      if (!queue->head)
        {
          queue->tail = NULL;
        }
      else
        {
          pool->check(pool, queue->head);
        }

      ret->flink = NULL;
    }

  return ret;
}

static inline void mempool_add_queue(FAR sq_queue_t *queue,
                                     FAR char *base, size_t nblks,
                                     size_t blocksize)
{
  while (nblks-- > 0)
    {
      sq_addlast((FAR sq_entry_t *)(base + blocksize * nblks), queue);
    }
}

static inline int mempool_lock(FAR struct mempool_s *pool)
{
  if (pool->ibase)
    {
      return spin_lock_irqsave(&pool->u.lock);
    }
  else
    {
      if (_SCHED_GETTID() < 0)
        {
          return -ESRCH;
        }

      return nxmutex_lock(&pool->u.mutex);
    }
}

static inline void mempool_unlock(FAR struct mempool_s *pool,
                                  int flags)
{
  if (pool->ibase)
    {
      spin_unlock_irqrestore(&pool->u.lock, flags);
    }
  else
    {
      nxmutex_unlock(&pool->u.mutex);
    }
}

#if CONFIG_MM_BACKTRACE >= 0
static inline void mempool_add_backtrace(FAR struct mempool_s *pool,
                                         FAR struct mempool_backtrace_s *buf)
{
  int flags = mempool_lock(pool);
  list_add_head(&pool->alist, &buf->node);
  mempool_unlock(pool, flags);

  buf->pid = _SCHED_GETTID();
  buf->seqno = g_mm_seqno++;
#  if CONFIG_MM_BACKTRACE > 0
  if (pool->procfs.backtrace)
    {
      int result = sched_backtrace(buf->pid, buf->backtrace,
                                   CONFIG_MM_BACKTRACE,
                                   CONFIG_MM_HEAP_MEMPOOL_BACKTRACE_SKIP);
      if (result < CONFIG_MM_BACKTRACE)
        {
          buf->backtrace[result] = NULL;
        }
    }
  else
    {
      buf->backtrace[0] = NULL;
    }
#  endif
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: mempool_init
 *
 * Description:
 *   Initialize a memory pool.
 *   The user needs to specify the initialization information of mempool
 *   including blocksize, initialsize, expandsize, interruptsize.
 *
 * Input Parameters:
 *   pool - Address of the memory pool to be used.
 *   name - The name of memory pool.
 *
 * Returned Value:
 *   Zero on success; A negated errno value is returned on any failure.
 *
 ****************************************************************************/

int mempool_init(FAR struct mempool_s *pool, FAR const char *name)
{
  size_t blocksize = MEMPOOL_REALBLOCKSIZE(pool);

  sq_init(&pool->queue);
  sq_init(&pool->iqueue);
  sq_init(&pool->equeue);

#if CONFIG_MM_BACKTRACE >= 0
  list_initialize(&pool->alist);
#else
  pool->nalloc = 0;
#endif

  if (pool->interruptsize >= blocksize)
    {
      size_t ninterrupt = pool->interruptsize / blocksize;
      size_t size = ninterrupt * blocksize;

      pool->ibase = pool->alloc(pool, size);
      if (pool->ibase == NULL)
        {
          return -ENOMEM;
        }

      mempool_add_queue(&pool->iqueue, pool->ibase, ninterrupt, blocksize);
      kasan_poison(pool->ibase, size);
      spin_initialize(&pool->u.lock, SP_UNLOCKED);
    }
  else
    {
      pool->ibase = NULL;
      nxmutex_init(&pool->u.mutex);
    }

  if (pool->initialsize >= blocksize + sizeof(sq_entry_t))
    {
      size_t ninitial = (pool->initialsize - sizeof(sq_entry_t)) / blocksize;
      size_t size = ninitial * blocksize + sizeof(sq_entry_t);
      FAR char *base;

      base = pool->alloc(pool, size);
      if (base == NULL)
        {
          if (pool->ibase)
            {
              pool->free(pool, pool->ibase);
            }

          return -ENOMEM;
        }

      mempool_add_queue(&pool->queue, base, ninitial, blocksize);
      sq_addlast((FAR sq_entry_t *)(base + ninitial * blocksize),
                  &pool->equeue);
      kasan_poison(base, size);
    }

  if (pool->wait && pool->expandsize == 0)
    {
      nxsem_init(&pool->waitsem, 0, 0);
    }

#if defined(CONFIG_FS_PROCFS) && !defined(CONFIG_FS_PROCFS_EXCLUDE_MEMPOOL)
  mempool_procfs_register(&pool->procfs, name);
#  ifdef CONFIG_MM_BACKTRACE_DEFAULT
  pool->procfs.backtrace = true;
#  elif CONFIG_MM_BACKTRACE > 0
  pool->procfs.backtrace = false;
#  endif
#endif

  return 0;
}

/****************************************************************************
 * Name: mempool_allocate
 *
 * Description:
 *   Allocate an block from a specific memory pool.
 *
 *   If there isn't enough memory blocks, This function will expand memory
 *   pool if expandsize isn't zero.
 *
 * Input Parameters:
 *   pool - Address of the memory pool to be used.
 *
 * Returned Value:
 *   The pointer to the allocated block on success; NULL on any failure.
 *
 ****************************************************************************/

FAR void *mempool_allocate(FAR struct mempool_s *pool)
{
  FAR sq_entry_t *blk;
  int flags;

retry:
  flags = mempool_lock(pool);
  blk = mempool_remove_queue(pool, &pool->queue);
  if (blk == NULL)
    {
      if (up_interrupt_context())
        {
          blk = mempool_remove_queue(pool, &pool->iqueue);
          if (blk == NULL)
            {
              mempool_unlock(pool, flags);
              return NULL;
            }
        }
      else
        {
          size_t blocksize = MEMPOOL_REALBLOCKSIZE(pool);

          mempool_unlock(pool, flags);
          if (pool->expandsize >= blocksize + sizeof(sq_entry_t))
            {
              size_t nexpand = (pool->expandsize - sizeof(sq_entry_t)) /
                               blocksize;
              size_t size = nexpand * blocksize + sizeof(sq_entry_t);
              FAR char *base = pool->alloc(pool, size);

              if (base == NULL)
                {
                  return NULL;
                }

              kasan_poison(base, size);
              flags = mempool_lock(pool);
              mempool_add_queue(&pool->queue, base, nexpand, blocksize);
              sq_addlast((FAR sq_entry_t *)(base + nexpand * blocksize),
                         &pool->equeue);
              blk = mempool_remove_queue(pool, &pool->queue);
            }
          else if (!pool->wait ||
                   nxsem_wait_uninterruptible(&pool->waitsem) < 0)
            {
              return NULL;
            }
          else
            {
              goto retry;
            }
        }
    }

#if CONFIG_MM_BACKTRACE < 0
  pool->nalloc++;
#endif

  mempool_unlock(pool, flags);
  blk = kasan_unpoison(blk, pool->blocksize);
#ifdef CONFIG_MM_FILL_ALLOCATIONS
  memset(blk, MM_ALLOC_MAGIC, pool->blocksize);
#endif

#if CONFIG_MM_BACKTRACE >= 0
  mempool_add_backtrace(pool, (FAR struct mempool_backtrace_s *)
                              ((FAR char *)blk + pool->blocksize));
#endif

  return blk;
}

/****************************************************************************
 * Name: mempool_release
 *
 * Description:
 *   Release an memory block to the pool.
 *
 * Input Parameters:
 *   pool - Address of the memory pool to be used.
 *   blk  - The pointer of memory block.
 ****************************************************************************/

int mempool_release(FAR struct mempool_s *pool, FAR void *blk)
{
  int flags = mempool_lock(pool);
  size_t blocksize = MEMPOOL_REALBLOCKSIZE(pool);

  if (flags < 0)
    {
      return flags;
    }

#if CONFIG_MM_BACKTRACE >= 0
  FAR struct mempool_backtrace_s *buf =
    (FAR struct mempool_backtrace_s *)((FAR char *)blk + pool->blocksize);

  /* Check double free */

  DEBUGASSERT(list_in_list(&buf->node));
  list_delete(&buf->node);
#else
  pool->nalloc--;
#endif

#ifdef CONFIG_MM_FILL_ALLOCATIONS
  memset(blk, MM_FREE_MAGIC, pool->blocksize);
#endif

  if (pool->interruptsize > blocksize)
    {
      if ((FAR char *)blk >= pool->ibase &&
          (FAR char *)blk < pool->ibase + pool->interruptsize - blocksize)
        {
          sq_addlast(blk, &pool->iqueue);
        }
      else
        {
          sq_addlast(blk, &pool->queue);
        }
    }
  else
    {
      sq_addlast(blk, &pool->queue);
    }

  kasan_poison(blk, pool->blocksize);
  mempool_unlock(pool, flags);
  if (pool->wait && pool->expandsize == 0)
    {
      int semcount;

      nxsem_get_value(&pool->waitsem, &semcount);
      if (semcount < 1)
        {
          nxsem_post(&pool->waitsem);
        }
    }

  return 0;
}

/****************************************************************************
 * Name: mempool_info
 *
 * Description:
 *   mempool_info returns a copy of updated current mempool information.
 *
 * Input Parameters:
 *   pool    - Address of the memory pool to be used.
 *   info    - The pointer of mempoolinfo.
 *
 * Returned Value:
 *   OK on success; A negated errno value on any failure.
 ****************************************************************************/

int mempool_info(FAR struct mempool_s *pool, FAR struct mempoolinfo_s *info)
{
  size_t blocksize = MEMPOOL_REALBLOCKSIZE(pool);
  int flags;

  DEBUGASSERT(pool != NULL && info != NULL);

  flags = mempool_lock(pool);
  info->ordblks = sq_count(&pool->queue);
  info->iordblks = sq_count(&pool->iqueue);
#if CONFIG_MM_BACKTRACE >= 0
  info->aordblks = list_length(&pool->alist);
#else
  info->aordblks = pool->nalloc;
#endif
  info->arena = sq_count(&pool->equeue) * sizeof(sq_entry_t) +
    (info->aordblks + info->ordblks + info->iordblks) * blocksize;
  mempool_unlock(pool, flags);
  info->sizeblks = blocksize;
  if (pool->wait && pool->expandsize == 0)
    {
      int semcount;

      nxsem_get_value(&pool->waitsem, &semcount);
      info->nwaiter = -semcount;
    }
  else
    {
      info->nwaiter = 0;
    }

  return 0;
}

/****************************************************************************
 * Name: mempool_info_task
 ****************************************************************************/

struct mallinfo_task
mempool_info_task(FAR struct mempool_s *pool,
                  FAR const struct malltask *task)
{
  size_t blocksize = MEMPOOL_REALBLOCKSIZE(pool);
  int flags = mempool_lock(pool);
  struct mallinfo_task info =
    {
      0, 0
    };

  if (task->pid == PID_MM_FREE)
    {
      size_t count = sq_count(&pool->queue) +
                     sq_count(&pool->iqueue);
      info.aordblks += count;
      info.uordblks += count * blocksize;
    }
#if CONFIG_MM_BACKTRACE < 0
  else if (task->pid == PID_MM_ALLOC)
    {
      info.aordblks += pool->nalloc;
      info.uordblks += pool->nalloc * blocksize;
    }
#else
  else
    {
      FAR struct mempool_backtrace_s *buf;

      list_for_every_entry(&pool->alist, buf,
                           struct mempool_backtrace_s, node)
        {
          if ((MM_DUMP_ASSIGN(task->pid, buf->pid) ||
               MM_DUMP_ALLOC(task->pid, buf->pid) ||
               MM_DUMP_LEAK(task->pid, buf->pid)) &&
              buf->seqno >= task->seqmin && buf->seqno <= task->seqmax)
            {
              info.aordblks++;
              info.uordblks += blocksize;
            }
        }
    }
#endif

  mempool_unlock(pool, flags);
  return info;
}

/****************************************************************************
 * Name: mempool_memdump
 *
 * Description:
 *   mempool_memdump returns a memory info about specified pid of
 *   task/thread. if pid equals -1, this function will dump all allocated
 *   node and output backtrace for every allocated node for this mempool,
 *   if pid equals -2, this function will dump all free node for this
 *   mempool, and if pid is greater than or equal to 0, will dump pid
 *   allocated node and output backtrace.
 *
 * Input Parameters:
 *   pool    - Address of the memory pool to be used.
 *   dump    - The information of what need dump.
 *
 * Returned Value:
 *   OK on success; A negated errno value on any failure.
 ****************************************************************************/

void mempool_memdump(FAR struct mempool_s *pool,
                     FAR const struct mm_memdump_s *dump)
{
  size_t blocksize = MEMPOOL_REALBLOCKSIZE(pool);
  int flags = mempool_lock(pool);
  if (dump->pid == PID_MM_FREE)
    {
      FAR sq_entry_t *entry;

      sq_for_every(&pool->queue, entry)
        {
          syslog(LOG_INFO, "%12zu%*p\n",
                 blocksize, MM_PTR_FMT_WIDTH, (FAR char *)entry);
        }

      sq_for_every(&pool->iqueue, entry)
        {
          syslog(LOG_INFO, "%12zu%*p\n",
                 blocksize, MM_PTR_FMT_WIDTH, (FAR char *)entry);
        }
    }
#if CONFIG_MM_BACKTRACE >= 0
  else
    {
      FAR struct mempool_backtrace_s *buf;

      list_for_every_entry(&pool->alist, buf,
                           struct mempool_backtrace_s, node)
        {
          if ((MM_DUMP_ASSIGN(dump->pid, buf->pid) ||
               MM_DUMP_ALLOC(dump->pid, buf->pid) ||
               MM_DUMP_LEAK(dump->pid, buf->pid)) &&
              buf->seqno >= dump->seqmin && buf->seqno <= dump->seqmax)
            {
              char tmp[CONFIG_MM_BACKTRACE * MM_PTR_FMT_WIDTH + 1] = "";

#  if CONFIG_MM_BACKTRACE > 0
              FAR const char *format = " %0*p";
              int i;

              for (i = 0; i < CONFIG_MM_BACKTRACE && buf->backtrace[i]; i++)
                {
                  snprintf(tmp + i * MM_PTR_FMT_WIDTH,
                           sizeof(tmp) - i * MM_PTR_FMT_WIDTH,
                           format, MM_PTR_FMT_WIDTH - 1, buf->backtrace[i]);
                }
#  endif

              syslog(LOG_INFO, "%6d%12zu%12lu%*p%s\n",
                     buf->pid, blocksize, buf->seqno,
                     MM_PTR_FMT_WIDTH, ((FAR char *)buf - blocksize), tmp);
            }
        }
    }
#endif

  mempool_unlock(pool, flags);
}

/****************************************************************************
 * Name: mempool_deinit
 *
 * Description:
 *   Deallocate a memory pool.
 *
 * Input Parameters:
 *   pool    - Address of the memory pool to be used.
 ****************************************************************************/

int mempool_deinit(FAR struct mempool_s *pool)
{
  size_t blocksize = MEMPOOL_REALBLOCKSIZE(pool);
  FAR sq_entry_t *blk;
  size_t count = 0;

#if CONFIG_MM_BACKTRACE >= 0
  if (!list_is_empty(&pool->alist))
#else
  if (pool->nalloc != 0)
#endif
    {
      return -EBUSY;
    }

  if (pool->initialsize >= blocksize + sizeof(sq_entry_t))
    {
      count = (pool->initialsize - sizeof(sq_entry_t)) / blocksize;
    }

  if (count == 0)
    {
      if (pool->expandsize >= blocksize + sizeof(sq_entry_t))
        {
          count = (pool->expandsize - sizeof(sq_entry_t)) / blocksize;
        }
    }

#if defined(CONFIG_FS_PROCFS) && !defined(CONFIG_FS_PROCFS_EXCLUDE_MEMPOOL)
  mempool_procfs_unregister(&pool->procfs);
#endif

  while ((blk = mempool_remove_queue(pool, &pool->equeue)) != NULL)
    {
      blk = (FAR sq_entry_t *)((FAR char *)blk - count * blocksize);
      pool->free(pool, blk);
      if (pool->expandsize >= blocksize + sizeof(sq_entry_t))
        {
          count = (pool->expandsize - sizeof(sq_entry_t)) / blocksize;
        }
    }

  if (pool->ibase)
    {
      pool->free(pool, pool->ibase);
    }
  else
    {
      nxmutex_destroy(&pool->u.mutex);
    }

  if (pool->wait && pool->expandsize == 0)
    {
      nxsem_destroy(&pool->waitsem);
    }

  return 0;
}
