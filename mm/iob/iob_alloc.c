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
#ifdef CONFIG_IOB_ALLOC
#  include <nuttx/kmalloc.h>
#endif
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
 *   Internal allocation using mempool.
 *
 ****************************************************************************/

static FAR struct iob_s *iob_alloc_internal(bool throttled,
                                            unsigned int timeout)
{
  FAR struct iob_s *iob;

  if (timeout == 0)
    {
      iob = mempool_tryallocate_throttled(g_iob_pool[0], throttled);
    }
  else
    {
      iob = mempool_allocate_throttled(g_iob_pool[0], timeout, throttled);
    }

  if (iob != NULL)
    {
      /* Verify IOB address alignment for flag bits in io_flink */

      DEBUGASSERT(((uintptr_t)iob & IOB_FLAGS_MASK) == 0);

      /* Initialize the IOB */

      iob->io_flink  = NULL; /* Not in a chain */
      iob->io_len    = 0;    /* Length of the data in the entry */
      iob->io_offset = 0;    /* Offset to the beginning of data */
      iob->io_pktlen = 0;    /* Total length of the packet */
      iob->io_conn   = NULL;
#ifdef CONFIG_IOB_ALLOC
      iob->io_bufsize = CONFIG_IOB_BUFSIZE;
      iob->io_data    = (FAR uint8_t *)ALIGN_UP((uintptr_t)(iob + 1),
                                                IOB_ALIGNMENT);
#endif

      /* Verify IOB and IOB data address alignment */

      DEBUGASSERT((uintptr_t)iob % IOB_ALIGNMENT == 0);
      DEBUGASSERT((uintptr_t)iob->io_data % IOB_ALIGNMENT == 0);
    }
  else
    {
      ioberr("iob_alloc_internal: Failed to allocate IOB\n");
    }

  return iob;
}

#ifdef CONFIG_IOB_ALLOC
/****************************************************************************
 * Name: iob_free_dynamic
 *
 * Description:
 *   Free the I/O buffer and payload to the heap
 *
 * Input Parameters:
 *   data -
 *
 ****************************************************************************/

static void iob_free_dynamic(FAR void *data)
{
  kmm_free(data);
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: iob_timedalloc
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

FAR struct iob_s *iob_timedalloc(bool throttled, unsigned int timeout)
{
  /* Were we called from the interrupt level? */

  if (up_interrupt_context() || sched_idletask())
    {
      /* Yes, then try to allocate an I/O buffer without waiting */

      return iob_alloc_internal(throttled, 0);
    }
  else
    {
      /* Then allocate an I/O buffer, with optional waiting */

      return iob_alloc_internal(throttled, timeout);
    }
}

/****************************************************************************
 * Name: iob_alloc
 *
 * Description:
 *  Allocate an I/O buffer by taking the buffer at the head of the free list.
 *
 ****************************************************************************/

FAR struct iob_s *iob_alloc(bool throttled)
{
  return iob_timedalloc(throttled, UINT_MAX);
}

/****************************************************************************
 * Name: iob_tryalloc
 *
 * Description:
 *   Try to allocate an I/O buffer by taking the buffer at the head of the
 *   free list without waiting for a buffer to become free.
 *
 ****************************************************************************/

FAR struct iob_s *iob_tryalloc(bool throttled)
{
  return iob_alloc_internal(throttled, 0);
}

#ifdef CONFIG_IOB_ALLOC

/****************************************************************************
 * Name: iob_alloc_dynamic
 *
 * Description:
 *   Allocate an I/O buffer and playload from heap
 *
 * Input Parameters:
 *   size    - The size of the io_data that is allocated.
 *
 *             +---------+
 *             |   IOB   |
 *             | io_data |--+
 *             | buffer  |<-+
 *             +---------+
 *
 ****************************************************************************/

FAR struct iob_s *iob_alloc_dynamic(uint16_t size)
{
  FAR struct iob_s *iob;
  size_t alignsize;

  alignsize = ALIGN_UP(sizeof(struct iob_s), IOB_ALIGNMENT) + size;

  iob = kmm_memalign(IOB_ALIGNMENT, alignsize);
  if (iob)
    {
      memset(iob, 0, offsetof(struct iob_s, io_data));
      iob->io_bufsize = size;             /* Total length of the iob buffer */
      iob->io_free    = iob_free_dynamic; /* Customer free callback */
      iob->io_data    = (FAR uint8_t *)ALIGN_UP((uintptr_t)(iob + 1),
                                                IOB_ALIGNMENT);
    }

  return iob;
}

/****************************************************************************
 * Name: iob_alloc_with_data
 *
 * Description:
 *   Allocate an I/O buffer from heap and attach the external payload
 *
 * Input Parameters:
 *   data    - Make io_data point to a specific address, the caller is
 *             responsible for the memory management. The caller should
 *             ensure that the memory is not freed before the iob is freed.
 *
 *             +---------+  +-->+--------+
 *             |   IOB   |  |   |  data  |
 *             | io_data |--+   +--------+
 *             +---------+
 *
 *   size    - The size of the data parameter
 *   free_cb - Notify the caller when the iob is freed. The caller can
 *             perform additional operations on the data before it is freed.
 *             The free_cb is called when the iob is freed.
 *
 ****************************************************************************/

FAR struct iob_s *iob_alloc_with_data(FAR void *data, uint16_t size,
                                      iob_free_cb_t free_cb)
{
  FAR struct iob_s *iob;

  DEBUGASSERT(free_cb != NULL);

  iob = kmm_zalloc(sizeof(struct iob_s));
  if (iob)
    {
      iob->io_bufsize = size;    /* Total length of the iob buffer */
      iob->io_free    = free_cb; /* Customer free callback */
      iob->io_data    = data;
    }

  return iob;
}

/****************************************************************************
 * Name: iob_init_with_data
 *
 * Description:
 *   Initialize an I/O buffer and playload
 *
 * Input Parameters:
 *   data    - Make io_data point to a specific address, the caller is
 *             responsible for the memory management. The caller should
 *             ensure that the memory is not freed before the iob is freed,
 *             and caller need to reserve space for alignment.
 *   size    - The size of the data parameter
 *   free_cb - Notify the caller when the iob is freed. The caller can
 *             perform additional operations on the data before it is freed.
 *
 *             +---------+
 *             |   IOB   |
 *             | io_data |--+
 *             | buffer  |<-+
 *             +---------+
 *
 ****************************************************************************/

FAR struct iob_s *iob_init_with_data(FAR void *data, uint16_t size,
                                     iob_free_cb_t free_cb)
{
  FAR struct iob_s *iob = (FAR struct iob_s *)data;

  memset(iob, 0, offsetof(struct iob_s, io_data));
  iob->io_free    = free_cb; /* Customer free callback */
  iob->io_data    = (FAR uint8_t *)ALIGN_UP((uintptr_t)(iob + 1),
                                            IOB_ALIGNMENT);
  iob->io_bufsize = ((FAR uint8_t *)data + size) - iob->io_data;

  return iob;
}

#endif
