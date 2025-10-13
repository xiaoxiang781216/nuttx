/****************************************************************************
 * sched/group/group_heap.c
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

#include <nuttx/arch.h>

#include "group/group.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#if defined(CONFIG_BUILD_PROTECTED) && defined(__KERNEL__)
#  define GROUP_HEAP USERSPACE_HEAP
#else
#  define GROUP_HEAP g_mmheap
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: group_heap_initialize
 *
 * Description:
 *   This function initializes the group heap.
 *
 * Input Parameters:
 *   heap      - The heap to initialize.
 *   alignment - The alignment to use for the heap.
 *   heapsize  - The size of the heap.
 *
 * Returned Value:
 *   OK on success; a negated errno value on failure.
 *
 ****************************************************************************/

int group_heap_initialize(FAR struct mm_heap_s **heap,
                          size_t alignment, size_t heapsize)
{
  if (heapsize == 0)
    {
      heapsize = CONFIG_MM_TASK_HEAP_DEFAULT_SIZE;
    }

  if (alignment == 0)
    {
      alignment = CONFIG_MM_TASK_HEAP_DEFAULT_ALIGN;
    }

  *heap = mm_memalign(GROUP_HEAP, alignment, heapsize);
  if (*heap != NULL)
    {
      /* Initialize the memory manager */

      mm_initialize("Task heap", *heap, heapsize);
    }
  else
    {
      return -ENOMEM;
    }

  return OK;
}

/****************************************************************************
 * Name: group_heap_size
 *
 * Description:
 *   This function returns the size of the group heap.
 *
 * Input Parameters:
 *   heap - The heap to query.
 *
 * Returned Value:
 *   The size of the heap.
 *
 ****************************************************************************/

int group_heap_size(FAR struct mm_heap_s *heap)
{
  return mm_malloc_size(GROUP_HEAP, heap);
}

/****************************************************************************
 * Name: group_heap_uninitialize
 *
 * Description:
 *   This function uninitializes the group heap.
 *
 * Input Parameters:
 *   group - The group to uninitialize.
 *
 ****************************************************************************/

void group_heap_uninitialize(FAR struct mm_heap_s *heap)
{
  mm_uninitialize(heap);
  if (mm_heapmember(GROUP_HEAP, heap))
    {
      mm_free(GROUP_HEAP, heap);
    }
}
