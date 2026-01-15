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

static inline FAR struct mempool_s *
iob_find_mempool(size_t size, FAR int *idx)
{
  int i = CONFIG_IOB_NPOOLS - 1;

  if (size > CONFIG_IOB_BUFSIZE)
    {
      return NULL;
    }
  else if (CONFIG_IOB_NPOOLS == 1)
    {
      *idx = 0;
      return g_iob_pool[0];
    }

  do
    {
      if (IOB_ALIGN_SIZE(size) <= g_iob_pool[i]->blocksize)
        {
          *idx = i;
          return g_iob_pool[i];
        }
    }
  while (--i >= 0);

  return NULL;
}

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
  int idx;

  mempool = iob_find_mempool(size, &idx);
  if (mempool == NULL)
    {
      return NULL;
    }

  if (timeout == 0)
    {
      do
        {
          iob = mempool_tryallocate_throttled(mempool, throttled);
          if (iob != NULL)
            {
              break;
            }
        }
      while (--idx >= 0 && (mempool = g_iob_pool[idx]) != NULL);
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
      iob->io_pool    = mempool;
#endif
      iob->io_flink   = NULL; /* Not in a chain */
      iob->io_len     = 0;    /* Length of the data in the entry */
      iob->io_offset  = 0;    /* Offset to the beginning of data */
      iob->io_pktlen  = 0;    /* Total length of the packet */
      iob->io_conn    = NULL;
#ifdef CONFIG_IOB_ALLOC
      iob->io_data    = (FAR uint8_t *)ALIGN_UP((uintptr_t)(iob + 1),
                                                IOB_ALIGNMENT);
      iob->io_bufsize = mempool->blocksize - IOB_ALIGN_SIZE(0);
#endif

      /* Verify IOB and IOB data address alignment */

      DEBUGASSERT((uintptr_t)iob % IOB_ALIGNMENT == 0);
      DEBUGASSERT((uintptr_t)iob->io_data % IOB_ALIGNMENT == 0);
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
