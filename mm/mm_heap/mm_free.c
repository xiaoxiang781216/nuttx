/****************************************************************************
 * mm/mm_heap/mm_free.c
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

#include <assert.h>
#include <debug.h>
#include <execinfo.h>

#include <nuttx/arch.h>
#include <nuttx/sched.h>
#include <nuttx/trace.h>
#include <nuttx/mm/mm.h>
#include <nuttx/mm/kasan.h>
#include <nuttx/sched_note.h>

#include "mm_heap/mm.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void add_delaylist(FAR struct mm_heap_s *heap, FAR void *mem,
                          bool asan_check)
{
#if defined(CONFIG_BUILD_FLAT) || defined(__KERNEL__)
  FAR struct mm_delayhead_s *delay = get_delayhead(heap);
  FAR struct mm_delaynode_s *tmp = mem;
  irqstate_t flags;
  bool bypass;

  /* Delay the deallocation until a more appropriate time. */

  flags = up_irq_save();
  bypass = kasan_bypass(true);

#ifdef CONFIG_DEBUG_ASSERTIONS
#  ifdef CONFIG_MM_HEAP_MEMPOOL
  if (!heap->mm_mpool || !mempool_multiple_member(heap->mm_mpool, mem))
#  endif
    {
      FAR struct mm_freenode_s *node;
      node = (FAR struct mm_freenode_s *)((FAR char *)mem -
             MM_SIZEOF_ALLOCNODE);
      DEBUGASSERT(MM_NODE_IS_ALLOC(node));
    }
#endif

  tmp->flink  = delay->head;
  delay->head = tmp;

#if CONFIG_MM_FREE_DELAYCOUNT_MAX > 0
  delay->delaycount++;
#endif

  if (asan_check)
    {
      size_t size = mm_malloc_size(heap, mem);
#ifdef CONFIG_MM_FILL_ALLOCATIONS
      memset(mem, MM_FREE_MAGIC, size);
#endif
      UNUSED(size);
      kasan_poison(mem, size);
    }

  kasan_bypass(bypass);
  up_irq_restore(flags);
#endif
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: mm_forcefree
 *
 * Description:
 *   Heap mem release, add to chunk.
 *
 ****************************************************************************/

void mm_forcefree(FAR struct mm_heap_s *heap, FAR void *mem)
{
  FAR struct mm_freenode_s *node;
  FAR struct mm_freenode_s *prev;
  FAR struct mm_freenode_s *next;
  size_t nodesize;
  size_t prevsize;
  bool bypass;

  /* Free mempool blk first */

#ifdef CONFIG_MM_HEAP_MEMPOOL
  if (heap->mm_mpool && mempool_multiple_free(heap->mm_mpool, mem) >= 0)
    {
      return;
    }
#endif

  DEBUGVERIFY(nxrmutex_lock(&heap->mm_lock));
  bypass = kasan_bypass(true);
  nodesize = mm_malloc_size(heap, mem);
#ifdef CONFIG_MM_FILL_ALLOCATIONS
  memset(mem, MM_FREE_MAGIC, nodesize);
#endif
  kasan_poison(mem, nodesize);

  /* Map the memory chunk into a free node */

  node = (FAR struct mm_freenode_s *)
         ((FAR char *)kasan_clear_tag(mem) - MM_SIZEOF_ALLOCNODE);
  nodesize = MM_SIZEOF_NODE(node);
#ifdef CONFIG_MM_RECORD_STACK
  backtrace_remove(node->stack);
#endif

  /* Sanity check against double-frees */

  if (MM_NODE_IS_ALLOC(node))
    {
      mm_trace_diag(DIAG_MM_FREE_FAIL, "address:%p, reason:%s",
                    mem, "double-free");
    }

  DEBUGASSERT(MM_NODE_IS_ALLOC(node));
  node->size &= ~MM_ALLOC_BIT;

  /* Update heap statistics */

  heap->mm_curused -= nodesize;
  sched_note_heap(NOTE_HEAP_FREE, heap, mem, nodesize, heap->mm_curused);

  /* Check if the following node is free and, if so, merge it */

  next = (FAR struct mm_freenode_s *)((FAR char *)node + nodesize);
  DEBUGASSERT(MM_PREVNODE_IS_ALLOC(next));
  if (MM_NODE_IS_FREE(next))
    {
      FAR struct mm_allocnode_s *andbeyond;
      size_t nextsize = MM_SIZEOF_NODE(next);

      /* Get the node following the next node (which will
       * become the new next node). We know that we can never
       * index past the tail chunk because it is always allocated.
       */

      andbeyond = (FAR struct mm_allocnode_s *)((FAR char *)next + nextsize);
      DEBUGASSERT(MM_PREVNODE_IS_FREE(andbeyond) &&
                  andbeyond->preceding == nextsize);

      /* Remove the next node.  There must be a predecessor,
       * but there may not be a successor node.
       */

      DEBUGASSERT(next->blink);
      next->blink->flink = next->flink;
      if (next->flink)
        {
          next->flink->blink = next->blink;
        }

      /* Then merge the two chunks */

      nodesize            += nextsize;
      node->size           = nodesize | (node->size & MM_MASK_BIT);
      andbeyond->preceding = nodesize;
      next                 = (FAR struct mm_freenode_s *)andbeyond;
    }
  else
    {
      next->size     |= MM_PREVFREE_BIT;
      next->preceding = nodesize;
    }

  /* Check if the preceding node is also free and, if so, merge
   * it with this node
   */

  if (MM_PREVNODE_IS_FREE(node))
    {
      prev = (FAR struct mm_freenode_s *)
        ((FAR char *)node - node->preceding);
      prevsize = MM_SIZEOF_NODE(prev);
      DEBUGASSERT(MM_NODE_IS_FREE(prev) && node->preceding == prevsize);

      /* Remove the node.  There must be a predecessor, but there may
       * not be a successor node.
       */

      DEBUGASSERT(prev->blink);
      prev->blink->flink = prev->flink;
      if (prev->flink)
        {
          prev->flink->blink = prev->blink;
        }

      /* Then merge the two chunks */

      prevsize       += nodesize;
      prev->size      = prevsize | (prev->size & MM_MASK_BIT);
      next->preceding = prevsize;
      node            = prev;
    }

  /* Add the merged node to the nodelist */

  mm_addfreechunk(heap, node);
  kasan_bypass(bypass);
  DEBUGVERIFY(nxrmutex_unlock(&heap->mm_lock));
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

  mm_forcefree(heap, mem);
#endif
}
