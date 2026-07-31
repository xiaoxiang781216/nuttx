/****************************************************************************
 * arch/zsp/src/common/zsp_internal.h
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

#ifndef __ARCH_ZSP_SRC_COMMON_ZSP_INTERNAL_H
#define __ARCH_ZSP_SRC_COMMON_ZSP_INTERNAL_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifndef __ASSEMBLY__
#  include <nuttx/arch.h>
#  include <nuttx/compiler.h>
#  include <arch/irq.h>
#  include <sys/types.h>
#  include <stdint.h>
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Public Data
 ****************************************************************************/

#ifndef __ASSEMBLY__

/* Linker-exported symbols */

extern char _ebss[];

/* Idle thread stack boundaries (computed from CONFIG_*) */

extern const uintptr_t g_idle_topstack;
extern const uintptr_t g_idle_basestack;

/* Interrupt stack boundaries (computed from CONFIG_*) */

extern const uintptr_t g_intstackalloc;
extern const uintptr_t g_intstacktop;

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/* Context switching (assembly, in zsp_saverestore.S) */

void zsp_fullcontextrestore(uint32_t *regs) noreturn_function;
void zsp_switchcontext(uint32_t **saveregs, uint32_t *restoreregs);

/* IRQ handling */

uint32_t *zsp_doirq(int irq, uint32_t *regs);
void zsp_data_initialize(void);

#endif /* __ASSEMBLY__ */
#endif /* __ARCH_ZSP_SRC_COMMON_ZSP_INTERNAL_H */
