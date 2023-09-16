/****************************************************************************
 * include/perf/arm_pmuv3.h
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

#ifndef __INCLUDE_PERF_ARM_PMUV3_H
#define __INCLUDE_PERF_ARM_PMUV3_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <debug.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* ID_AA64DFR0_EL1.PMUVer */

#define ID_AA64DFR0_EL1_PMUVER_SHIFT    0x8
#define ID_AA64DFR0_EL1_PMUVER_NI       0x0
#define ID_AA64DFR0_EL1_PMUVER_V3P4     0x5
#define ID_AA64DFR0_EL1_PMUVER_V3P5     0x6
#define ID_AA64DFR0_EL1_PMUVER_IMP_DEF  0xf

#define PMU_MAX_COUNTERS                32
#define PMU_COUNTER_MASK                (PMU_MAX_COUNTERS - 1)

#define PMU_IDX_CYCLE_COUNTER           0
#define PMU_IDX_COUNTER0                1
#define PMU_IDX_CYCLE_COUNTER_USER      32

#define PMU_IDX_TO_COUNTER(x) \
        (((x) - PMU_IDX_COUNTER0) & PMU_COUNTER_MASK)

/* Common microarchitectural events */

#define PMUV3_PERFCTR_SW_INCR                   0x0000
#define PMUV3_PERFCTR_L1I_CACHE_REFILL          0x0001
#define PMUV3_PERFCTR_L1I_TLB_REFILL            0x0002
#define PMUV3_PERFCTR_L1D_CACHE_REFILL          0x0003
#define PMUV3_PERFCTR_L1D_CACHE                 0x0004
#define PMUV3_PERFCTR_L1D_TLB_REFILL            0x0005
#define PMUV3_PERFCTR_LD_RETIRED                0x0006
#define PMUV3_PERFCTR_ST_RETIRED                0x0007
#define PMUV3_PERFCTR_INST_RETIRED              0x0008
#define PMUV3_PERFCTR_EXC_TAKEN                 0x0009
#define PMUV3_PERFCTR_EXC_RETURN                0x000A
#define PMUV3_PERFCTR_CID_WRITE_RETIRED         0x000B
#define PMUV3_PERFCTR_PC_WRITE_RETIRED          0x000C
#define PMUV3_PERFCTR_BR_IMMED_RETIRED          0x000D
#define PMUV3_PERFCTR_BR_RETURN_RETIRED         0x000E
#define PMUV3_PERFCTR_UNALIGNED_LDST_RETIRED    0x000F
#define PMUV3_PERFCTR_BR_MIS_PRED               0x0010
#define PMUV3_PERFCTR_CPU_CYCLES                0x0011
#define PMUV3_PERFCTR_BR_PRED                   0x0012
#define PMUV3_PERFCTR_MEM_ACCESS                0x0013
#define PMUV3_PERFCTR_L1I_CACHE                 0x0014
#define PMUV3_PERFCTR_L1D_CACHE_WB              0x0015
#define PMUV3_PERFCTR_L2D_CACHE                 0x0016
#define PMUV3_PERFCTR_L2D_CACHE_REFILL          0x0017
#define PMUV3_PERFCTR_L2D_CACHE_WB              0x0018
#define PMUV3_PERFCTR_BUS_ACCESS                0x0019
#define PMUV3_PERFCTR_MEMORY_ERROR              0x001A
#define PMUV3_PERFCTR_INST_SPEC                 0x001B
#define PMUV3_PERFCTR_TTBR_WRITE_RETIRED        0x001C
#define PMUV3_PERFCTR_BUS_CYCLES                0x001D
#define PMUV3_PERFCTR_CHAIN                     0x001E
#define PMUV3_PERFCTR_L1D_CACHE_ALLOCATE        0x001F
#define PMUV3_PERFCTR_L2D_CACHE_ALLOCATE        0x0020
#define PMUV3_PERFCTR_BR_RETIRED                0x0021
#define PMUV3_PERFCTR_BR_MIS_PRED_RETIRED       0x0022
#define PMUV3_PERFCTR_STALL_FRONTEND            0x0023
#define PMUV3_PERFCTR_STALL_BACKEND             0x0024
#define PMUV3_PERFCTR_L1D_TLB                   0x0025
#define PMUV3_PERFCTR_L1I_TLB                   0x0026
#define PMUV3_PERFCTR_L2I_CACHE                 0x0027
#define PMUV3_PERFCTR_L2I_CACHE_REFILL          0x0028
#define PMUV3_PERFCTR_L3D_CACHE_ALLOCATE        0x0029
#define PMUV3_PERFCTR_L3D_CACHE_REFILL          0x002A
#define PMUV3_PERFCTR_L3D_CACHE                 0x002B
#define PMUV3_PERFCTR_L3D_CACHE_WB              0x002C
#define PMUV3_PERFCTR_L2D_TLB_REFILL            0x002D
#define PMUV3_PERFCTR_L2I_TLB_REFILL            0x002E
#define PMUV3_PERFCTR_L2D_TLB                   0x002F
#define PMUV3_PERFCTR_L2I_TLB                   0x0030
#define PMUV3_PERFCTR_REMOTE_ACCESS             0x0031
#define PMUV3_PERFCTR_LL_CACHE                  0x0032
#define PMUV3_PERFCTR_LL_CACHE_MISS             0x0033
#define PMUV3_PERFCTR_DTLB_WALK                 0x0034
#define PMUV3_PERFCTR_ITLB_WALK                 0x0035
#define PMUV3_PERFCTR_LL_CACHE_RD               0x0036
#define PMUV3_PERFCTR_LL_CACHE_MISS_RD          0x0037
#define PMUV3_PERFCTR_REMOTE_ACCESS_RD          0x0038
#define PMUV3_PERFCTR_L1D_CACHE_LMISS_RD        0x0039
#define PMUV3_PERFCTR_OP_RETIRED                0x003A
#define PMUV3_PERFCTR_OP_SPEC                   0x003B
#define PMUV3_PERFCTR_STALL                     0x003C
#define PMUV3_PERFCTR_STALL_SLOT_BACKEND        0x003D
#define PMUV3_PERFCTR_STALL_SLOT_FRONTEND       0x003E
#define PMUV3_PERFCTR_STALL_SLOT                0x003F

/* Extension microarchitectural events */

#define PMUV3_PERFCTR_SAMPLE_POP                0x4000
#define PMUV3_PERFCTR_SAMPLE_FEED               0x4001
#define PMUV3_PERFCTR_SAMPLE_FILTRATE           0x4002
#define PMUV3_PERFCTR_SAMPLE_COLLISION          0x4003

/* PMCR: Config reg */

#define PMU_PMCR_E                (1 << 0)
#define PMU_PMCR_P                (1 << 1)
#define PMU_PMCR_C                (1 << 2)
#define PMU_PMCR_D                (1 << 3)
#define PMU_PMCR_X                (1 << 4)
#define PMU_PMCR_DP               (1 << 5)
#define PMU_PMCR_LC               (1 << 6)
#define PMU_PMCR_LP               (1 << 7)
#define PMU_PMCR_N_SHIFT          11
#define PMU_PMCR_N_MASK           0x1f
#define PMU_PMCR_MASK             0xff

/* PMXEVTYPER: Event selection reg */

#define PMU_EVTYPE_MASK           0xc800ffff
#define PMU_EVTYPE_EVENT          0xffff

/* PMEVTYPER<n>_EL0 or PMCCFILTR_EL0: Event filters. */

#define PMU_EXCLUDE_EL1           (1U << 31)
#define PMU_EXCLUDE_EL0           (1U << 30)
#define PMU_INCLUDE_EL2           (1U << 27)

/* PMUSERENR: User enable reg */

#define PMU_USERENR_MASK          0xf
#define PMU_USERENR_EN            (1 << 0)
#define PMU_USERENR_SW            (1 << 1)
#define PMU_USERENR_CR            (1 << 2)
#define PMU_USERENR_ER            (1 << 3)

#define PMEVN_CASE(n, case_macro)                     \
                   case n: case_macro(n); break

#define PMEVN_SWITCH(x, case_macro)                   \
  do                                                  \
    {                                                 \
      switch (x)                                      \
        {                                             \
          PMEVN_CASE(0,  case_macro);                 \
          PMEVN_CASE(1,  case_macro);                 \
          PMEVN_CASE(2,  case_macro);                 \
          PMEVN_CASE(3,  case_macro);                 \
          PMEVN_CASE(4,  case_macro);                 \
          PMEVN_CASE(5,  case_macro);                 \
          PMEVN_CASE(6,  case_macro);                 \
          PMEVN_CASE(7,  case_macro);                 \
          PMEVN_CASE(8,  case_macro);                 \
          PMEVN_CASE(9,  case_macro);                 \
          PMEVN_CASE(10, case_macro);                 \
          PMEVN_CASE(11, case_macro);                 \
          PMEVN_CASE(12, case_macro);                 \
          PMEVN_CASE(13, case_macro);                 \
          PMEVN_CASE(14, case_macro);                 \
          PMEVN_CASE(15, case_macro);                 \
          PMEVN_CASE(16, case_macro);                 \
          PMEVN_CASE(17, case_macro);                 \
          PMEVN_CASE(18, case_macro);                 \
          PMEVN_CASE(19, case_macro);                 \
          PMEVN_CASE(20, case_macro);                 \
          PMEVN_CASE(21, case_macro);                 \
          PMEVN_CASE(22, case_macro);                 \
          PMEVN_CASE(23, case_macro);                 \
          PMEVN_CASE(24, case_macro);                 \
          PMEVN_CASE(25, case_macro);                 \
          PMEVN_CASE(26, case_macro);                 \
          PMEVN_CASE(27, case_macro);                 \
          PMEVN_CASE(28, case_macro);                 \
          PMEVN_CASE(29, case_macro);                 \
          PMEVN_CASE(30, case_macro);                 \
          default: _warn("Invalid PMEV* index\n");    \
        }                                             \
    }                                                 \
  while (0)

#define BITMAP_LAST_WORD_MASK(nbits) (~0UL >> (-(nbits) & (64 - 1)))
#define DIV_ROUND_UP(n,d) (((n) + (d) - 1) / (d))

/****************************************************************************
 * Inline Functions
 ****************************************************************************/

static inline void bitmap_from_arr32(unsigned long *bitmap,
                                     const uint32_t *buf,
                                     unsigned int nbits)
{
  unsigned int i;
  unsigned int halfwords;

  halfwords = DIV_ROUND_UP(nbits, 32);
  for (i = 0; i < halfwords; i++)
    {
      bitmap[i / 2] = (unsigned long) buf[i];
      if (++i < halfwords)
        {
          bitmap[i / 2] |= ((unsigned long) buf[i]) << 32;
        }
    }

  if (nbits % 64)
    {
      bitmap[(halfwords - 1) / 2] &= BITMAP_LAST_WORD_MASK(nbits);
    }
}

#endif /* __INCLUDE_PERF_ARM_PMUV3_H */
