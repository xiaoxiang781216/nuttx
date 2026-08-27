/****************************************************************************
 * libs/libc/sched/clock_perf.c
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

#include <nuttx/arch.h>
#include <nuttx/clock.h>
#include <nuttx/seqlock.h>
#include <nuttx/userspace.h>

#ifdef CONFIG_PERF_CORRECTION

/****************************************************************************
 * Public Data
 ****************************************************************************/

#undef g_perf
#if defined(CONFIG_BUILD_PROTECTED) && defined(__KERNEL__) && \
    defined(CONFIG_ARCH_HAVE_PERF_EVENTS_USER_ACCESS)
#  define g_perf (*(USERSPACE->us_data->us_perf))
#else
DEFINE_PER_CPU_BMP(struct perf_s, g_perf) =
{
  .lock = SEQLOCK_INITIALIZER,
};
#  define g_perf this_cpu_var_bmp(g_perf)
#endif

#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * perf_gettime
 ****************************************************************************/

clock_t perf_gettime(void)
{
  clock_t result;

#ifdef CONFIG_PERF_OVERFLOW_CORRECTION
  clock_t overflow;
  uint32_t seq;

  do
    {
      seq = read_seqbegin(&g_perf.lock);
      clock_t now = up_perf_gettime();

      if (now < g_perf.last)
        {
          overflow = g_perf.overflow + 1;
        }
      else
        {
          overflow = g_perf.overflow;
        }

      result = now | (overflow << CONFIG_ARCH_PERF_COUNT_BITWIDTH);
    }
  while (read_seqretry(&g_perf.lock, seq));
#else
  result = up_perf_gettime();
#endif

#ifdef CONFIG_PERF_OFFSET_CORRECTION
  result += g_perf.offset;
#endif

  return result;
}

/****************************************************************************
 * perf_gettime_raw
 ****************************************************************************/

clock_t perf_gettime_raw(void)
{
  return up_perf_gettime();
}

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
