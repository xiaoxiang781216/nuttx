/****************************************************************************
 * arch/arm/src/realview/chip.h
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

#ifndef __ARCH_ARM_SRC_REALVIEW_CHIP_H
#define __ARCH_ARM_SRC_REALVIEW_CHIP_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "hardware/realview_memorymap.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* GIC base address and offsets for mpcore.h consumption.
 *
 * Non-MPCore EB: standalone GIC at 0x10040000
 *   - CPU interface at offset 0x0000
 *   - Distributor at offset 0x1000
 *
 * MPCore EB: periphbase at 0x10100000
 *   - CPU interface at offset 0x0100
 *   - Distributor at offset 0x1000
 */

#define CHIP_MPCORE_VBASE    0x10040000
#define MPCORE_ICC_OFFSET    0x0000
#define MPCORE_ICD_OFFSET    0x1000

#endif /* __ARCH_ARM_SRC_REALVIEW_CHIP_H */
