/****************************************************************************
 * sched/clock/clock_perf.c
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

#include <stdint.h>

#include <nuttx/arch.h>
#include <nuttx/clock.h>
#include <nuttx/spinlock.h>
#include <nuttx/seqlock.h>
#include <nuttx/userspace.h>
#include <nuttx/hrtimer.h>
#include <nuttx/clock_notifier.h>
#include <nuttx/timers/clkcnt.h>

/****************************************************************************
 * Preprocessors
 ****************************************************************************/

/****************************************************************************
 * Private Types
 ****************************************************************************/

/****************************************************************************
 * Private Data
 ****************************************************************************/

#ifdef CONFIG_PERF_OVERFLOW_CORRECTION
static DEFINE_PER_CPU_BSS_BMP(uint64_t, g_timeout_ns);
#  define g_timeout_ns this_cpu_var_bmp(g_timeout_ns)

static DEFINE_PER_CPU_BSS_BMP(hrtimer_t, g_timer);
#  define g_timer this_cpu_var_bmp(g_timer)
#endif

#ifdef CONFIG_PERF_OFFSET_CORRECTION
static DEFINE_PER_CPU_BSS_BMP(struct notifier_block, g_perf_clock_nb);
#  define g_perf_clock_nb this_cpu_var_bmp(g_perf_clock_nb)
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#ifdef CONFIG_PERF_OVERFLOW_CORRECTION
static uint64_t perf_update_overflow(FAR const hrtimer_t *timer,
                                     uint64_t expired)
{
  FAR struct perf_s *perf  = &g_perf;
  irqstate_t         flags = write_seqlock_irqsave(&perf->lock);
  clock_t            now   = up_perf_gettime();

  if (now < perf->last)
    {
      perf->overflow++;
    }

  perf->last = now;
  write_sequnlock_irqrestore(&perf->lock, flags);

  return g_timeout_ns;
}
#endif

#ifdef CONFIG_PERF_OFFSET_CORRECTION
static void perf_update_offset(FAR const struct timespec *ts)
{
  FAR struct perf_s *perf = &g_perf;
  unsigned long freq = up_perf_getfreq();
  irqstate_t flags;
  clock_t offset;

  offset = ts->tv_sec * freq +
           clkcnt_delta_time2cnt(ts->tv_nsec, freq, NSEC_PER_SEC) -
           up_perf_gettime();

  flags = write_seqlock_irqsave(&perf->lock);
  perf->offset = offset;
  write_sequnlock_irqrestore(&perf->lock, flags);
}

static int perf_clock_notify(FAR struct notifier_block *nb,
                             unsigned long action, FAR void *data)
{
  if (action == CLOCK_REALTIME)
    {
      perf_update_offset((FAR const struct timespec *)data);
    }

  return 0;
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

#ifdef CONFIG_PERF_CORRECTION

/****************************************************************************
 * perf_setup
 ****************************************************************************/

void perf_setup(void)
{
#  ifdef CONFIG_PERF_OFFSET_CORRECTION
  struct timespec ts;

  if (clock_gettime(CLOCK_REALTIME, &ts) == 0)
    {
      perf_update_offset(&ts);
      g_perf_clock_nb.notifier_call = perf_clock_notify;
      register_clock_notifier(&g_perf_clock_nb);
    }
#  endif

#  ifdef CONFIG_PERF_OVERFLOW_CORRECTION
  /* Setup periodic-hrtimer here, this function can be called after
   * perf_gettime(), but suggested to be called in initialization phase.
   */

  g_timeout_ns =
      ((uint64_t)1 << (CONFIG_ARCH_PERF_COUNT_BITWIDTH - 1)) *
      NSEC_PER_SEC / up_perf_getfreq();

  /* In case of starvation of the periodic timer. */

  DEBUGASSERT(g_timeout_ns >= NSEC_PER_TICK);

  /* Periodic check for overflow */

  hrtimer_start(&g_timer, perf_update_overflow, g_timeout_ns,
                HRTIMER_MODE_REL);
#  endif
}
#endif

#if !defined(CONFIG_ALARM_ARCH) && !defined(CONFIG_TIMER_ARCH) && \
    !defined(CONFIG_ARCH_PERF_EVENTS)
/****************************************************************************
 * perf_gettime
 ****************************************************************************/

clock_t up_perf_gettime(void)
{
  return clock_systime_ticks();
}

/****************************************************************************
 * perf_convert
 ****************************************************************************/

void up_perf_convert(clock_t elapsed, FAR struct timespec *ts)
{
  clock_ticks2time(ts, elapsed);
}

/****************************************************************************
 * perf_getfreq
 ****************************************************************************/

unsigned long up_perf_getfreq(void)
{
  return TICK_PER_SEC;
}

#endif
