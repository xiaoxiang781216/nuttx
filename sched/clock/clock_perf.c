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
#include <nuttx/wdog.h>

/****************************************************************************
 * Preprocessors
 ****************************************************************************/

#if defined(CONFIG_PERF_OVERFLOW_CORRECTION)

/****************************************************************************
 * Private Types
 ****************************************************************************/

/****************************************************************************
 * Private Data
 ****************************************************************************/

static DEFINE_PER_CPU_BSS_BMP(unsigned int, g_timeout);
#define g_timeout this_cpu_var_bmp(g_timeout)

static DEFINE_PER_CPU_BSS_BMP(struct wdog_s, g_wdog);
#define g_wdog this_cpu_var_bmp(g_wdog)

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * perf_update
 ****************************************************************************/

static void perf_update(wdparm_t arg)
{
  FAR struct perf_s *perf = (FAR struct perf_s *)arg;

  irqstate_t flags = write_seqlock_irqsave(&perf->lock);
  clock_t now = up_perf_gettime();

  if (now < perf->last)
    {
      perf->overflow++;
    }

  perf->last = now;
  write_sequnlock_irqrestore(&perf->lock, flags);
  wd_start_next(&g_wdog, g_timeout, perf_update, arg);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * perf_setup
 ****************************************************************************/

void perf_setup(void)
{
  FAR struct perf_s *perf = &g_perf;

  /* Settup periodic-wdog here, this function can be called after
   * perf_gettime(), but suggested to be called in initialization phase.
   */

  g_timeout =
      ((clock_t)1 << (CONFIG_ARCH_PERF_COUNT_BITWIDTH - 1)) *
      TICK_PER_SEC / up_perf_getfreq();

  /* Periodic check for overflow */

  wd_start(&g_wdog, g_timeout, perf_update, (wdparm_t)perf);
}
#elif !defined(CONFIG_ARCH_PERF_EVENTS_USER_ACCESS) && \
      !defined(CONFIG_ALARM_ARCH) && !defined(CONFIG_TIMER_ARCH) && \
      !defined(CONFIG_ARCH_PERF_EVENTS)

/****************************************************************************
 * perf_gettime
 ****************************************************************************/

clock_t noinstrument_function perf_gettime(void)
{
  return clock_systime_ticks();
}
#endif /* CONFIG_ARCH_PERF_EVENTS_USER_ACCESS */

#if defined(CONFIG_ALARM_ARCH) || defined (CONFIG_TIMER_ARCH) || \
    defined(CONFIG_ARCH_PERF_EVENTS)

/****************************************************************************
 * perf_convert
 ****************************************************************************/

void perf_convert(clock_t elapsed, FAR struct timespec *ts)
{
  up_perf_convert(elapsed, ts);
}

/****************************************************************************
 * perf_getfreq
 ****************************************************************************/

unsigned long perf_getfreq(void)
{
  return up_perf_getfreq();
}

#else

/****************************************************************************
 * perf_convert
 ****************************************************************************/

void perf_convert(clock_t elapsed, FAR struct timespec *ts)
{
  clock_ticks2time(ts, elapsed);
}

/****************************************************************************
 * perf_getfreq
 ****************************************************************************/

unsigned long perf_getfreq(void)
{
  return TICK_PER_SEC;
}

#endif
