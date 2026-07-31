/****************************************************************************
 * arch/zsp/src/common/zsp_initialstate.c
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
#include <nuttx/irq.h>
#include <string.h>
#include <nuttx/arch.h>
#include <arch/irq.h>

#include "zsp_internal.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Initial %imask value for new tasks: GIE enabled, Timer0 unmasked.
 * fullcontextrestore uses br (not reti), so %imask is written directly
 * and GIE takes effect immediately. PGIE is not used in this path.
 */

#define ZSP_IMASK_INIT  (ZSP_IMASK_GIE | ZSP_IMASK_MT0)

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void up_initial_state(struct tcb_s *tcb)
{
  struct xcptcontext *xcp = &tcb->xcp;

  memset(xcp, 0, sizeof(struct xcptcontext));

  if (tcb->pid == IDLE_PROCESS_ID)
    {
      tcb->stack_alloc_ptr = (void *)g_idle_basestack;
      tcb->stack_base_ptr  = tcb->stack_alloc_ptr;
      tcb->adj_stack_size  = g_idle_topstack - g_idle_basestack;
      return;
    }

  if (tcb->stack_base_ptr)
    {
      uintptr_t top = (uintptr_t)tcb->stack_base_ptr + tcb->adj_stack_size;
      top = STACK_ALIGN_DOWN(top);

      xcp->regs = (uint32_t *)(top - XCPTCONTEXT_SIZE);
      memset(xcp->regs, 0, XCPTCONTEXT_SIZE);

      /* SP points to frame; task grows stack downward */

      xcp->regs[REG_SP]    = (uint32_t)xcp->regs;
      xcp->regs[REG_PC]    = (uint32_t)tcb->start;
      xcp->regs[REG_IMASK] = ZSP_IMASK_INIT;
    }
}
