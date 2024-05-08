/****************************************************************************
 * arch/arm/include/irq.h
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

/* This file should never be included directly but, rather, only indirectly
 * through nuttx/irq.h
 */

#ifndef __ARCH_ARM_INCLUDE_IRQ_H
#define __ARCH_ARM_INCLUDE_IRQ_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <sys/types.h>
#ifndef __ASSEMBLY__
#  include <stdbool.h>
#  if defined(__GNUC__) && !defined(__clang__)
#    include <arch/syscall.h>
#  endif
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

/* Include chip-specific IRQ definitions (including IRQ numbers) */

#include <arch/chip/irq.h>

/* Include NuttX-specific IRQ definitions */

#include <nuttx/irq.h>

/* Include ARM architecture-specific IRQ definitions (including register
 * save structure and up_irq_save()/up_irq_restore() functions)
 */

#if defined(CONFIG_ARCH_ARMV7A)
#  include <arch/armv7-a/irq.h>
#elif defined(CONFIG_ARCH_ARMV7R)
#  include <arch/armv7-r/irq.h>
#elif defined(CONFIG_ARCH_ARMV8R)
#  include <arch/armv8-r/irq.h>
#elif defined(CONFIG_ARCH_ARMV7M)
#  include <arch/armv7-m/irq.h>
#elif defined(CONFIG_ARCH_ARMV8M)
#  include <arch/armv8-m/irq.h>
#elif defined(CONFIG_ARCH_ARMV6M)
#  include <arch/armv6-m/irq.h>
#else
#  include <arch/arm/irq.h>
#endif

/****************************************************************************
 * Pre-processor Prototypes
 ****************************************************************************/

#ifndef __ASSEMBLY__

/* Macros to handle saving and restoring interrupt state. */

#define arm_savestate(regs)    (regs = up_current_regs())
#define arm_restorestate(regs) up_set_current_regs(regs)

/* Context switching */

#ifndef arm_fullcontextrestore
#  define arm_fullcontextrestore(restoreregs) \
    sys_call1(SYS_restore_context, (uintptr_t)restoreregs);
#else
extern void arm_fullcontextrestore(uint32_t *restoreregs);
#endif

#ifdef CONFIG_ARCH_CHIP_TLSR82
#  define arm_switchcontext tc32_switchcontext
#else
#  define arm_switchcontext(saveregs, restoreregs) \
    sys_call2(SYS_switch_context, (uintptr_t)saveregs, (uintptr_t)restoreregs);
#endif

#define up_switch_context(tcb, rtcb)                       \
  do {                                                     \
    nxsched_suspend_scheduler(rtcb);                       \
    if (up_current_regs())                                 \
      {                                                    \
        arm_savestate(rtcb->xcp.regs);                     \
        nxsched_resume_scheduler(tcb);                     \
        arm_restorestate(tcb->xcp.regs);                   \
      }                                                    \
    else                                                   \
      {                                                    \
        nxsched_resume_scheduler(tcb);                     \
        arm_switchcontext(&rtcb->xcp.regs, tcb->xcp.regs); \
      }                                                    \
  } while (0)

#ifdef __cplusplus
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

/****************************************************************************
 * Name: up_getusrpc
 ****************************************************************************/

#define up_getusrpc(regs) \
    (((uint32_t *)((regs) ? (regs) : up_current_regs()))[REG_PC])

#endif /* __ASSEMBLY__ */

#undef EXTERN
#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_INCLUDE_IRQ_H */
