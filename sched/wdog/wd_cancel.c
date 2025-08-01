/****************************************************************************
 * sched/wdog/wd_cancel.c
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

#include <stdbool.h>
#include <assert.h>
#include <errno.h>

#include <nuttx/irq.h>
#include <nuttx/arch.h>
#include <nuttx/wdog.h>
#include <nuttx/sched_note.h>

#include "sched/sched.h"
#include "wdog/wdog.h"

/****************************************************************************
 * Inline Functions
 ****************************************************************************/

/****************************************************************************
 * Name: wd_cancel_running
 *
 * Description:
 *   This function cancels a currently inactive watchdog timer.
 *   Note that the inactive watchdog timer callback may be running.
 *
 * Input Parameters:
 *   wdog - ID of the watchdog to cancel.
 *
 * Returned Value:
 *   true  - the watchdog callback is running.
 *   false - the watchdog is already inactive.
 *
 ****************************************************************************/

static inline_function
bool wd_cancel_running(FAR struct wdog_s *wdog)
{
  int  cpu;
  bool running = false;

  DEBUGASSERT(wdog != NULL);

  /* Check if any core has marked the wdog via hazard-pointer. */

  for (cpu = 0; cpu < CONFIG_SMP_NCPUS; cpu++)
    {
      if (WDOG_GETRUNNING(cpu) == wdog)
        {
          /* Calling wd_cancel(wdog) in the wdog expiration callback
           * to cancel the wdog itself can lead to circular-wait,
           * which is not allowed.
           */

          DEBUGASSERT(cpu != this_cpu());

          /* Mark canceling state to block the remote thread
           * restarting the wdog.
           */

          WDOG_SETCANCELING(wdog, cpu);

          running = true;
        }
    }

  return running;
}

/****************************************************************************
 * Name: wd_cancel_active
 *
 * Description:
 *   This function cancels a currently active watchdog timer.
 *
 * Input Parameters:
 *   wdog - ID of the watchdog to cancel.
 *
 * Returned Value:
 *   If the head of the watchdog list has changed.
 *
 ****************************************************************************/

static inline_function bool wd_cancel_active(FAR struct wdog_s *wdog)
{
  bool head;

  sched_note_wdog(NOTE_WDOG_CANCEL, (FAR void *)wdog->func,
                  (FAR void *)(uintptr_t)wdog->expired);

  /* Prohibit timer interactions with the timer queue until the
   * cancellation is complete
   */

  head = list_is_head(&g_wdactivelist, &wdog->node);

  /* Now, remove the watchdog from the timer queue */

  list_delete(&wdog->node);

  /* Mark the watchdog inactive */

  wdog->func = NULL;

  return head;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: wd_try_cancel
 *
 * Description:
 *   This function set wdog to canceling state. This will prevent the wdog
 *   from restarting. However, this function can not ensure wdog ownership
 *   acquired after calling. Remote thread can still hold the reference
 *   to the wdog. It means this function CAN NOT PROVIDE MEMORY-SAFETY.
 *
 * Input Parameters:
 *   wdog - ID of the watchdog to cancel.
 *
 * Returned Value:
 *   Zero (OK) is returned on success; A negated errno value is returned to
 *   indicate the nature of any failure.
 *   -EINVAL is returned if wdog is NULL or inactive.
 *   -EBUSY is returned if the wdog callback is running.
 *
 ****************************************************************************/

int wd_try_cancel(FAR struct wdog_s *wdog)
{
  irqstate_t flags;
  int        ret     = -EINVAL;
  bool       head    = false;
  bool       running = false;

  if (wdog == NULL)
    {
      return ret;
    }

  flags = spin_lock_irqsave(&g_wdspinlock);

  /* Make sure that the watchdog is valid and still active. */

  if (WDOG_ISACTIVE(wdog))
    {
      head = wd_cancel_active(wdog);
      ret  = OK;
    }

  running = wd_cancel_running(wdog);

  spin_unlock_irqrestore(&g_wdspinlock, flags);

  if (head)
    {
      /* If the watchdog is at the head of the timer queue, then
       * we will need to re-adjust the interval timer that will
       * generate the next interval event.
       */

      nxsched_reassess_timer();
    }

  /* If the wdog callback is running. */

  return running ? -EBUSY : ret;
}

/****************************************************************************
 * Name: wd_cancel
 *
 * Description:
 *   This function cancels a currently running watchdog timer. Watchdog
 *   timers may be canceled from the interrupt level. This function ensure
 *   the watchdog timer ownership acquired. So users can free or reuse the
 *   wdog data-structure.
 *
 * Input Parameters:
 *   wdog - ID of the watchdog to cancel.
 *
 * Returned Value:
 *   Zero (OK) is returned on success;  A negated errno value is returned to
 *   indicate the nature of any failure.
 *   -EINVAL is returned if wdog is NULL or inactive.
 *
 ****************************************************************************/

int wd_cancel(FAR struct wdog_s *wdog)
{
  int cpu;
  int ret = wd_try_cancel(wdog);

  if (ret != -EBUSY)
    {
      return ret;
    }

  /* Wait for ALL references to the wdog being released. */

  for (cpu = 0; cpu < CONFIG_SMP_NCPUS; cpu++)
    {
      /* Since the callback is only called in the interrupt context,
       * spin-waiting should be enough.
       */

      while (WDOG_GETRUNNING(cpu) == wdog)
        {
          /* CPU Relaxing. */

          /* If other threads call wd_start in non-interrupt context to
           * restart this wdog, it will lead to canceling failure.
           * This is an ownership violation issue that should be prohibited.
           */

          DEBUGASSERT(!WDOG_ISACTIVE(wdog));
        }
    }

  return OK;
}
