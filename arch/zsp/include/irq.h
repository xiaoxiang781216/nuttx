/****************************************************************************
 * arch/zsp/include/irq.h
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

#ifndef __ARCH_ZSP_INCLUDE_IRQ_H
#define __ARCH_ZSP_INCLUDE_IRQ_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/percpu.h>
#include <arch/arch.h>
#include <sys/types.h>

#ifndef __ASSEMBLY__
#  include <stdbool.h>
#  include <stdint.h>
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Operand registers: r0-r31 are 16-bit each, stored as pairs in 32-bit
 * words: word[i] = (r[2i+1] << 16) | r[2i]
 */

#define REG_R0R1            0
#define REG_R2R3            1
#define REG_R4R5            2
#define REG_R6R7            3
#define REG_R8R9            4
#define REG_R10R11          5
#define REG_R12R13          6
#define REG_R14R15          7
#define REG_R16R17          8
#define REG_R18R19          9
#define REG_R20R21          10
#define REG_R22R23          11
#define REG_R24R25          12
#define REG_R26R27          13
#define REG_R28R29          14
#define REG_R30R31          15

/* Address registers: a0-a15, each 32-bit */

#define REG_A0              16
#define REG_A1              17
#define REG_A2              18
#define REG_A3              19
#define REG_A4              20
#define REG_A5              21
#define REG_A6              22
#define REG_A7              23  /* SP */
#define REG_A8              24
#define REG_A9              25
#define REG_A10             26
#define REG_A11             27
#define REG_A12             28
#define REG_A13             29
#define REG_A14             30
#define REG_A15             31

/* Control registers */

#define REG_RPC             32  /* Return PC */
#define REG_HWFLAG          33  /* Hardware flags */
#define REG_IMASK           34  /* Interrupt mask */
#define REG_LOOP0           35  /* Loop counter 0 */
#define REG_LOOP1           36  /* Loop counter 1 */
#define REG_LOOP2           37  /* Loop counter 2 */
#define REG_LOOP3           38  /* Loop counter 3 */
#define REG_LOOP0_BEG       39
#define REG_LOOP0_END       40
#define REG_LOOP1_BEG       41
#define REG_LOOP1_END       42

/* Total number of registers in context frame */

#define XCPTCONTEXT_REGS    43
#define XCPTCONTEXT_SIZE    (4 * XCPTCONTEXT_REGS)

/* Stack frame alignment (8 bytes for ZSP) */

#define STACKFRAME_ALIGN    8

/* %imask bit definitions */

#define ZSP_IMASK_GIE       (1 << 15)
#define ZSP_IMASK_PGIE      (1 << 14)
#define ZSP_IMASK_MT0       (1 << 5)

/* Convenience aliases */

#define REG_SP              REG_A7
#define REG_PC              REG_RPC

/* ZSP NanoPlus interrupt numbers */

/* Fault exceptions (from UVT) */

#define ZSP_IRQ_BUSFAULT    0
#define ZSP_IRQ_MPUFAULT    1
#define ZSP_IRQ_ILLFAULT    2
#define ZSP_IRQ_PRIFAULT    3
#define ZSP_IRQ_SYSCALL     4
#define ZSP_IRQ_COFAULT     5
#define ZSP_IRQ_FPUFAULT    6

/* Hardware interrupts */

#define ZSP_IRQ_NMI         7
#define ZSP_IRQ_INT0        8
#define ZSP_IRQ_INT1        9
#define ZSP_IRQ_INT2        10
#define ZSP_IRQ_INT3        11
#define ZSP_IRQ_INT4        12
#define ZSP_IRQ_INT5        13
#define ZSP_IRQ_INT6        14
#define ZSP_IRQ_INT7        15
#define ZSP_IRQ_INT8        16
#define ZSP_IRQ_INT9        17
#define ZSP_IRQ_INT10       18
#define ZSP_IRQ_INT11       19
#define ZSP_IRQ_INT12       20
#define ZSP_IRQ_INT13       21
#define ZSP_IRQ_TIMER1      22
#define ZSP_IRQ_TIMER0      23
#define ZSP_IRQ_INT14       24
#define ZSP_IRQ_INT15       25

/* NanoPlus extended interrupts */

#define ZSP_IRQ_INT16       26
#define ZSP_IRQ_INT17       27
#define ZSP_IRQ_INT18       28
#define ZSP_IRQ_INT19       29
#define ZSP_IRQ_INT20       30
#define ZSP_IRQ_INT21       31
#define ZSP_IRQ_INT22       32
#define ZSP_IRQ_INT23       33
#define ZSP_IRQ_INT24       34
#define ZSP_IRQ_INT25       35
#define ZSP_IRQ_INT26       36
#define ZSP_IRQ_INT27       37
#define ZSP_IRQ_INT28       38
#define ZSP_IRQ_INT29       39
#define ZSP_IRQ_INT30       40
#define ZSP_IRQ_INT31       41

#define NR_IRQS             42

/****************************************************************************
 * Public Data
 ****************************************************************************/

#ifndef __ASSEMBLY__

/* Exception context structure */

struct xcptcontext
{
  uint32_t *regs;                         /* Pointer to register save area */
  uint32_t *saved_regs;                   /* Saved regs for signal delivery */
};

#ifdef __cplusplus
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

DECLARE_PER_CPU(uint32_t *volatile, g_current_regs);
#define g_current_regs this_cpu_var(g_current_regs)

/****************************************************************************
 * Inline Functions
 ****************************************************************************/

static inline_function uint32_t *up_current_regs(void)
{
  return (uint32_t *)g_current_regs;
}

static inline_function void up_set_current_regs(uint32_t *regs)
{
  g_current_regs = regs;
}

static inline_function bool up_interrupt_context(void)
{
  return up_current_regs() != NULL;
}

/****************************************************************************
 * Name: up_irq_save / up_irq_restore / up_irq_enable
 *
 * Description:
 *   Manage global interrupt enable via %imask GIE bit (bit 15).
 *
 ****************************************************************************/

static inline_function irqstate_t up_irq_save(void)
{
  irqstate_t flags;
  __asm__ __volatile__(
    "mov32 %0, %%imask\n"
    "bitc  %%imask, 15\n"
    : "=r"(flags) : : "memory"
  );
  return flags;
}

static inline_function void up_irq_restore(irqstate_t flags)
{
  __asm__ __volatile__(
    "mov32 %%imask, %0\n"
    : : "r"(flags) : "memory"
  );
}

static inline_function void up_irq_enable(void)
{
  __asm__ __volatile__(
    "bits %%imask, 15\n"
    : : : "memory"
  );
}

#define up_getusrpc(regs) \
    (((uint32_t *)((regs) ? (regs) : up_current_regs()))[REG_PC])

#define up_getusrsp(regs) \
    ((uintptr_t)((uint32_t *)(regs))[REG_SP])

#undef EXTERN
#ifdef __cplusplus
}
#endif
#endif /* __ASSEMBLY__ */

#endif /* __ARCH_ZSP_INCLUDE_IRQ_H */
