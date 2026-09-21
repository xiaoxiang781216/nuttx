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

#include <nuttx/sched_note.h>

#ifdef CONFIG_PERF_EVENTS
#  include <nuttx/perf.h>
#endif

#ifdef CONFIG_SPINLOCK_DEBUG
#  include <nuttx/spinlock.h>
#endif

#ifdef CONFIG_SCHED_HOOK
#  include <errno.h>
#  include <nuttx/queue.h>
#  include <nuttx/spinlock.h>
#endif

#include "sched/sched.h"
#include "perf/perf.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

#ifdef CONFIG_SCHED_HOOK

/* Per-CPU chain head + spinlock.
 *
 * BMP builds: each core links a private image and consumers (e.g.
 * autocore SC2 timing protection) register one hook instance per
 * core, so per-CPU storage is the natural fit.
 * SMP builds: per-CPU also avoids cross-core cache traffic on the
 * dispatch hot path.
 *
 * Both register/unregister and dispatch take this spinlock.  All
 * three call sites (register, unregister, dispatch) live in this
 * file, so the storage symbols and the dispatcher are file-static.
 */

static DEFINE_PER_CPU_BSS_BMP(dq_queue_t, g_sched_hooks);
#define g_sched_hooks this_cpu_var_bmp(g_sched_hooks)

static DEFINE_PER_CPU_BMP(spinlock_t, g_sched_hooks_lock) = SP_UNLOCKED;
#define g_sched_hooks_lock this_cpu_var_bmp(g_sched_hooks_lock)

#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#ifdef CONFIG_SCHED_HOOK

/****************************************************************************
 * Name: nxsched_hook_dispatch
 *
 * Description:
 *   Walk the per-CPU registration list and invoke each callback in
 *   registration order.  Held under g_sched_hooks_lock so concurrent
 *   register/unregister cannot race with the walk.  Empty-list fast
 *   path is a single NULL load + branch out of the loop.
 *
 *   File-static — only nxsched_suspend_scheduler() / _resume_scheduler()
 *   below call it.
 *
 ****************************************************************************/

static inline void nxsched_hook_dispatch(FAR struct tcb_s *tcb,
                                         enum nxsched_hook_event_e event)
{
  FAR dq_entry_t *node;
  irqstate_t      flags;

  flags = spin_lock_irqsave_notrace(&g_sched_hooks_lock);
  dq_for_every(&g_sched_hooks, node)
    {
      FAR struct nxsched_hook_s *h = (FAR struct nxsched_hook_s *)node;
      h->fn(tcb, event);
    }

  spin_unlock_irqrestore_notrace(&g_sched_hooks_lock, flags);
}

#endif /* CONFIG_SCHED_HOOK */

/****************************************************************************
 * Public Functions
 ****************************************************************************/

#ifdef CONFIG_SCHED_HOOK

/****************************************************************************
 * Name: nxsched_hook_register
 ****************************************************************************/

int nxsched_hook_register(FAR struct nxsched_hook_s *hook)
{
  irqstate_t flags;

  if (hook == NULL || hook->fn == NULL)
    {
      return -EINVAL;
    }

  flags = spin_lock_irqsave(&g_sched_hooks_lock);

  /* Reject duplicate registration of the same struct.  A hook node
   * that has never been linked has both flink and blink == NULL; a
   * linked node has at least one of them non-NULL even if it is the
   * sole queue member (head and tail point at it).
   */

  if (hook->entry.flink != NULL || hook->entry.blink != NULL ||
      g_sched_hooks.head == &hook->entry)
    {
      spin_unlock_irqrestore(&g_sched_hooks_lock, flags);
      return -EEXIST;
    }

  /* Append at the tail so callbacks fire in registration order.
   * dq_addlast is O(1) — no walk-to-tail needed.
   */

  dq_addlast(&hook->entry, &g_sched_hooks);

  spin_unlock_irqrestore(&g_sched_hooks_lock, flags);
  return OK;
}

/****************************************************************************
 * Name: nxsched_hook_unregister
 ****************************************************************************/

int nxsched_hook_unregister(FAR struct nxsched_hook_s *hook)
{
  irqstate_t flags;
  int        ret = -ENOENT;

  if (hook == NULL)
    {
      return -EINVAL;
    }

  flags = spin_lock_irqsave(&g_sched_hooks_lock);

  /* Same "is this node linked?" check as in register. */

  if (hook->entry.flink != NULL || hook->entry.blink != NULL ||
      g_sched_hooks.head == &hook->entry)
    {
      dq_rem(&hook->entry, &g_sched_hooks);
      hook->entry.flink = NULL;
      hook->entry.blink = NULL;
      ret = OK;
    }

  spin_unlock_irqrestore(&g_sched_hooks_lock, flags);
  return ret;
}

#endif /* CONFIG_SCHED_HOOK */

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

__jumpfunc__ void nxsched_suspend_scheduler(FAR struct tcb_s *tcb)
{
  if (tcb == NULL)
    {
      return;
    }

  nxsched_checkstackoverflow(tcb);

#ifdef CONFIG_SPINLOCK_DEBUG
  spinlock_switch_context(tcb);
#endif

#ifdef CONFIG_SCHED_SPORADIC
  /* Perform sporadic schedule operations */

  if ((tcb->flags & TCB_FLAG_POLICY_MASK) == TCB_FLAG_SCHED_SPORADIC &&
      tcb->sporadic != NULL)
    {
      DEBUGVERIFY(nxsched_suspend_sporadic(tcb));
    }
#endif

#if CONFIG_RR_INTERVAL > 0 && defined(CONFIG_SCHED_TICKLESS)
  if ((tcb->flags & TCB_FLAG_POLICY_MASK) == TCB_FLAG_SCHED_RR)
    {
      nxsched_suspend_roundrobin(tcb);
    }
#endif

  /* Indicate that the task has been suspended */

#ifdef CONFIG_SCHED_CRITMONITOR
  nxsched_suspend_critmon(tcb);
#endif

#ifdef CONFIG_SCHED_INSTRUMENTATION
  sched_note_suspend(tcb);
#endif

#ifdef CONFIG_PERF_EVENTS
  perf_event_task_sched_out(tcb);
#endif

#ifdef CONFIG_SCHED_HOOK
  nxsched_hook_dispatch(tcb, NXSCHED_HOOK_SUSPEND);
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

__jumpfunc__ void nxsched_resume_scheduler(FAR struct tcb_s *tcb)
{
#ifdef CONFIG_SCHED_SPORADIC
  if ((tcb->flags & TCB_FLAG_POLICY_MASK) == TCB_FLAG_SCHED_SPORADIC &&
      tcb->sporadic != NULL)
    {
      /* Reset the replenishment cycle if it is appropriate to do so */

      DEBUGVERIFY(nxsched_resume_sporadic(tcb));
    }
#endif

#if CONFIG_RR_INTERVAL > 0 && defined(CONFIG_SCHED_TICKLESS)
  if ((tcb->flags & TCB_FLAG_POLICY_MASK) == TCB_FLAG_SCHED_RR)
    {
      nxsched_resume_roundrobin(tcb);
    }
#endif

  /* Indicate the task has been resumed */

#ifdef CONFIG_SCHED_CRITMONITOR
  nxsched_resume_critmon(tcb);
#endif

#ifdef CONFIG_SCHED_INSTRUMENTATION
  sched_note_resume(tcb);
#endif

#ifdef CONFIG_PERF_EVENTS
  perf_event_task_sched_in(tcb);
#endif

#ifdef CONFIG_SCHED_HOOK
  nxsched_hook_dispatch(tcb, NXSCHED_HOOK_RESUME);
#endif
}

void nxsched_switch(FAR struct tcb_s *tcb, FAR struct tcb_s *rtcb)
{
  uint16_t count = RSPINLOCK_COUNT(g_schedlock.local_val);

  nxsched_suspend_scheduler(rtcb);
  nxsched_resume_scheduler(tcb);
  up_switch_context(tcb, rtcb);

  if (!up_interrupt_context())
    {
      restore_critical_section(count);
    }

  UNUSED(count);
}
