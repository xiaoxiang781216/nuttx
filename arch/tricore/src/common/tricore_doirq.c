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
 * Pre-processor Prototypes
 ****************************************************************************/

/* Define a csa switch wrapper function for isr. */

#define TRICORE_INTERRUPT_DEFINE(isr)                                    \
  static inline_function void isr##_internal(void);                      \
  void isr(void)                                                         \
  {                                                                      \
    uintptr_t *regs = tricore_csa2addr(__mfcr(CPU_PCXI));                \
    uintptr_t saved_pcx;                                                 \
    uintptr_t saved_fcx;                                                 \
    uintptr_t saved_lcx;                                                 \
    bool switch_csa;                                                     \
                                                                         \
    switch_csa = !(tricore_csa2addr(regs[REG_LPCXI])[REG_PSW] & PSW_IS); \
    if (switch_csa)                                                      \
      {                                                                  \
        uintptr_t istackbase = up_get_intstackbase(up_cpu_index());      \
                                                                         \
        saved_pcx = __mfcr(CPU_PCXI);                                    \
        saved_fcx = __mfcr(CPU_FCX);                                     \
        saved_lcx = __mfcr(CPU_LCX);                                     \
        __mtcr(CPU_FCX, tricore_addr2csa(istackbase));                   \
        __mtcr(CPU_LCX, tricore_addr2csa(istackbase +                    \
                                         CONFIG_ARCH_INTERRUPTSTACK -    \
                                         XCPTCONTEXT_SIZE));             \
        UP_ISB();                                                        \
      }                                                                  \
                                                                         \
    isr##_internal();                                                    \
                                                                         \
    if (switch_csa)                                                      \
      {                                                                  \
        if (g_switchcsa.pcx)                                             \
          {                                                              \
            saved_pcx = g_switchcsa.pcx;                                 \
            saved_fcx = g_switchcsa.pcx + 1;                             \
            saved_lcx = g_switchcsa.lcx;                                 \
            g_switchcsa.pcx = 0;                                         \
          }                                                              \
        __mtcr(CPU_PCXI, saved_pcx);                                     \
        __mtcr(CPU_FCX, saved_fcx);                                      \
        __mtcr(CPU_LCX, saved_lcx);                                      \
        UP_ISB();                                                        \
      }                                                                  \
    __jumpBackToLink();                                                  \
  }                                                                      \
  static inline_function void isr##_internal(void)

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct csa_info
{
  uintptr_t pcx;
  uintptr_t lcx;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static DEFINE_PER_CPU_BSS_BMP(struct csa_info, g_switchcsa);
#define g_switchcsa this_cpu_var_bmp(g_switchcsa)

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: tricore_irq_direct
 *
 * Description:
 *   Fast interrupt entry point (analogous to ARM-M exception_direct).
 *   Only dispatches the IRQ handler, no context switch.
 *   If a context switch is needed, triggers tricore_taskswitch_irq (GPSR)
 *   which will execute after all higher-priority interrupts have returned.
 *
 ****************************************************************************/

TRICORE_INTERRUPT_DEFINE(tricore_irq_direct)
{
  Ifx_CPU_ICR icr;
  int irq;

#ifdef CONFIG_ARCH_IRQPRIO
  up_irq_enable();
#endif

  icr.U = __mfcr(CPU_ICR);
  irq = tricore_srpn_to_irq(icr.B.CCPN);
  tricore_ack_irq(irq);
  irq_dispatch(irq, NULL);

  if (g_running_task != this_task())
    {
      cpu_set_t cpuset;
      CPU_ZERO(&cpuset);
      CPU_SET(up_this_cpu(), &cpuset);
      up_trigger_irq(TRICORE_SCHEDIRQ, cpuset);
    }
}

/****************************************************************************
 * Name: tricore_irq_sched
 *
 * Description:
 *   GPSR interrupt entry point for context switch (analogous to arm_doirq).
 *   This is triggered by tricore_taskswitch_irq when tricore_irq_direct
 *   detects that a context switch is needed. It also handles the
 *   systimer oneshot callback.
 *
 *   Because the GPSR SRPN is the lowest priority, this only executes
 *   after all normal interrupts have completed, guaranteeing safe
 *   context switch without race conditions.
 *
 ****************************************************************************/

TRICORE_INTERRUPT_DEFINE(tricore_irq_sched)
{
  struct tcb_s **running_task = &g_running_task;
  struct tcb_s *tcb;
  uintptr_t *regs;

#ifdef CONFIG_ARCH_IRQPRIO
  up_irq_enable();
#endif

  regs = tricore_csa2addr(__mfcr(CPU_PCXI));
  if (*running_task != NULL)
    {
      (*running_task)->xcp.regs = regs - TC_CONTEXT_REGS;
    }

  board_autoled_on(LED_INIRQ);

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

      /* Update PPRS register */

      tricore_store_pprs(*running_task);
      tricore_restore_pprs(tcb);

      /* Update the TLS pointer */

      tricore_set_tls_info(tcb->stack_alloc_ptr);

      /* Record the new "running" task when context switch occurred.
       * g_running_tasks[] is only used by assertion logic for reporting
       * crashes.
       */

      *running_task = tcb;

      g_switchcsa.pcx = tricore_addr2csa(tcb->xcp.regs + TC_CONTEXT_REGS);
      g_switchcsa.lcx = tricore_addr2csa((uintptr_t)tcb->stack_base_ptr +
                                         tcb->adj_stack_size -
                                         XCPTCONTEXT_SIZE);
    }

  (*running_task)->xcp.regs = NULL;
  tricore_change_pprs(*running_task, UINT32_MAX);

  board_autoled_off(LED_INIRQ);
}
