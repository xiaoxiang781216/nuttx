/****************************************************************************
 * libs/libc/unistd/lib_alarm.c
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

#include <unistd.h>
#include <sys/time.h>

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: alarm
 *
 * Description:
 *   The alarm() function will arranges for a SIGALRM signal to be delivered
 *   to the calling process in seconds seconds.
 *
 *   Interactions between alarm() and any of setitimer() are unspecified.
 *
 * Input Parameters:
 *   seconds - The number of seconds to alarm
 *
 * Returned Value:
 *   alarm() returns the number of seconds remaining until any previously
 *   scheduled alarm was due to be delivered, or zero if there was no
 *   previously scheduled alarm.
 *
 * Assumptions:
 *
 ****************************************************************************/

unsigned int alarm(unsigned int seconds)
{
  struct itimerval value =
  {
    {0, 0},
    {0, 0}
  };

  struct itimerval ovalue =
  {
    {0, 0},
    {0, 0}
  };

  value.it_value.tv_sec = seconds;
  setitimer(ITIMER_REAL, &value, &ovalue);

  /* alarm() has one-second granularity: round the remaining time up to the
   * next whole second.  A just-armed alarm(N) polled immediately still has
   * (N seconds minus a few microseconds) left, so flooring it to whole
   * seconds would return N-1 instead of N.  That fails the VSX PSE52 alarm
   * conformance test, which expects alarm(1) followed at once by alarm(0)
   * to return 1.  Rounding up any non-zero sub-second remainder matches the
   * historical Unix behaviour; a disarmed timer has a zero ovalue, so it
   * still returns 0.
   */

  if (ovalue.it_value.tv_usec != 0)
    {
      ovalue.it_value.tv_sec++;
    }

  return ovalue.it_value.tv_sec;
}
