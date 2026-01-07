/****************************************************************************
 * mm/pool_heap/pool_heap.c
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

#include <nuttx/config.h>
#include <nuttx/nuttx.h>

#include <string.h>
#include <assert.h>
#include <debug.h>
#include <sys/prctl.h>
#include <execinfo.h>
#include <sys/types.h>
#include <stdbool.h>
#include <unistd.h>

#include <nuttx/lib/lib.h>
#include <nuttx/sched_note.h>
#include <nuttx/mm/mm.h>
#include <nuttx/mm/kasan.h>
#include <nuttx/mutex.h>
#include <nuttx/sched.h>
#include <nuttx/fs/procfs.h>
#include <nuttx/lib/math32.h>
#include <nuttx/mm/mempool.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct mm_heap_s
{
  FAR const char *name;
  rmutex_t mm_lock;

  FAR void *mm_heapstart[CONFIG_MM_REGIONS];
  FAR void *mm_heapend[CONFIG_MM_REGIONS];
  FAR void *mm_heapnext[CONFIG_MM_REGIONS];

#if CONFIG_MM_REGIONS > 1
  int mm_nregions;
#endif

  size_t mm_curused;
  FAR struct mempool_multiple_s *mm_mpool;
#if defined(CONFIG_FS_PROCFS) && !defined(CONFIG_FS_PROCFS_EXCLUDE_MEMINFO)
  FAR struct procfs_meminfo_entry_s *mm_procfs;
#endif

  /* Kasan is disable or enable for this heap */

  bool mm_nokasan;
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: mempool_memalign
 *
 * Description:
 *   This function call mm_memalign and set mm_backtrace pid to free pid
 *   avoid repeated calculation.
 ****************************************************************************/

static FAR void *mempool_memalign(FAR void *arg, size_t alignment,
                                  size_t size)
{
  FAR struct mm_heap_s *heap = (FAR struct mm_heap_s *)arg;
  FAR void *ret = NULL;
  int i;

  DEBUGVERIFY(nxrmutex_lock(&heap->mm_lock));
  for (i = 0; i < CONFIG_MM_REGIONS; i++)
    {
      FAR char *addr = (FAR char *)heap->mm_heapnext[i];

      addr = (FAR char *)ALIGN_UP((uintptr_t)addr, alignment);
      if (addr + size <= (FAR char *)heap->mm_heapend[i])
        {
          heap->mm_heapnext[i] = addr + size;
          heap->mm_curused += size;
          ret = addr;
          break;
        }
    }

  DEBUGVERIFY(nxrmutex_unlock(&heap->mm_lock));
  return ret;
}

static void mempool_free(FAR void *arg, FAR void *mem)
{
}

static size_t mempool_malloc_size(FAR void *arg, FAR void *mem)
{
  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: mm_addregion
 *
 * Description:
 *   This function adds a region of contiguous memory to the selected heap.
 *
 * Input Parameters:
 *   heap      - The selected heap
 *   heapstart - Start of the heap region
 *   heapsize  - Size of the heap region
 *
 * Returned Value:
 *   None
 *
 * Assumptions:
 *
 ****************************************************************************/

void mm_addregion(FAR struct mm_heap_s *heap, FAR void *heapstart,
                  size_t heapsize)
{
#if CONFIG_MM_REGIONS > 1
  int idx;

  idx = heap->mm_nregions;

  /* Writing past CONFIG_MM_REGIONS would have catastrophic consequences */

  DEBUGASSERT(idx < CONFIG_MM_REGIONS);
  if (idx >= CONFIG_MM_REGIONS)
    {
      return;
    }

#else
#  define idx 0
#endif

#ifdef CONFIG_MM_FILL_ALLOCATIONS
  /* Use the fill value to mark uninitialized user memory */

  memset(heapstart, MM_INIT_MAGIC, heapsize);
#endif

  /* Register to KASan for access check */

  if (!heap->mm_nokasan)
    {
      kasan_register(heapstart, &heapsize);
    }

  DEBUGVERIFY(nxrmutex_lock(&heap->mm_lock));

  minfo("Region %d: base=%p size=%zu\n", idx + 1, heapstart, heapsize);

  /* Save the start and end of the heap */

  heap->mm_heapstart[idx] = heapstart;
  heap->mm_heapend[idx]   = heapstart + heapsize;

#undef idx

#if CONFIG_MM_REGIONS > 1
  heap->mm_nregions++;
#endif

  sched_note_heap(NOTE_HEAP_ADD, heap, heapstart, heapsize,
                  heap->mm_curused);
  DEBUGVERIFY(nxrmutex_unlock(&heap->mm_lock));
}

/****************************************************************************
 * Name: mm_initialize_heap
 *
 * Description:
 *   Initialize the selected heap data structures, providing the initial
 *   heap region.
 *
 * Input Parameters:
 *   config - The heap config structure
 *   heap - The heap instance
 *
 * Returned Value:
 *   None
 *
 * Assumptions:
 *
 ****************************************************************************/

void mm_initialize_heap(FAR const struct mm_heap_config_s *config,
                        FAR struct mm_heap_s **heap)
{
  minfo("Heap: name=%s, start=%p size=%zu\n", config->name, config->start,
        config->size);
  *heap = (struct mm_heap_s *)config->start;
  memset(*heap, 0, sizeof(struct mm_heap_s));
  (*heap)->name = config->name;

  /* Initialize the malloc mutex (to support one-at-
   * a-time access to private data sets).
   */

  nxrmutex_init(&(*heap)->mm_lock);
  (*heap)->mm_curused = sizeof(struct mm_heap_s);
  (*heap)->mm_heapnext[0] = (char *)config->start +
                          sizeof(struct mm_heap_s);
  mm_addregion(*heap, config->start, config->size);

#if defined(CONFIG_FS_PROCFS) && !defined(CONFIG_FS_PROCFS_EXCLUDE_MEMINFO)
#  if defined(CONFIG_BUILD_FLAT) || defined(__KERNEL__)
  (*heap)->mm_procfs = procfs_register_meminfo(config->name, *heap,
                                               NULL, NULL);
#  endif
#endif
}

/****************************************************************************
 * Name: mm_initialize_pool
 *
 * Description:
 *   Initialize the selected multiple mempool data structures, providing the
 *   initial multiple mempool region.
 *
 * Input Parameters:
 *   poolconfig - The multiple mempool config structure
 *   config - The heap config structure
 *   heap - The created heap instance
 *
 * Returned Value:
 *   None
 *
 * Assumptions:
 *
 ****************************************************************************/

void mm_initialize_pool(FAR const struct mm_heap_config_s *config,
                        FAR const struct mm_pool_config_s *poolconfig,
                        FAR struct mm_heap_s **heap)
{
  struct mm_pool_config_s def;

  DEBUGASSERT(poolconfig);
  DEBUGASSERT(poolconfig->poolsize);
  DEBUGASSERT(poolconfig->npools);

  memcpy(&def, poolconfig, sizeof(struct mm_pool_config_s));

  if (def.threshold == 0u)
    {
      def.threshold = CONFIG_MM_HEAP_MEMPOOL_THRESHOLD;
    }

  if (def.chunksize == 0u)
    {
      def.chunksize = CONFIG_MM_HEAP_MEMPOOL_CHUNK_SIZE;
    }

  if (def.init_chunksize == 0u)
    {
      def.init_chunksize = CONFIG_MM_HEAP_MEMPOOL_INIT_CHUNK_SIZE;
    }

  if (def.expandsize == 0u)
    {
      def.expandsize = CONFIG_MM_HEAP_MEMPOOL_EXPAND_SIZE;
    }

  if (def.dict_expendsize == 0u)
    {
      def.dict_expendsize = CONFIG_MM_HEAP_MEMPOOL_DICTIONARY_EXPAND_SIZE;
    }

  mm_initialize_heap(config, heap);

  /* Initialize the multiple mempool in heap */

  if (def.poolsize != NULL && def.npools != 0u)
    {
      (*heap)->mm_mpool = mempool_multiple_init(config->name,
                                                &def,
                                                mempool_memalign,
                                                mempool_malloc_size,
                                                mempool_free,
                                                *heap);
      DEBUGASSERT((*heap)->mm_mpool);
    }
}

/****************************************************************************
 * Name: mm_heapmember
 *
 * Description:
 *   Check if an address lies in the heap.
 *
 * Parameters:
 *   heap - The heap to check
 *   mem  - The address to check
 *
 * Return Value:
 *   true if the address is a member of the heap.  false if not
 *   not.  If the address is not a member of the heap, then it
 *   must be a member of the user-space heap (unchecked)
 *
 ****************************************************************************/

bool mm_heapmember(FAR struct mm_heap_s *heap, FAR void *mem)
{
  return mempool_multiple_member(heap->mm_mpool, mem);
}

/****************************************************************************
 * Name: mm_malloc
 *
 * Description:
 *  Find the smallest chunk that satisfies the request. Take the memory from
 *  that chunk, save the remaining, smaller chunk (if any).
 *
 *  8-byte alignment of the allocated data is assured.
 *
 ****************************************************************************/

FAR void *mm_malloc(FAR struct mm_heap_s *heap, size_t size)
{
  return mempool_multiple_alloc(heap->mm_mpool, size);
}

/****************************************************************************
 * Name: mm_memalign
 *
 * Description:
 *   memalign requests more than enough space from malloc, finds a region
 *   within that chunk that meets the alignment request and then frees any
 *   leading or trailing space.
 *
 *   The alignment argument must be a power of two. 16-byte alignment is
 *   guaranteed by normal malloc calls.
 *
 ****************************************************************************/

FAR void *mm_memalign(FAR struct mm_heap_s *heap, size_t alignment,
                      size_t size)
{
  return mempool_multiple_memalign(heap->mm_mpool, alignment, size);
}

/****************************************************************************
 * Name: mm_zalloc
 *
 * Description:
 *   mm_zalloc calls mm_malloc, then zeroes out the allocated chunk.
 *
 ****************************************************************************/

FAR void *mm_zalloc(FAR struct mm_heap_s *heap, size_t size)
{
  FAR void *alloc = mm_malloc(heap, size);
  if (alloc)
    {
       memset(alloc, 0, size);
    }

  return alloc;
}

/****************************************************************************
 * Name: mm_realloc
 *
 * Description:
 *   If the reallocation is for less space, then:
 *
 *     (1) the current allocation is reduced in size
 *     (2) the remainder at the end of the allocation is returned to the
 *         free list.
 *
 *  If the request is for more space and the current allocation can be
 *  extended, it will be extended by:
 *
 *     (1) Taking the additional space from the following free chunk, or
 *     (2) Taking the additional space from the preceding free chunk.
 *     (3) Or both
 *
 *  If the request is for more space but the current chunk cannot be
 *  extended, then malloc a new buffer, copy the data into the new buffer,
 *  and free the old buffer.
 *
 ****************************************************************************/

FAR void *mm_realloc(FAR struct mm_heap_s *heap, FAR void *oldmem,
                     size_t size)
{
  return mempool_multiple_realloc(heap->mm_mpool, oldmem, size);
}

/****************************************************************************
 * Name: mm_calloc
 *
 * Descriptor:
 *   mm_calloc() calculates the size of the allocation and calls mm_zalloc()
 *
 ****************************************************************************/

FAR void *mm_calloc(FAR struct mm_heap_s *heap, size_t n, size_t elem_size)
{
  FAR void *mem = NULL;

  /* Verify input parameters
   *
   * elem_size or n is zero treats as valid input.
   *
   * Assure that the following multiplication cannot overflow the size_t
   * type, i.e., that:  SIZE_MAX >= n * elem_size
   *
   * Refer to SEI CERT C Coding Standard.
   */

  if (elem_size == 0u || n <= (SIZE_MAX / elem_size))
    {
      mem = mm_zalloc(heap, n * elem_size);
    }

  return mem;
}

/****************************************************************************
 * Name: mm_free
 *
 * Description:
 *   Returns a chunk of memory to the list of free nodes,  merging with
 *   adjacent free chunks if possible.
 *
 ****************************************************************************/

void mm_free(FAR struct mm_heap_s *heap, FAR void *mem)
{
  /* Free mempool blk */

  mempool_multiple_free(heap->mm_mpool, mem);
}

/****************************************************************************
 * Name: mm_delayfree
 *
 * Description:
 *   Add mem to delaylist, mem will be freed after a while.
 *
 ****************************************************************************/

void mm_delayfree(FAR struct mm_heap_s *heap, FAR void *mem)
{
  mm_free(heap, mem);
}

/****************************************************************************
 * Name: mm_mallinfo
 *
 * Description:
 *   mallinfo returns a copy of updated current heap information.
 *
 ****************************************************************************/

struct mallinfo mm_mallinfo(FAR struct mm_heap_s *heap)
{
  return mempool_multiple_mallinfo(heap->mm_mpool);
}

/****************************************************************************
 * Name: mm_mallinfo_task
 *
 * Description:
 *   mallinfo returns a copy of updated current heap information for task
 *   with pid.
 *
 ****************************************************************************/

struct mallinfo_task mm_mallinfo_task(FAR struct mm_heap_s *heap,
                                      FAR const struct malltask *task)
{
  return mempool_multiple_info_task(heap->mm_mpool, task);
}

/****************************************************************************
 * Name: mm_heapfree
 *
 * Description:
 *   Return the total free size (in bytes) in the heap
 *
 ****************************************************************************/

size_t mm_heapfree(FAR struct mm_heap_s *heap)
{
  struct mallinfo info;

  info = mm_mallinfo(heap);
  return info.fordblks;
}

/****************************************************************************
 * Name: mm_heapfree_largest
 *
 * Description:
 *   Return the largest chunk of contiguous memory in the heap
 *
 ****************************************************************************/

size_t mm_heapfree_largest(FAR struct mm_heap_s *heap)
{
  struct mallinfo info;

  info = mm_mallinfo(heap);
  return info.mxordblk;
}

size_t mm_malloc_size(FAR struct mm_heap_s *heap, FAR void *mem)
{
  ssize_t size;

  size = mempool_multiple_alloc_size(heap->mm_mpool, mem);
  if (size < 0)
    {
      size = 0;
    }

  return (size_t)size;
}

/****************************************************************************
 * Name: mm_memdump
 *
 * Description:
 *   mm_memdump returns a memory info about specified pid of task/thread.
 *   if pid equals -1, this function will dump all allocated node and output
 *   backtrace for every allocated node for this heap, if pid equals -2, this
 *   function will dump all free node for this heap, and if pid is greater
 *   than or equal to 0, will dump pid allocated node and output backtrace.
 ****************************************************************************/

void mm_memdump(FAR struct mm_heap_s *heap,
                FAR const struct mm_memdump_s *dump)
{
  struct mallinfo_task info;
  pid_t pid = dump->pid;

  memset(&info, 0, sizeof(struct mallinfo_task));
  switch (pid)
    {
      case PID_MM_MEMPOOL:
        syslog(LOG_INFO, "Memdump mempool\n");
        break;
      case PID_MM_LEAK:
        syslog(LOG_INFO, "Memdump leak\n");
        info = mempool_multiple_info_task(heap->mm_mpool, dump);
        break;
      case PID_MM_ALLOC:
        syslog(LOG_INFO, "Dump all used memory node info\n");
        info = mempool_multiple_info_task(heap->mm_mpool, dump);
        break;
      case PID_MM_FREE:
        syslog(LOG_INFO, "Dump all free memory node info\n");
        break;
      case PID_MM_ORPHAN:
        syslog(LOG_INFO, "Dump allocated orphan node\n");
        break;

      default:
        if (pid >= 0)
          {
            char name[CONFIG_TASK_NAME_SIZE + 1];

            memset(name, 0, sizeof(name));

            if (prctl(PR_GET_NAME_EXT, name, pid) < 0)
              {
                syslog(LOG_INFO, "Memdump task Unknown %d\n", get_errno());
              }
            else
              {
                syslog(LOG_INFO, "Memdump task %s\n", name);
              }

            info = mempool_multiple_info_task(heap->mm_mpool,
                                              dump);
          }
        else
          {
            syslog(LOG_INFO, "Memdump no support %d\n", pid);
          }

        break;
    }

  syslog(LOG_INFO,
#ifdef CONFIG_MM_RECORD_PID
                   "%6s"
#endif
                   "%12s%9s"
#  ifdef CONFIG_MM_RECORD_SEQNO
                   "%12s"
#  endif
                   "%*s "
#ifdef CONFIG_MM_RECORD_STACK
                   "%s"
#endif
                   "\n",
#ifdef CONFIG_MM_RECORD_PID
                   "PID",
#endif
                   "Size", "Overhead",
#ifdef CONFIG_MM_RECORD_SEQNO
                   "Sequence",
#  endif
                    BACKTRACE_PTR_FMT_WIDTH,
                   "Address"
#ifdef CONFIG_MM_RECORD_STACK
                   , " Backtrace"
#endif
                  );

  if (info.aordblks > 0u)
    {
      mempool_multiple_memdump(heap->mm_mpool, dump);
    }

  syslog(LOG_INFO, "%12s%12s\n", "Total Blks", "Total Size");
  syslog(LOG_INFO, "%12zu%12zu\n", info.aordblks, info.uordblks);
}

/****************************************************************************
 * Name: mm_extend
 *
 * Description:
 *   Extend a heap region by add a block of (virtually) contiguous memory
 *   to the end of the heap.
 *
 ****************************************************************************/

void mm_extend(FAR struct mm_heap_s *heap, FAR void *mem, size_t size,
               int region)
{
  /* Make sure that we were passed valid parameters */

#if CONFIG_MM_REGIONS > 1
  DEBUGASSERT(region >= 0 && region < heap->mm_nregions);
#else
  DEBUGASSERT(region == 0);
#endif
  DEBUGASSERT(mem == heap->mm_heapend[region]);

  /* Take the memory manager mutex */

  DEBUGVERIFY(nxrmutex_lock(&heap->mm_lock));

  /* Save the new size */

  heap->mm_heapend[region] += size;

  DEBUGVERIFY(nxrmutex_unlock(&heap->mm_lock));
}

/****************************************************************************
 * Name: mm_brkaddr
 *
 * Description:
 *   Return the break address of a heap region.  Zero is returned if the
 *   memory region is not initialized.
 *
 ****************************************************************************/

FAR void *mm_brkaddr(FAR struct mm_heap_s *heap, int region)
{
#if CONFIG_MM_REGIONS > 1
  DEBUGASSERT(region >= 0 && region < heap->mm_nregions);
#else
  DEBUGASSERT(region == 0);
#endif

  return heap->mm_heapend[region];
}

/****************************************************************************
 * Name: mm_sbrk
 *
 * Description:
 *    The sbrk() function is used to change the amount of space allocated
 *    for the calling process. The change is made by resetting the process's
 *    break value and allocating the appropriate amount of space.  The amount
 *    of allocated space increases as the break value increases.
 *
 *    The sbrk() function adds 'incr' bytes to the break value and changes
 *    the allocated space accordingly. If incr is negative, the amount of
 *    allocated space is decreased by incr bytes. The current value of the
 *    program break is returned by sbrk(0).
 *
 * Input Parameters:
 *    heap - The heap to be used.
 *    incr - Specifies the number of bytes to add or to remove from the
 *      space allocated for the process.
 *    mem  - The address of the new program break.
 *
 * Returned Value:
 *    ENOMEM - The requested change would allocate more space than
 *      allowed under system limits.
 *    EAGAIN - The total amount of system memory available for allocation
 *      to this process is temporarily insufficient. This may occur even
 *      though the space requested was less than the maximum data segment
 *      size.
 *
 ****************************************************************************/

#ifdef CONFIG_BUILD_KERNEL
int mm_sbrk(FAR struct mm_heap_s *heap, intptr_t incr, FAR void **mem)
{
  uintptr_t brkaddr;
  uintptr_t allocbase;
  unsigned int pgincr;
  size_t bytesize;

  DEBUGASSERT(incr >= 0);
  if (incr < 0)
    {
      return -ENOSYS;
    }

  /* Get the current break address (NOTE: assumes region 0). */

  brkaddr = (uintptr_t)mm_brkaddr(heap, 0);
  if (incr > 0)
    {
      /* Convert the increment to multiples of the page size */

      pgincr = MM_NPAGES(incr);

      /* Allocate the requested number of pages and map them to the
       * break address.
       */

      allocbase = pgalloc(brkaddr, pgincr);
      if (allocbase == 0)
        {
          return -EAGAIN;
        }

      /* Extend the heap (region 0) */

      bytesize = pgincr << MM_PGSHIFT;
      mm_extend(heap, (FAR void *)allocbase, bytesize, 0);
    }

  if (mem)
    {
      *mem = (FAR void *)brkaddr;
    }

  return 0;
}

#endif
