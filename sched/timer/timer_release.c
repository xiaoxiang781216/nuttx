/****************************************************************************
 * sched/timer/timer_release.c
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

#include <errno.h>

#include <nuttx/irq.h>
#include <nuttx/queue.h>
#include <nuttx/kmalloc.h>
#include <nuttx/spinlock.h>

#include "timer/timer.h"

#ifndef CONFIG_DISABLE_POSIX_TIMERS

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: timer_free
 *
 * Description:
 *   Remove the timer from the allocated timer list and free it or return it
 *   to the free list (depending on whether or not the timer is one of the
 *   preallocated timers)
 *
 * Assumptions:
 *   The timer should not be NULL.
 *
 ****************************************************************************/

static inline_function void timer_free(FAR struct posix_timer_s *timer)
{
  DEBUGASSERT(timer);

#if CONFIG_PREALLOC_TIMERS > 0
  if ((timer->pt_flags & PT_FLAGS_PREALLOCATED) != 0u)
    {
      irqstate_t flags = spin_lock_irqsave(&g_locktimers);
      timer->flink = g_freetimers;
      g_freetimers = timer;
      spin_unlock_irqrestore(&g_locktimers, flags);
    }
  else
#endif
    {
      kmm_free(timer);
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: timer_release
 *
 * Description:
 *   timer_release implements the heart of timer_delete.  It waits for any
 *   running callback to complete, then frees the timer structure.
 *
 * Input Parameters:
 *   timer - The per-thread timer, previously created by the call to
 *     timer_create(), to be deleted.
 *   tcb  - the TCB of the timer owner.
 *
 * Returned Value:
 *   None.
 *
 * Assumptions:
 *   The timer should not be NULL.
 *
 ****************************************************************************/

void timer_release(FAR struct posix_timer_s *timer, FAR struct tcb_s *tcb)
{
  /* Some sanity checks */

  DEBUGASSERT(timer && tcb && timer->pt_owner == tcb->pid);

  /* Wait the callback to finish then we acquire the ownership. */

  hrtimer_cancel_sync(&timer->pt_timer);

  /* Cancel any pending notification */

  nxsig_cancel_notification(&timer->pt_work);

  /* Release the timer structure.  The internal interval timer backing
   * setitimer()/alarm() (group->itimer) was not counted on creation, so
   * do not decrement itimer_count for it here.
   */

  if ((timer_t)timer != tcb->group->itimer)
    {
      atomic_sub(&tcb->group->itimer_count, 1);
    }

  timer_free(timer);
}

#endif /* CONFIG_DISABLE_POSIX_TIMERS */
