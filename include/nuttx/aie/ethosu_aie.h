/****************************************************************************
 * include/nuttx/aie/ethosu_aie.h
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

#ifndef __INCLUDE_NUTTX_AIE_ETHOSU_AIE_H
#define __INCLUDE_NUTTX_AIE_ETHOSU_AIE_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>
#include <stddef.h>
#include <nuttx/aie/ai_engine.h>

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Structure to pass Ethos-U invoke parameters through AIE ioctl */

struct ethosu_aie_invoke_params
{
  FAR const void *custom_data_ptr;       /* Command stream data pointer */
  int custom_data_size;                  /* Command stream data size */
  FAR uint64_t *base_addr;               /* Base addresses array */
  FAR const size_t *base_addr_size;      /* Base address sizes array */
  int num_base_addr;                     /* Number of base addresses */
};

/****************************************************************************
 * Public Definitions
 ****************************************************************************/

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: ethosu_initialize
 *
 * Description:
 *   Initialize the Ethos-U NPU driver and register it with the AI Engine.
 *   This function should be called during board initialization to set up
 *   the Ethos-U hardware support.
 *
 * Input Parameters:
 *   devpath          - The full path to the driver device node to be
 *                      registered (e.g., "/dev/aie0"). Must not be NULL.
 *   base_address     - NPU register base address
 *   fast_memory      - Fast memory area, used for Ethos-U65 with spilling
 *   fast_memory_size - Size in bytes of fast memory area
 *   secure_enable    - Configure NPU in secure- or non-secure mode
 *   privilege_enable - Configure NPU in privileged- or non-privileged mode
 *   irq              - IRQ number used by the NPU
 *
 * Returned Value:
 *   OK (0) on success; a negated errno value on failure.
 *
 ****************************************************************************/

int ethosu_initialize(FAR const char *devpath,
                      FAR void *base_address,
                      FAR void *fast_memory,
                      size_t fast_memory_size,
                      bool secure_enable,
                      bool privilege_enable,
                      int irq);

#endif /* __INCLUDE_NUTTX_AIE_ETHOSU_AIE_H */
