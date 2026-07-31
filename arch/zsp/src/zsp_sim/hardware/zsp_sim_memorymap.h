/****************************************************************************
 * arch/zsp/src/zsp_sim/hardware/zsp_sim_memorymap.h
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

#ifndef __ARCH_ZSP_SRC_ZSP_SIM_HARDWARE_ZSP_SIM_MEMORYMAP_H
#define __ARCH_ZSP_SRC_ZSP_SIM_HARDWARE_ZSP_SIM_MEMORYMAP_H

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* ZSP simulator memory map.  These addresses are adjustable by the SoC
 * integrator (per VeriSilicon linker_g5.lcf documentation), but fixed for
 * the zsp_sim simulator target at their NanoPlus default values.
 */

#define ZSP_ITCM_BASE    0
#define ZSP_ITCM_SIZE    0x100000                   /* 1MB */
#define ZSP_UVT_BASE     ZSP_ITCM_BASE
#define ZSP_UVT_SIZE     0x10000                    /* 64KB */
#define ZSP_DTCM_BASE    (ZSP_ITCM_BASE + ZSP_ITCM_SIZE)
#define ZSP_DTCM_SIZE    0x100000                   /* 1MB */
#define ZSP_PMEM_BASE    (ZSP_DTCM_BASE + ZSP_DTCM_SIZE)
#define ZSP_PMEM_SIZE    0x100000                   /* 1MB */
#define ZSP_TEXT_BASE    0x40000000                 /* 1024MB */

#endif /* __ARCH_ZSP_SRC_ZSP_SIM_HARDWARE_ZSP_SIM_MEMORYMAP_H */
