/****************************************************************************
 * arch/risc-v/src/common/riscv_common_memorymap.h
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

#ifndef __ARCH_RISC_V_SRC_COMMON_RISCV_COMMON_MEMORYMAP_H
#define __ARCH_RISC_V_SRC_COMMON_RISCV_COMMON_MEMORYMAP_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define _START_TEXT  _stext
#define _END_TEXT    _etext
#define _START_BSS   _sbss
#define _END_BSS     _ebss
#define _DATA_INIT   _eronly
#define _START_DATA  _sdata
#define _END_DATA    _edata
#define _START_TDATA _stdata
#define _END_TDATA   _etdata
#define _START_TBSS  _stbss
#define _END_TBSS    _etbss

#ifdef CONFIG_ARCH_RV_SHADOW_STACK
#  define SMP_SHADOW_STACK_SIZE (CONFIG_IDLETHREAD_STACKSIZE / 2)
#else
#  define SMP_SHADOW_STACK_SIZE (0)
#endif

#define SMP_STACK_MASK (15)

/**
 * SMP_STACK_SIZE: Total per-CPU idle stack size (aligned to 16 bytes),
 * including both the normal stack (CONFIG_IDLETHREAD_STACKSIZE) and the
 * shadow stack (SMP_SHADOW_STACK_SIZE, if enabled).
 */

#define SMP_STACK_SIZE \
   ((CONFIG_IDLETHREAD_STACKSIZE + SMP_SHADOW_STACK_SIZE) & ~SMP_STACK_MASK)

/****************************************************************************
 * Public Types
 ****************************************************************************/

#undef EXTERN
#if defined(__cplusplus)
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

#ifndef __ASSEMBLY__

/* Memory layout for idle stacks (from low to high address):
 *
 * Low Address
 * +-------------------------+
 * | CPU0 Normal Stack       | CONFIG_IDLETHREAD_STACKSIZE bytes
 * +-------------------------+
 * | CPU0 Shadow Stack       | SMP_SHADOW_STACK_SIZE bytes (if enabled)
 * +-------------------------+
 * | CPU1 Normal Stack       | CONFIG_IDLETHREAD_STACKSIZE bytes
 * +-------------------------+
 * | CPU1 Shadow Stack       | SMP_SHADOW_STACK_SIZE bytes (if enabled)
 * +-------------------------+
 * | ...                     |
 * +-------------------------+
 * | CPUn Normal Stack       | CONFIG_IDLETHREAD_STACKSIZE bytes
 * +-------------------------+
 * | CPUn Shadow Stack       | SMP_SHADOW_STACK_SIZE bytes (if enabled)
 * +-------------------------+ <- g_idle_topstack
 * High Address
 *
 * Each CPU gets SMP_STACK_SIZE bytes total (aligned to 16-byte boundary):
 * - CONFIG_IDLETHREAD_STACKSIZE bytes for normal stack
 * - SMP_SHADOW_STACK_SIZE bytes for shadow stack
 *     (if CONFIG_ARCH_RV_SHADOW_STACK enabled)
 */

EXTERN uintptr_t g_idle_topstack;

/* Address of per-cpu idle stack base */

#define g_cpux_idlestack(cpuid) \
   (g_idle_topstack - SMP_STACK_SIZE * (CONFIG_SMP_NCPUS - (cpuid)))

/* Address of per-cpu idle shadow stack base */

#define SMP_IDLE_STACK_SIZE (SMP_STACK_SIZE - SMP_SHADOW_STACK_SIZE)

#define g_cpux_idleshadowstack(cpuid) \
   (g_cpux_idlestack(cpuid) + SMP_IDLE_STACK_SIZE)

/* Address of the saved user stack pointer */

#if CONFIG_ARCH_INTERRUPTSTACK > 15
EXTERN uint8_t g_intstackalloc[]; /* Allocated stack base */
EXTERN uint8_t g_intstacktop[];   /* Initial top of interrupt stack */
#endif

/* These symbols are setup by the linker script. */

EXTERN uint8_t _stext[];           /* Start of .text */
EXTERN uint8_t _etext[];           /* End_1 of .text + .rodata */
EXTERN const uint8_t _eronly[];    /* End+1 of read only section (.text + .rodata) */
EXTERN uint8_t _sdata[];           /* Start of .data */
EXTERN uint8_t _edata[];           /* End+1 of .data */
EXTERN uint8_t _sbss[];            /* Start of .bss */
EXTERN uint8_t _ebss[];            /* End+1 of .bss */
EXTERN uint8_t _stdata[];          /* Start of .tdata */
EXTERN uint8_t _etdata[];          /* End+1 of .tdata */
EXTERN uint8_t _stbss[];           /* Start of .tbss */
EXTERN uint8_t _etbss[];           /* End+1 of .tbss */

#endif /* __ASSEMBLY__ */

#undef EXTERN
#ifdef __cplusplus
}
#endif
#endif /* __ARCH_RISC_V_SRC_COMMON_RISCV_COMMON_MEMORYMAP_H */
