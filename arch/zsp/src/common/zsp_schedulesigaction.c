/****************************************************************************
 * arch/zsp/src/common/zsp_schedulesigaction.c
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
#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <arch/irq.h>
#include <string.h>

#include "sched/sched.h"
#include "signal/signal.h"
#include "zsp_internal.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: zsp_sigdeliver
 *
 * Description:
 *   Signal delivery trampoline. Delivers pending signals then restores
 *   the original task context saved before signal dispatch.
 *
 ****************************************************************************/

static void zsp_sigdeliver(void)
{
  struct tcb_s *rtcb = this_task();

  /* Enable interrupts for signal handler execution */

  up_irq_enable();

  /* Deliver all pending signals */

  nxsig_deliver(rtcb);

  /* Restore original context from saved_regs pointer.
   * This does not return.
   */

  up_irq_save();

  zsp_fullcontextrestore(rtcb->xcp.saved_regs);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_schedule_sigaction
 *
 * Description:
 *   Schedule signal delivery. Saves the current register context and
 *   redirects the task to the signal trampoline (zsp_sigdeliver) which
 *   will deliver pending signals then restore the original context.
 *
 *   This follows the ARM Cortex-A/R pattern (arm_schedulesigaction.c):
 *   unconditionally allocate a new frame below the current one, copy the
 *   original context, and redirect PC to the trampoline. This works for
 *   all cases (interrupt context, running task, blocked task) uniformly.
 *
 *   Called from critical section.
 *
 ****************************************************************************/

void up_schedule_sigaction(struct tcb_s *tcb)
{
  /* Save pointer to current frame (original context) */

  tcb->xcp.saved_regs = tcb->xcp.regs;

  /* Allocate a new frame below the current one on the stack.
   * Copy the original context into it.
   */

  tcb->xcp.regs = (uint32_t *)((uintptr_t)tcb->xcp.regs -
                                XCPTCONTEXT_SIZE);
  memcpy(tcb->xcp.regs, tcb->xcp.saved_regs, XCPTCONTEXT_SIZE);

  /* Set SP in the new frame to point above the signal frame.
   * The signal trampoline's function calls grow downward from here,
   * which may overwrite the signal frame - acceptable since sigdeliver
   * restores from saved_regs (the original frame) not the signal frame.
   */

  tcb->xcp.regs[REG_SP]    = (uint32_t)tcb->xcp.regs + XCPTCONTEXT_SIZE;

  /* Redirect to signal trampoline with interrupts disabled */

  tcb->xcp.regs[REG_PC]    = (uint32_t)zsp_sigdeliver;
  tcb->xcp.regs[REG_IMASK] &= ~ZSP_IMASK_GIE;
}
