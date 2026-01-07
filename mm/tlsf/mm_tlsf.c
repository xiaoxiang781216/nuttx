/****************************************************************************
 * mm/tlsf/mm_tlsf.c
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

#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <debug.h>
#include <execinfo.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <sys/param.h>

#include <nuttx/arch.h>
#include <nuttx/fs/procfs.h>
#include <nuttx/lib/lib.h>
#include <nuttx/mutex.h>
#include <nuttx/mm/mm.h>
#include <nuttx/mm/kasan.h>
#include <nuttx/mm/mempool.h>
#include <nuttx/sched_note.h>
#include <nuttx/tls.h>
#include <nuttx/pgalloc.h>

#include "tlsf/tlsf.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#if CONFIG_MM_HEAP_MEMPOOL_THRESHOLD > 0
#  define MEMPOOL_NPOOLS (CONFIG_MM_HEAP_MEMPOOL_THRESHOLD / tlsf_align_size())
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct mm_delaynode_s
{
  FAR struct mm_delaynode_s *flink;
};

struct mm_delayhead_s
{
  FAR struct mm_delaynode_s *head;

#if CONFIG_MM_FREE_DELAYCOUNT_MAX > 0
  size_t delaycount;
#endif
};

#ifdef CONFIG_SMP
typedef struct mm_delayhead_s mm_delaylist_t[CONFIG_SMP_NCPUS];
#else
typedef struct mm_delayhead_s mm_delaylist_t;
#endif

struct mm_heap_s
{
  /* Mutually exclusive access to this data set is enforced with
   * the following un-named mutex.
   */

  rmutex_t mm_lock;

  /* This is the size of the heap provided to mm */

  size_t mm_heapsize;

  /* This is the heap maximum used memory size */

  size_t mm_maxused;

  /* This is the current used size of the heap */

  size_t mm_curused;

  /* This is the first and last of the heap */

  FAR void *mm_heapstart[CONFIG_MM_REGIONS];
  FAR void *mm_heapend[CONFIG_MM_REGIONS];

#if CONFIG_MM_REGIONS > 1
  int mm_nregions;
#endif

  tlsf_t mm_tlsf; /* The tlfs context */

  /* The is a multiple mempool of the heap */

#ifdef CONFIG_MM_HEAP_MEMPOOL
  size_t                         mm_threshold;
  FAR struct mempool_multiple_s *mm_mpool;
#endif

  /* Free delay list, for some situation can't do free immediately */

  mm_delaylist_t delay;

#if defined(CONFIG_FS_PROCFS) && !defined(CONFIG_FS_PROCFS_EXCLUDE_MEMINFO)
  FAR struct procfs_meminfo_entry_s *mm_procfs;
#endif

  /* Kasan is disable or enable for this heap */

  bool mm_nokasan;
};

#ifdef CONFIG_MM_RECORD
struct memdump_record_s
{
#ifdef CONFIG_MM_RECORD_PID
  pid_t pid;                                /* The pid for caller */
#endif
#ifdef CONFIG_MM_RECORD_SEQNO
  unsigned long seqno;                      /* The sequence of memory malloc */
#endif
#ifdef CONFIG_MM_RECORD_STACK
  FAR void *stack;                          /* The backtrace buffer for caller */
#endif
};
#endif

struct mm_mallinfo_handler_s
{
  FAR const struct malltask *task;
  FAR struct mallinfo_task *info;
};

#if CONFIG_MM_HEAP_BIGGEST_COUNT > 0
struct mm_tlsf_node_s
{
  FAR void *ptr;
  size_t size;
};
#endif

struct mm_memdump_priv_s
{
  struct mm_memdump_s dump;
  struct mallinfo_task info;
#if CONFIG_MM_HEAP_BIGGEST_COUNT > 0
  struct mm_tlsf_node_s node[CONFIG_MM_HEAP_BIGGEST_COUNT];
  size_t filled;
#endif
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#ifdef CONFIG_MM_HEAP_MEMPOOL
static inline_function
void memdump_info_pool(FAR struct mm_memdump_priv_s *priv,
                       FAR struct mm_heap_s *heap)
{
  priv->info = mempool_multiple_info_task(heap->mm_mpool, &priv->dump);
}

static inline_function
void memdump_dump_pool(FAR struct mm_memdump_priv_s *priv,
                       FAR struct mm_heap_s *heap)
{
  if (priv->info.aordblks > 0)
    {
      mempool_multiple_memdump(heap->mm_mpool, &priv->dump);
    }
}
#else
#  define memdump_info_pool(priv,heap)
#  define memdump_dump_pool(priv,heap)
#endif

static void memdump_allocnode(FAR void *ptr, size_t size)
{
#ifdef CONFIG_MM_RECORD
  FAR struct memdump_record_s *buf =
    ptr + size - sizeof(struct memdump_record_s);
#endif
#ifdef CONFIG_MM_RECORD_STACK
  char tmp[BACKTRACE_BUFFER_SIZE(CONFIG_LIBC_BACKTRACE_DEPTH)] = "";
  FAR void **stack;
  int stacksize;

  stack = backtrace_get(buf->stack, &stacksize);
  if (stack && stacksize)
    {
      backtrace_format(tmp, sizeof(tmp), stack, stacksize);
    }
#else
  const char *tmp = "";
#endif

  syslog(LOG_INFO,
#ifdef CONFIG_MM_RECORD_PID
         "%6d"
#endif
         "%12zu"
#ifdef CONFIG_MM_RECORD_SEQNO
         "%12lu"
#endif
         "%*p %s\n",
#ifdef CONFIG_MM_RECORD_PID
         buf->pid,
#endif
         size,
#ifdef CONFIG_MM_RECORD_SEQNO
         buf->seqno,
#endif
         BACKTRACE_PTR_FMT_WIDTH, ptr, tmp);
}

#if CONFIG_MM_HEAP_BIGGEST_COUNT > 0
static int memdump_record_comare(FAR const void *a, FAR const void *b)
{
  FAR struct mm_tlsf_node_s *node_a = (FAR struct mm_tlsf_node_s *)a;
  FAR struct mm_tlsf_node_s *node_b = (FAR struct mm_tlsf_node_s *)b;
  size_t size_a = node_a->size;
  size_t size_b = node_b->size;
  return size_a > size_b ? 1 : -1;
}

static void memdump_record_biggest(FAR struct mm_memdump_priv_s *priv,
                                   FAR void *ptr, size_t size)
{
  if (priv->filled < CONFIG_MM_HEAP_BIGGEST_COUNT)
    {
      priv->node[priv->filled].ptr  = ptr;
      priv->node[priv->filled].size = size;
      priv->filled++;
    }
  else
    {
      if (size <= priv->node[0].size)
        {
          return;
        }

      priv->node[0].ptr  = ptr;
      priv->node[0].size = size;
    }

  if (priv->filled > 1)
    {
      qsort(priv->node, priv->filled, sizeof(struct mm_tlsf_node_s),
            memdump_record_comare);
    }
}

static void memdump_dump_biggestnodes(FAR struct mm_memdump_priv_s *priv)
{
  size_t i;
  for (i = 0; i < priv->filled; i++)
    {
      priv->info.uordblks += priv->node[i].size;
      memdump_allocnode(priv->node[i].ptr, priv->node[i].size);
    }

  priv->info.aordblks = priv->filled;
}
#endif

#ifdef CONFIG_MM_RECORD
/****************************************************************************
 * Name: memdump_backtrace
 ****************************************************************************/

static void memdump_backtrace(FAR struct mm_heap_s *heap,
                              FAR struct memdump_record_s *buf)
{
#  ifdef CONFIG_MM_RECORD_STACK
  FAR struct tls_info_s *info = tls_get_info();
#  endif

#  ifdef CONFIG_MM_RECORD_PID
  buf->pid = _SCHED_GETTID();
#  endif
  MM_INCSEQNO(buf);
#  ifdef CONFIG_MM_RECORD_STACK
  if ((heap->mm_procfs && heap->mm_procfs->backtrace) ||
      (info && info->tl_flags & TLS_FLAG_HEAP_DUMP))
    {
      buf->stack = backtrace_record(CONFIG_LIBC_BACKTRACE_DEPTH);
    }
#  endif
}
#endif

/****************************************************************************
 * Name: get_delayhead
 ****************************************************************************/

static inline_function
FAR struct mm_delayhead_s *get_delayhead(FAR struct mm_heap_s *heap)
{
#ifdef CONFIG_SMP
  return &heap->delay[this_cpu()];
#else
  return &heap->delay;
#endif
}

/****************************************************************************
 * Name: add_delaylist
 ****************************************************************************/

static void add_delaylist(FAR struct mm_heap_s *heap, FAR void *mem,
                          bool asan_check)
{
#if defined(CONFIG_BUILD_FLAT) || defined(__KERNEL__)
  FAR struct mm_delayhead_s *delay = get_delayhead(heap);
  FAR struct mm_delaynode_s *tmp = mem;
  irqstate_t flags;

  /* Delay the deallocation until a more appropriate time. */

  flags = up_irq_save();

  tmp->flink  = delay->head;
  delay->head = tmp;

#  if CONFIG_MM_FREE_DELAYCOUNT_MAX > 0
  delay->delaycount++;
#  endif

  if (asan_check)
    {
      size_t size = mm_malloc_size(heap, mem);
#  ifdef CONFIG_MM_FILL_ALLOCATIONS
      memset(mem, MM_FREE_MAGIC, size);
#  endif
      kasan_poison(mem, size);
      UNUSED(size);
    }

  up_irq_restore(flags);
#endif
}

/****************************************************************************
 * Name: forcefree
 *
 * Description:
 *   Free memory immediately.
 *
 ****************************************************************************/

static void forcefree(FAR struct mm_heap_s *heap, FAR void *mem)
{
  size_t size;

  /* Free mempool blk first */

#ifdef CONFIG_MM_HEAP_MEMPOOL
  if (heap->mm_mpool && mempool_multiple_free(heap->mm_mpool, mem) >= 0)
    {
      return;
    }
#endif

  DEBUGVERIFY(nxrmutex_lock(&heap->mm_lock));
  size = mm_malloc_size(heap, mem);
#ifdef CONFIG_MM_FILL_ALLOCATIONS
  memset(mem, MM_FREE_MAGIC, size);
#endif
  kasan_poison(mem, size);

  /* Update heap statistics */

  heap->mm_curused -= size;
  sched_note_heap(NOTE_HEAP_FREE, heap, mem, size, heap->mm_curused);
  tlsf_free(heap->mm_tlsf, mem);
  DEBUGVERIFY(nxrmutex_unlock(&heap->mm_lock));
}

/****************************************************************************
 * Name: free_delaylist
 ****************************************************************************/

static bool free_delaylist(FAR struct mm_heap_s *heap, bool force)
{
  bool ret = false;
#if defined(CONFIG_BUILD_FLAT) || defined(__KERNEL__)
  FAR struct mm_delayhead_s *delay = get_delayhead(heap);
  FAR struct mm_delaynode_s *tmp;
  irqstate_t flags;

  /* Move the delay list to local */

  flags = up_irq_save();

  tmp = delay->head;

#  if CONFIG_MM_FREE_DELAYCOUNT_MAX > 0
  if (tmp == NULL || (!force &&
      delay->delaycount < CONFIG_MM_FREE_DELAYCOUNT_MAX))
    {
      up_irq_restore(flags);
      return false;
    }

  delay->delaycount = 0;
#  endif

  delay->head = NULL;

  up_irq_restore(flags);

  /* Test if the delayed is empty */

  ret = !!tmp;

  while (tmp)
    {
      FAR void *address;

      /* Get the first delayed deallocation */

      address = tmp;
      tmp = tmp->flink;

      /* The address should always be non-NULL since that was checked in the
       * 'while' condition above.
       */

      forcefree(heap, address);
    }

#endif
  return ret;
}

/****************************************************************************
 * Name: mempool_memalign
 *
 * Description:
 *   This function call mm_memalign and set mm_backtrace pid to free pid
 *   avoid repeated calculation.
 ****************************************************************************/

#if defined(CONFIG_MM_HEAP_MEMPOOL) && defined(CONFIG_MM_RECORD_PID)
static FAR void *mempool_memalign(FAR void *arg, size_t alignment,
                                  size_t size)
{
  FAR struct memdump_record_s *buf;
  FAR void *ret;

  ret = mm_memalign(arg, alignment, size);
  if (ret)
    {
      buf = ret + mm_malloc_size(arg, ret);
      buf->pid = PID_MM_MEMPOOL;
    }

  return ret;
}
#else
#  define mempool_memalign ((mempool_multiple_alloc_t)mm_memalign)
#endif

#  define mempool_malloc_size ((mempool_multiple_alloc_size_t)mm_malloc_size)
#  define mempool_free ((mempool_multiple_free_t)mm_free)

/****************************************************************************
 * Name: mallinfo_handler
 ****************************************************************************/

static void mallinfo_handler(FAR void *ptr, size_t size, int used,
                             FAR void *user)
{
  FAR struct mallinfo *info = user;

  if (!used)
    {
      info->ordblks++;
      info->fordblks += size;
      if (size > info->mxordblk)
        {
          info->mxordblk = size;
        }
    }
  else
    {
      info->aordblks++;
    }
}

/****************************************************************************
 * Name: mallinfo_task_handler
 ****************************************************************************/

static void mallinfo_task_handler(FAR void *ptr, size_t size, int used,
                                  FAR void *user)
{
  FAR struct mm_mallinfo_handler_s *handler = user;
  FAR const struct malltask *task = handler->task;
  FAR struct mallinfo_task *info = handler->info;

  if (used)
    {
#ifdef CONFIG_MM_RECORD
      FAR struct memdump_record_s *buf =
        ptr + size - sizeof(struct memdump_record_s);
#else
#  define buf NULL
#endif

      if ((MM_DUMP_ASSIGN(task, buf) || MM_DUMP_ALLOC(task, buf) ||
           MM_DUMP_LEAK(task, buf)) && MM_DUMP_SEQNO(task, buf))
        {
          info->aordblks++;
          info->uordblks += size;
        }
#undef buf
    }
  else if (task->pid == PID_MM_FREE)
    {
      info->aordblks++;
      info->uordblks += size;
    }
}

/****************************************************************************
 * Name: memdump_handler
 ****************************************************************************/

static void memdump_handler(FAR void *ptr, size_t size, int used,
                            FAR void *user)
{
  FAR struct mm_memdump_priv_s *priv = user;
  FAR struct mm_memdump_s *dump = &priv->dump;

  if (used)
    {
#ifdef CONFIG_MM_RECORD
      FAR struct memdump_record_s *buf =
        ptr + size - sizeof(struct memdump_record_s);
#else
#  define buf NULL
#endif
#if CONFIG_MM_HEAP_BIGGEST_COUNT > 0
      if (dump->pid == PID_MM_BIGGEST && MM_DUMP_SEQNO(dump, buf))
        {
          memdump_record_biggest(priv, ptr, size);
        }
      else
#endif
      if ((MM_DUMP_ASSIGN(dump, buf) || MM_DUMP_ALLOC(dump, buf) ||
           MM_DUMP_LEAK(dump, buf)) && MM_DUMP_SEQNO(dump, buf))
        {
          priv->info.aordblks++;
          priv->info.uordblks += size;
          memdump_allocnode(ptr, size);
        }
#undef buf
    }
  else if (dump->pid == PID_MM_FREE)
    {
      priv->info.aordblks++;
      priv->info.uordblks += size;
      syslog(LOG_INFO, "%12zu%*p\n", size, BACKTRACE_PTR_FMT_WIDTH, ptr);
    }
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

  /* Add the size of this region to the total size of the heap */

  heap->mm_heapsize += heapsize;

  /* Save the start and end of the heap */

  heap->mm_heapstart[idx] = heapstart;
  heap->mm_heapend[idx]   = heapstart + heapsize;

#undef idx

#if CONFIG_MM_REGIONS > 1
  heap->mm_nregions++;
#endif

  /* Add memory to the tlsf pool */

  tlsf_add_pool(heap->mm_tlsf, heapstart, heapsize);
  sched_note_heap(NOTE_HEAP_ADD, heap, heapstart, heapsize,
                  heap->mm_curused);
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

  if (elem_size == 0 || n <= (SIZE_MAX / elem_size))
    {
      mem = mm_zalloc(heap, n * elem_size);
    }

  return mem;
}

#ifdef CONFIG_DEBUG_MM
/****************************************************************************
 * Name: mm_checkcorruption
 *
 * Description:
 *   mm_checkcorruption is used to check whether memory heap is normal.
 *
 ****************************************************************************/

void mm_checkcorruption(FAR struct mm_heap_s *heap)
{
#if CONFIG_MM_REGIONS > 1
  int region;
#else
#  define region 0
#endif

  free_delaylist(heap, true);

  /* Visit each region */

#if CONFIG_MM_REGIONS > 1
  for (region = 0; region < heap->mm_nregions; region++)
#endif
    {
      /* Retake the mutex for each region to reduce latencies */

      DEBUGVERIFY(nxrmutex_lock(&heap->mm_lock));

      /* Check tlsf control block in the first pass */

      if (region == 0)
        {
          tlsf_check(heap->mm_tlsf);
        }

      /* Check tlsf pool in each iteration temporarily */

      tlsf_check_pool(heap->mm_heapstart[region]);

      /* Release the mutex */

      DEBUGVERIFY(nxrmutex_unlock(&heap->mm_lock));
    }
#undef region
}
#endif

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
  size_t oldsize;

  /* Make sure that we were passed valid parameters */

#if CONFIG_MM_REGIONS > 1
  DEBUGASSERT(region >= 0 && region < heap->mm_nregions);
#else
  DEBUGASSERT(region == 0);
#endif
  DEBUGASSERT(mem == heap->mm_heapend[region]);

  /* Take the memory manager mutex */

  DEBUGVERIFY(nxrmutex_lock(&heap->mm_lock));

  /* Extend the tlsf pool */

  oldsize = heap->mm_heapend[region] - heap->mm_heapstart[region];
  tlsf_extend_pool(heap->mm_tlsf, heap->mm_heapstart[region], oldsize, size);

  /* Save the new size */

  heap->mm_heapsize += size;
  heap->mm_heapend[region] += size;

  DEBUGVERIFY(nxrmutex_unlock(&heap->mm_lock));
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
  minfo("Freeing %p\n", mem);

  /* Protect against attempts to free a NULL reference */

  if (mem == NULL)
    {
      return;
    }

  DEBUGASSERT(mm_heapmember(heap, mem));

#if CONFIG_MM_FREE_DELAYCOUNT_MAX > 0
  add_delaylist(heap, mem, true);
#else
  forcefree(heap, mem);
#endif
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
  minfo("Adding delaylist %p\n", mem);

  /* Protect against attempts to free a NULL reference */

  if (mem == NULL)
    {
      return;
    }

  DEBUGASSERT(mm_heapmember(heap, mem));
  add_delaylist(heap, mem, false);
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
#if CONFIG_MM_REGIONS > 1
  int i;

  /* A valid address from the heap for this region would have to lie
   * between the region's two guard nodes.
   */

  for (i = 0; i < heap->mm_nregions; i++)
    {
      if (mem >= heap->mm_heapstart[i] &&
          mem < heap->mm_heapend[i])
        {
          return true;
        }
    }

  /* The address does not like any any region assigned to the heap */

  return false;

#else
  /* A valid address from the heap would have to lie between the
   * two guard nodes.
   */

  if (mem >= heap->mm_heapstart[0] &&
      mem < heap->mm_heapend[0])
    {
      return true;
    }

  /* Otherwise, the address does not lie in the heap */

  return false;

#endif
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
 *   Node
 *
 * Assumptions:
 *
 ****************************************************************************/

void mm_initialize_heap(FAR const struct mm_heap_config_s *config,
                        FAR struct mm_heap_s **heap)
{
  FAR const char *name = config->name;
  FAR void *heapstart = config->start;
  size_t heapsize = config->size;

  minfo("Heap: name=%s start=%p size=%zu\n", name, heapstart, heapsize);
  if (!config->allocheap)
    {
      /* Reserve a block space for mm_heap_s context */

      DEBUGASSERT(heapsize > sizeof(struct mm_heap_s));
      *heap = (FAR struct mm_heap_s *)heapstart;
      heapstart += sizeof(struct mm_heap_s);
      heapsize -= sizeof(struct mm_heap_s);

      memset(*heap, 0, sizeof(struct mm_heap_s));
      (*heap)->mm_curused = sizeof(struct mm_heap_s);
    }
  else
    {
      *heap = lib_memalign(MM_ALIGN, sizeof(struct mm_heap_s));
      DEBUGASSERT(*heap != NULL);

      memset(*heap, 0, sizeof(struct mm_heap_s));
    }

  (*heap)->mm_nokasan = config->nokasan;

  /* Allocate and create TLSF context */

  DEBUGASSERT(heapsize > tlsf_size());
  (*heap)->mm_tlsf = tlsf_create(heapstart);
  heapstart += tlsf_size();
  heapsize -= tlsf_size();

  /* Initialize the malloc mutex (to support one-at-
   * a-time access to private data sets).
   */

  nxrmutex_init(&(*heap)->mm_lock);

  /* Add the initial region of memory to the heap */

  mm_addregion(*heap, heapstart, heapsize);

#if defined(CONFIG_FS_PROCFS) && !defined(CONFIG_FS_PROCFS_EXCLUDE_MEMINFO)
#if defined(CONFIG_BUILD_FLAT) || defined(__KERNEL__)
  (*heap)->mm_procfs = procfs_register_meminfo(name, *heap, NULL, NULL);
#endif
#endif
}

#ifdef CONFIG_MM_HEAP_MEMPOOL
void mm_initialize_pool(FAR const struct mm_heap_config_s *config,
                        FAR const struct mm_pool_config_s *poolconfig,
                        FAR struct mm_heap_s **heap)
{
#if CONFIG_MM_HEAP_MEMPOOL_THRESHOLD > 0
  size_t poolsize[MEMPOOL_NPOOLS];
#endif
  struct mm_pool_config_s def;

  if (poolconfig)
    {
      memcpy(&def, poolconfig, sizeof(struct mm_pool_config_s));
    }
  else
    {
      memset(&def, 0, sizeof(struct mm_pool_config_s));
    }

#if CONFIG_MM_HEAP_MEMPOOL_THRESHOLD > 0
  if (def.poolsize == 0 || def.npools == 0)
    {
      int i;

      for (i = 0; i < MEMPOOL_NPOOLS; i++)
        {
#if CONFIG_MM_MIN_BLKSIZE != 0
          poolsize[i] = (i + 1) * CONFIG_MM_MIN_BLKSIZE;
#else
          poolsize[i] = (i + 1) * tlsf_align_size();
#endif
        }

      def.poolsize = poolsize;
      def.npools = MEMPOOL_NPOOLS;
    }

  if (def.threshold == 0)
    {
      def.threshold = CONFIG_MM_HEAP_MEMPOOL_THRESHOLD;
    }

  if (def.chunksize == 0)
    {
      def.chunksize = CONFIG_MM_HEAP_MEMPOOL_CHUNK_SIZE;
    }

  if (def.init_chunksize == 0)
    {
      def.init_chunksize = CONFIG_MM_HEAP_MEMPOOL_INIT_CHUNK_SIZE;
    }

  if (def.expandsize == 0)
    {
      def.expandsize = CONFIG_MM_HEAP_MEMPOOL_EXPAND_SIZE;
    }

  if (def.dict_expendsize == 0)
    {
      def.dict_expendsize = CONFIG_MM_HEAP_MEMPOOL_DICTIONARY_EXPAND_SIZE;
    }
#endif

  mm_initialize_heap(config, heap);

  /* Initialize the multiple mempool in heap */

  (*heap)->mm_threshold = def.threshold;
  if (def.poolsize != NULL && def.npools != 0)
    {
      (*heap)->mm_mpool = mempool_multiple_init(config->name,
                                                &def,
                                                mempool_memalign,
                                                mempool_malloc_size,
                                                mempool_free,
                                                *heap);
    }
}
#endif

/****************************************************************************
 * Name: mm_mallinfo
 *
 * Description:
 *   mallinfo returns a copy of updated current heap information.
 *
 ****************************************************************************/

struct mallinfo mm_mallinfo(FAR struct mm_heap_s *heap)
{
  struct mallinfo info;
#ifdef CONFIG_MM_HEAP_MEMPOOL
  struct mallinfo poolinfo;
#endif
#if CONFIG_MM_REGIONS > 1
  int region;
#else
#  define region 0
#endif

  memset(&info, 0, sizeof(struct mallinfo));

  free_delaylist(heap, true);

  /* Visit each region */

#if CONFIG_MM_REGIONS > 1
  for (region = 0; region < heap->mm_nregions; region++)
#endif
    {
      /* Retake the mutex for each region to reduce latencies */

      DEBUGVERIFY(nxrmutex_lock(&heap->mm_lock));
      tlsf_walk_pool(heap->mm_heapstart[region],
                     mallinfo_handler, &info);
      DEBUGVERIFY(nxrmutex_unlock(&heap->mm_lock));
    }
#undef region

  info.arena    = heap->mm_heapsize;
  info.uordblks = info.arena - info.fordblks;
  info.usmblks  = heap->mm_maxused;

#ifdef CONFIG_MM_HEAP_MEMPOOL
  poolinfo = mempool_multiple_mallinfo(heap->mm_mpool);

  info.uordblks -= poolinfo.fordblks;
  info.fordblks += poolinfo.fordblks;
#endif

  return info;
}

struct mallinfo_task mm_mallinfo_task(FAR struct mm_heap_s *heap,
                                      FAR const struct malltask *task)
{
  struct mm_mallinfo_handler_s handle;
  struct mallinfo_task info =
    {
      0, 0
    };

#if CONFIG_MM_REGIONS > 1
  int region;
#else
#  define region 0
#endif

  free_delaylist(heap, true);

#ifdef CONFIG_MM_HEAP_MEMPOOL
  info = mempool_multiple_info_task(heap->mm_mpool, task);
#endif

  handle.task = task;
  handle.info = &info;
#if CONFIG_MM_REGIONS > 1
  for (region = 0; region < heap->mm_nregions; region++)
#endif
    {
      /* Retake the mutex for each region to reduce latencies */

      DEBUGVERIFY(nxrmutex_lock(&heap->mm_lock));
      tlsf_walk_pool(heap->mm_heapstart[region],
                     mallinfo_task_handler, &handle);
      DEBUGVERIFY(nxrmutex_unlock(&heap->mm_lock));
    }
#undef region

  return info;
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
#if CONFIG_MM_REGIONS > 1
  int region;
#else
#  define region 0
#endif
  struct mm_memdump_priv_s priv;
  pid_t pid = dump->pid;

  memset(&priv, 0, sizeof(struct mm_memdump_priv_s));

#ifdef CONFIG_MM_RECORD_SEQNO
  if (dump->seqmin == 0 && dump->seqmax == 0)
    {
      priv.dump.seqmin = 0;
      priv.dump.seqmax = ULONG_MAX;
    }
  else
    {
      priv.dump.seqmin = dump->seqmin;
      priv.dump.seqmax = dump->seqmax;
    }
#endif

  free_delaylist(heap, true);

  if (pid == PID_MM_MEMPOOL)
    {
      syslog(LOG_INFO, "Memdump mempool\n");
    }
  else if (pid == PID_MM_LEAK)
    {
      syslog(LOG_INFO, "Memdump leak\n");
      memdump_info_pool(&priv, heap);
    }
  else if (pid == PID_MM_ALLOC || pid >= 0)
    {
      FAR struct tcb_s *tcb = NULL;
      FAR const char   *name;

      if (pid == PID_MM_ALLOC)
        {
          name = "ALL";
        }
      else
        {
          name = "Unknown";
          tcb  = nxsched_get_tcb(pid);
        }

      if (tcb == NULL)
        {
          syslog(LOG_INFO, "Memdump task %s\n", name);
        }
      else
        {
          name = get_task_name(tcb);
          syslog(LOG_INFO, "Memdump task stack_alloc_ptr: %p,"
                           " adj_stack_size: %zu, name: %s\n",
                           tcb->stack_alloc_ptr, tcb->adj_stack_size, name);
        }

      nxsched_put_tcb(tcb);
      memdump_info_pool(&priv, heap);
    }
  else if (pid == PID_MM_FREE)
    {
      syslog(LOG_INFO, "Dump all free memory node info\n");
      memdump_info_pool(&priv, heap);
    }
#if CONFIG_MM_HEAP_BIGGEST_COUNT > 0
  else if (pid == PID_MM_BIGGEST)
    {
      syslog(LOG_INFO, "Memdump biggest allocated top %d\n",
                       CONFIG_MM_HEAP_BIGGEST_COUNT);
    }
#endif

  syslog(LOG_INFO,
#ifdef CONFIG_MM_RECORD_PID
        "%6s"
#endif
        "%12s"
#ifdef CONFIG_MM_RECORD_SEQNO
        "%12s"
#endif
        "%*s "
#ifdef CONFIG_MM_RECORD_STACK
        "%s"
#endif
        "\n",
#ifdef CONFIG_MM_RECORD_PID
        "PID",
#endif
        "Size",
#ifdef CONFIG_MM_RECORD_SEQNO
        "Sequence",
#endif
        BACKTRACE_PTR_FMT_WIDTH, "Address"
#ifdef CONFIG_MM_RECORD_STACK
        , "Backtrace"
#endif
        );

  memdump_dump_pool(&priv, heap);

#if CONFIG_MM_REGIONS > 1
  for (region = 0; region < heap->mm_nregions; region++)
#endif
    {
      DEBUGVERIFY(nxrmutex_lock(&heap->mm_lock));
      tlsf_walk_pool(heap->mm_heapstart[region],
                     memdump_handler, &priv);
      DEBUGVERIFY(nxrmutex_unlock(&heap->mm_lock));
    }
#undef region

#if CONFIG_MM_HEAP_BIGGEST_COUNT > 0
  if (pid == PID_MM_BIGGEST)
    {
      memdump_dump_biggestnodes(&priv);
    }
#endif

  syslog(LOG_INFO, "%12s%12s\n", "Total Blks", "Total Size");
  syslog(LOG_INFO, "%12zu%12zu\n", priv.info.aordblks, priv.info.uordblks);
}

/****************************************************************************
 * Name: mm_malloc_size
 ****************************************************************************/

size_t mm_malloc_size(FAR struct mm_heap_s *heap, FAR void *mem)
{
#ifdef CONFIG_MM_HEAP_MEMPOOL
  if (heap->mm_mpool)
    {
      ssize_t size = mempool_multiple_alloc_size(heap->mm_mpool, mem);
      if (size >= 0)
        {
          return size;
        }
    }
#endif

#ifdef CONFIG_MM_RECORD
  return tlsf_block_size(mem) - sizeof(struct memdump_record_s);
#else
  return tlsf_block_size(mem);
#endif
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
  size_t nodesize;
  FAR void *ret;

  /* In case of zero-length allocations allocate the minimum size object */

  if (size < 1)
    {
      size = 1;
    }

#ifdef CONFIG_MM_HEAP_MEMPOOL
  if (heap->mm_mpool)
    {
      ret = mempool_multiple_alloc(heap->mm_mpool, size);
      if (ret != NULL)
        {
          return ret;
        }
    }
#endif

  /* Free the delay list first */

  free_delaylist(heap, false);

  /* Allocate from the tlsf pool */

  DEBUGVERIFY(nxrmutex_lock(&heap->mm_lock));
#ifdef CONFIG_MM_RECORD
  ret = tlsf_malloc(heap->mm_tlsf, size +
                    sizeof(struct memdump_record_s));
#else
  ret = tlsf_malloc(heap->mm_tlsf, size);
#endif

  nodesize = mm_malloc_size(heap, ret);
  heap->mm_curused += nodesize;
  if (heap->mm_curused > heap->mm_maxused)
    {
      heap->mm_maxused = heap->mm_curused;
    }

  if (ret)
    {
      sched_note_heap(NOTE_HEAP_ALLOC, heap, ret, nodesize,
                      heap->mm_curused);
    }

  DEBUGVERIFY(nxrmutex_unlock(&heap->mm_lock));

  if (ret)
    {
#ifdef CONFIG_MM_RECORD
      FAR struct memdump_record_s *buf = ret + nodesize;

      memdump_backtrace(heap, buf);
#endif

      ret = kasan_unpoison(ret, nodesize);

#ifdef CONFIG_MM_FILL_ALLOCATIONS
      memset(ret, MM_ALLOC_MAGIC, nodesize);
#endif
    }

#if CONFIG_MM_FREE_DELAYCOUNT_MAX > 0
  /* Try again after free delay list */

  else if (free_delaylist(heap, true))
    {
      return mm_malloc(heap, size);
    }
#endif

  return ret;
}

/****************************************************************************
 * Name: mm_memalign
 *
 * Description:
 *   memalign requests more than enough space from malloc, finds a region
 *   within that chunk that meets the alignment request and then frees any
 *   leading or trailing space.
 *
 *   The alignment argument must be a power of two (not checked).  8-byte
 *   alignment is guaranteed by normal malloc calls.
 *
 ****************************************************************************/

FAR void *mm_memalign(FAR struct mm_heap_s *heap, size_t alignment,
                      size_t size)
{
  size_t nodesize;
  FAR void *ret;

#ifdef CONFIG_MM_HEAP_MEMPOOL
  if (heap->mm_mpool)
    {
      ret = mempool_multiple_memalign(heap->mm_mpool, alignment, size);
      if (ret != NULL)
        {
          return ret;
        }
    }
#endif

  /* Free the delay list first */

  free_delaylist(heap, false);

  /* Allocate from the tlsf pool */

  DEBUGVERIFY(nxrmutex_lock(&heap->mm_lock));
#ifdef CONFIG_MM_RECORD
  ret = tlsf_memalign(heap->mm_tlsf, alignment, size +
                      sizeof(struct memdump_record_s));
#else
  ret = tlsf_memalign(heap->mm_tlsf, alignment, size);
#endif

  nodesize = mm_malloc_size(heap, ret);
  heap->mm_curused += nodesize;
  if (heap->mm_curused > heap->mm_maxused)
    {
      heap->mm_maxused = heap->mm_curused;
    }

  if (ret)
    {
      sched_note_heap(NOTE_HEAP_ALLOC, heap, ret, nodesize,
                      heap->mm_curused);
    }

  DEBUGVERIFY(nxrmutex_unlock(&heap->mm_lock));

  if (ret)
    {
#ifdef CONFIG_MM_RECORD
      FAR struct memdump_record_s *buf = ret + nodesize;

      memdump_backtrace(heap, buf);
#endif
      ret = kasan_unpoison(ret, nodesize);
    }

#if CONFIG_MM_FREE_DELAYCOUNT_MAX > 0
  /* Try again after free delay list */

  else if (free_delaylist(heap, true))
    {
      return mm_memalign(heap, alignment, size);
    }
#endif

  return ret;
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
  FAR void *newmem;
#ifndef CONFIG_MM_KASAN
  size_t oldsize;
  size_t newsize;
#endif

  /* If oldmem is NULL, then realloc is equivalent to malloc */

  if (oldmem == NULL)
    {
      return mm_malloc(heap, size);
    }

  /* If size is zero, reallocate to the minim size object, so
   * the memory pointed by oldmem is freed
   */

  if (size < 1)
    {
      size = 1;
    }

#ifdef CONFIG_MM_HEAP_MEMPOOL
  if (heap->mm_mpool)
    {
      newmem = mempool_multiple_realloc(heap->mm_mpool, oldmem, size);
      if (newmem != NULL)
        {
          return newmem;
        }
      else if (size <= heap->mm_threshold ||
               mempool_multiple_alloc_size(heap->mm_mpool, oldmem) >= 0)
        {
          newmem = mm_malloc(heap, size);
          if (newmem != 0)
            {
              memcpy(newmem, oldmem, size);
              mm_free(heap, oldmem);
              return newmem;
            }
        }
    }
#endif

#ifdef CONFIG_MM_KASAN
  newmem = mm_malloc(heap, size);
  if (newmem)
    {
      if (size > mm_malloc_size(heap, oldmem))
        {
          size = mm_malloc_size(heap, oldmem);
        }

      memcpy(newmem, oldmem, size);
      mm_free(heap, oldmem);
    }
#else
  /* Free the delay list first */

  free_delaylist(heap, false);

  /* Allocate from the tlsf pool */

  DEBUGVERIFY(nxrmutex_lock(&heap->mm_lock));
  oldsize = mm_malloc_size(heap, oldmem);
  heap->mm_curused -= oldsize;
#ifdef CONFIG_MM_RECORD
  newmem = tlsf_realloc(heap->mm_tlsf, oldmem, size +
                        sizeof(struct memdump_record_s));
#else
  newmem = tlsf_realloc(heap->mm_tlsf, oldmem, size);
#endif

  newsize = mm_malloc_size(heap, newmem);
  heap->mm_curused += newmem ? newsize : oldsize;
  if (heap->mm_curused > heap->mm_maxused)
    {
      heap->mm_maxused = heap->mm_curused;
    }

  if (newmem)
    {
      sched_note_heap(NOTE_HEAP_FREE, heap, oldmem, oldsize,
                      heap->mm_curused - newsize);
      sched_note_heap(NOTE_HEAP_ALLOC, heap, newmem, newsize,
                      heap->mm_curused);
    }

  DEBUGVERIFY(nxrmutex_unlock(&heap->mm_lock));

  if (newmem)
    {
#ifdef CONFIG_MM_RECORD
      FAR struct memdump_record_s *buf = newmem + newsize;
      memdump_backtrace(heap, buf);
#endif
    }

#if CONFIG_MM_FREE_DELAYCOUNT_MAX > 0
  /* Try again after free delay list */

  else if (free_delaylist(heap, true))
    {
      return mm_realloc(heap, oldmem, size);
    }
#endif

#endif
  return newmem;
}

/****************************************************************************
 * Name: mm_uninitialize
 *
 * Description:
 *   Uninitialize the selected heap data structures.
 *
 * Input Parameters:
 *   heap - The heap to uninitialize
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void mm_uninitialize(FAR struct mm_heap_s *heap)
{
  int i;

#ifdef CONFIG_MM_HEAP_MEMPOOL
  mempool_multiple_deinit(heap->mm_mpool);
#endif

  free_delaylist(heap, true);

  for (i = 0; i < CONFIG_MM_REGIONS; i++)
    {
      if (!heap->mm_nokasan)
        {
          kasan_unregister(heap->mm_heapstart[i]);
        }

      sched_note_heap(NOTE_HEAP_REMOVE, heap, heap->mm_heapstart[i],
                      (uintptr_t)heap->mm_heapend[i] -
                      (uintptr_t)heap->mm_heapstart[i], heap->mm_curused);
    }

#if defined(CONFIG_FS_PROCFS) && (defined(CONFIG_BUILD_FLAT) || \
    defined(__KERNEL__)) && !defined(CONFIG_FS_PROCFS_EXCLUDE_MEMINFO)
  procfs_unregister_meminfo(heap->mm_procfs);
#endif
  nxrmutex_destroy(&heap->mm_lock);
  tlsf_destroy(&heap->mm_tlsf);
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
 * Name: mm_heapfree
 *
 * Description:
 *   Return the total free size (in bytes) in the heap
 *
 ****************************************************************************/

size_t mm_heapfree(FAR struct mm_heap_s *heap)
{
  free_delaylist(heap, true);
  return heap->mm_heapsize - heap->mm_curused;
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
  size_t max_free;

  free_delaylist(heap, true);

  DEBUGVERIFY(nxrmutex_lock(&heap->mm_lock));
  max_free = tlsf_largest_free_block(heap->mm_tlsf);
  DEBUGVERIFY(nxrmutex_unlock(&heap->mm_lock));

  return max_free;
}
