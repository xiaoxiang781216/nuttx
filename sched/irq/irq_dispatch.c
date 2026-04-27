/****************************************************************************
 * sched/irq/irq_dispatch.c
 *
 * SPDX-License-Identifier: Apache-2.0
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

#include <debug.h>
#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <nuttx/irq_monitor.h>
#include <nuttx/mm/mm.h>
#include <nuttx/random.h>
#include <nuttx/sched_note.h>
#include <nuttx/tls.h>

#if defined(CONFIG_SCHED_IRQMONITOR) && defined(CONFIG_PM)
#  include <nuttx/power/pm.h>
#endif

#include "irq/irq.h"
#include "clock/clock.h"
#include "sched/sched.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifdef CONFIG_ARCH_MINIMAL_VECTORTABLE
#  define NUSER_IRQS CONFIG_ARCH_NUSER_INTERRUPTS
#else
#  define NUSER_IRQS NR_IRQS
#endif

/* CALL_VECTOR - Call the interrupt service routine attached to this
 * interrupt request
 */

#ifdef CONFIG_SCHED_IRQMONITOR
#  ifdef CONFIG_ARCH_IRQPRIO
#    define INC_NESTLEVEL() atomic_add(&g_irq_level, 1)
#    define DEC_NESTLEVEL(ndx) \
       do \
         { \
           if (atomic_sub(&g_irq_level, 1) > 1) \
             { \
               atomic_add(&g_irqvector[ndx].nests, 1); \
             } \
         } \
       while (0)
#  else
#    define INC_NESTLEVEL()
#    define DEC_NESTLEVEL(ndx)
#  endif

#  ifdef CONFIG_PM
#    define WAKEUP_COUNT(ndx) \
       if (g_wakeup_state != PM_NORMAL) \
         { \
           atomic_add(&g_irqvector[ndx].nwakeup[g_wakeup_state], 1); \
           g_wakeup_state = PM_NORMAL; \
         }
#  else
#    define WAKEUP_COUNT(ndx)
#  endif

/* CALL_VECTOR always collects the cheap statistics (interrupt count, nest
 * level and, when PM is enabled, the wakeup count) for any valid IRQ.  The
 * expensive timing statistics are only collected for IRQs that opted in via
 * the per-IRQ monitor flag.
 */

#  define CALL_VECTOR(ndx, vector, irq, context, arg) \
     do \
       { \
         if ((unsigned int)(ndx) < NUSER_IRQS) \
           { \
             INC_NESTLEVEL(); \
             WAKEUP_COUNT(ndx); \
             atomic_add(&g_irqvector[ndx].count, 1); \
             if (g_irqvector[ndx].monitor) \
               { \
                 clock_t start; \
                 clock_t elapsed; \
                 start = perf_gettime(); \
                 vector(irq, context, arg); \
                 elapsed = perf_gettime() - start; \
                 if (elapsed > g_irqvector[ndx].time) \
                   { \
                     g_irqvector[ndx].time = elapsed; \
                   } \
                 if (CONFIG_SCHED_CRITMONITOR_MAXTIME_IRQ > 0 && \
                     elapsed > CONFIG_SCHED_CRITMONITOR_MAXTIME_IRQ) \
                   { \
                     CRITMONITOR_PANIC("IRQ %d(%p), execute time too long " \
                                       "%ju\n", irq, vector, \
                                       (uintmax_t)elapsed); \
                   } \
               } \
             else \
               { \
                 vector(irq, context, arg); \
               } \
             DEC_NESTLEVEL(ndx); \
           } \
         else \
           { \
             vector(irq, context, arg); \
           } \
       } \
     while (0)
#else
#  define CALL_VECTOR(ndx, vector, irq, context, arg) \
     vector(irq, context, arg)
#endif /* CONFIG_SCHED_IRQMONITOR */

/****************************************************************************
 * Privtae Data
 ****************************************************************************/

#if defined(CONFIG_SCHED_IRQMONITOR) && defined(CONFIG_ARCH_IRQPRIO)
static DEFINE_PER_CPU_BSS(atomic_t, g_irq_level);
#  define g_irq_level this_cpu_var(g_irq_level)
#endif

#if defined(CONFIG_SCHED_IRQMONITOR) && defined(CONFIG_PM)
static DEFINE_PER_CPU_BSS(enum pm_state_e, g_wakeup_state);
#  define g_wakeup_state this_cpu_var(g_wakeup_state)

static void irq_wakeup_notify(FAR struct pm_callback_s *cb, int domain,
                              enum pm_state_e pmstate)
{
  /* Arm the wakeup tag when entering a low-power state.  The first IRQ
   * dispatched after this point is the one that woke the system, so it
   * consumes the tag in WAKEUP_COUNT().  PM_RESTORE and PM_NORMAL leave
   * the tag untouched.
   */

  if (pmstate >= PM_IDLE && pmstate < PM_COUNT)
    {
      g_wakeup_state = pmstate;
    }
}

static struct pm_callback_s g_wakeup_cb =
{
  .notify = irq_wakeup_notify,
};
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

#if defined(CONFIG_SCHED_IRQMONITOR) && defined(CONFIG_PM)
void irq_wakeup_initialize(void)
{
  int i;

  for (i = 0; i < CONFIG_PM_NDOMAINS; i++)
    {
      pm_domain_register(i, &g_wakeup_cb);
    }
}
#endif

/****************************************************************************
 * Name: irq_dispatch
 *
 * Description:
 *   This function must be called from the architecture-specific logic in
 *   order to dispatch an interrupt to the appropriate, registered handling
 *   logic.
 *
 ****************************************************************************/

__jumpfunc__ void irq_dispatch(int irq, FAR void *context)
{
#ifdef CONFIG_DEBUG_MM
  FAR struct tls_info_s *info = tls_get_info();
  int heap_check = info && (info->tl_flags & TLS_FLAG_HEAP_CHECK);
#endif
  xcpt_t vector = irq_unexpected_isr;
  FAR void *arg = NULL;
  int ndx = irq;

#if NR_IRQS > 0
  if (ndx >= 0)
    {
#ifdef CONFIG_ARCH_MINIMAL_VECTORTABLE
      ndx = g_irqmap[irq];
      if (ndx < CONFIG_ARCH_NUSER_INTERRUPTS)
        {
          if (g_irqvector[ndx].handler)
            {
              vector = g_irqvector[ndx].handler;
              arg    = g_irqvector[ndx].arg;
            }
        }
#else
      if (g_irqvector[ndx].handler)
        {
          vector = g_irqvector[ndx].handler;
          arg    = g_irqvector[ndx].arg;
        }
#endif
    }
#endif

#ifdef CONFIG_CRYPTO_RANDOM_POOL_COLLECT_IRQ_RANDOMNESS
  /* Add interrupt timing randomness to entropy pool */

  add_irq_randomness(irq);
#endif

#ifdef CONFIG_SCHED_INSTRUMENTATION_IRQHANDLER
  /* Notify that we are entering into the interrupt handler */

  sched_note_irq_enter(irq, vector);
#endif

  /* Then dispatch to the interrupt handler */

  CALL_VECTOR(ndx, vector, irq, context, arg);
  UNUSED(ndx);

#ifdef CONFIG_SCHED_INSTRUMENTATION_IRQHANDLER
  /* Notify that we are leaving from the interrupt handler */

  sched_note_irq_leave(irq, vector);
#endif

#ifdef CONFIG_DEBUG_MM
  if (heap_check)
    {
      kmm_checkcorruption();
    }
  else
    {
      FAR struct tls_info_s *info_now = tls_get_info();
      if (info_now && info_now->tl_flags & TLS_FLAG_HEAP_CHECK)
        {
          kmm_checkcorruption();
        }
    }
#endif

#if defined(CONFIG_STACKCHECK_MARGIN) && CONFIG_STACKCHECK_MARGIN > 0 && \
    defined(CONFIG_ARCH_INTERRUPTSTACK) && CONFIG_ARCH_INTERRUPTSTACK > 0
    DEBUGASSERT(up_check_intstack(this_cpu(),
                                  CONFIG_STACKCHECK_MARGIN) == 0);
#endif
}

/****************************************************************************
 * Name: irq_monitor_enable / irq_monitor_disable
 *
 * Description:
 *   Dynamically enable/disable IRQ timing statistics for a specific IRQ.
 *
 ****************************************************************************/

#ifdef CONFIG_SCHED_IRQMONITOR
void irq_monitor_enable(int irq)
{
  int ndx = IRQ_TO_NDX(irq);
  if (ndx >= 0 && ndx < NUSER_IRQS)
    {
      g_irqvector[ndx].monitor = true;
    }
}

void irq_monitor_disable(int irq)
{
  int ndx = IRQ_TO_NDX(irq);
  if (ndx >= 0 && ndx < NUSER_IRQS)
    {
      g_irqvector[ndx].monitor = false;
    }
}

/****************************************************************************
 * Name: irq_monitor_enable_all / irq_monitor_disable_all
 *
 * Description:
 *   Global enable/disable of IRQ statistics collection for all IRQs.
 *   When disabled, no IRQ timing statistics, frequency information, or
 *   counters are updated for any IRQ.
 *
 * Input Parameters:
 *   None
 *
 ****************************************************************************/

void irq_monitor_enable_all(void)
{
  int i;

  for (i = 0; i < NUSER_IRQS; i++)
    {
      g_irqvector[i].monitor = true;
    }
}

void irq_monitor_disable_all(void)
{
  int i;

  for (i = 0; i < NUSER_IRQS; i++)
    {
      g_irqvector[i].monitor = false;
    }
}

/****************************************************************************
 * Name: irq_monitor_get_freq
 *
 * Description:
 *   Query the interrupt frequency (rate) for a specific IRQ.
 *   Optionally reset the counters to restart the measurement window.
 *
 * Input Parameters:
 *   irq      - The IRQ number to query
 *   reset    - If true, reset counters after reading
 *   freq     - Pointer to store the integer part of frequency
 *              (interrupts/sec). Must not be NULL.
 *   fracfreq - Pointer to store the fractional part (*1000) of
 *              frequency. Can be NULL if not needed.
 *
 * Returned Value:
 *   OK on success, -EINVAL on bad IRQ or NULL freq.
 *
 ****************************************************************************/

int irq_monitor_get_freq(int irq, bool reset, FAR unsigned long *freq,
                         FAR unsigned long *fracfreq)
{
  int ndx = IRQ_TO_NDX(irq);
  clock_t elapsed;
#ifdef CONFIG_PM
  int i;
#endif
#ifdef CONFIG_HAVE_LONG_LONG
  unsigned long long intcount;
#else
  unsigned long intcount;
#endif

  if (freq == NULL || ndx < 0 || ndx >= NUSER_IRQS)
    {
      return -EINVAL;
    }

  /* Calculate elapsed perf ticks since last reset */

  elapsed = perf_gettime() - g_irqvector[ndx].start;
  elapsed = elapsed ? elapsed : 1;

  /* freq = count * perf_getfreq() / elapsed
   * Use 64-bit arithmetic when available to avoid overflow.
   */

#ifdef CONFIG_HAVE_LONG_LONG
  intcount = atomic_read(&g_irqvector[ndx].count) *
             (unsigned long long)perf_getfreq();
#else
  intcount = atomic_read(&g_irqvector[ndx].count) *
             (unsigned long)perf_getfreq();
#endif
  *freq    = intcount / elapsed;

  if (fracfreq != NULL)
    {
      *fracfreq = (intcount - *freq * elapsed) * 1000 / elapsed;
    }

  /* Optionally restart the measurement window */

  if (reset)
    {
      g_irqvector[ndx].start = perf_gettime();
      g_irqvector[ndx].time  = 0;
      atomic_set(&g_irqvector[ndx].count, 0);
#ifdef CONFIG_ARCH_IRQPRIO
      atomic_set(&g_irqvector[ndx].nests, 0);
#endif
#if defined(CONFIG_PM)
      for (i = 0; i < PM_COUNT; i++)
        {
          atomic_set(&g_irqvector[ndx].nwakeup[i], 0);
        }
#endif
    }

  return OK;
}
#endif
