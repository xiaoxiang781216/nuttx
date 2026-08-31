/****************************************************************************
 * arch/risc-v/src/common/riscv_plic.h
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

#ifndef __ARCH_RISCV_SRC_COMMON_RISCV_PLIC_H
#define __ARCH_RISCV_SRC_COMMON_RISCV_PLIC_H

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Default PLIC context: M-mode hart 0 = 0, S-mode hart 0 = 1 */

#ifndef RISCV_PLIC_CONTEXT
#  ifdef CONFIG_ARCH_USE_S_MODE
#    define RISCV_PLIC_CONTEXT 1
#  else
#    define RISCV_PLIC_CONTEXT 0
#  endif
#endif

#ifndef RISCV_PLIC_IRQ_OFFSET
#  define RISCV_PLIC_IRQ_OFFSET RISCV_IRQ_EXT
#endif

#define RISCV_PLIC_PRIORITY (RISCV_PLIC_BASE + 0x000000)
#define RISCV_PLIC_PENDING  (RISCV_PLIC_BASE + 0x001000)
#define RISCV_PLIC_ENABLE_BASE \
  (RISCV_PLIC_BASE + 0x002000 + RISCV_PLIC_CONTEXT * 0x80)
#define RISCV_PLIC_THRESHOLD \
  (RISCV_PLIC_BASE + 0x200000 + RISCV_PLIC_CONTEXT * 0x1000)
#define RISCV_PLIC_CLAIM \
  (RISCV_PLIC_BASE + 0x200004 + RISCV_PLIC_CONTEXT * 0x1000)

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

void riscv_plic_init(void);

#endif /* __ARCH_RISCV_SRC_COMMON_RISCV_PLIC_H */
