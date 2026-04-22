/****************************************************************************
 * sched/timer/timer_delete.c
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

#include "timer/timer.h"

#ifndef CONFIG_DISABLE_POSIX_TIMERS

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: timer_delete
 *
 * Description:
 *   The timer_delete() function deletes the specified timer, timerid,
 *   previously created by the timer_create() function. If the timer is
 *   armed when timer_delete() is called, the timer will be automatically
 *   disarmed before removal. The disposition of pending signals for the
 *   deleted timer is unspecified.
 *
 * Input Parameters:
 *   timerid - The per-thread timer, previously created by the call to
 *   timer_create(), to be deleted.
 *
 * Returned Value:
 *   If the call succeeds, timer_create() will return 0 (OK).  Otherwise,
 *   the function will return a value of -1 (ERROR) and set errno to
 *   indicate the error.
 *
 *   EINVAL - The timer specified timerid is not valid.
 *
 * Assumptions:
 *
 ****************************************************************************/

int timer_delete(timer_t timerid)
{
  int ret;
  FAR struct posix_timer_s *timer = timer_gethandle(timerid);

  if (timer == NULL)
    {
      set_errno(EINVAL);
      ret = ERROR;
    }
  else
    {
      /* Note that the timer_release may return 1, which is non-compliant
       * with the POSIX standards.
       */

      timer_release(timer, false, false);
      ret = OK;
    }

  return ret;
}

#endif /* CONFIG_DISABLE_POSIX_TIMERS */
