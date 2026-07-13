/****************************************************************************
 * sched/clock/clock_realtime2abstime.c
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

#include <time.h>
#include <errno.h>
#include <debug.h>
#include "clock/clock.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: clock_realtime2abstime
 *
 * Description:
 *   Convert real time to monotonic ticks.
 *
 * Input Parameters:
 *   mono - Return the converted time here.
 *   abstime - Convert this absolute time to ticks
 *
 * Returned Value:
 *   OK (0) on success; a negated errno value on failure.
 *
 * Assumptions:
 *   Interrupts should be disabled so that the time is not changing during
 *   the calculation
 *
 ****************************************************************************/

int clock_realtime2abstime(FAR const struct timespec *realtime,
                           FAR struct timespec *ts)
{
#ifdef CONFIG_CLOCK_TIMEKEEPING
  struct timespec now;

  nxclock_gettime(CLOCK_REALTIME, &now);
  clock_timespec_subtract(realtime, &now, ts);

  clock_systime_timespec(&now);
  clock_timespec_add(ts, &now, ts);
#else
  clock_timespec_subtract(realtime, &g_basetime, ts);
#endif
  return OK;
}
