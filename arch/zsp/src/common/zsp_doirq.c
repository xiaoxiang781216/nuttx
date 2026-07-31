/****************************************************************************
 * arch/zsp/src/common/zsp_doirq.c
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
#include <assert.h>

#include "sched/sched.h"
#include "zsp_internal.h"

/****************************************************************************
 * Public Data
 ****************************************************************************/

#undef g_current_regs
DEFINE_PER_CPU_BSS(uint32_t *volatile, g_current_regs);

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: zsp_doirq
 *
 * Description:
 *   C-level interrupt dispatch. Called from zsp_irq_entry.S with the
 *   interrupt number and a pointer to the register save area on the stack.
 *
 *   Returns the register save area pointer to restore (may differ from
 *   input if a context switch occurred).
 *
 ****************************************************************************/

uint32_t *zsp_doirq(int irq, uint32_t *regs)
{
  struct tcb_s *tcb = this_task();

  /* Nested interrupts are not supported */

  DEBUGASSERT(!up_interrupt_context());

  /* Save current task state */

  tcb->xcp.regs = regs;

  /* Mark that we are in interrupt context */

  up_set_current_regs(regs);

  /* Dispatch the interrupt */

  irq_dispatch(irq, regs);

  /* If a context switch occurred, tcb at head of ready list changed */

  tcb = this_task();

  if (regs != tcb->xcp.regs)
    {
      /* Context switch: update g_running_task */

      g_running_task = tcb;
      regs = tcb->xcp.regs;
    }

  /* Mark that we are leaving interrupt context.
   * tcb->xcp.regs is about to be restored to real registers and may
   * be overwritten, so invalidate the pointer.
   */

  tcb->xcp.regs = NULL;
  up_set_current_regs(NULL);
  return regs;
}
