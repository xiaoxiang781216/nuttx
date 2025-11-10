/****************************************************************************
 * mm/mempool/mempool_multiple.c
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
#include <strings.h>
#include <syslog.h>
#include <sys/param.h>

#include <nuttx/mutex.h>
#include <nuttx/nuttx.h>
#include <nuttx/kmalloc.h>
#include <nuttx/mm/mempool.h>
#include <nuttx/mm/kasan.h>

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct mpool_dict_s
{
  FAR struct mempool_s *pool; /* Record pool when expanding */
  FAR void             *addr; /* Record expand memary address */
  size_t                size; /* Record expand memary size */
};

struct mpool_chunk_s
{
  sq_entry_t entry;
  FAR void *start;
  FAR void *next;
  FAR void *end;
  size_t used;
};

struct mempool_multiple_s
{
  FAR struct mempool_s         *pools;       /* The memory pool array */
  size_t                        npools;      /* The number of memory pool array elements */
  size_t                        expandsize;  /* The number not will use it to init erery
                                              * pool expandsize
                                              */
  size_t                        minpoolsize; /* The number is align for each memory pool */
  FAR void                     *arg;         /* This pointer is used to store the user's
                                              * private data
                                              */
  mempool_multiple_alloc_t      alloc;       /* The alloc function for mempool */
  mempool_multiple_alloc_size_t alloc_size;  /* Get the address size of the
                                              * alloc function
                                              */
  mempool_multiple_free_t       free;        /* The free function for mempool */
  size_t                        alloced;     /* Total size of alloc */

  /* This delta describes the relationship between the block size of each
   * mempool in multiple mempool by user initialized. It is automatically
   * detected by the mempool_multiple_init function. If the delta is not
   * equal to 0, the block size of the pool in the multiple mempool is an
   * arithmetic progressions, otherwise it is an increasing progressions.
   */

  size_t                        delta;

  /* It is used to record the information recorded by the mempool during
   * expansion, and find the mempool by adding an index
   */

  rmutex_t                      lock;
  sq_queue_t                    chunk_queue;
  size_t                        chunksize;
  size_t                        init_chunksize;
  size_t                        dict_used;
  size_t                        dict_col_num_log2;
  size_t                        dict_row_num;
  FAR struct mpool_dict_s     **dict;
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static inline FAR struct mempool_s *
mempool_multiple_find(FAR struct mempool_multiple_s *mpool, size_t size)
{
  FAR struct mempool_s *ret = NULL;
  size_t right;
  size_t left = 0;
  size_t mid;

  if (mpool != NULL)
    {
      right = mpool->npools;
      if (mpool->delta != 0)
        {
          left = mpool->pools[0].blocksize;
          if (left >= size)
            {
              ret = &mpool->pools[0];
            }
          else
            {
              mid = (size - left + mpool->delta - 1) / mpool->delta;
              ret = mid < right ? &mpool->pools[mid] : NULL;
            }
        }
      else
        {
          while (left < right)
            {
              mid = (left + right) >> 1;
              if (mpool->pools[mid].blocksize > size)
                {
                  right = mid;
                }
              else
                {
                  left = mid + 1;
                }
            }

          if (left != mpool->npools)
            {
              ret = &mpool->pools[left];
            }
        }
    }

  return ret;
}

static FAR void *
mempool_multiple_alloc_chunk(FAR struct mempool_multiple_s *mpool,
                             size_t align, size_t size)
{
  FAR char *ret = NULL;

  if (mpool->chunksize < mpool->expandsize)
    {
      ret = mpool->alloc(mpool->arg, align, size);
      if (ret)
        {
          mpool->alloced += mpool->alloc_size(mpool->arg, ret);
        }
    }
  else
    {
      FAR struct mpool_chunk_s *chunk;

      chunk = (FAR struct mpool_chunk_s *)sq_peek(&mpool->chunk_queue);

      if (chunk != NULL)
        {
          ret = (FAR char *)ALIGN_UP((uintptr_t)(FAR char *)chunk->next,
                                                            align);
        }

      while (chunk == NULL || (size_t)((FAR char *)chunk->end - ret) < size)
        {
          size_t chunksize;
          FAR char *tmp;

          chunksize = mpool->init_chunksize != mpool->chunksize ?
                       mpool->init_chunksize : mpool->chunksize;
          tmp = mpool->alloc(mpool->arg, mpool->expandsize,
                             chunksize + sizeof(struct mpool_chunk_s));

          if (tmp == NULL)
            {
              ret = NULL;
              break;
            }

          mpool->init_chunksize = mpool->chunksize;
          mpool->alloced += mpool->alloc_size(mpool->arg, tmp);
          chunk = (FAR struct mpool_chunk_s *)(tmp + chunksize);
          chunk->end = tmp + chunksize;
          chunk->start = tmp;
          chunk->next = tmp;
          chunk->used = 0;
          sq_addfirst(&chunk->entry, &mpool->chunk_queue);
          ret = (FAR char *)ALIGN_UP((uintptr_t)(FAR char *)chunk->next,
                                                            align);
        }

      if (ret != NULL)
        {
          chunk->used++;
          chunk->next = (FAR char *)ret + size;
        }
    }

  return ret;
}

static void
mempool_multiple_free_chunk(FAR struct mempool_multiple_s *mpool,
                            FAR void *ptr)
{
  FAR struct mpool_chunk_s *chunk;
  FAR sq_entry_t *entry;

  if (mpool->chunksize < mpool->expandsize)
    {
      mpool->free(mpool->arg, ptr);
    }
  else
    {
      if (nxrmutex_lock(&mpool->lock) >= 0)
        {
          sq_for_every(&mpool->chunk_queue, entry)
            {
              chunk = (FAR struct mpool_chunk_s *)entry;
              if (ptr >= chunk->start && ptr < chunk->next)
                {
                  if (--chunk->used == 0)
                    {
                      sq_rem(&chunk->entry, &mpool->chunk_queue);
                      mpool->free(mpool->arg, chunk->start);
                    }

                  break;
                }
            }

          nxrmutex_unlock(&mpool->lock);
        }
    }
}

static FAR void *mempool_multiple_alloc_callback(FAR struct mempool_s *pool,
                                                 size_t size)
{
  FAR struct mempool_multiple_s *mpool = pool->priv;
  FAR void *ret = NULL;
  size_t row;
  size_t col;

  if (nxrmutex_lock(&mpool->lock) >= 0)
    {
      ret = mempool_multiple_alloc_chunk(mpool, mpool->expandsize,
                                        mpool->minpoolsize + size);
      if (ret != NULL)
        {
          row = mpool->dict_used >> mpool->dict_col_num_log2;

          /* There is no new pointer address to store the dictionaries */

          DEBUGASSERT(mpool->dict_row_num > row);

          col = mpool->dict_used - (row << mpool->dict_col_num_log2);

          if (mpool->dict[row] == NULL)
            {
              mpool->dict[row] =
                mempool_multiple_alloc_chunk(mpool, sizeof(uintptr_t),
                                            (1 << mpool->dict_col_num_log2)
                                            * sizeof(struct mpool_dict_s));
            }

          mpool->dict[row][col].pool = pool;
          mpool->dict[row][col].addr = ret;
          mpool->dict[row][col].size = mpool->minpoolsize + size;
          *(FAR size_t *)ret = mpool->dict_used++;
          ret = (FAR char *)ret + mpool->minpoolsize;
        }

      nxrmutex_unlock(&mpool->lock);
    }

  return ret;
}

static void mempool_multiple_free_callback(FAR struct mempool_s *pool,
                                           FAR void *addr)
{
  FAR struct mempool_multiple_s *mpool = pool->priv;

  mempool_multiple_free_chunk(mpool,
                              (FAR char *)addr - mpool->minpoolsize);
}

/****************************************************************************
 * Name: mempool_multiple_get_dict
 *
 * Description:
 *   Obtain the dict through mpool and blk
 *
 * Input Parameters:
 *   mpool - The handle of the multiple memory pool to be used.
 *   blk   - The pointer of memory block.
 *
 * Returned Value:
 *   Address of the dict to be used or NULL is not find.
 *
 ****************************************************************************/

static FAR struct mpool_dict_s *
mempool_multiple_get_dict(FAR struct mempool_multiple_s *mpool,
                          FAR void *blk)
{
  FAR struct mpool_dict_s *ret = NULL;
  FAR char *addr;
  bool   bypass;
  size_t index;
  size_t row;
  size_t col;

  if (mpool != NULL && blk != NULL && mpool->dict != NULL)
    {
      bypass = kasan_bypass(true);
      addr = (FAR char *)ALIGN_DOWN((uintptr_t)(FAR char *)blk,
                                     mpool->expandsize);

      /* When blk is addr, It is not a memory block allocated by mempool
       * Because the blk is need not aligned with the expandsize
       * in head memory.
       */

      if (blk != addr)
        {
          index = *(FAR size_t *)addr;
          if (index < mpool->dict_used)
            {
              row = index >> mpool->dict_col_num_log2;
              col = index - (row << mpool->dict_col_num_log2);

              addr = kasan_clear_tag(addr);
              if (kasan_clear_tag(mpool->dict[row]) != NULL &&
                  kasan_clear_tag(mpool->dict[row][col].addr) == addr &&
                  ((size_t)((FAR char *)kasan_clear_tag(blk) -
                   (FAR char *)addr) < mpool->dict[row][col].size))
                {
                  ret = &mpool->dict[row][col];
                }
            }

          kasan_bypass(bypass);
        }
    }

  return ret;
}

/****************************************************************************
 * Name: mempool_multiple_check
 *
 * Description:
 *   Check the blk is in the pool
 *
 * Input Parameters:
 *   mpool - The handle of the multiple memory pool to be used.
 *   blk   - The pointer of memory block.
 *
 ****************************************************************************/

static void mempool_multiple_check(FAR struct mempool_s *pool,
                                   FAR void *blk)
{
  assert(mempool_multiple_get_dict(pool->priv, blk));
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: mempool_multiple_init
 *
 * Description:
 *   Initialize multiple memory pool, each element represents a memory pool.
 *   The user needs to specify the initialization information of each mempool
 *   in the array, including blocksize, initialsize, expandsize,
 *   interruptsize, wait. These mempool will be initialized by mempool_init.
 *   The name of all mempool are "name".
 *
 *   This function will initialize the member delta by detecting the
 *   relationship between the each block size of mempool in multiple mempool.
 *
 * Input Parameters:
 *   name            - The name of memory pool.
 *   config          - The mempool config info.
 *   alloc           - The alloc memory function for multiples pool.
 *   alloc_size      - Get the address size of the alloc function.
 *   free            - The free memory function for multiples pool.
 *   arg             - The alloc & free memory fuctions used arg.
 * Returned Value:
 *   Return an initialized multiple pool pointer on success,
 *   otherwise NULL is returned.
 *
 ****************************************************************************/

FAR struct mempool_multiple_s *
mempool_multiple_init(FAR const char *name,
                      FAR const struct mm_pool_config_s *config,
                      mempool_multiple_alloc_t alloc,
                      mempool_multiple_alloc_size_t allocsize,
                      mempool_multiple_free_t free, FAR void *arg)
{
  FAR struct mempool_multiple_s *mpool = NULL;
  FAR struct mempool_s *pools;
  size_t maxpoolszie;
  size_t minpoolsize;
  size_t mempoolsize;
  int i;

  if (!(config->expandsize & (config->expandsize - 1)))
    {
      maxpoolszie = config->poolsize[0];
      minpoolsize = config->poolsize[0];
      for (i = 0; i < config->npools; i++)
        {
          if (maxpoolszie < config->poolsize[i])
            {
              maxpoolszie = config->poolsize[i];
            }

          if (minpoolsize > config->poolsize[i])
            {
              minpoolsize = config->poolsize[i];
            }
        }

      mempoolsize = sizeof(struct mempool_multiple_s) +
                    config->npools * sizeof(struct mempool_s);
      mpool = alloc(arg, sizeof(uintptr_t), mempoolsize);
      if (mpool)
        {
          memset(mpool, 0, mempoolsize);

          pools = (FAR struct mempool_s *)
                  ((uintptr_t)mpool + sizeof(struct mempool_multiple_s));

          mpool->alloc_size = allocsize;
          mpool->expandsize = config->expandsize;
          mpool->chunksize = config->chunksize;
          mpool->init_chunksize = config->init_chunksize;
          mpool->alloc = alloc;
          mpool->free = free;
          mpool->arg = arg;
          mpool->alloced = allocsize(arg, mpool);
          sq_init(&mpool->chunk_queue);
          mpool->pools = pools;
          mpool->npools = config->npools;
          mpool->minpoolsize = minpoolsize;

          for (i = 0; i < config->npools; i++)
            {
              pools[i].blocksize = config->poolsize[i];
              pools[i].expandsize = config->expandsize - mpool->minpoolsize;
              pools[i].priv = mpool;
              pools[i].name = name;
              pools[i].alloc = mempool_multiple_alloc_callback;
              pools[i].free = mempool_multiple_free_callback;
              pools[i].check = mempool_multiple_check;

              if (mempool_init(pools + i) >= 0)
                {
                  if (i + 1 != config->npools)
                    {
                      size_t delta = config->poolsize[i + 1] -
                                     config->poolsize[i];

                      if (i == 0)
                        {
                          mpool->delta = delta;
                        }
                      else if (delta != mpool->delta)
                        {
                          mpool->delta = 0;
                        }
                    }
                }
              else
                {
                  while (--i >= 0)
                    {
                      mempool_deinit(pools + i);
                    }

                  mpool = NULL;
                }
            }

          if (mpool)
            {
              mpool->dict_col_num_log2 = fls(config->dict_expendsize /
                                            sizeof(struct mpool_dict_s));

              mpool->dict_row_num = config->dict_expendsize /
                                    sizeof(FAR struct mpool_dict_s *);
              mpool->dict =
                  mempool_multiple_alloc_chunk(
                    mpool, sizeof(FAR struct mpool_dict_s *),
                    sizeof(FAR struct mpool_dict_s *) * mpool->dict_row_num);
              if (mpool->dict)
                {
                  memset(mpool->dict, 0, mpool->dict_row_num *
                                         sizeof(FAR struct mpool_dict_s *));
                  nxrmutex_init(&mpool->lock);
                }
              else
                {
                  while (--i >= 0)
                    {
                      mempool_deinit(pools + i);
                    }

                  mpool = NULL;
                }
            }
        }
    }

  return mpool;
}

/****************************************************************************
 * Name: mempool_multiple_alloc
 *
 * Description:
 *   Allocate an block from specific multiple memory pool.
 *   If the mempool of the corresponding size doesn't have free block,
 *   it will continue to alloc memory for a larger memory pool until last
 *   mempool in multiple mempools.
 *
 * Input Parameters:
 *   mpool - The handle of multiple memory pool to be used.
 *   size  - The size of alloc blk.
 *
 * Returned Value:
 *   The pointer to the allocated block on success; NULL on any failure.
 *
 ****************************************************************************/

FAR void *mempool_multiple_alloc(FAR struct mempool_multiple_s *mpool,
                                 size_t size)
{
  FAR struct mempool_s *end;
  FAR struct mempool_s *pool;
  FAR void *blk = NULL;

  pool = mempool_multiple_find(mpool, size);
  if (pool != NULL)
    {
      end = mpool->pools + mpool->npools;
      do
        {
          blk = mempool_allocate(pool, UINT_MAX);

          if (blk)
            {
              break;
            }
        }
      while (++pool < end);
    }

  return blk;
}

/****************************************************************************
 * Name: mempool_multiple_realloc
 *
 * Description:
 *   Change the size of the block memory pointed to by oldblk to size bytes.
 *
 * Input Parameters:
 *   mpool  - The handle of multiple memory pool to be used.
 *   oldblk - The pointer to change the size of the block memory.
 *   size   - The size of alloc blk.
 *
 * Returned Value:
 *   The pointer to the allocated block on success; NULL on any failure.
 *
 ****************************************************************************/

FAR void *mempool_multiple_realloc(FAR struct mempool_multiple_s *mpool,
                                   FAR void *oldblk, size_t size)
{
  FAR struct mpool_dict_s *dict;
  FAR void *blk = NULL;

  if (oldblk == NULL)
    {
      blk = mempool_multiple_alloc(mpool, size);
    }
  else
    {
      dict = mempool_multiple_get_dict(mpool, oldblk);
      if (dict != NULL)
        {
          blk = mempool_multiple_alloc(mpool, size);
          if (blk != NULL && oldblk != NULL)
            {
              size = MIN(size, dict->pool->blocksize);
              memcpy(blk, oldblk, size);
              mempool_multiple_free(mpool, oldblk);
            }
        }
    }

  return blk;
}

/****************************************************************************
 * Name: mempool_multiple_free
 *
 * Description:
 *   Release a memory block to the multiple memory pool. The blk must have
 *   been returned by a previous call to mempool_multiple_alloc.
 *
 * Input Parameters:
 *   mpool - The handle of multiple memory pool to be used.
 *   blk  - The pointer of memory block.
 *
 * Returned Value:
 *   Zero on success; Negative number on any failure.
 *
 ****************************************************************************/

int mempool_multiple_free(FAR struct mempool_multiple_s *mpool,
                          FAR void *blk)
{
  FAR struct mpool_dict_s *dict;
  int ret = -EINVAL;

  dict = mempool_multiple_get_dict(mpool, blk);
  if (dict != NULL)
    {
      blk = (FAR char *)blk - ((size_t)((FAR char *)kasan_clear_tag(blk) -
                                ((FAR char *)kasan_clear_tag(dict->addr) +
                                 mpool->minpoolsize)) %
                               MEMPOOL_REALBLOCKSIZE(dict->pool->blocksize));

#ifdef CONFIG_MM_RECORD
      blk = mempool_get_block_from_record(blk);
#endif

      mempool_release(dict->pool, blk);
      ret = 0;
    }

  return ret;
}

/****************************************************************************
 * Name: mempool_multiple_alloc_size
 *
 * Description:
 *   Get size of memory block from multiple memory.
 *
 * Input Parameters:
 *   mpool - The handle of multiple memory pool to be used.
 *   blk  - The pointer of memory block.
 *
 * Returned Value:
 *   The size of memory block on success. Negative number on any failure.
 *
 ****************************************************************************/

ssize_t mempool_multiple_alloc_size(FAR struct mempool_multiple_s *mpool,
                                    FAR void *blk)
{
  FAR struct mpool_dict_s *dict;
  ssize_t ret = -EINVAL;

  DEBUGASSERT(blk != NULL);

  dict = mempool_multiple_get_dict(mpool, blk);
  if (dict != NULL)
    {
      ret = dict->pool->blocksize;
    }

  return ret;
}

/****************************************************************************
 * Name: mempool_multiple_memalign
 *
 * Description:
 *   This function requests more than enough space from malloc, finds a
 *   region within that chunk that meets the alignment request.
 *
 *   The alignment argument must be a power of two.
 *
 *   The memalign is special to multiple mempool because multiple mempool
 *   doesn't support split and shrink chunk operate. So When you alloc a
 *   memory block and find an aligned address in this block, you need to
 *   occupy 8 bytes before the address to save the address of the padding
 *   size and pool to ensure correct use in realloc and free operations.
 *   So we will use bit1 in the previous address of the address to represent
 *   that it is applied by memalign.
 *
 * Input Parameters:
 *   mpool     - The handle of multiple memory pool to be used.
 *   alignment - The alignment request of memory block.
 *   size      - The size of alloc blk.
 *
 * Returned Value:
 *   The size of memory block.
 *
 ****************************************************************************/

FAR void *mempool_multiple_memalign(FAR struct mempool_multiple_s *mpool,
                                    size_t alignment, size_t size)
{
  FAR struct mempool_s *end;
  FAR struct mempool_s *pool;
  FAR char *ret = NULL;

  DEBUGASSERT((alignment & (alignment - 1)) == 0);

  pool = mempool_multiple_find(mpool, size + alignment);
  if (pool != NULL)
    {
      end = mpool->pools + mpool->npools;
      do
        {
          FAR char *blk = mempool_allocate(pool, UINT_MAX);
          if (blk != NULL)
            {
              ret = (FAR char *)ALIGN_UP((uintptr_t)blk, alignment);
              break;
            }
        }
      while (++pool < end);
    }

  return ret;
}

/****************************************************************************
 * Name: mempool_multiple_foreach
 ****************************************************************************/

void mempool_multiple_foreach(FAR struct mempool_multiple_s *mpool,
                              mempool_multiple_foreach_t handle,
                              FAR void *arg)
{
  size_t i;
  for (i = 0; i < mpool->npools; i++)
    {
      handle(mpool->pools + i, arg);
    }
}

/****************************************************************************
 * Name: mempool_multiple_mallinfo
 ****************************************************************************/

struct mallinfo
mempool_multiple_mallinfo(FAR struct mempool_multiple_s *mpool)
{
  struct mallinfo info;
  size_t i;

  memset(&info, 0, sizeof(struct mallinfo));
  if (mpool != NULL)
    {
      if (nxrmutex_lock(&mpool->lock) >= 0)
        {
          info.arena = mpool->alloced;

          if (mpool->chunksize >= mpool->expandsize)
            {
              FAR struct mpool_chunk_s *chunk;

              chunk = (FAR struct mpool_chunk_s *)
                        sq_peek(&mpool->chunk_queue);
              info.fordblks += (uintptr_t)((FAR char *)chunk->end -
                                           (FAR char *)chunk->next);
            }

          nxrmutex_unlock(&mpool->lock);

          for (i = 0; i < mpool->npools; i++)
            {
              struct mempoolinfo_s poolinfo;

              mempool_info(mpool->pools + i, &poolinfo);
              info.fordblks += (poolinfo.ordblks + poolinfo.iordblks)
                              * poolinfo.sizeblks;
              info.ordblks += poolinfo.ordblks + poolinfo.iordblks;
              info.aordblks += poolinfo.aordblks;
              if (info.mxordblk < poolinfo.sizeblks)
                {
                  info.mxordblk = poolinfo.sizeblks;
                }
            }

          info.uordblks += mpool->alloced - info.fordblks;
        }
    }

  return info;
}

/****************************************************************************
 * Name: mempool_multiple_info_task
 ****************************************************************************/

struct mallinfo_task
mempool_multiple_info_task(FAR struct mempool_multiple_s *mpool,
                           FAR const struct malltask *task)
{
  int i;
  struct mallinfo_task info;
  struct mallinfo_task ret =
    {
      0, 0
    };

  if (mpool != NULL)
    {
      for (i = 0; i < mpool->npools; i++)
        {
          info = mempool_info_task(mpool->pools + i, task);
          ret.aordblks += info.aordblks;
          ret.uordblks += info.uordblks;
        }
    }

  return ret;
}

/****************************************************************************
 * Name: mempool_multiple_memdump
 *
 * Description:
 *   mempool_multiple_memdump returns a memory info about specified pid of
 *   task/thread. if pid equals -1, this function will dump all allocated
 *   node and output backtrace for every allocated node for this multiple
 *   mempool, if pid equals -2, this function will dump all free node for
 *   this multiple mempool, and if pid is greater than or equal to 0, will
 *   dump pid allocated node and output backtrace.
 *
 * Input Parameters:
 *   mpool - The handle of multiple memory pool to be used.
 *   dump  - The information of what need dump.
 *
 ****************************************************************************/

void mempool_multiple_memdump(FAR struct mempool_multiple_s *mpool,
                              FAR const struct mm_memdump_s *dump)
{
  size_t i;

  if (mpool != NULL)
    {
      for (i = 0; i < mpool->npools; i++)
        {
          mempool_memdump(mpool->pools + i, dump);
        }
    }
}

/****************************************************************************
 * Name: mempool_multiple_deinit
 *
 * Description:
 *   Deallocate multiple memory pool.
 *
 * Input Parameters:
 *   mpool - The handle of multiple memory pool to be used.
 *
 ****************************************************************************/

void mempool_multiple_deinit(FAR struct mempool_multiple_s *mpool)
{
  size_t i;

  if (mpool != NULL)
    {
      for (i = 0; i < mpool->npools; i++)
        {
          DEBUGVERIFY(mempool_deinit(mpool->pools + i));
        }

      for (i = 0; i < mpool->dict_row_num; i++)
        {
          if (mpool->dict[i] != NULL)
            {
              mempool_multiple_free_chunk(mpool, mpool->dict[i]);
            }
          else
            {
              break;
            }
        }

      mempool_multiple_free_chunk(mpool, mpool->dict);
      mpool->dict = NULL;
      nxrmutex_destroy(&mpool->lock);
      mpool->free(mpool->arg, mpool);
    }
}

/****************************************************************************
 * Name: mempool_multiple_member
 *
 * Description:
 *   Check if an address lies in the mempool.
 *
 * Input Parameters:
 *   mpool - The handle of the multiple memory pool to be used.
 *   blk   - The pointer of memory block.
 *
 * Returned Value:
 *   true if the address is a member of the mempool.  false if not.
 *
 ****************************************************************************/

bool mempool_multiple_member(FAR struct mempool_multiple_s *mpool,
                             FAR void *blk)
{
  return mempool_multiple_get_dict(mpool, blk) != NULL;
}
