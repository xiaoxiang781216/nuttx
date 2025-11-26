/****************************************************************************
 * arch/tricore/src/common/tricore_doirq.c
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

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <assert.h>

#include <nuttx/irq.h>
#include <nuttx/arch.h>
#include <nuttx/addrenv.h>
#include <nuttx/board.h>

#include <arch/barriers.h>
#include <arch/board/board.h>

#include <sched/sched.h>

#include "tricore_internal.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

#ifndef CONFIG_ARCH_HAVE_MULTICPU
IFX_INTERRUPT_INTERNAL(tricore_doirq, 0, 255)
#else
#define IFX_INT_WRAPPER(COREID) IFX_INTERRUPT_INTERNAL(tricore_doirq, COREID, 255)
IFX_INT_WRAPPER(CONFIG_CPU_COREID)
#endif
{
  struct tcb_s **running_task = &g_running_tasks[this_cpu()];
  struct tcb_s *tcb;

#ifdef CONFIG_SUPPRESS_INTERRUPTS
  PANIC();
#else
  Ifx_CPU_ICR icr;
  uintptr_t *regs;
  uintptr_t *cpu_lcx;

  icr.U = __mfcr(CPU_ICR);
  regs = tricore_csa2addr(__mfcr(CPU_PCXI));

  if (*running_task != NULL)
    {
      (*running_task)->xcp.regs = regs - TC_CONTEXT_REGS;
    }

  /* set registers related to csa */

  __mtcr(CPU_FCX, tricore_addr2csa(g_intstackalloc));
  __mtcr(CPU_LCX, tricore_addr2csa(g_intstacktop - 2 * TC_CONTEXT_SIZE));
  UP_ISB();

  board_autoled_on(LED_INIRQ);

  /* Nested interrupts are not supported */

  DEBUGASSERT(!up_interrupt_context());

  /* Set irq flag */

  up_set_interrupt_context(true);

  /* Deliver the IRQ */

  tricore_ack_irq(NDX_TO_IRQ(icr.B.CCPN));

  irq_dispatch(NDX_TO_IRQ(icr.B.CCPN), regs);

  tcb = this_task();

  /* Check for a context switch. */

  if (*running_task != tcb)
    {
#ifdef CONFIG_ARCH_ADDRENV
      /* Make sure that the address environment for the previously
       * running task is closed down gracefully (data caches dump,
       * MMU flushed) and set up the address environment for the new
       * thread at the head of the ready-to-run list.
       */

      addrenv_switch(tcb);
      tcb = this_task();
#endif

      /* Record the new "running" task when context switch occurred.
       * g_running_tasks[] is only used by assertion logic for reporting
       * crashes.
       */

      *running_task = tcb;
    }

  /* Reserve at least two csa for CPU_LCX */

  cpu_lcx =
    (uintptr_t *)((uint8_t *)tcb->stack_base_ptr + tcb->adj_stack_size) -
    2 * TC_CONTEXT_REGS;
  __mtcr(CPU_PCXI, tricore_addr2csa(tcb->xcp.regs + TC_CONTEXT_REGS));
  __mtcr(CPU_FCX, tricore_addr2csa(tcb->xcp.regs + 2 * TC_CONTEXT_REGS));
  __mtcr(CPU_LCX, tricore_addr2csa(cpu_lcx));
  UP_ISB();

  /* Set irq flag */

  up_set_interrupt_context(false);

  /* (*running_task)->xcp.regs is about to become invalid
   * and will be marked as NULL to avoid misusage.
   */

  (*running_task)->xcp.regs = NULL;
  board_autoled_off(LED_INIRQ);
#endif
}
