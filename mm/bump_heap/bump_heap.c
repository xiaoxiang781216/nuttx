/****************************************************************************
 * mm/bump_heap/bump_heap.c
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

#include <string.h>
#include <stddef.h>
#include <malloc.h>
#include <assert.h>
#include <errno.h>

#include <nuttx/atomic.h>

#include <nuttx/nuttx.h>
#include <nuttx/mm/mm.h>
#include <nuttx/spinlock.h>

#if defined(CONFIG_FS_PROCFS) && !defined(CONFIG_FS_PROCFS_EXCLUDE_MEMINFO)
#  include <nuttx/fs/procfs.h>
#endif

#ifdef CONFIG_MM_HEAP_FORBID_HOOK
#  include <nuttx/board.h>
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct mm_bump_region_s
{
  FAR char *start;              /* Region start address */
  FAR char *end;                /* Region end address (exclusive) */
};

struct mm_heap_s
{
  FAR const char  *name;        /* Heap name for diagnostics */
  FAR char        *next;        /* Next allocation address */
  unsigned long    aordblks;    /* Number of successful allocations */
  struct mm_bump_region_s regions[CONFIG_MM_REGIONS];
#if CONFIG_MM_REGIONS > 1
  int              nregions;    /* Number of registered regions */
  int              region;      /* Currently active region index */
#endif
#ifndef CONFIG_MM_HEAP_FORBID_NONE
  atomic_t         forbid;      /* Non-zero forbids allocation */
#endif
#if defined(CONFIG_FS_PROCFS) && !defined(CONFIG_FS_PROCFS_EXCLUDE_MEMINFO)
  FAR struct procfs_meminfo_entry_s *procfs; /* procfs meminfo handle */
#endif
  spinlock_t       lock;        /* IRQ-safe spinlock */
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: mm_skip_operation
 *
 * Description:
 *   Check if the current heap operation should be skipped due to forbid
 *   state. Dispatches to the configured policy (PANIC / HOOK).
 *
 *   For free/realloc (mem != NULL), the forbid is structural —
 *   bump-allocated memory is inherently non-freeable, so the policy is
 *   always enforced regardless of the forbid runtime flag.
 *
 *   For malloc (mem == NULL), the policy is only enforced when
 *   forbid has been set at runtime.
 *
 ****************************************************************************/

#ifdef CONFIG_MM_HEAP_FORBID_NONE
#  define mm_skip_operation(heap, mem, size) false
#else
static inline_function bool mm_skip_operation(FAR struct mm_heap_s *heap,
                                              FAR void *mem, size_t size)
{
  if (mem == NULL && atomic_read(&heap->forbid) == 0)
    {
      return false;
    }

#  if defined(CONFIG_MM_HEAP_FORBID_PANIC)
  PANIC();
  return true;
#  elif defined(CONFIG_MM_HEAP_FORBID_HOOK)
  return board_heap_forbid_hook(heap, mem, size);
#  else
#    error "Invalid MM_HEAP_FORBID config for bump allocator"
#  endif
}
#endif

/****************************************************************************
 * Name: mm_bump_region_end
 *
 * Description:
 *   Return the end address of the current active region.
 *
 ****************************************************************************/

static inline FAR char *mm_bump_region_end(FAR struct mm_heap_s *heap)
{
#if CONFIG_MM_REGIONS > 1
  return heap->regions[heap->region].end;
#else
  return heap->regions[0].end;
#endif
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: mm_initialize_heap
 *
 * Description:
 *   Initialize a bump-only heap instance. The heap structure is placed
 *   at the beginning of the provided memory region. The remaining space
 *   is registered as the first region via mm_addregion.
 *
 ****************************************************************************/

void mm_initialize_heap(FAR const struct mm_heap_config_s *config,
                        FAR struct mm_heap_s **heap)
{
  FAR struct mm_heap_s *h;
  FAR char *region_start;
  size_t region_size;

  DEBUGASSERT(config != NULL && config->start != NULL &&
              config->size >= sizeof(struct mm_heap_s) + MM_ALIGN);

  /* Place the heap structure at the beginning of the region */

  h = (FAR struct mm_heap_s *)config->start;
  memset(h, 0, sizeof(*h));

  region_start = (FAR char *)config->start + sizeof(*h);
  region_size  = config->size - sizeof(*h);

  h->name = config->name;
  h->next = region_start;
  spin_lock_init(&h->lock);

  *heap = h;

  /* Register the initial usable region */

  mm_addregion(h, region_start, region_size);

  /* Register with procfs so that the heap shows up in /proc/meminfo
   * (i.e. the NSH "free" command). The bump allocator implements
   * mm_mallinfo, so the default handlers are used.
   */

#if defined(CONFIG_FS_PROCFS) && !defined(CONFIG_FS_PROCFS_EXCLUDE_MEMINFO)
#  if !defined(CONFIG_BUILD_KERNEL) || defined(__KERNEL__)
  h->procfs = procfs_register_meminfo(config->name, h, NULL, NULL);
#  endif
#endif
}

/****************************************************************************
 * Name: mm_initialize_pool
 *
 * Description:
 *   Compatibility stub. Bump allocator does not use mempools, so this
 *   simply calls mm_initialize_heap and ignores the pool config.
 *
 ****************************************************************************/

#ifdef CONFIG_MM_HEAP_MEMPOOL
void mm_initialize_pool(FAR const struct mm_heap_config_s *config,
                        FAR const struct mm_pool_config_s *poolconfig,
                        FAR struct mm_heap_s **heap)
{
  UNUSED(poolconfig);
  mm_initialize_heap(config, heap);
}
#endif

/****************************************************************************
 * Name: mm_uninitialize
 *
 * Description:
 *   Stub -- bump allocator has no resources to release.
 *
 ****************************************************************************/

void mm_uninitialize(FAR struct mm_heap_s *heap)
{
#if defined(CONFIG_FS_PROCFS) && !defined(CONFIG_FS_PROCFS_EXCLUDE_MEMINFO)
#  if !defined(CONFIG_BUILD_KERNEL) || defined(__KERNEL__)
  procfs_unregister_meminfo(heap->procfs);
#  endif
#endif
  UNUSED(heap);
}

/****************************************************************************
 * Name: mm_addregion
 *
 * Description:
 *   Register a memory region with the bump allocator. Regions are used
 *   in registration order -- when the current region is exhausted, the
 *   allocator advances to the next.
 *
 ****************************************************************************/

void mm_addregion(FAR struct mm_heap_s *heap, FAR void *start, size_t size)
{
  irqstate_t flags;
#if CONFIG_MM_REGIONS > 1
  int idx;
#endif

  if (start == NULL || size == 0)
    {
      return;
    }

  flags = spin_lock_irqsave(&heap->lock);

#if CONFIG_MM_REGIONS > 1
  idx = heap->nregions;
  DEBUGASSERT(idx < CONFIG_MM_REGIONS);
  if (idx >= CONFIG_MM_REGIONS)
    {
      spin_unlock_irqrestore(&heap->lock, flags);
      return;
    }

  heap->regions[idx].start = start;
  heap->regions[idx].end   = (FAR char *)start + size;
  heap->nregions = idx + 1;
#else
  heap->regions[0].start = start;
  heap->regions[0].end   = (FAR char *)start + size;
#endif

  spin_unlock_irqrestore(&heap->lock, flags);
}

/****************************************************************************
 * Name: mm_malloc
 *
 * Description:
 *   Bump allocate with default alignment. Forwards to mm_memalign.
 *
 ****************************************************************************/

FAR void *mm_malloc(FAR struct mm_heap_s *heap, size_t size)
{
  return mm_memalign(heap, MM_ALIGN, size);
}

/****************************************************************************
 * Name: mm_memalign
 *
 * Description:
 *   Allocate memory with specified alignment from the bump heap.
 *   If the current region cannot satisfy the request, advances to the
 *   next registered region (CONFIG_MM_REGIONS > 1).
 *
 ****************************************************************************/

FAR void *mm_memalign(FAR struct mm_heap_s *heap,
                      size_t alignment, size_t size)
{
  irqstate_t flags;
  FAR char *aligned;
  FAR char *end;
  size_t remaining;

  if (size == 0 || alignment == 0)
    {
      return NULL;
    }

  if (mm_skip_operation(heap, NULL, size))
    {
      return NULL;
    }

  flags = spin_lock_irqsave(&heap->lock);

#if CONFIG_MM_REGIONS > 1
  while (heap->region < heap->nregions)
#endif
    {
      end = mm_bump_region_end(heap);
      aligned = (FAR char *)ALIGN_UP((uintptr_t)heap->next, alignment);

      /* Check alignment didn't wrap and stays within region */

      if (aligned >= heap->next && aligned < end)
        {
          remaining = end - aligned;
          if (size <= remaining)
            {
              heap->next = aligned + size;
              heap->aordblks++;

              spin_unlock_irqrestore(&heap->lock, flags);
              return aligned;
            }
        }

      /* Current region cannot satisfy this request, try next */

#if CONFIG_MM_REGIONS > 1
      heap->region++;
      if (heap->region < heap->nregions)
        {
          heap->next = heap->regions[heap->region].start;
        }
#endif
    }

  spin_unlock_irqrestore(&heap->lock, flags);
  return NULL;
}

/****************************************************************************
 * Name: mm_zalloc
 *
 * Description:
 *   Allocate and zero-fill memory.
 *
 ****************************************************************************/

FAR void *mm_zalloc(FAR struct mm_heap_s *heap, size_t size)
{
  FAR void *mem = mm_malloc(heap, size);
  if (mem != NULL)
    {
      memset(mem, 0, size);
    }

  return mem;
}

/****************************************************************************
 * Name: mm_calloc
 *
 * Description:
 *   Allocate an array and zero-fill.
 *
 ****************************************************************************/

FAR void *mm_calloc(FAR struct mm_heap_s *heap, size_t n, size_t elem_size)
{
  if (elem_size != 0 && n > SIZE_MAX / elem_size)
    {
      return NULL;
    }

  return mm_zalloc(heap, n * elem_size);
}

/****************************************************************************
 * Name: mm_free
 *
 * Description:
 *   Bump allocator mm_free implementation.
 *   Bump-allocated memory cannot be freed; dispatches to the configured
 *   forbid policy via mm_skip_operation().
 *
 ****************************************************************************/

void mm_free(FAR struct mm_heap_s *heap, FAR void *mem)
{
  if (mem == NULL)
    {
      return;
    }

  mm_skip_operation(heap, mem, 0);
}

/****************************************************************************
 * Name: mm_realloc
 *
 * Description:
 *   Bump allocator mm_realloc implementation.
 *   realloc(NULL, size) -> malloc (normal allocation path).
 *   realloc(ptr, size)  -> policy dispatch then return NULL (cannot resize).
 *
 ****************************************************************************/

FAR void *mm_realloc(FAR struct mm_heap_s *heap,
                     FAR void *mem, size_t size)
{
  if (mem == NULL)
    {
      return mm_malloc(heap, size);
    }

  mm_skip_operation(heap, mem, size);

  /* Bump allocator cannot resize -- fail explicitly.
   * Per C standard, on failure the original pointer remains valid.
   */

  return NULL;
}

/****************************************************************************
 * Name: mm_delayfreecb
 *
 * Description:
 *   Bump allocator mm_delayfreecb implementation.
 *   Dispatches to mm_skip_operation() unconditionally.
 *
 ****************************************************************************/

void mm_delayfreecb(FAR struct mm_heap_s *heap, FAR void *mem,
                    mm_delaycb_t cb)
{
  UNUSED(cb);

  if (mem == NULL)
    {
      return;
    }

  mm_skip_operation(heap, mem, 0);
}

/****************************************************************************
 * Name: mm_heapmember
 *
 * Description:
 *   Check if an address belongs to this heap (any registered region).
 *
 ****************************************************************************/

bool mm_heapmember(FAR struct mm_heap_s *heap, FAR void *mem)
{
#if CONFIG_MM_REGIONS > 1
  int i;

  for (i = 0; i < heap->nregions; i++)
    {
      if ((FAR char *)mem >= heap->regions[i].start &&
          (FAR char *)mem < heap->regions[i].end)
        {
          return true;
        }
    }

  return false;
#else
  return (FAR char *)mem >= heap->regions[0].start &&
         (FAR char *)mem < heap->regions[0].end;
#endif
}

/****************************************************************************
 * Name: mm_mallinfo
 *
 * Description:
 *   Return heap usage statistics across all regions.
 *
 ****************************************************************************/

struct mallinfo mm_mallinfo(FAR struct mm_heap_s *heap)
{
  struct mallinfo info;
  irqstate_t flags;
  size_t arena = 0;
  size_t used = 0;
  size_t largest = 0;
#if CONFIG_MM_REGIONS > 1
  int i;
#endif

  /* Zero all fields first: the bump allocator does not track every
   * mallinfo field, and returning uninitialized stack data would show
   * up as garbage in /proc/meminfo (e.g. the "free" command).
   */

  memset(&info, 0, sizeof(info));

  flags = spin_lock_irqsave(&heap->lock);

#if CONFIG_MM_REGIONS > 1
  for (i = 0; i < heap->nregions; i++)
    {
      size_t region_size = heap->regions[i].end -
                           heap->regions[i].start;
      size_t free_in_region;

      arena += region_size;

      if (i < heap->region)
        {
          /* Fully consumed regions */

          used += region_size;
          free_in_region = 0;
        }
      else if (i == heap->region)
        {
          /* Current region: used up to heap_next */

          used += heap->next - heap->regions[i].start;
          free_in_region = heap->regions[i].end -
                           heap->next;
        }
      else
        {
          /* Regions after cur_region are entirely free */

          free_in_region = region_size;
        }

      if (free_in_region > largest)
        {
          largest = free_in_region;
        }
    }
#else
  arena = heap->regions[0].end - heap->regions[0].start;
  used  = heap->next - heap->regions[0].start;
  largest = arena - used;
#endif

  info.arena    = arena;
  info.uordblks = used;
  info.fordblks = arena - used;
  info.mxordblk = largest;

  /* The bump allocator only grows, so the current usage is also the peak
   * usage (usmblks). aordblks is the running count of successful
   * allocations tracked in heap->aordblks.
   */

  info.usmblks  = used;
  info.aordblks = heap->aordblks;
#if CONFIG_MM_REGIONS > 1
  info.ordblks  = heap->nregions - heap->region;
#else
  info.ordblks  = (heap->next < heap->regions[0].end) ? 1 : 0;
#endif

  spin_unlock_irqrestore(&heap->lock, flags);
  return info;
}

/****************************************************************************
 * Name: mm_malloc_size
 *
 * Description:
 *   Returns 0 -- bump allocator does not track individual block sizes.
 *
 ****************************************************************************/

size_t mm_malloc_size(FAR struct mm_heap_s *heap, FAR void *mem)
{
  UNUSED(heap);
  UNUSED(mem);
  return 0;
}

/****************************************************************************
 * Name: mm_brkaddr
 *
 * Description:
 *   Return the current break address (heap_next) for the given region.
 *   For the active region, returns heap_next. For fully consumed regions,
 *   returns the region end. For unused regions, returns the region start.
 *
 ****************************************************************************/

FAR void *mm_brkaddr(FAR struct mm_heap_s *heap, int region)
{
  irqstate_t flags;
  FAR void *brk;

#if CONFIG_MM_REGIONS > 1
  if (region < 0 || region >= heap->nregions)
    {
      return NULL;
    }
#else
  if (region != 0)
    {
      return NULL;
    }
#endif

  flags = spin_lock_irqsave(&heap->lock);

#if CONFIG_MM_REGIONS > 1
  if (region < heap->region)
    {
      brk = heap->regions[region].end;
    }
  else if (region == heap->region)
    {
      brk = heap->next;
    }
  else
    {
      brk = heap->regions[region].start;
    }
#else
  brk = heap->next;
#endif

  spin_unlock_irqrestore(&heap->lock, flags);
  return brk;
}

/****************************************************************************
 * Name: mm_extend
 *
 * Description:
 *   Extend the bump allocator heap. If mem is contiguous with an existing
 *   region's end, that region is extended in-place. Otherwise, the memory
 *   is registered as a new region via mm_addregion.
 *
 ****************************************************************************/

void mm_extend(FAR struct mm_heap_s *heap, FAR void *mem, size_t size,
               int region)
{
  irqstate_t flags;
  int last;

  UNUSED(region);

  if (mem == NULL || size == 0)
    {
      return;
    }

  flags = spin_lock_irqsave(&heap->lock);

  /* Check if this extends the last region contiguously */

#if CONFIG_MM_REGIONS > 1
  last = heap->nregions - 1;
#else
  last = 0;
#endif

  if (last >= 0 && mem == heap->regions[last].end)
    {
      /* Overflow check */

      if (size > (SIZE_MAX -
                  (uintptr_t)heap->regions[last].end))
        {
          spin_unlock_irqrestore(&heap->lock, flags);
          return;
        }

      heap->regions[last].end += size;
      spin_unlock_irqrestore(&heap->lock, flags);
      return;
    }

  spin_unlock_irqrestore(&heap->lock, flags);

  /* Not contiguous with last region -- register as new region */

  mm_addregion(heap, mem, size);
}

/****************************************************************************
 * Name: mm_heapfree
 *
 * Description:
 *   Return the total number of free bytes remaining across all regions.
 *
 ****************************************************************************/

size_t mm_heapfree(FAR struct mm_heap_s *heap)
{
  irqstate_t flags;
  size_t freesize = 0;
#if CONFIG_MM_REGIONS > 1
  int i;
#endif

  flags = spin_lock_irqsave(&heap->lock);

#if CONFIG_MM_REGIONS > 1
  for (i = heap->region; i < heap->nregions; i++)
    {
      if (i == heap->region)
        {
          freesize += heap->regions[i].end - heap->next;
        }
      else
        {
          freesize += heap->regions[i].end -
                      heap->regions[i].start;
        }
    }
#else
  freesize = heap->regions[0].end - heap->next;
#endif

  spin_unlock_irqrestore(&heap->lock, flags);
  return freesize;
}

/****************************************************************************
 * Name: mm_heapfree_largest
 *
 * Description:
 *   Return the size of the largest contiguous free block. For bump
 *   allocator this is the largest single region's free space.
 *
 ****************************************************************************/

size_t mm_heapfree_largest(FAR struct mm_heap_s *heap)
{
  irqstate_t flags;
  size_t largest = 0;
#if CONFIG_MM_REGIONS > 1
  int i;
#endif

  flags = spin_lock_irqsave(&heap->lock);

#if CONFIG_MM_REGIONS > 1
  for (i = heap->region; i < heap->nregions; i++)
    {
      size_t free_in_region;

      if (i == heap->region)
        {
          free_in_region = heap->regions[i].end -
                           heap->next;
        }
      else
        {
          free_in_region = heap->regions[i].end -
                           heap->regions[i].start;
        }

      if (free_in_region > largest)
        {
          largest = free_in_region;
        }
    }
#else
  largest = heap->regions[0].end - heap->next;
#endif

  spin_unlock_irqrestore(&heap->lock, flags);
  return largest;
}

/****************************************************************************
 * Name: mm_mallinfo_task
 *
 * Description:
 *   Per-task heap statistics. The bump allocator does not track per-task
 *   ownership, so a zeroed mallinfo_task is returned.
 *
 ****************************************************************************/

struct mallinfo_task mm_mallinfo_task(FAR struct mm_heap_s *heap,
                                      FAR const struct malltask *task)
{
  struct mallinfo_task info;

  UNUSED(heap);
  UNUSED(task);

  memset(&info, 0, sizeof(info));
  return info;
}

/****************************************************************************
 * Name: mm_memdump
 *
 * Description:
 *   Stub -- the bump allocator does not keep per-block metadata, so there
 *   is nothing to dump beyond the aggregate mallinfo.
 *
 ****************************************************************************/

void mm_memdump(FAR struct mm_heap_s *heap,
                FAR const struct mm_memdump_s *dump)
{
  UNUSED(heap);
  UNUSED(dump);
}

/****************************************************************************
 * Name: mm_sbrk
 *
 * Description:
 *   Advance the bump allocator heap_next by 'incr' bytes within the
 *   current active region, returning the previous heap_next in *mem.
 *   Only positive incr is supported.
 *
 ****************************************************************************/

int mm_sbrk(FAR struct mm_heap_s *heap, intptr_t incr, FAR void **mem)
{
  irqstate_t flags;
  FAR char *end;
  size_t remaining;

  if (incr <= 0 || mem == NULL)
    {
      return -ENOSYS;
    }

  if (mm_skip_operation(heap, NULL, incr))
    {
      return -EPERM;
    }

  flags = spin_lock_irqsave(&heap->lock);

  end = mm_bump_region_end(heap);

  if (heap->next >= end)
    {
      spin_unlock_irqrestore(&heap->lock, flags);
      return -ENOMEM;
    }

  remaining = end - heap->next;
  if (incr > (intptr_t)remaining)
    {
      spin_unlock_irqrestore(&heap->lock, flags);
      return -ENOMEM;
    }

  *mem = heap->next;
  heap->next += incr;

  spin_unlock_irqrestore(&heap->lock, flags);
  return 0;
}

/****************************************************************************
 * Name: mm_checkcorruption
 *
 * Description:
 *   Stub -- the bump allocator has no free list to corrupt.
 *
 ****************************************************************************/

#ifdef CONFIG_DEBUG_MM
void mm_checkcorruption(FAR struct mm_heap_s *heap)
{
  UNUSED(heap);
}
#endif

/****************************************************************************
 * Name: mm_notify_pressure
 *
 * Description:
 *   Stub -- only compiled when CONFIG_FS_PROCFS_INCLUDE_PRESSURE is
 *   enabled (matching the declaration in mm.h).
 *
 ****************************************************************************/

#ifdef CONFIG_FS_PROCFS_INCLUDE_PRESSURE
void mm_notify_pressure(FAR struct mm_heap_s *heap)
{
  UNUSED(heap);
}
#endif

/****************************************************************************
 * Name: mm_heap_forbid / mm_heap_is_forbid
 *
 * Description:
 *   Forbid (or allow) allocation on this heap. While forbidden, mm_malloc
 *   and mm_memalign return NULL.
 *
 ****************************************************************************/

#ifndef CONFIG_MM_HEAP_FORBID_NONE
bool mm_heap_forbid(FAR struct mm_heap_s *heap, bool forbid)
{
  DEBUGASSERT(heap != NULL);

  return atomic_xchg(&heap->forbid, forbid) != 0;
}

bool mm_heap_is_forbid(FAR struct mm_heap_s *heap)
{
  DEBUGASSERT(heap != NULL);

  return atomic_read(&heap->forbid) != 0;
}
#endif
