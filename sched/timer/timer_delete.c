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

#include <nuttx/sched.h>

#include "timer/timer.h"

#ifndef CONFIG_DISABLE_POSIX_TIMERS

/****************************************************************************
 * Inline Functions
 ****************************************************************************/

/****************************************************************************
 * Name: timer_remove_from_alloclist
 *
 * Description:
 *   Remove a timer from the allocated timer list.
 *
 * Input Parameters:
 *   timerid- The timerid to remove from g_alloctimers.
 *
 * Returned Value:
 *   The timer pointer if it was found and removed, or NULL if not found.
 *
 ****************************************************************************/

static inline_function
FAR struct posix_timer_s *timer_remove_from_alloclist(timer_t timerid)
{
  FAR struct posix_timer_s  *entry;
  FAR struct posix_timer_s  *ret        = NULL;
  FAR struct posix_timer_s **prev_flink = &g_alloctimers;
  irqstate_t                 flags      = spin_lock_irqsave(&g_locktimers);

  for (entry = g_alloctimers; entry != NULL; entry = entry->flink)
    {
      if ((uintptr_t)entry == timerid)
        {
          *prev_flink = entry->flink;
          ret         = entry;
          break;
        }

      prev_flink = &entry->flink;
    }

  spin_unlock_irqrestore(&g_locktimers, flags);

  return ret;
}

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
 ****************************************************************************/

int timer_delete(timer_t timerid)
{
  int ret = OK;
  FAR struct posix_timer_s *timer = timer_remove_from_alloclist(timerid);

  if (timer != NULL)
    {
      /* The POSIX timer_delete() API may be called from any context,
       * including a SIGEV_THREAD notification callback dispatched on the
       * work queue. this_task() is therefore not necessarily the timer
       * owner; resolve the owner TCB from pt_owner so that timer_release()
       * accounts the itimer_count decrement to the correct group.
       */

      FAR struct tcb_s *tcb = nxsched_get_tcb(timer->pt_owner);
      timer_release(timer, tcb);
      nxsched_put_tcb(tcb);
    }
  else
    {
      set_errno(EINVAL);
      ret = ERROR;
    }

  return ret;
}

#endif /* CONFIG_DISABLE_POSIX_TIMERS */
