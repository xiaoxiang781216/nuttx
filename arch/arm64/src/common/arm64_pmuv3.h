/****************************************************************************
 * arch/arm64/src/common/arm64_pmuv3.h
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

#ifndef __ARCH_ARM64_SRC_COMMON_ARM64_PMUV3_H
#define __ARCH_ARM64_SRC_COMMON_ARM64_PMUV3_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <debug.h>
#include <perf/arm_pmuv3.h>
#include "arm64_arch.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define RETURN_READ_PMEVCNTRN(n) \
        return read_sysreg(pmevcntr##n##_el0)

#define WRITE_PMEVCNTRN(n) \
        write_sysreg(val, pmevcntr##n##_el0)

#define WRITE_PMEVTYPERN(n) \
        write_sysreg(val, pmevtyper##n##_el0)

/****************************************************************************
 * Inline Functions
 ****************************************************************************/

static inline uint64_t read_pmevcntrn(int n)
{
  PMEVN_SWITCH(n, RETURN_READ_PMEVCNTRN);
  return 0;
}

static inline void write_pmevcntrn(int n, unsigned long val)
{
  PMEVN_SWITCH(n, WRITE_PMEVCNTRN);
}

static inline void write_pmevtypern(int n, unsigned long val)
{
  PMEVN_SWITCH(n, WRITE_PMEVTYPERN);
}

static inline uint32_t read_pmuver(void)
{
  uint64_t ver = read_sysreg(id_aa64dfr0_el1);

  ver = (uint64_t)(ver << (64 - 4 - ID_AA64DFR0_EL1_PMUVER_SHIFT))
                          >> (64 - 4);

  return (uint32_t)ver;
}

static inline void write_pmcr(uint32_t val)
{
  write_sysreg(val, pmcr_el0);
}

static inline uint32_t read_pmcr(void)
{
  return read_sysreg(pmcr_el0);
}

static inline void write_pmselr(uint32_t val)
{
  write_sysreg(val, pmselr_el0);
}

static inline uint32_t read_pmselr(void)
{
  return read_sysreg(pmselr_el0);
}

static inline void write_pmccntr(uint64_t val)
{
  write_sysreg(val, pmccntr_el0);
}

static inline uint64_t read_pmccntr(void)
{
  return read_sysreg(pmccntr_el0);
}

static inline void write_pmxevcntr(uint32_t val)
{
  write_sysreg(val, pmxevcntr_el0);
}

static inline uint32_t read_pmxevcntr(void)
{
  return read_sysreg(pmxevcntr_el0);
}

static inline void write_pmxevtyper(uint32_t val)
{
  write_sysreg(val, pmxevtyper_el0);
}

static inline void write_pmcntenset(uint32_t val)
{
  write_sysreg(val, pmcntenset_el0);
}

static inline void write_pmcntenclr(uint32_t val)
{
  write_sysreg(val, pmcntenclr_el0);
}

static inline void write_pmintenset(uint32_t val)
{
  write_sysreg(val, pmintenset_el1);
}

static inline void write_pmintenclr(uint32_t val)
{
  write_sysreg(val, pmintenclr_el1);
}

static inline void write_pmccfiltr(uint32_t val)
{
  write_sysreg(val, pmccfiltr_el0);
}

static inline void write_pmovsclr(uint32_t val)
{
  write_sysreg(val, pmovsclr_el0);
}

static inline uint32_t read_pmovsclr(void)
{
  return read_sysreg(pmovsclr_el0);
}

static inline void write_pmuserenr(uint32_t val)
{
  write_sysreg(val, pmuserenr_el0);
}

static inline uint32_t read_pmceid0(void)
{
  return read_sysreg(pmceid0_el0);
}

static inline uint32_t read_pmceid1(void)
{
  return read_sysreg(pmceid1_el0);
}

static inline bool pmuv3_implemented(int pmuver)
{
  return !(pmuver == ID_AA64DFR0_EL1_PMUVER_IMP_DEF ||
           pmuver == ID_AA64DFR0_EL1_PMUVER_NI);
}

#endif /* __ARCH_ARM64_SRC_COMMON_ARM64_PMUV3_H */
