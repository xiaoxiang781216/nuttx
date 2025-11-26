/****************************************************************************
 * arch/tricore/include/arch.h
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

#ifndef __ARCH_TRICORE_INCLUDE_ARCH_H
#define __ARCH_TRICORE_INCLUDE_ARCH_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifndef __ASSEMBLY__
#  include <stdint.h>
#  include <stddef.h>
#endif

#include <arch/barriers.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Address <--> Context Save Areas */

#define tricore_csa2addr(csa) ((uintptr_t *)((((csa) & 0x000F0000) << 12) \
                                             | (((csa) & 0x0000FFFF) << 6)))
#define tricore_addr2csa(addr) ((uintptr_t)(((((uintptr_t)(addr)) & 0xF0000000) >> 12) \
                                            | (((uintptr_t)(addr) & 0x003FFFC0) >> 6)))

/* write/read */

#ifdef CONFIG_TRICORE_TOOLCHAIN_TASKING
  #define read_sysreg(reg)                                                 \
    ({                                                                     \
      uint32_t __val;                                                      \
      __asm__ volatile("mov.d\t %0, "#reg"" : "=d" (__val) :: "memory");   \
      __val;                                                               \
    })

  #define write_sysreg(__val, reg)                                         \
    ({                                                                     \
      __asm__ volatile("mov.a\t "#reg", %0" :: "d" (__val) : "memory");    \
    })
#else
  #define read_sysreg(reg)                                                 \
    ({                                                                     \
      uint32_t __val;                                                      \
      __asm__ volatile("mov.d\t %0, %%"#reg"" : "=d" (__val) :: "memory"); \
      __val;                                                               \
    })

  #define write_sysreg(__val, reg)                                         \
    ({                                                                     \
      __asm__ volatile("mov.a\t %%"#reg", %0" :: "d" (__val) : "memory");  \
    })
#endif

/****************************************************************************
 * Public Types
 ****************************************************************************/

#ifdef CONFIG_ARCH_ADDRENV
/* The task group resources are retained in a single structure, task_group_s
 * that is defined in the header file nuttx/include/nuttx/sched.h. The type
 * arch_addrenv_t must be defined by platform specific logic in
 * nuttx/arch/<architecture>/include/arch.h.
 *
 * These tables would hold the physical address of the level 2 page tables.
 * All would be initially NULL and would not be backed up with physical
 * memory until mappings in the level 2 page table are required.
 */

#ifdef CONFIG_ARCH_USE_MPU
struct arch_addrenv_s
{
  uintptr_t text;
  size_t    textsize;
  uintptr_t data;
  size_t    datasize;
  uintptr_t heap;
  size_t    heapsize;
};
#endif

typedef struct arch_addrenv_s arch_addrenv_t;
#endif

#ifdef CONFIG_PERCPU_SECTION
#  define up_this_cpu_var(v) (v)
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

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

#endif /* __ARCH_TRICORE_INCLUDE_ARCH_H */
