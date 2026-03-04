/****************************************************************************
 * arch/arm/src/arm_a_r/arm_pmuv2.h
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

#ifndef __ARCH_ARM_SRC_ARMV7_A_ARMV7_PMU_H
#define __ARCH_ARM_SRC_ARMV7_A_ARMV7_PMU_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define ARMV7PMU_IRQ               23
#define ARMV7PMU_MAX_HWEVENTS      32

/* Counter index mapping used by the ARMv7 PMU implementation.
 * - Index 0 is the dedicated cycle counter (PMCCNTR).
 * - Index 1..N are programmable event counters (PMNx).
 */

#define ARMV7_IDX_CYCLE_COUNTER      0
#define ARMV7_IDX_COUNTER0           1
#define ARMV7_IDX_COUNTER_MAX        ARMV7PMU_MAX_HWEVENTS
#define ARMV7_IDX_CYCLE_COUNTER_USER 32

/* Convert internal counter index (ARMV7_IDX_COUNTER0..) to PMU counter
 * register number (0..).
 */
#define ARMV7_IDX_TO_COUNTER(idx)    ((idx) - ARMV7_IDX_COUNTER0)

/* PMNC (Performance Monitor Control Register) fields.
 * E: enable all counters, P: reset event counters, C: reset cycle counter.
 * N: number of event counters implemented.
 */

#define ARMV7_PMNC_MASK              0x3F
#define ARMV7_PMNC_E                 BIT(0)
#define ARMV7_PMNC_P                 BIT(1)
#define ARMV7_PMNC_C                 BIT(2)
#define ARMV7_PMNC_N_SHIFT           11
#define ARMV7_PMNC_N_MASK            0x1F

/* Event selection encoding (PMXEVTYPER).
 * EVENT: low 8 bits event id; MASK keeps architectural fields used by driver
 */

#define ARMV7_EVTYPE_EVENT           0xFF
#define ARMV7_EVTYPE_MASK            0xC80000FF

/* Mode filtering bits (implementation-defined/architectural extensions).
 * EXCLUDE_USER: don't count in EL0/user.
 * EXCLUDE_PL1 : don't count in privileged (PL1/EL1).
 * INCLUDE_HYP : include hypervisor (if present).
 */

#define ARMV7_EXCLUDE_USER           BIT(30)
#define ARMV7_EXCLUDE_PL1            BIT(31)
#define ARMV7_INCLUDE_HYP            BIT(27)

/* Mask for overflow status value (clear reserved/flag bit positions).
 * BIT(30) is masked out per ARMv7 PMU overflow indication semantics.
 */

#define ARMV7_OVERFLOWED_MASK        (UINT32_MAX & ~BIT(30))

/* Common ARMv7 architectural event numbers (PMU event codes). */

#define ARMV7_PERFCTR_L1_ICACHE_REFILL   0x01
#define ARMV7_PERFCTR_ITLB_REFILL        0x02
#define ARMV7_PERFCTR_L1_DCACHE_REFILL   0x03
#define ARMV7_PERFCTR_L1_DCACHE_ACCESS   0x04
#define ARMV7_PERFCTR_DTLB_REFILL        0x05
#define ARMV7_PERFCTR_INSTR_EXECUTED     0x08
#define ARMV7_PERFCTR_PC_WRITE           0x0C
#define ARMV7_PERFCTR_PC_BRANCH_MIS_PRED 0x10
#define ARMV7_PERFCTR_CPU_CYCLES         0xFF
#define ARMV7_PERFCTR_PC_BRANCH_PRED     0x12
#define ARMV7_PERFCTR_BUS_CYCLES         0x1D

#endif /* __ARCH_ARM_SRC_ARMV7_A_ARMV7_PMU_H */
