/****************************************************************************
 * mm/iob/iob.h
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

#ifndef __MM_IOB_IOB_H
#define __MM_IOB_IOB_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <debug.h>

#include <nuttx/mm/iob.h>
#include <nuttx/mm/mempool.h>

#ifdef CONFIG_MM_IOB

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Public Data
 ****************************************************************************/

/* Multiple mempool for I/O buffers.
 *
 * Non-BMP: flat array indexed by pool level.
 * BMP: 2D array indexed by [cpu][level]; use g_iob_pool to
 * get the current CPU's pool array.
 */

#ifdef CONFIG_BMP
extern FAR struct mempool_s *g_iob_cpu_pool[CONFIG_NCPUS][CONFIG_IOB_NPOOLS];
#  define g_iob_pool g_iob_cpu_pool[this_cpu()]
#else
extern FAR struct mempool_s *g_iob_pool[CONFIG_IOB_NPOOLS];
#endif

#if CONFIG_IOB_EXTRA_HEADS > 0
/* Dedicated pool for fixed-size struct iob_s headers used by the
 * external-payload paths (iob_alloc_with_data / iob_init_with_data).
 *
 * Non-BMP: a single global head pool.
 * BMP    : one head pool per CPU, sized/sectioned per CPU; the
 *          g_iob_head_pool macro resolves to the current CPU's slot.
 *          CPUs whose IOB_NPOOLS_CPUn == 0 have a NULL slot, in which
 *          case external-payload allocations fail gracefully on that
 *          CPU (mempool_zallocate() returns NULL for a NULL pool).
 */

#  ifdef CONFIG_BMP
extern FAR struct mempool_s *g_iob_head_cpu_pool[CONFIG_NCPUS];
#    define g_iob_head_pool g_iob_head_cpu_pool[this_cpu()]
#  else
extern FAR struct mempool_s *g_iob_head_pool;
#  endif
#endif

/****************************************************************************
 * Inline Functions
 ****************************************************************************/

/****************************************************************************
 * Name: iob_notifier_signal
 *
 * Description:
 *   An IOB has become available.  Signal all threads waiting for an IOB
 *   that an IOB is available.
 *
 *   When an IOB becomes available, *all* of the waiters in this thread will
 *   be signaled.  If there are multiple waiters then only the highest
 *   priority thread will get the IOB.  Lower priority threads will need to
 *   call iob_notifier_setup() once again.
 *
 * Input Parameters:
 *   None.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

#ifdef CONFIG_IOB_NOTIFIER
void iob_notifier_signal(void);
#endif

/****************************************************************************
 * Name: iob_find_mempool
 *
 * Description:
 *  Allocate an I/O buffer by taking the buffer at the head of the free list.
 *  This wait will be terminated when the specified timeout expires.
 *
 * Input Parameters:
 *   throttled  - An indication of the IOB allocation is "throttled"
 *   timeout    - Timeout value in milliseconds.
 *
 ****************************************************************************/

static inline_function
FAR struct mempool_s *iob_find_mempool(size_t size)
{
  FAR struct mempool_s **pool = g_iob_pool;
  int i = CONFIG_IOB_NPOOLS - 1;

  if (size > CONFIG_IOB_BUFSIZE)
    {
      return NULL;
    }
  else if (CONFIG_IOB_NPOOLS == 1)
    {
      return pool[0];
    }

  do
    {
      if (pool[i] != NULL && pool[i]->blocksize > 0 &&
          IOB_ALIGN_SIZE(size) <= pool[i]->blocksize)
        {
          return pool[i];
        }
    }
  while (--i >= 0);

  return NULL;
}

#endif /* CONFIG_MM_IOB */
#endif /* __MM_IOB_IOB_H */
