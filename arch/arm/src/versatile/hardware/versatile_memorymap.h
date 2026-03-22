/****************************************************************************
 * arch/arm/src/versatile/hardware/versatile_memorymap.h
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

#ifndef __ARCH_ARM_SRC_VERSATILE_HARDWARE_VERSATILE_MEMORYMAP_H
#define __ARCH_ARM_SRC_VERSATILE_HARDWARE_VERSATILE_MEMORYMAP_H

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* VersatilePB Physical Memory Map ******************************************/

/* RAM: 128 MiB starting at 0x00000000 */

#define VERSATILE_SDRAM_PADDR        0x00000000
#define VERSATILE_SDRAM_SIZE         0x08000000  /* 128 MiB */

/* Peripheral Registers: 0x10000000 - 0x101fffff (2 MiB) */

#define VERSATILE_PERIPH_PADDR       0x10000000
#define VERSATILE_PERIPH_SIZE        0x00200000  /* 2 MiB */

/* System registers */

#define VERSATILE_SYS_BASE           0x10000000  /* System registers */
#define VERSATILE_SIC_BASE           0x10003000  /* Secondary Interrupt Controller */

/* Peripheral base addresses */

#define VERSATILE_VIC_BASE           0x10140000  /* Primary VIC (PL190) */
#define VERSATILE_TIMER01_BASE       0x101e2000  /* SP804 Timer 0/1 */
#define VERSATILE_TIMER23_BASE       0x101e3000  /* SP804 Timer 2/3 */
#define VERSATILE_UART0_BASE         0x101f1000  /* PL011 UART0 */
#define VERSATILE_UART1_BASE         0x101f2000  /* PL011 UART1 */
#define VERSATILE_UART2_BASE         0x101f3000  /* PL011 UART2 */

/* MMU Section Mapping Definitions ******************************************/

/* Number of 1 MiB sections for each region */

#define VERSATILE_SDRAM_NSECTIONS    (VERSATILE_SDRAM_SIZE >> 20)
#define VERSATILE_PERIPH_NSECTIONS   (VERSATILE_PERIPH_SIZE >> 20)

/* Virtual addresses - use identity mapping for simplicity */

#define VERSATILE_SDRAM_VSECTION     VERSATILE_SDRAM_PADDR
#define VERSATILE_PERIPH_VSECTION    VERSATILE_PERIPH_PADDR

/* Low vectors at 0x00000000.
 * Page table is placed at 0x4000 so that the vector region at address 0
 * does not conflict with the L1 page table.
 */

#define PGTABLE_BASE_PADDR           (VERSATILE_SDRAM_PADDR + 0x4000)
#define PGTABLE_BASE_VADDR           (VERSATILE_SDRAM_VSECTION + 0x4000)

/* NuttX entry point starts after the page table (16 KiB) */

#define NUTTX_START_PADDR            (PGTABLE_BASE_PADDR + PGTABLE_SIZE)
#define NUTTX_START_VADDR            (PGTABLE_BASE_VADDR + PGTABLE_SIZE)

/* NuttX .text region - used by arm_head.S for MMU section mapping.
 * Since versatile-pb runs from SDRAM, text and RAM share the same region.
 * Addresses are aligned to 1MB boundaries for L1 section mapping.
 */

#define NUTTX_TEXT_PADDR             (CONFIG_RAM_START & 0xfff00000)
#define NUTTX_TEXT_VADDR             (CONFIG_RAM_VSTART & 0xfff00000)
#define NUTTX_TEXT_PEND              ((CONFIG_RAM_END + 0x000fffff) & 0xfff00000)
#define NUTTX_TEXT_SIZE              (NUTTX_TEXT_PEND - NUTTX_TEXT_PADDR)

#endif /* __ARCH_ARM_SRC_VERSATILE_HARDWARE_VERSATILE_MEMORYMAP_H */
