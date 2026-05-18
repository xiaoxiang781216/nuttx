/****************************************************************************
 * arch/arm/src/arm_m/arm_doirq.c
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
#include <nuttx/addrenv.h>
#include <nuttx/arch.h>
#include <nuttx/board.h>
#include <arch/board/board.h>
#include <sched/sched.h>

#include "arm_internal.h"
#include "exc_return.h"
#include "nvic.h"
#include "psr.h"

/****************************************************************************
 * Trustzone latency measurement via shared memory
 *
 * arm_doirq need to track Trustzone execution time:
 *
 * Secure arm_doirq:
 *   Entry: if incoming LR shows "Secure interrupted Non-secure"
 *          (bit0=1,bit6=0) → Secure starts, record secure_start
 *   Exit:  if outgoing LR shows "returning to Non-secure" (bit0=1,bit6=0)
 *          → Secure ends, record accumulate
 *
 * Non-secure arm_doirq:
 *   Entry: if incoming LR shows "Non-secure interrupted Secure"
 *          (bit0=0,bit6=1) → Secure was running and got interrupted,
 *   Exit:  if outgoing LR shows "returning to Secure" (bit0=0,bit6=1)
 *          → Secure resumes, record secure_start
 *
 * All data stored in shared SRAM (via linker symbol __trustzone_shared).
 ****************************************************************************/

#ifdef CONFIG_ARCH_TRUSTZONE_MONITOR
/* Helper: record Secure start (Secure begins/resumes execution) */

static inline_function void arm_enter_secure(void)
{
  if (g_trustzone_monitor.secure_start == 0)
    {
      g_trustzone_monitor.secure_start = perf_gettime();
    }
}

static inline_function void arm_leave_secure(void)
{
  if (g_trustzone_monitor.secure_start != 0)
    {
      clock_t start = g_trustzone_monitor.secure_start;
      clock_t now = perf_gettime();
      clock_t duration = now - start;

      g_trustzone_monitor.secure_start = 0;
      g_trustzone_monitor.secure_total += duration;
      g_trustzone_monitor.secure_count++;
      if (duration > g_trustzone_monitor.secure_max)
        {
          g_trustzone_monitor.secure_max = duration;
        }
    }
}

/****************************************************************************
 * Name: arm_enter_irq
 *
 * Description:
 *   Check EXC_RETURN at IRQ entry to track Secure execution time.
 *   - Secure side (bit0=1, bit6=0): Secure interrupted Non-secure →
 *                 Secure starts
 *   - Non-secure side (bit0=0, bit6=1): Non-secure interrupted Secure →
 *                                       Secure ends
 *
 ****************************************************************************/

static inline_function void arm_enter_irq(uint32_t exc_lr)
{
  bool exc_secure = (exc_lr & EXC_RETURN_EXC_SECURE) != 0;
  bool sec_stack  = (exc_lr & EXC_RETURN_SECURE_STACK) != 0;

  if (exc_secure && !sec_stack)
    {
      /* Secure exception interrupted Non-secure: Secure starts */

      arm_enter_secure();
    }
  else if (!exc_secure && sec_stack)
    {
      /* Non-secure exception interrupted Secure: Secure ends */

      arm_leave_secure();
    }
}

/****************************************************************************
 * Name: arm_leave_irq
 *
 * Description:
 *   Check EXC_RETURN at IRQ exit to track Secure execution time.
 *   - Secure side (bit0=1, bit6=0): returning to Non-secure → Secure ends
 *   - Non-secure side (bit0=0, bit6=1): returning to Secure →
 *                                       Secure resumes
 *
 ****************************************************************************/

static inline_function void arm_leave_irq(uint32_t exc_lr)
{
  bool exc_secure = (exc_lr & EXC_RETURN_EXC_SECURE) != 0;
  bool sec_stack = (exc_lr & EXC_RETURN_SECURE_STACK) != 0;

  if (exc_secure && !sec_stack)
    {
      /* Returning from Secure to Non-secure: Secure ends */

      arm_leave_secure();
    }
  else if (!exc_secure && sec_stack)
    {
      /* Returning from Non-secure to Secure: Secure resumes */

      arm_enter_secure();
    }
}

#else
#  define arm_enter_irq(exc_lr)
#  define arm_leave_irq(exc_lr)
#endif /* CONFIG_ARCH_TRUSTZONE_MONITOR */

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void exception_direct(void)
{
  int irq = getipsr();
  uint32_t exc_lr;

  __asm__ __volatile__("mov %0, lr" : "=r"(exc_lr));
  up_addrenv_enter_kernel();

  arm_enter_irq(exc_lr);

#ifdef CONFIG_ARCH_FPU
  __asm__ __volatile__
    (
      "mov r0, %0\n"
      "vmsr fpscr, r0\n"
      :
      : "i" (ARM_FPSCR_LTPSIZE_NONE)
      : "r0"
    );
#endif

  arm_ack_irq(irq);
  irq_dispatch(irq, NULL);

  if (g_running_task != this_task())
    {
      up_trigger_irq(NVIC_IRQ_PENDSV, 0);
    }

  if ((getreg32(NVIC_INTCTRL) & NVIC_INTCTRL_PENDSVSET) == 0)
    {
      /* No context switch and no PendSV pending, returning to the
       * same world we interrupted. Reverse the entry timing action.
       */

      arm_leave_irq(exc_lr);
    }

  up_addrenv_leave_kernel();
  UNUSED(exc_lr);
}

uint32_t *arm_doirq(int irq, uint32_t *regs)
{
  struct tcb_s **running_task;
  struct tcb_s *tcb;

  up_addrenv_enter_kernel();
  arm_enter_irq(regs[REG_EXC_RETURN]);
  running_task = &g_running_task;
  tcb = *running_task;

  /* This judgment proves that (*running_task)->xcp.regs
   * is invalid, and we can safely overwrite it.
   */

  if (tcb != NULL)
    {
      tcb->xcp.regs = regs;
    }

  board_autoled_on(LED_INIRQ);
#ifdef CONFIG_SUPPRESS_INTERRUPTS
  PANIC();
#else

  /* Acknowledge the interrupt */

  arm_ack_irq(irq);

  if (irq == NVIC_IRQ_PENDSV)
    {
#ifdef CONFIG_ARCH_HIPRI_INTERRUPT
      /* Dispatch the PendSV interrupt */

      irq_dispatch(irq, regs);
#endif

      up_irq_save();

#ifndef CONFIG_DISABLE_SIGNALS
      if (tcb && tcb->sigdeliver && !tcb->xcp.saved_regs)
        {
          /* Pendsv able to access running tcb with no critical section */

          up_schedule_sigaction(tcb);
        }
#endif
    }
  else
    {
      irq_dispatch(irq, regs);
    }

  tcb = this_task();

  if (tcb != *running_task)
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

  regs = tcb->xcp.regs;
#endif

  board_autoled_off(LED_INIRQ);

#ifdef CONFIG_ARCH_TRUSTZONE_HYBRID
  if (((1 << this_cpu()) & CONFIG_ARCH_TRUSTZONE_CPU_BITMASK) == 0)
    {
      regs[REG_EXC_RETURN] &=
        ~(EXC_RETURN_EXC_SECURE | EXC_RETURN_SECURE_STACK);
    }
  else
    {
      regs[REG_EXC_RETURN] |=
        (EXC_RETURN_EXC_SECURE | EXC_RETURN_SECURE_STACK);
    }
#endif

  /* (*running_task)->xcp.regs is about to become invalid
   * and will be marked as NULL to avoid misusage.
   */

  (*running_task)->xcp.regs = NULL;
  arm_leave_irq(regs[REG_EXC_RETURN]);
  up_addrenv_leave_kernel();
  return regs;
}
