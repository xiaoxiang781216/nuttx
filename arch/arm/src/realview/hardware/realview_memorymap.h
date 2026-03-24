/****************************************************************************
 * arch/arm/src/realview/hardware/realview_memorymap.h
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

#ifndef __ARCH_ARM_SRC_REALVIEW_HARDWARE_REALVIEW_MEMORYMAP_H
#define __ARCH_ARM_SRC_REALVIEW_HARDWARE_REALVIEW_MEMORYMAP_H

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* RealView-EB Physical Memory Map ******************************************/

/* RAM: 256 MiB starting at 0x00000000 */

#define REALVIEW_SDRAM_PADDR         0x00000000
#define REALVIEW_SDRAM_SIZE          0x10000000  /* 256 MiB */

/* Peripheral Registers: 0x10000000 - 0x100fffff (1 MiB) */

#define REALVIEW_PERIPH_PADDR        0x10000000
#define REALVIEW_PERIPH_SIZE         0x00100000  /* 1 MiB */

/* Peripheral base addresses */

#define REALVIEW_TIMER01_BASE        0x10011000  /* SP804 Timer 0/1 */
#define REALVIEW_TIMER23_BASE        0x10012000  /* SP804 Timer 2/3 */

/* External peripherals */

#define REALVIEW_ETH_PADDR           0x4e000000  /* SMC91C111 Ethernet */
#define REALVIEW_ETH_SIZE            0x00100000  /* 1 MiB */

/* MMU Section Mapping Definitions ******************************************/

/* Number of 1 MiB sections for each region */

#define REALVIEW_SDRAM_NSECTIONS     (REALVIEW_SDRAM_SIZE >> 20)
#define REALVIEW_PERIPH_NSECTIONS    (REALVIEW_PERIPH_SIZE >> 20)
#define REALVIEW_ETH_NSECTIONS       (REALVIEW_ETH_SIZE >> 20)

/* Virtual addresses - use identity mapping for simplicity */

#define REALVIEW_SDRAM_VSECTION      REALVIEW_SDRAM_PADDR
#define REALVIEW_PERIPH_VSECTION     REALVIEW_PERIPH_PADDR
#define REALVIEW_ETH_VSECTION        REALVIEW_ETH_PADDR

/* Low vectors at 0x00000000.
 * Page table is placed at 0x4000 so that the vector region at address 0
 * does not conflict with the L1 page table.
 */

#define PGTABLE_BASE_PADDR           (REALVIEW_SDRAM_PADDR + 0x4000)
#define PGTABLE_BASE_VADDR           (REALVIEW_SDRAM_VSECTION + 0x4000)

/* NuttX entry point starts after the page table (16 KiB) */

#define NUTTX_START_PADDR            (PGTABLE_BASE_PADDR + PGTABLE_SIZE)
#define NUTTX_START_VADDR            (PGTABLE_BASE_VADDR + PGTABLE_SIZE)

/* NuttX .text region - used by arm_head.S for MMU section mapping.
 * Since realview-eb runs from SDRAM, text and RAM share the same region.
 * Addresses are aligned to 1MB boundaries for L1 section mapping.
 */

#define NUTTX_TEXT_PADDR             (CONFIG_RAM_START & 0xfff00000)
#define NUTTX_TEXT_VADDR             (CONFIG_RAM_VSTART & 0xfff00000)
#define NUTTX_TEXT_PEND              ((CONFIG_RAM_END + 0x000fffff) & 0xfff00000)
#define NUTTX_TEXT_SIZE              (NUTTX_TEXT_PEND - NUTTX_TEXT_PADDR)

#endif /* __ARCH_ARM_SRC_REALVIEW_HARDWARE_REALVIEW_MEMORYMAP_H */
