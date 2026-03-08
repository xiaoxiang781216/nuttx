/****************************************************************************
 * mm/iob/iob_alloc.c
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
#include <errno.h>

#include <nuttx/irq.h>
#include <nuttx/arch.h>
#include <nuttx/sched.h>
#include <nuttx/nuttx.h>
#include <nuttx/mm/iob.h>
#include <nuttx/mm/mempool.h>

#include "iob.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: iob_alloc_internal
 *
 * Description:
 *   Internal allocation using mempool with specific size.
 *
 ****************************************************************************/

static FAR struct iob_s *iob_alloc_internal(size_t size, bool throttled,
                                            unsigned int timeout)
{
  FAR struct mempool_s *mempool;
  FAR struct iob_s *iob;

  mempool = iob_find_mempool(size);
  if (mempool == NULL)
    {
      return NULL;
    }

  if (timeout == 0)
    {
      iob = mempool_tryallocate_throttled(mempool, throttled);
    }
  else
    {
      iob = mempool_allocate_throttled(mempool, timeout, throttled);
    }

  if (iob != NULL)
    {
      /* Verify IOB address alignment for flag bits in io_flink */

      DEBUGASSERT(((uintptr_t)iob & IOB_FLAGS_MASK) == 0);

      /* Initialize the IOB */

#if CONFIG_IOB_NPOOLS > 1
      iob->io_pool   = mempool;
#endif
      iob->io_flink  = NULL; /* Not in a chain */
      iob->io_len    = 0;    /* Length of the data in the entry */
      iob->io_offset = 0;    /* Offset to the beginning of data */
      iob->io_pktlen = 0;    /* Total length of the packet */
      iob->io_conn   = NULL;
#ifdef CONFIG_DEBUG_IOB
      iob->io_owner[0] = '\0';
#endif

      /* Verify IOB and IOB data address alignment */

      DEBUGASSERT((uintptr_t)iob % IOB_ALIGNMENT == 0);
      DEBUGASSERT((uintptr_t)iob->io_data % IOB_ALIGNMENT == 0);
    }

  return iob;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: iob_timedalloc_with_size
 *
 * Description:
 *  Allocate an I/O buffer with specified size. This wait will be terminated
 *  when the specified timeout expires.
 *
 * Input Parameters:
 *   size       - The size of buffer to allocate
 *   throttled  - An indication of the IOB allocation is "throttled"
 *   timeout    - Timeout value in milliseconds.
 *
 ****************************************************************************/

FAR struct iob_s *iob_timedalloc_with_size(uint16_t size, bool throttled,
                                           unsigned int timeout)
{
  /* Were we called from the interrupt level? */

  if (up_interrupt_context() || sched_idletask())
    {
      /* Yes, then try to allocate an I/O buffer without waiting */

      return iob_alloc_internal(size, throttled, 0);
    }
  else
    {
      /* Then allocate an I/O buffer, with optional waiting */

      return iob_alloc_internal(size, throttled, timeout);
    }
}
