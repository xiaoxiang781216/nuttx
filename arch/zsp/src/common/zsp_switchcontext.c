/****************************************************************************
 * arch/zsp/src/common/zsp_switchcontext.c
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
#include <arch/irq.h>

#include "sched/sched.h"
#include "zsp_internal.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_switch_context
 *
 * Description:
 *   Switch from one task to another. If we are in interrupt context then
 *   simply update the register pointer. Otherwise perform a cooperative
 *   context switch using zsp_switchcontext assembly, or a pure restore
 *   if rtcb is NULL (called from up_exit where old task is destroyed).
 *
 ****************************************************************************/

void up_switch_context(struct tcb_s *tcb, struct tcb_s *rtcb)
{
  if (up_current_regs())
    {
      /* In interrupt context: just swap the frame pointer.
       * rtcb should not be NULL in interrupt context.
       */

      rtcb->xcp.regs = up_current_regs();
      up_set_current_regs(tcb->xcp.regs);
    }
  else if (rtcb == NULL)
    {
      /* No old task to save (called from up_exit).
       * Just restore the new task context directly.
       */

      g_running_task = tcb;
      zsp_fullcontextrestore(tcb->xcp.regs);
    }
  else
    {
      /* Cooperative context switch: save old, restore new */

      zsp_switchcontext(&rtcb->xcp.regs, tcb->xcp.regs);
    }
}
