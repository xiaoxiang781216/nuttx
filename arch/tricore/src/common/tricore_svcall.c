/****************************************************************************
 * arch/tricore/src/common/tricore_svcall.c
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
#include <string.h>
#include <assert.h>
#include <debug.h>
#include <syscall.h>

#include <arch/barriers.h>
#include <arch/irq.h>
#include <sched/sched.h>
#include <nuttx/sched.h>

#include "tricore_internal.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: tricore_svcall
 *
 * Description:
 *   This is SVCall exception handler that performs context switching
 *
 ****************************************************************************/

void tricore_svcall(volatile void *trap)
{
  struct tcb_s *tcb = this_task();
  struct tcb_s **running_task = &g_running_tasks[this_cpu()];
  uintptr_t *regs;
  uint32_t cmd;

#ifdef CONFIG_LIB_SYSCALL
  uint32_t  arg1;
  uint32_t  arg2;
  uint32_t  arg3;
  uint32_t  arg4;
  uint32_t  arg5;
  uint32_t  arg6;
#endif

  regs = (uintptr_t *)__mfcr(CPU_PCXI);

  /* DSYNC instruction should be executed immediately prior to the MTCR */

  UP_DSB();

  regs = tricore_csa2addr((uintptr_t)regs);

  /* Set irq flag */

  up_set_interrupt_context(true);

  cmd = regs[REG_D8];

  if (cmd != SYS_restore_context)
    {
      (*running_task)->xcp.regs = tricore_csa2addr(regs[REG_UPCXI]);
    }
  else
    {
      tricore_reclaim_csa(regs[REG_UPCXI]);
    }

  /* Handle the SVCall according to the command in R0 */

  switch (cmd)
    {
      case SYS_switch_context:
        nxsched_switch_context(*running_task, tcb);

      case SYS_restore_context:
        *running_task = tcb;
        regs[REG_UPCXI] = tricore_addr2csa(tcb->xcp.regs);
        UP_ISB();
        break;
      case SYS_assert_handler:
        {
          _assert((const char *)regs[REG_D9], (int)regs[REG_D10],
                  (const char *)regs[REG_D11], (void *)running_regs(),
                  false);
        }
        break;
      default:
        {
#ifdef CONFIG_LIB_SYSCALL
          int ret;
          int nbr = cmd - CONFIG_SYS_RESERVED;
          uintptr_t * low_csa = tricore_csa2addr(regs[REG_UPCXI]);
          uintptr_t * up_csa = tricore_csa2addr(low_csa[REG_UPCXI]);
          struct tcb_s *rtcb = nxsched_self();
          syscall_stub_t stub;

          DEBUGASSERT(nbr < SYS_nsyscalls);
          DEBUGASSERT(rtcb->xcp.nsyscalls < CONFIG_SYS_NNEST);

          stub = (syscall_stub_t)g_stublookup[nbr];

          arg1 = regs[REG_D9];
          arg2 = regs[REG_D10];
          arg3 = regs[REG_D11];
          arg4 = regs[REG_D12];
          arg5 = regs[REG_D13];
          arg6 = regs[REG_D14];

          /* Setup nested syscall */

          rtcb->xcp.nsyscalls += 1;

          /* Call syscall function */

          ret = stub(nbr, arg1, arg2, arg3, arg4, arg5, arg6);

          /* Setup return from nested syscall */

          rtcb->xcp.nsyscalls -= 1;

          /* Find the first UP CSA block, then assign the return value to the
           * D8 register.
           */

          up_csa[REG_D8] = ret;
#else
        svcerr("ERROR: Bad SYS call: %d\n", (int)regs[REG_D0]);
#endif
        }
        break;
    }

  /* Set irq flag */

  up_set_interrupt_context(false);
}
