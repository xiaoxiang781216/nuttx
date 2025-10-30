/****************************************************************************
 * mm/mempool/mempool.c
 *
 * SPDX-License-Identifier: Apache-2.0
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
#include <debug.h>
#include <execinfo.h>
#include <stdbool.h>
#include <stdio.h>
#include <syslog.h>

#include <nuttx/kmalloc.h>
#include <nuttx/mm/kasan.h>
#include <nuttx/mm/mempool.h>
#include <nuttx/mutex.h>
#include <nuttx/nuttx.h>
#include <nuttx/sched.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifdef CONFIG_MM_RECORD
#define MEMPOOL_MAGIC_FREE  0x55555555
#define MEMPOOL_MAGIC_ALLOC 0xAAAAAAAA

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef void (*mempool_callback_t)(FAR struct mempool_s *pool,
                                   FAR struct mempool_record_s *record,
                                   FAR const void *input, FAR void *output);
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static inline FAR sq_entry_t *
mempool_remove_queue(FAR struct mempool_s *pool, FAR sq_queue_t *queue)
{
  FAR sq_entry_t *ret = queue->head;
  FAR void *addr = ret;

  if (ret)
    {
      if (!ret->flink)
        {
          queue->tail = NULL;
        }
      else if (pool->initialbase == NULL || addr < pool->initialbase ||
               addr >= (void *)((char *)pool->initialbase +
                                pool->initialsize))
        {
          pool->check(pool, addr);
        }

      queue->head = ret->flink;
      ret->flink = NULL;
    }

  return ret;
}

static inline void mempool_add_queue(FAR struct mempool_s *pool,
                                     FAR sq_queue_t *queue,
                                     FAR char *base, size_t nblks,
                                     size_t blocksize)
{
  while (nblks-- > 0u)
    {
      FAR sq_entry_t *node = (FAR sq_entry_t *)(base + blocksize * nblks);
#ifdef CONFIG_MM_RECORD
      FAR struct mempool_record_s *record = (FAR void *)node;

      /* Usr mem layout after record payload,  keep record when overlap. */

      node = (FAR void *)mempool_get_block_from_record(record);
#  ifdef CONFIG_MM_RECORD_STACK
      record->stack = NULL;
      record->freestack = NULL;
#  endif
      record->magic = MEMPOOL_MAGIC_FREE;
#endif
      sq_addlast(node, queue);
    }
}

#ifdef CONFIG_MM_RECORD
static inline void mempool_record(FAR struct mempool_s *pool,
                                  FAR struct mempool_record_s *record,
                                  unsigned int magic)
{
#  ifdef CONFIG_MM_RECORD_STACK
  FAR void **stack;
#  endif

  record->magic = magic;
#  ifdef CONFIG_MM_RECORD_PID
  if (magic == MEMPOOL_MAGIC_ALLOC)
    {
      record->pid = _SCHED_GETTID();
    }
  else
    {
      record->freepid = _SCHED_GETTID();
    }
#  endif

  MM_INCSEQNO(record);

#  ifdef CONFIG_MM_RECORD_STACK
  stack = (magic == MEMPOOL_MAGIC_ALLOC ?
           &record->stack : &record->freestack);
  backtrace_remove(*stack);
  if (pool->procfs.backtrace)
    {
      *stack = backtrace_record(
          CONFIG_MM_HEAP_MEMPOOL_RECORD_STACK_SKIP);
    }
  else
    {
      *stack = NULL;
    }
#  endif
}

static void mempool_foreach(FAR struct mempool_s *pool,
                            mempool_callback_t callback,
                            FAR const void *input, FAR void *output)
{
  size_t blocksize = MEMPOOL_REALBLOCKSIZE(pool->blocksize);
  FAR struct mempool_record_s *record;
  FAR sq_entry_t *entry;
  FAR char *base;
  size_t nblks;

  if (pool->ibase != NULL)
    {
      nblks = pool->interruptsize / blocksize;
      while (nblks--)
        {
          record = (FAR struct mempool_record_s *)
                   (pool->ibase + nblks * blocksize);
          callback(pool, record, input, output);
        }
    }

  sq_for_every(&pool->equeue, entry)
    {
      nblks = (pool->expandsize - MEMPOOL_HEADER_SIZE) / blocksize;
      base = (FAR char *)entry - (nblks * blocksize);

      while (nblks--)
        {
          record = (FAR struct mempool_record_s *)
                   (base + nblks * blocksize);
          callback(pool, record, input, output);
        }
    }
}

#ifdef CONFIG_MM_RECORD_PID
static void mempool_info_task_callback(FAR struct mempool_s *pool,
                                       FAR struct mempool_record_s *record,
                                       FAR const void *input,
                                       FAR void *output)
{
  size_t blocksize = MEMPOOL_REALBLOCKSIZE(pool->blocksize);
  FAR const struct malltask *task = input;
  FAR struct mallinfo_task *info = output;

  if (record->magic == MEMPOOL_MAGIC_FREE)
    {
      return;
    }

  if ((MM_DUMP_ASSIGN(task, record) || MM_DUMP_ALLOC(task, record) ||
       MM_DUMP_LEAK(task, record)) && MM_DUMP_SEQNO(task, record))
    {
      info->aordblks++;
      info->uordblks += blocksize;
    }
}
#endif

static void mempool_memdump_callback(FAR struct mempool_s *pool,
                                     FAR struct mempool_record_s *record,
                                     FAR const void *input, FAR void *output)
{
  size_t blocksize = MEMPOOL_REALBLOCKSIZE(pool->blocksize);
  size_t overhead = blocksize - pool->blocksize;
  FAR const struct mm_memdump_s *dump = input;
  UNUSED(dump);

  if (record->magic == MEMPOOL_MAGIC_FREE)
    {
      return;
    }

  if ((MM_DUMP_ASSIGN(dump, record) || MM_DUMP_ALLOC(dump, record) ||
       MM_DUMP_LEAK(dump, record)) && MM_DUMP_SEQNO(dump, record))
    {
#  ifdef CONFIG_MM_RECORD_STACK
      char tmp[BACKTRACE_BUFFER_SIZE(CONFIG_LIBC_BACKTRACE_DEPTH)] = "";
      FAR void **stack;
      int stacksize;

      stack = backtrace_get(record->stack, &stacksize);
      if (stack && stacksize)
        {
          backtrace_format(tmp, sizeof(tmp), stack, stacksize);
        }
#  else
      FAR const char *tmp = "";
#  endif

      syslog(LOG_INFO,
#ifdef CONFIG_MM_RECORD_PID
             "%6d"
#endif
             "%12zu%9zu"
#ifdef CONFIG_MM_RECORD_SEQNO
             "%12lu"
#endif
             "%*p %s\n",
#ifdef CONFIG_MM_RECORD_PID
             record->pid,
#endif
             blocksize, overhead,
#ifdef CONFIG_MM_RECORD_SEQNO
             record->seqno,
#endif
             BACKTRACE_PTR_FMT_WIDTH,
             ((FAR char *)record - pool->blocksize), tmp);
    }
}

static void
mempool_memdump_free_callback(FAR struct mempool_s *pool,
                              FAR struct mempool_record_s *record,
                              FAR const void *input, FAR void *output)
{
  size_t blocksize = MEMPOOL_REALBLOCKSIZE(pool->blocksize);
  size_t overhead = blocksize - pool->blocksize;

  if (record->magic == MEMPOOL_MAGIC_FREE)
    {
#ifdef CONFIG_MM_RECORD_STACK
      char tmp[BACKTRACE_BUFFER_SIZE(CONFIG_LIBC_BACKTRACE_DEPTH)] = "";
      FAR void **stack;
      int stacksize;

      stack = backtrace_get(record->freestack, &stacksize);
      if (stack && stacksize)
        {
          backtrace_format(tmp, sizeof(tmp), stack, stacksize);
        }
#else
      FAR const char *tmp = "";
#endif
      syslog(LOG_INFO,
#ifdef CONFIG_MM_RECORD_PID
             "%6d"
#endif
             "%12zu%9zu%*p %s\n",
#ifdef CONFIG_MM_RECORD_PID
             record->freepid,
#endif
             blocksize, overhead, BACKTRACE_PTR_FMT_WIDTH,
             mempool_get_block_from_record(record), tmp);
    }
}
#endif

/****************************************************************************
 * Name: mempool_alloc_callback
 *
 * Description:
 *   The static mempool alloc callback function.
 *
 ****************************************************************************/

static FAR void *mempool_alloc_callback(FAR struct mempool_s *pool,
                                        size_t size)
{
  return mm_malloc(pool->priv, size);
}

/****************************************************************************
 * Name: mempool_free_callback
 *
 * Description:
 *   The static mempool free callback function.
 *
 ****************************************************************************/

static void mempool_free_callback(FAR struct mempool_s *pool, FAR void *addr)
{
  mm_free(pool->priv, addr);
}

/****************************************************************************
 * Name: mempool_check_callback
 *
 * Description:
 *   The static mempool check callback function.
 *
 ****************************************************************************/

static void mempool_check_callback(FAR struct mempool_s *pool,
                                   FAR void *addr)
{
  DEBUGASSERT(mm_heapmember(pool->priv, addr));
}

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
 *   This function is also used to auto init the mempool if user do not
 *   call the mempool_init() explictly.
 *   And if user do not specified the `priv`, `alloc`, `free` and `check`
 *   elements, mempool_init() will use a default dynamic expand method
 *   (expand memory from USER_HEAP/KNR_HEAP) and MEMPOOL_DEFINE defined
 *   mempool will use this method too.
 *
 * Input Parameters:
 *   pool - Address of the memory pool to be used.
 *
 * Returned Value:
 *   Zero on success; A negated errno value is returned on any failure.
 *
 ****************************************************************************/

int mempool_init(FAR struct mempool_s *pool)
{
  static DEFINE_PER_CPU_BMP(mutex_t, g_mempool_init_lock) =
                                     NXMUTEX_INITIALIZER;
  #define g_mempool_init_lock this_cpu_var_bmp(g_mempool_init_lock)
  size_t blocksize = MEMPOOL_REALBLOCKSIZE(pool->blocksize);
  int ret = OK;

  if (!pool->init)
    {
#ifdef CONFIG_PERCPU_SECTION
      pool->initialbase += PERCPU_OFFSET * this_cpu();
#endif

      nxmutex_lock(&g_mempool_init_lock);
      if (!pool->init)
        {
          if (pool->priv == NULL && pool->alloc == NULL &&
              pool->free == NULL && pool->check == NULL)
            {
              /* Default dynamic expand feature */

#ifdef __KERNEL__
              pool->priv = KNR_HEAP;
#else
              pool->priv = USR_HEAP;
#endif
              pool->alloc = mempool_alloc_callback;
              pool->free  = mempool_free_callback;
              pool->check = mempool_check_callback;
            }

          sq_init(&pool->queue);
          sq_init(&pool->iqueue);
          sq_init(&pool->equeue);
          pool->nalloc = 0;

          if (pool->interruptsize >= blocksize && pool->alloc && pool->priv)
            {
              size_t ninterrupt = pool->interruptsize / blocksize;
              size_t size = ninterrupt * blocksize;

              pool->ibase = pool->alloc(pool, size);
              if (pool->ibase == NULL)
                {
                  ret = -ENOMEM;
                }
              else
                {
                  mempool_add_queue(pool, &pool->iqueue,
                                    pool->ibase, ninterrupt, blocksize);
                  kasan_poison(pool->ibase, size);
                }
            }
          else
            {
              pool->ibase = NULL;
            }

          if (ret >= 0 &&
              pool->initialsize >= blocksize + MEMPOOL_HEADER_SIZE)
            {
              size_t ninitial = (pool->initialsize - MEMPOOL_HEADER_SIZE)
                                / blocksize;
              size_t size = ninitial * blocksize + MEMPOOL_HEADER_SIZE;
              FAR char *base;

              if (pool->initialbase == NULL && pool->alloc && pool->priv)
                {
                  base = pool->alloc(pool, size);
                  if (base == NULL)
                    {
                      if (pool->ibase)
                        {
                          pool->free(pool, pool->ibase);
                        }

                      ret = -ENOMEM;
                    }
                  else
                    {
                      mempool_add_queue(pool, &pool->queue, base,
                                        ninitial, blocksize);
                      sq_addlast((FAR sq_entry_t *)(base + ninitial *
                                 blocksize), &pool->equeue);
                      kasan_poison(base, size);
                    }
                }
              else
                {
                  DEBUGASSERT(pool->initialsize >= size);
                  base = pool->initialbase;
                  mempool_add_queue(pool, &pool->queue, base, ninitial,
                                    blocksize);
                }
            }

          if (ret >= 0)
            {
              spin_lock_init(&pool->lock);
              if (pool->wait && pool->expandsize == 0u)
                {
                  nxsem_init(&pool->waitsem, 0, 0);
                }

#if defined(CONFIG_FS_PROCFS) && !defined(CONFIG_FS_PROCFS_EXCLUDE_MEMPOOL)
              mempool_procfs_register(&pool->procfs, pool->name);
#  ifdef CONFIG_MM_RECORD_STACK_DEFAULT
              pool->procfs.backtrace = true;
#  elif defined(CONFIG_MM_RECORD_STACK)
              pool->procfs.backtrace = false;
#  endif
#endif

              pool->init = true;
            }
        }

      nxmutex_unlock(&g_mempool_init_lock);
    }

  return ret;
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
 *   pool    - Address of the memory pool to be used.
 *   timeout - The maximum time (ms) to wait for a buffer to become
 *             available.
 *
 * Returned Value:
 *   The pointer to the allocated block on success; NULL on any failure.
 *
 ****************************************************************************/

FAR void *mempool_allocate(FAR struct mempool_s *pool, unsigned int timeout)
{
#ifdef CONFIG_MM_RECORD
  FAR struct mempool_record_s *record;
#endif
  FAR sq_entry_t *blk = NULL;
  irqstate_t flags;
  bool bypass;
  bool retry;

  mempool_init(pool);

  do
    {
      retry = false;
      flags = spin_lock_irqsave(&pool->lock);
      if (pool->maxalloc > 0u && pool->nalloc >= pool->maxalloc)
        {
          spin_unlock_irqrestore(&pool->lock, flags);
          merr("ERROR: mempool_allocate: maxalloc=%zu\n", pool->maxalloc);
        }
      else
        {
          bypass = kasan_bypass(true);

          blk = mempool_remove_queue(pool, &pool->queue);
          if (blk == NULL)
            {
#if defined(CONFIG_BUILD_FLAT) || defined(__KERNEL__)
              if (up_interrupt_context())
                {
                  blk = mempool_remove_queue(pool, &pool->iqueue);
                  if (blk == NULL)
                    {
                      kasan_bypass(bypass);
                      spin_unlock_irqrestore(&pool->lock, flags);
                    }
                }
              else
#endif
                {
                  size_t blocksize = MEMPOOL_REALBLOCKSIZE(pool->blocksize);

                  kasan_bypass(bypass);
                  spin_unlock_irqrestore(&pool->lock, flags);
                  if (pool->expandsize >= blocksize + MEMPOOL_HEADER_SIZE)
                    {
                      size_t nexpand = (pool->expandsize -
                                        MEMPOOL_HEADER_SIZE) / blocksize;
                      size_t size = nexpand * blocksize +
                                    MEMPOOL_HEADER_SIZE;
                      FAR char *base = pool->alloc(pool, size);

                      if (base != NULL)
                        {
                          kasan_poison(base, size);

                          flags = spin_lock_irqsave(&pool->lock);
                          bypass = kasan_bypass(true);

                          mempool_add_queue(pool, &pool->queue,
                                            base, nexpand, blocksize);
                          sq_addlast((FAR sq_entry_t *)(base + nexpand *
                                     blocksize), &pool->equeue);
                          blk = mempool_remove_queue(pool, &pool->queue);
                        }
                    }
                  else if (pool->wait && timeout)
                    {
                      int ret;

                      if (timeout == UINT_MAX)
                        {
                          ret = nxsem_wait_uninterruptible(&pool->waitsem);
                        }
                      else
                        {
                          ret = nxsem_tickwait_uninterruptible
                                  (&pool->waitsem, MSEC2TICK(timeout));
                        }

                      if (ret >= 0)
                        {
                          retry = true;
                        }
                    }
                }
            }
        }
    }
  while (retry);

  if (blk != NULL)
    {
      pool->nalloc++;
      kasan_bypass(bypass);
      spin_unlock_irqrestore(&pool->lock, flags);

#ifdef CONFIG_MM_RECORD
      record = mempool_get_record_from_block(blk);
      DEBUGASSERT(record->magic == MEMPOOL_MAGIC_FREE);
      mempool_record(pool, record, MEMPOOL_MAGIC_ALLOC);
#endif

      blk = kasan_unpoison(blk, pool->blocksize);
#ifdef CONFIG_MM_FILL_ALLOCATIONS
      memset(blk, MM_ALLOC_MAGIC, pool->blocksize);
#endif
    }

  return blk;
}

/****************************************************************************
 * Name: mempool_zallocate
 *
 * Description:
 *   Allocate an block from a specific memory pool and zero the allocated
 *   block.
 *
 *   If there isn't enough memory blocks, This function will expand memory
 *   pool if expandsize isn't zero.
 *
 * Input Parameters:
 *   pool    - Address of the memory pool to be used.
 *   timeout - The maximum time (ms) to wait for a buffer to become
 *             available.
 *
 * Returned Value:
 *   The pointer to the allocated block on success; NULL on any failure.
 *
 ****************************************************************************/

FAR void *mempool_zallocate(FAR struct mempool_s *pool, unsigned int timeout)
{
  FAR void *blk = mempool_allocate(pool, timeout);
  if (blk != NULL)
    {
      memset(blk, 0, pool->blocksize);
    }

  return blk;
}

/****************************************************************************
 * Name: mempool_release
 *
 * Description:
 *   Release a memory block to the pool.
 *
 * Input Parameters:
 *   pool - Address of the memory pool to be used.
 *   blk  - The pointer of memory block.
 ****************************************************************************/

void mempool_release(FAR struct mempool_s *pool, FAR void *blk)
{
  irqstate_t flags;
  bool bypass = kasan_bypass(true);

#ifdef CONFIG_MM_RECORD
  FAR struct mempool_record_s *record;

  /* Backward to real record, not really free, blk don't have to update */

  record = mempool_get_record_from_block(blk);

  /* Check double free or out of out of bounds */

  DEBUGASSERT(record->magic == MEMPOOL_MAGIC_ALLOC);

  mempool_record(pool, record, MEMPOOL_MAGIC_FREE);
#endif

  flags = spin_lock_irqsave(&pool->lock);

  pool->nalloc--;

#ifdef CONFIG_MM_FILL_ALLOCATIONS
  memset(blk, MM_FREE_MAGIC, pool->blocksize);
#endif

  if (pool->ibase)
    {
      if ((FAR char *)blk >= pool->ibase &&
          (FAR char *)blk < pool->ibase + pool->interruptsize)
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
  kasan_bypass(bypass);
  spin_unlock_irqrestore(&pool->lock, flags);
  if (pool->wait && pool->expandsize == 0u)
    {
      int semcount = 0;

      nxsem_get_value(&pool->waitsem, &semcount);
      if (semcount < 1)
        {
          nxsem_post(&pool->waitsem);
        }
    }
}

/****************************************************************************
 * Name: mempool_navail
 *
 * Description:
 *   Return the number of available buffers in the mempool.
 *
 * Input Parameters:
 *   pool - Address of the memory pool to be used.
 *
 * Returned Value:
 *   Return the number of available buffers in the mempool, 0 means no room.
 *
 ****************************************************************************/

size_t mempool_navail(FAR struct mempool_s *pool)
{
  irqstate_t flags;
  size_t ret = SIZE_MAX;

  DEBUGASSERT(pool != NULL);
  mempool_init(pool);

  flags = spin_lock_irqsave(&pool->lock);
  if (pool->maxalloc != 0u)
    {
      ret = pool->maxalloc > pool->nalloc ?
            pool->maxalloc - pool->nalloc : 0u;
    }

  spin_unlock_irqrestore(&pool->lock, flags);
  return ret;
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
  size_t blocksize = MEMPOOL_REALBLOCKSIZE(pool->blocksize);
  irqstate_t flags;
  bool bypass;

  DEBUGASSERT(pool != NULL && info != NULL);
  mempool_init(pool);

  flags = spin_lock_irqsave(&pool->lock);
  bypass = kasan_bypass(true);

  info->ordblks = sq_count(&pool->queue);
  info->iordblks = sq_count(&pool->iqueue);
  info->aordblks = pool->nalloc;
  info->arena = sq_count(&pool->equeue) * MEMPOOL_HEADER_SIZE +
    (info->aordblks + info->ordblks + info->iordblks) * blocksize;
  info->maxalloc = pool->maxalloc;
  kasan_bypass(bypass);
  spin_unlock_irqrestore(&pool->lock, flags);
  info->sizeblks = blocksize;
  if (pool->wait && pool->expandsize == 0u)
    {
      int semcount = 0;

      nxsem_get_value(&pool->waitsem, &semcount);
      info->nwaiter = (unsigned long)-semcount;
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
  size_t blocksize = MEMPOOL_REALBLOCKSIZE(pool->blocksize);
  bool bypass;
  struct mallinfo_task info =
    {
      0, 0
    };

  mempool_init(pool);

  if (task->pid == PID_MM_FREE)
    {
      irqstate_t flags = spin_lock_irqsave(&pool->lock);
      bypass = kasan_bypass(true);
      size_t count = sq_count(&pool->queue) +
                     sq_count(&pool->iqueue);

      kasan_bypass(bypass);
      spin_unlock_irqrestore(&pool->lock, flags);
      info.aordblks += count;
      info.uordblks += count * blocksize;
    }
  else if (task->pid == PID_MM_ALLOC)
    {
      irqstate_t flags = spin_lock_irqsave(&pool->lock);
      bypass = kasan_bypass(true);
      size_t nalloc = pool->nalloc;

      kasan_bypass(bypass);
      spin_unlock_irqrestore(&pool->lock, flags);

      info.aordblks += nalloc;
      info.uordblks += nalloc * blocksize;
    }
#ifdef CONFIG_MM_RECORD_PID
  else
    {
      mempool_foreach(pool, mempool_info_task_callback, task, &info);
    }
#endif

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
  mempool_init(pool);

#ifdef CONFIG_MM_RECORD
  if (dump->pid == PID_MM_FREE)
    {
      mempool_foreach(pool, mempool_memdump_free_callback, NULL, NULL);
    }
  else
    {
      mempool_foreach(pool, mempool_memdump_callback, dump, NULL);
    }
#else
  size_t blocksize = MEMPOOL_REALBLOCKSIZE(pool->blocksize);
  size_t overhead = blocksize - pool->blocksize;

  /* Avoid race condition.
   * When backtrace is not enabled, printing all nodes requires traversing
   * the linked list, but there is no safe way to protect it. So we only
   * print the block dump when backtrace is enabled, and this is done by
   * magic to perform a safe traversal.
   */

  syslog(LOG_INFO, "%12zu%9zu%*p skip block dump\n",
         blocksize, overhead, BACKTRACE_PTR_FMT_WIDTH, pool);
#endif
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
  size_t blocksize = MEMPOOL_REALBLOCKSIZE(pool->blocksize);
  FAR sq_entry_t *blk;
  size_t count = 0;
  int ret = -EBUSY;

  if (pool->nalloc == 0u)
    {
      if (pool->initialsize >= blocksize + MEMPOOL_HEADER_SIZE)
        {
          count = (pool->initialsize - MEMPOOL_HEADER_SIZE) / blocksize;
        }

      if (count == 0u)
        {
          if (pool->expandsize >= blocksize + MEMPOOL_HEADER_SIZE)
            {
              count = (pool->expandsize - MEMPOOL_HEADER_SIZE) / blocksize;
            }
        }

#if defined(CONFIG_FS_PROCFS) && !defined(CONFIG_FS_PROCFS_EXCLUDE_MEMPOOL)
      mempool_procfs_unregister(&pool->procfs);
#endif

      while ((blk = mempool_remove_queue(pool, &pool->equeue)) != NULL)
        {
          blk = (FAR sq_entry_t *)((FAR char *)blk - count * blocksize);

          blk = kasan_unpoison(blk, count * blocksize + MEMPOOL_HEADER_SIZE);
          pool->free(pool, blk);
          if (pool->expandsize >= blocksize + MEMPOOL_HEADER_SIZE)
            {
              count = (pool->expandsize - MEMPOOL_HEADER_SIZE) / blocksize;
            }
        }

      if (pool->ibase)
        {
          pool->ibase = kasan_unpoison(pool->ibase,
                          pool->interruptsize / blocksize * blocksize);
          pool->free(pool, pool->ibase);
        }

      if (pool->wait && pool->expandsize == 0u)
        {
          nxsem_destroy(&pool->waitsem);
        }

      ret = OK;
    }

  return ret;
}
