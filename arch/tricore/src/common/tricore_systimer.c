/****************************************************************************
 * arch/tricore/src/common/tricore_systimer.c
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

#include <nuttx/irq.h>
#include <nuttx/kmalloc.h>
#include <nuttx/timers/oneshot.h>
#include <nuttx/timers/arch_alarm.h>

#include "tricore_internal.h"

#include "IfxStm.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define systimer_trigger_irq TRICORE_GPSR_IRQNUM(up_this_cpu(), up_this_cpu())

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* This structure provides the private representation of the "lower-half"
 * driver state structure.  This structure must be cast-compatible with the
 * oneshot_lowerhalf_s structure.
 */

struct tricore_systimer_lowerhalf_s
{
  struct oneshot_lowerhalf_s lower;
  volatile void             *tbase;
  volatile void             *freerun_tbase;
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: tricore_systimer_max_delay
 *
 * Description:
 *   Determine the maximum delay of the one-shot timer
 *
 * Input Parameters:
 *   lower   An instance of the lower-half oneshot state structure.  This
 *           structure must have been previously initialized via a call to
 *           oneshot_initialize();
 *
 * Returned Value:
 *   The maximum delay value.
 *
 ****************************************************************************/

static clkcnt_t tricore_systimer_max_delay(struct oneshot_lowerhalf_s *lower)
{
  return UINT32_MAX;
}

/****************************************************************************
 * Name: tricore_systimer_updata
 ****************************************************************************/

static void tricore_systimer_updata(struct oneshot_lowerhalf_s *lower,
                                    clkcnt_t clkcnt, bool relative)
{
  struct tricore_systimer_lowerhalf_s *priv =
    (struct tricore_systimer_lowerhalf_s *)lower;
  irqstate_t flags;
  uint64_t   expected;
  uint64_t   current;
  cpu_set_t  cpuset;

  flags = up_irq_save();

  current = IfxStm_get(priv->tbase);
  expected = relative ? clkcnt + current : clkcnt;

  /* Time synchronization between cores */

  if (priv->freerun_tbase)
    {
      expected = expected - IfxStm_get(priv->freerun_tbase) + current;
    }

  /* The comparator register is 32-bits. */

  DEBUGASSERT(expected <= current + UINT32_MAX);

  IfxStm_updateCompare(priv->tbase, IfxStm_Comparator_0,
                       (uint32_t)(expected & UINT32_MAX));

  if (IfxStm_get(priv->tbase) > expected)
    {
      IfxStm_clearCompareFlag(priv->tbase, IfxStm_Comparator_0);
      CPU_ZERO(&cpuset);
      CPU_SET(up_this_cpu(), &cpuset);
      up_trigger_irq(systimer_trigger_irq, cpuset);
    }

  up_irq_restore(flags);
}

/****************************************************************************
 * Name: tricore_systimer_start
 *
 * Description:
 *   Start the oneshot timer. Note that the tricore systimer is special, the
 *   IRQ is only triggered when timecmp == mtime, so we should avoid the case
 *   that we miss the timecmp.
 *
 * Input Parameters:
 *   lower   An instance of the lower-half oneshot state structure.  This
 *           structure must have been previously initialized via a call to
 *           oneshot_initialize();
 *   delta   Provides the duration of delta count.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void tricore_systimer_start(struct oneshot_lowerhalf_s *lower,
                                   clkcnt_t delta)
{
  tricore_systimer_updata(lower, delta, true);
}

/****************************************************************************
 * Name: tricore_systimer_start_absolute
 *
 * Description:
 *   Start the oneshot timer. Note that the tricore systimer is special, the
 *   IRQ is only triggered when timecmp == mtime, so we should avoid the case
 *   that we miss the timecmp.
 *
 * Input Parameters:
 *   lower    An instance of the lower-half oneshot state structure.  This
 *            structure must have been previously initialized via a call to
 *            oneshot_initialize();
 *   expected Target
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void
tricore_systimer_start_absolute(struct oneshot_lowerhalf_s *lower,
                                clkcnt_t expected)
{
  tricore_systimer_updata(lower, expected, false);
}

/****************************************************************************
 * Name: tricore_systimer_cancel
 *
 * Description:
 *   Cancel the oneshot timer and return the time remaining on the timer.
 *
 *   NOTE: This function may execute at a high rate with no timer running (as
 *   when pre-emption is enabled and disabled).
 *
 * Input Parameters:
 *   lower   Caller allocated instance of the oneshot state structure.  This
 *           structure must have been previously initialized via a call to
 *           oneshot_initialize();
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void tricore_systimer_cancel(struct oneshot_lowerhalf_s *lower)
{
  struct tricore_systimer_lowerhalf_s *priv =
    (struct tricore_systimer_lowerhalf_s *)lower;

  IfxStm_updateCompare(priv->tbase, IfxStm_Comparator_0, UINT32_MAX);
}

/****************************************************************************
 * Name: tricore_systimer_current
 *
 * Description:
 *  Get the current time.
 *
 * Input Parameters:
 *   lower   Caller allocated instance of the oneshot state structure.  This
 *           structure must have been previously initialized via a call to
 *           oneshot_initialize();
 *
 * Returned Value:
 *   Current timer count.
 *
 ****************************************************************************/

static clkcnt_t tricore_systimer_current(struct oneshot_lowerhalf_s *lower)
{
  struct tricore_systimer_lowerhalf_s *priv =
    (struct tricore_systimer_lowerhalf_s *)lower;

  if (priv->freerun_tbase)
    {
      return IfxStm_get(priv->freerun_tbase);
    }
  else
    {
      return IfxStm_get(priv->tbase);
    }
}

/****************************************************************************
 * Name: tricore_systimer_interrupt
 *
 * Description:
 *   This function is software interrupt handler to proceed
 *   the system timer interrupt.
 *
 ****************************************************************************/

static int tricore_systimer_interrupt(int irq, void *context, void *arg)
{
  struct tricore_systimer_lowerhalf_s *priv = arg;

  /* We do not need to clear the compare register here. */

  oneshot_process_callback(&priv->lower);

  return 0;
}

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct oneshot_operations_s g_tricore_oneshot_ops =
{
  .current        = tricore_systimer_current,
  .start          = tricore_systimer_start,
  .start_absolute = tricore_systimer_start_absolute,
  .cancel         = tricore_systimer_cancel,
  .max_delay      = tricore_systimer_max_delay
};

static DEFINE_PER_CPU_BSS_BMP(struct tricore_systimer_lowerhalf_s,
                              g_tricore_oneshot_lowerhalf);
#define g_tricore_oneshot_lowerhalf this_cpu_var_bmp(g_tricore_oneshot_lowerhalf)

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: tricore_systimer_initialize
 *
 * Description:
 *   This function is called during start-up to initialize
 *   the timer interrupt.
 *
 ****************************************************************************/

struct oneshot_lowerhalf_s *
tricore_systimer_initialize(volatile void *tbase, volatile void *fr_tbase,
                            int irq, uint64_t freq)
{
  struct tricore_systimer_lowerhalf_s *priv = &g_tricore_oneshot_lowerhalf;

  priv->lower.ops = &g_tricore_oneshot_ops;
  priv->tbase = tbase;
  priv->freerun_tbase = fr_tbase;

  ASSERT(freq <= UINT32_MAX);

  oneshot_count_init(&priv->lower, (uint32_t)freq);

  IfxStm_setCompareControl(tbase,
      IfxStm_Comparator_0,
      IfxStm_ComparatorOffset_0,
      IfxStm_ComparatorSize_32Bits,
      IfxStm_ComparatorInterrupt_ir0);

  IfxStm_clearCompareFlag(tbase, IfxStm_Comparator_0);
  IfxStm_updateCompare(priv->tbase, IfxStm_Comparator_0, UINT32_MAX);
  IfxStm_enableComparatorInterrupt(tbase, IfxStm_Comparator_0);

  irq_attach(irq, tricore_systimer_interrupt, priv);
  irq_attach(systimer_trigger_irq, tricore_systimer_interrupt, priv);
  up_enable_irq(irq);
  up_enable_irq(systimer_trigger_irq);

  return (struct oneshot_lowerhalf_s *)priv;
}
