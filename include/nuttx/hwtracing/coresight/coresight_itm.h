/****************************************************************************
 * include/nuttx/hwtracing/coresight/coresight_itm.h
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

#ifndef __INCLUDE_NUTTX_HWTRACING_CORESIGHT_CORESIGHT_ITM_H
#define __INCLUDE_NUTTX_HWTRACING_CORESIGHT_CORESIGHT_ITM_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/hwtracing/hwtracing.h>
#include <nuttx/spinlock.h>
#include <nuttx/streams.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct coresight_itm_dev_s
{
  struct hwtracing_dev_s htdev;
  int traceid;                       /* Trace id. */

  /* The total number of stimulus port support by this ITM. */

  spinlock_t lock;
  uint32_t numsp;
  uint32_t map;
};

struct itm_port_s
{
  spinlock_t lock;
  uintptr_t stimulus;
  uint32_t num;
};

struct itm_stream_s
{
  struct lib_outstream_s common;
  FAR struct itm_port_s *port;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: itm_alloc_port
 *
 * Description:
 *   Allocate a ITM stimulus port.
 *
 * Returned Value:
 *   Pointer to the ITM stimulus port on success; NULL on failure.
 *
 ****************************************************************************/

FAR struct itm_port_s *itm_alloc_port(void);

/****************************************************************************
 * Name: itm_free_port
 *
 * Description:
 *   Free a ITM stimulus port.
 *
 * Input Parameters:
 *   stimulus - Pointer to the ITM stimulus port.
 *
 * Returned Value:
 *   OK on success; a negated errno value on failure.
 *
 ****************************************************************************/

int itm_free_port(FAR struct itm_port_s *stimulus);

/****************************************************************************
 * Name: itm_stream_open
 *
 * Description:
 *   Open a ITM stream.
 *
 ****************************************************************************/

void itm_stream_open(FAR struct itm_port_s *port,
                     FAR struct itm_stream_s *stream);

/****************************************************************************
 * Name: itm_send
 *
 * Description:
 *   Send data to ITM stimulus port.
 *
 * Input Parameters:
 *   stimulus - Pointer to the ITM stimulus port.
 *   data     - Pointer to the data to send.
 *   size     - The size of the data to send.
 *
 * Returned Value:
 *   The number of bytes sent on success; a negated errno value on failure.
 *
 ****************************************************************************/

ssize_t itm_send(FAR struct itm_port_s *port, FAR const void *buffer,
                 size_t size);

/****************************************************************************
 * Name: itm_register
 *
 * Description:
 *   Register a ITM devices.
 *
 * Input Parameters:
 *   desc  - A description of this coresight device.
 *
 * Returned Value:
 *   Pointer to a ITM device on success; NULL on failure.
 *
 ****************************************************************************/

FAR struct coresight_itm_dev_s *
itm_register(FAR const struct hwtracing_desc_s *desc);

/****************************************************************************
 * Name: itm_unregister
 *
 * Description:
 *   Unregister a ITM devices.
 *
 * Input Parameters:
 *   itmdev  - Pointer to the ITM device.
 *
 ****************************************************************************/

void itm_unregister(FAR struct coresight_itm_dev_s *itmdev);

#endif /* __INCLUDE_NUTTX_HWTRACING_CORESIGHT_CORESIGHT_ITM_H */
