/****************************************************************************
 * arch/arm64/include/arch.h
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

/* This file should never be included directly but, rather,
 * only indirectly through nuttx/arch.h
 */

#ifndef __ARCH_ARM64_INCLUDE_ARCH_H
#define __ARCH_ARM64_INCLUDE_ARCH_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifndef __ASSEMBLY__
#  include <stdint.h>
#  include <stddef.h>
#  include <nuttx/macro.h>
#endif

#include <arch/barriers.h>

/* Include chip-specific IRQ definitions (including IRQ numbers) */

#include <arch/chip/irq.h>

/****************************************************************************
 * Pre-processor Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name:
 *   read_/write_/zero_/modify_ sysreg
 *
 * Description:
 *
 *   ARMv8 Architecture Registers access method
 *   All the macros need a memory clobber
 *
 ****************************************************************************/

#define read_sysreg(reg)                            \
  ({                                                \
    uint64_t __val;                                 \
    __asm__ volatile ("mrs %0, " STRINGIFY(reg)     \
                    : "=r" (__val) :: "memory");    \
    __val;                                          \
  })

#define write_sysreg(__val, reg)                    \
  ({                                                \
    __asm__ volatile ("msr " STRINGIFY(reg) ", %x0"  \
                      : : "r" (__val) : "memory");  \
  })

#define zero_sysreg(reg)                            \
  ({                                                \
    __asm__ volatile ("msr " STRINGIFY(reg) ", xzr" \
                      ::: "memory");                \
  })

#define modify_sysreg(v,m,a)                        \
  write_sysreg((read_sysreg(a) & ~(m)) |            \
               ((uintptr_t)(v) & (m)), a)

#ifdef CONFIG_PERCPU_SECTION
#  if defined(CONFIG_BUILD_FLAT) || defined(__KERNEL__) /* Kernel Space */
#    define up_this_cpu_var(v) (*(typeof(v) *)(read_sysreg(tpidr_el1) + (uintptr_t)&(v)))
#  else /* User Space */
#    define up_this_cpu_var(v) (*(typeof(v) *)(read_sysreg(tpidrro_el0) + (uintptr_t)&(v)))
#  endif
#endif

#ifdef CONFIG_ARCH_ADDRENV

/* All implementations have 4 levels of page tables */

#define ARCH_PGT_MAX_LEVELS (4)
#define ARCH_SPGTS          (ARCH_PGT_MAX_LEVELS - 1)

#endif /* CONFIG_ARCH_ADDRENV */

#define UP_WFE() __asm__ __volatile__ ("wfe" : : : "memory")
#define UP_SEV() __asm__ __volatile__ ("sev" : : : "memory")

/* MPIDR_EL1, Multiprocessor Affinity Register */

#define MPIDR_AFFLVL_MASK   (0xff)

#define MPIDR_AFF0_SHIFT    (0)
#define MPIDR_AFF1_SHIFT    (8)
#define MPIDR_AFF2_SHIFT    (16)
#define MPIDR_AFF3_SHIFT    (32)

/* mpidr_el1 register, the register is define:
 *   - bit 0~7:   Aff0
 *   - bit 8~15:  Aff1
 *   - bit 16~23: Aff2
 *   - bit 24:    MT, multithreading
 *   - bit 25~29: RES0
 *   - bit 30:    U, multiprocessor/Uniprocessor
 *   - bit 31:    RES1
 *   - bit 32~39: Aff3
 *   - bit 40~63: RES0
 *   Different ARM64 Core will use different Affn define, the mpidr_el1
 *  value is not CPU number, So we need to change CPU number to mpid
 *  and vice versa
 */

#define GET_MPIDR()                              \
  ({                                             \
    uint64_t __val;                              \
    __asm__ volatile ("mrs %0, mpidr_el1"        \
                    : "=r" (__val) :: "memory"); \
    __val;                                       \
  })

/****************************************************************************
 * Inline functions
 ****************************************************************************/

/****************************************************************************
 * Public Types
 ****************************************************************************/

#ifdef CONFIG_ARCH_ADDRENV
#ifndef __ASSEMBLY__

/* A task group must have its L1 table in memory always, and the rest can
 * be dynamically committed to memory (and even swapped).
 *
 * In this implementation level tables except the final level N are always
 * kept in static memory, while the level N tables are always dynamically
 * allocated. There is one static page per level in `spgtables[]`.
 *
 * For the VMSAv8-64 address translation system this means that:
 * - A task can not have more than 1GB of memory allocated. This should be
 *   plenty enough...
 * - The minimum amount of memory needed for page tables per task is 12K,
 *   which gives access to 2MB of memory. This is plenty for many tasks.
 */

#ifdef CONFIG_ARCH_USE_MMU
struct arch_addrenv_s
{
  /* Physical addresses of the static page tables (levels N-1) here, these
   * are allocated when a task is created.
   */

  uintptr_t spgtables[ARCH_SPGTS];

  /* The text, data, heap bases and heap size here */

  uintptr_t textvbase;
  uintptr_t datavbase;
  uintptr_t heapvbase;
  size_t    heapsize;

  /* The page directory root (ttbr0) value */

  uintptr_t ttbr0;
};

#elif defined(CONFIG_ARCH_USE_MPU)
struct arch_addrenv_s
{
  uintptr_t text;
  size_t textsize;
  uintptr_t data;
  size_t datasize;
  uintptr_t heap;
  size_t heapsize;
#  ifdef CONFIG_ARCH_VMA_MAPPING
  uintptr_t shm;
  size_t shmsize;
#  endif
};
#endif

typedef struct arch_addrenv_s arch_addrenv_t;

#endif /* __ASSEMBLY__ */
#endif /* CONFIG_ARCH_ADDRENV */

/****************************************************************************
 * Public Data
 ****************************************************************************/

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: up_cpu_index
 *
 * Description:
 *   Return the real core number regardless CONFIG_SMP setting
 *
 ****************************************************************************/

#ifdef CONFIG_ARCH_HAVE_MULTICPU
#  ifndef MPID_TO_CORE
#    define MPID_TO_CORE(mpid) \
            (((mpid) >> MPIDR_AFF0_SHIFT) & MPIDR_AFFLVL_MASK)
#  endif
#  define up_cpu_index() ((int)MPID_TO_CORE(GET_MPIDR()))
#endif /* CONFIG_ARCH_HAVE_MULTICPU */

#ifdef __cplusplus
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

#undef EXTERN
#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM64_INCLUDE_ARCH_H */
