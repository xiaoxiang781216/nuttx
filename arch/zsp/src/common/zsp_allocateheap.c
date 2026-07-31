/****************************************************************************
 * arch/zsp/src/common/zsp_allocateheap.c
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
#include <nuttx/kmalloc.h>

#include "zsp_internal.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_allocate_heap
 *
 * Description:
 *   This function is called from nx_start() to provide the heap region
 *   boundaries for the kernel/system heap.
 *
 *   Memory layout (flat build):
 *     _ebss
 *     +-- idle thread stack (CONFIG_IDLETHREAD_STACKSIZE)
 *     +-- heap (fills remaining space up to interrupt stack)
 *     ...
 *     +-- interrupt stack (CONFIG_ARCH_INTERRUPTSTACK) at top of RAM
 *     CONFIG_RAM_START + CONFIG_RAM_SIZE
 *
 ****************************************************************************/

void up_allocate_heap(void **heap_start, size_t *heap_size)
{
  *heap_start = (void *)g_idle_topstack;
  *heap_size  = CONFIG_RAM_END - g_idle_topstack -
                CONFIG_ARCH_INTERRUPTSTACK;
}
