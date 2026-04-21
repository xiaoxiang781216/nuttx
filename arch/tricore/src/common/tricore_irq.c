/****************************************************************************
 * arch/tricore/src/common/tricore_irq.c
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
#include <debug.h>
#include <sched.h>

#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <nuttx/spinlock.h>

#include "tricore_internal.h"

#include "IfxSrc.h"
#include "IfxCpu.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

const IfxSrc_Tos tricore_tos[] =
{
  IfxSrc_Tos_cpu0,
  IfxSrc_Tos_cpu1,
  IfxSrc_Tos_cpu2,
  IfxSrc_Tos_cpu3,
  IfxSrc_Tos_cpu4,
  IfxSrc_Tos_cpu5,
#ifdef CONFIG_ARCH_CHIP_TC4XX
  IfxSrc_Tos_csrm,
#endif
};

typedef int srpn_to_irq_t[TRICORE_IRQ_MAX_PRIORITY + 1];
static DEFINE_PER_CPU_BSS_BMP(srpn_to_irq_t, g_srpn_to_irq);
#define g_srpn_to_irq this_cpu_var_bmp(g_srpn_to_irq)

static spinlock_t g_irqlock = SP_UNLOCKED;
static int g_irqmap_count = 1;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: tricore_irq_to_srpn
 *
 * Description:
 *   Get the SRPN for an IRQ. If not yet assigned, auto-allocate the lowest
 *   available priority.
 *
 *   Lookup searches the full range [MIN, MAX].
 *   Allocation range depends on CONFIG_ARCH_HIPRI_INTERRUPT:
 *     - Disabled: [MIN, MAX]
 *     - Enabled:  [MIN, DISABLE_PRIORITY], SRPNs above DISABLE_PRIORITY
 *       are reserved for high-priority interrupts via up_prioritize_irq().
 *
 ****************************************************************************/

int tricore_irq_to_srpn(int irq)
{
  int avail_srpn = 0;
  int i;

  /* Scheduling GPSR always gets the lowest priority */

  if (irq == TRICORE_SCHEDIRQ)
    {
      g_srpn_to_irq[TRICORE_IRQ_MIN_PRIORITY] = irq;

      return TRICORE_IRQ_MIN_PRIORITY;
    }

  /* Lookup: search the full SRPN range [MIN + 1, MAX] */

  for (i = TRICORE_IRQ_MIN_PRIORITY + 1; i <= TRICORE_IRQ_MAX_PRIORITY; i++)
    {
      if (g_srpn_to_irq[i] == irq)
        {
          return i;
        }

      /* Record first free slot within allocation range */

      if (!avail_srpn && !g_srpn_to_irq[i]
#ifdef CONFIG_ARCH_HIPRI_INTERRUPT
          && i <= TRICORE_IRQ_DISABLE_PRIORITY
#endif
         )
        {
          avail_srpn = i;
        }
    }

  if (avail_srpn)
    {
      g_srpn_to_irq[avail_srpn] = irq;
      return avail_srpn;
    }

  /* Should never get here: SRPN pool exhausted */

  DEBUGPANIC();
  return 0;
}

/****************************************************************************
 * Name: tricore_srpn_to_irq
 *
 * Description:
 *   Convert SRPN to IRQ number using the mapping table.
 *
 ****************************************************************************/

int tricore_srpn_to_irq(int srpn)
{
  DEBUGASSERT(srpn >= TRICORE_IRQ_MIN_PRIORITY &&
              srpn <= TRICORE_IRQ_MAX_PRIORITY);

  return g_srpn_to_irq[srpn];
}

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: tricore_color_intstack
 *
 * Description:
 *   Set the interrupt stack to a value so that later we can determine how
 *   much stack space was used by interrupt handling logic
 *
 ****************************************************************************/

#if defined(CONFIG_STACK_COLORATION) && CONFIG_ARCH_INTERRUPTSTACK > 15
static inline void tricore_color_intstack(void)
{
  uint32_t *ptr = (uint32_t *)up_get_intstackbase(up_cpu_index());
  ssize_t size;

  for (size = (CONFIG_ARCH_INTERRUPTSTACK & ~15);
       size > 0;
       size -= sizeof(uint32_t), ptr++)
    {
      if (!TC_CONTEXT_ALIGNED(ptr))
        {
          *ptr = STACK_COLOR;
        }
    }
}
#else
#  define tricore_color_intstack()
#endif

/****************************************************************************
 * Name: tricore_gettos
 *
 * Description:
 *   Get current cpu tos
 *
 ****************************************************************************/

static inline_function IfxSrc_Tos tricore_gettos(int cpu_idx)
{
  return tricore_tos[cpu_idx];
}

#ifdef CONFIG_ARCH_HAVE_IRQTRIGGER
/****************************************************************************
 * Name: tricore_gpsrinitialize
 *
 * Description:
 *   Perform gpsr initialization for the CPU
 *
 ****************************************************************************/

static inline_function void tricore_gpsrinitialize(void)
{
  volatile Ifx_SRC_SRCR *src;
  int cpu_idx;
  int i;

  cpu_idx = up_cpu_index();
  src = &SRC_GPSR0_SR0 + cpu_idx;

  /* Cpux gpsr init */

  for (i = 0; i < CONFIG_NCPUS; i++)
    {
#ifdef CONFIG_ARCH_CHIP_TC3XX
      IfxSrc_init(src, tricore_gettos(cpu_idx),
                  tricore_irq_to_srpn(TRICORE_SRC2IRQ(src)));
#else
      IfxSrc_init(src, tricore_gettos(cpu_idx),
                  tricore_irq_to_srpn(TRICORE_SRC2IRQ(src)),
                  IfxSrc_VmId_none);
#endif

      src += TRICORE_SRCNUM_PER_GPSR;
    }

  /* Cpucs gpsr init */

#ifdef CONFIG_HAVE_SECURITY_CORE
  src = &SRC_GPSR6_SR0 + cpu_idx;
  IfxSrc_init(src, tricore_gettos(cpu_idx),
              tricore_irq_to_srpn(TRICORE_SRC2IRQ(src)),
              IfxSrc_VmId_none);
#endif
}

#  ifndef CONFIG_UP
/****************************************************************************
 * Name: tricore_cpucallinitialize
 *
 * Description:
 *   Initialize the cpucall feature, attach cpu call handler to the event irq
 *
 ****************************************************************************/

static inline_function void tricore_cpucallinitialize(void)
{
  volatile Ifx_SRC_SRCR *src;
  int cpu = up_cpu_index();
  int irq;

  /* Use GPSR{cpu}_SR7 for cpucall instead of GPSR7_SR{cpu}.
   * TC48X/TC4ZX do not have GPSR7, but all TC4XX chips have SR7
   * in each GPSR group, so this works across all variants.
   */

  src = TRICORE_CPUCALL_SRC(cpu);
  irq = TRICORE_SRC2IRQ(src);

#ifdef CONFIG_ARCH_CHIP_TC3XX
  IfxSrc_init(src, tricore_gettos(cpu), tricore_irq_to_srpn(irq));
#else
  IfxSrc_init(src, tricore_gettos(cpu), tricore_irq_to_srpn(irq),
              IfxSrc_VmId_none);
#endif

  /* Each CPU attaches one handler to GPSR{cpu}_SR7. */

  if (irq_attach(irq, nxsched_cpu_call_handler, NULL) >= 0)
    {
      up_enable_irq(irq);
    }
  else
    {
      PANIC();
    }
}
#  endif
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_irq_enable
 *
 * Description:
 *   Enable interrupts globally.
 *
 ****************************************************************************/

void up_irq_enable(void)
{
  IfxCpu_enableInterrupts();
}

/****************************************************************************
 * Name: up_irqinitialize
 ****************************************************************************/

void up_irqinitialize(void)
{
#ifdef CONFIG_ARCH_HAVE_IRQTRIGGER
  tricore_gpsrinitialize();
#  ifndef CONFIG_UP
  tricore_cpucallinitialize();
#  endif
#endif

  tricore_region_csainit((void *)up_get_intstackbase(up_cpu_index()),
                         CONFIG_ARCH_INTERRUPTSTACK);

  tricore_color_intstack();
  up_enable_irq(TRICORE_SCHEDIRQ);
  up_irq_enable();
}

/****************************************************************************
 * Name: up_disable_irq
 *
 * Description:
 *   Disable the IRQ specified by 'irq'
 *
 ****************************************************************************/

void up_disable_irq(int irq)
{
  volatile Ifx_SRC_SRCR *src = &SRC_CPU_CPU0_SB + irq;
  Ifx_SRC_SRCR srctmp;

  IfxSrc_disable(src);

  /* Clear, keep SRPN */

  srctmp.U      = 0U;
  srctmp.B.TOS  = ~0;
  srctmp.B.SRPN = src->B.SRPN;
  src->U = srctmp.U;
}

/****************************************************************************
 * Name: up_enable_irq
 *
 * Description:
 *   Enable the IRQ specified by 'irq'
 *
 ****************************************************************************/

void up_enable_irq(int irq)
{
  volatile Ifx_SRC_SRCR *src = &SRC_CPU_CPU0_SB + irq;

#ifdef CONFIG_ARCH_CHIP_TC3XX
  IfxSrc_init(src, tricore_gettos(up_cpu_index()), tricore_irq_to_srpn(irq));
#else
  IfxSrc_init(src, tricore_gettos(up_cpu_index()), tricore_irq_to_srpn(irq),
              IfxSrc_VmId_none);
#endif
  IfxSrc_enable(src);
}

/****************************************************************************
 * Name: tricore_ack_irq
 *
 * Description:
 *   Acknowledge the IRQ
 *
 ****************************************************************************/

void tricore_ack_irq(int irq)
{
  volatile Ifx_SRC_SRCR *src = &SRC_CPU_CPU0_SB + irq;
  IfxSrc_clearRequest(src);
}

#ifdef CONFIG_ARCH_HAVE_IRQTRIGGER
/****************************************************************************
 * Name: up_trigger_irq
 *
 * Description:
 *   Trigger an IRQ by software. May not be supported by all architectures.
 *
 ****************************************************************************/

void up_trigger_irq(int irq, cpu_set_t cpuset)
{
  volatile Ifx_INT_SRB *srb = &INT_SRB0 + up_cpu_index();
  srb->U = cpuset;
}
#endif

/****************************************************************************
 * Name: up_prioritize_irq
 *
 * Description:
 *   Set the priority (SRPN) of an IRQ.This is the only way to assign an SRPN
 *   above TRICORE_IRQ_DISABLE_PRIORITY When CONFIG_ARCH_HIPRI_INTERRUPT is
 *   enabled.
 *
 ****************************************************************************/

#ifdef CONFIG_ARCH_IRQPRIO
int up_prioritize_irq(int irq, int priority)
{
  int i;

  DEBUGASSERT(priority > TRICORE_IRQ_MIN_PRIORITY &&
              priority <= TRICORE_IRQ_MAX_PRIORITY &&
              irq != TRICORE_SCHEDIRQ);

  /* IRQ already mapped to the target SRPN, nothing to do */

  if (g_srpn_to_irq[priority] == irq)
    {
      return OK;
    }

  /* Check if target SRPN is already occupied by another IRQ */

  DEBUGASSERT(!g_srpn_to_irq[priority]);

  /* Clear old mapping if this IRQ had a different SRPN */

  for (i = TRICORE_IRQ_MIN_PRIORITY + 1; i <= TRICORE_IRQ_MAX_PRIORITY; i++)
    {
      if (g_srpn_to_irq[i] == irq)
        {
          g_srpn_to_irq[i] = 0;
          break;
        }
    }

  g_srpn_to_irq[priority] = irq;

  return OK;
}
#endif

/****************************************************************************
 * Name: up_irq_to_ndx
 *
 * Description:
 *   Irq to ndx
 *
 ****************************************************************************/

int up_irq_to_ndx(int irq)
{
  volatile Ifx_SRC_SRCR *src = &SRC_CPU_CPU0_SB + irq;
  Ifx_SRC_SRCR srctmp;
  irqstate_t flags;
  int ndx;

  ndx = src->B.SRPN;
  if (ndx != 0)
    {
      return ndx;
    }

  flags = spin_lock_irqsave(&g_irqlock);
  ndx = src->B.SRPN;
  if (ndx == 0)
    {
      ndx = g_irqmap_count++;
      g_irqrevmap[ndx] = irq;
      srctmp.U = src->U;
      srctmp.B.SRPN = ndx;
      src->U = srctmp.U;
    }

  spin_unlock_irqrestore(&g_irqlock, flags);

  return ndx;
}

/****************************************************************************
 * Name: up_affinity_irq
 *
 * Description:
 *   Set an IRQ affinity by software.
 *
 ****************************************************************************/

#ifndef CONFIG_UP
void up_affinity_irq(int irq, cpu_set_t cpuset)
{
  volatile Ifx_SRC_SRCR *src = &SRC_CPU_CPU0_SB + irq;
  bool enabled = src->B.SRE;
  int cpu;

  for (cpu = 0; cpu < CONFIG_NCPUS; cpu++)
    {
      if (CPU_ISSET(cpu, &cpuset))
        {
          IfxSrc_deinit(src);
#ifdef CONFIG_ARCH_CHIP_TC3XX
          IfxSrc_init(src, tricore_gettos(cpu), tricore_irq_to_srpn(irq));
#else
          IfxSrc_init(src, tricore_gettos(cpu), tricore_irq_to_srpn(irq),
                      IfxSrc_VmId_none);
#endif
          if (enabled)
            {
              IfxSrc_enable(src);
            }

          return;
        }
    }
}
#endif
