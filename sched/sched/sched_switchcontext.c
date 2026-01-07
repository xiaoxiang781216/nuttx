/****************************************************************************
 * sched/sched/sched_switchcontext.c
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

#include "sched/sched.h"
#include <nuttx/sched_note.h>

#ifdef CONFIG_SCHED_PERF_EVENTS
#  include <nuttx/perf.h>
#endif

#ifdef CONFIG_SPINLOCK_DEBUG
#  include <nuttx/spinlock.h>
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: nxsched_suspend_scheduler
 *
 * Description:
 *   Called by architecture specific implementations that starts task
 *   execution.  This function prepares the scheduler for the thread that is
 *   about to be restarted.
 *
 * Input Parameters:
 *   tcb - The TCB of the thread that is being suspended.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void nxsched_suspend_scheduler(FAR struct tcb_s *tcb)
{
  nxsched_checkstackoverflow(tcb);

#ifdef CONFIG_SPINLOCK_DEBUG
  spinlock_switch_context(tcb);
#endif

#ifdef CONFIG_SCHED_SPORADIC
  /* Perform sporadic schedule operations */

  if ((tcb->flags & TCB_FLAG_POLICY_MASK) == TCB_FLAG_SCHED_SPORADIC)
    {
      DEBUGVERIFY(nxsched_suspend_sporadic(tcb));
    }
#endif

  /* Indicate that the task has been suspended */

#ifdef CONFIG_SCHED_CRITMONITOR
  nxsched_suspend_critmon(tcb);
#endif

#ifdef CONFIG_SCHED_INSTRUMENTATION
  sched_note_suspend(tcb);
#endif

#ifdef CONFIG_SCHED_PERF_EVENTS
  perf_event_task_sched_out(tcb);
#endif
}

/****************************************************************************
 * Name: nxsched_resume_scheduler
 *
 * Description:
 *   Called by architecture specific implementations that block task
 *   execution.  This function prepares the scheduler for the thread that is
 *   about to be restarted.
 *
 * Input Parameters:
 *   tcb - The TCB of the thread to be restarted.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void nxsched_resume_scheduler(FAR struct tcb_s *tcb)
{
#ifdef CONFIG_SCHED_SPORADIC
  if ((tcb->flags & TCB_FLAG_POLICY_MASK) == TCB_FLAG_SCHED_SPORADIC)
    {
      /* Reset the replenishment cycle if it is appropriate to do so */

      DEBUGVERIFY(nxsched_resume_sporadic(tcb));
    }
#endif

  /* Indicate the task has been resumed */

#ifdef CONFIG_SCHED_CRITMONITOR
  nxsched_resume_critmon(tcb);
#endif

#ifdef CONFIG_SCHED_INSTRUMENTATION
  sched_note_resume(tcb);
#endif

#ifdef CONFIG_SCHED_PERF_EVENTS
  perf_event_task_sched_in(tcb);
#endif
}

void nxsched_switch(FAR struct tcb_s *tcb, FAR struct tcb_s *rtcb)
{
  uint16_t count;

#if defined(CONFIG_SCHED_TICKLESS) && CONFIG_RR_INTERVAL > 0
  /* Before the context switch, we should set the timer for RR. */

  if ((tcb->flags & TCB_FLAG_POLICY_MASK) == TCB_FLAG_SCHED_RR)
    {
      nxsched_reassess_timer();
    }
#endif

  count = rspin_lock_count(&g_schedlock);

  nxsched_suspend_scheduler(rtcb);
  nxsched_resume_scheduler(tcb);
  up_switch_context(tcb, rtcb);

  if (!up_interrupt_context())
    {
      restore_critical_section(count);
    }

  UNUSED(count);
}
