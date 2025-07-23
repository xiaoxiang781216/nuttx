/****************************************************************************
 * include/nuttx/hwtracing/tricoreht/tricoreht_trif.h
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

#ifndef __INCLUDE_NUTTX_TRICOREHT_TRICOREHT_TRIF_H
#define __INCLUDE_NUTTX_TRICOREHT_TRICOREHT_TRIF_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/mutex.h>
#include <nuttx/hwtracing/hwtracing.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Public Types
 ****************************************************************************/

enum trif_config_type_e
{
  TRIF_CONFIG_TYPE_TFIF,
  TRIF_CONFIG_TYPE_TME_ETH,
  TRIF_CONFIG_TYPE_TME_RAM,
  TRIF_CONFIG_TYPE_SGBT,
};

struct tricoreht_trif_dev_s
{
  struct hwtracing_dev_s htdev;
  enum trif_config_type_e type;  /* Device type: tfif/tme/sgbt. */
  uint32_t size;                 /* RAM buffer size. */
  FAR uint32_t *buf;             /* Pointer to the RAM buf. */
  uint32_t len;                  /* Valid data len in RAM buffer. */
  mutex_t lock;                  /* Mutex for driver's open/close. */
  uint32_t offset;               /* Data offset in ETR buffer. */
  uint8_t opencnt;               /* trif device's open count. */
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: trif_register
 *
 * Description:
 *   Register a trif devices.
 *
 * Input Parameters:
 *   desc  - A description of this tricoreht device.
 *
 * Returned Value:
 *   Pointer to a trif device on success; NULL on failure.
 *
 ****************************************************************************/

FAR struct tricoreht_trif_dev_s *
trif_register(FAR const struct hwtracing_desc_s *desc);

/****************************************************************************
 * Name: trif_unregister
 *
 * Description:
 *   Unregister a trif devices.
 *
 * Input Parameters:
 *   trifdev  - Pointer to the trif device.
 *
 ****************************************************************************/

void trif_unregister(FAR struct tricoreht_trif_dev_s *trifdev);

#endif  //__INCLUDE_NUTTX_TRICOREHT_TRICOREHT_TRIF_H