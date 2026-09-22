/****************************************************************************
 * sched/signal/sig_cleanup.c
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
#include <nuttx/arch.h>
#include <nuttx/spinlock.h>

#include "signal/signal.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: nxsig_cleanup
 *
 * Description:
 *   Deallocate the signal-related lists of a task that is leaving the
 *   system, either because it exits or because it is restarted.  This
 *   covers the queues owned by the TCB itself as well as the group
 *   pending signals that are directed to this task.
 *
 ****************************************************************************/

void nxsig_cleanup(FAR struct tcb_s *stcb)
{
  FAR struct task_group_s *group = stcb->group;
  FAR sigpendq_t *sigpend;
  FAR sigpendq_t *next;
  FAR sigq_t *sigq;
  sq_queue_t pendingsig;
  irqstate_t flags;

  DEBUGASSERT(group != NULL);

  /* Deallocate all entries in the list of pending signal actions */

  while ((sigq = (FAR sigq_t *)sq_remfirst(&stcb->sigpendactionq)) != NULL)
    {
      nxsig_release_pendingsigaction(sigq);
    }

  /* Deallocate all entries in the list of posted signal actions */

  while ((sigq = (FAR sigq_t *)sq_remfirst(&stcb->sigpostedq)) != NULL)
    {
      nxsig_release_pendingsigaction(sigq);
    }

  /* Unlink the group pending signals that are directed to this task.  They
   * cannot be released while tg_lock is held because
   * nxsig_release_pendingsignal() may call kmm_free().
   */

  sq_init(&pendingsig);

  flags = spin_lock_irqsave(&group->tg_lock);

  for (sigpend = (FAR sigpendq_t *)group->tg_sigpendingq.head;
       sigpend != NULL; sigpend = next)
    {
      next = sigpend->flink;

      if (sigpend->tcb == stcb)
        {
          sq_rem((FAR sq_entry_t *)sigpend, &group->tg_sigpendingq);
          sq_addlast((FAR sq_entry_t *)sigpend, &pendingsig);
        }
    }

  spin_unlock_irqrestore(&group->tg_lock, flags);

  while ((sigpend = (FAR sigpendq_t *)sq_remfirst(&pendingsig)) != NULL)
    {
      nxsig_release_pendingsignal(sigpend);
    }

  /* Misc. signal-related clean-up */

  sigfillset(&stcb->sigprocmask);
  sigemptyset(&stcb->sigwaitmask);
}

/****************************************************************************
 * Name: nxsig_release
 *
 * Description:
 *   Deallocate all signal-related lists in a group.  This function is
 *   called only when the last thread leaves the group.  The caller is
 *   expected to have assured the critical section necessary to perform
 *   this action.
 *
 ****************************************************************************/

void nxsig_release(FAR struct task_group_s *group)
{
  FAR sigactq_t  *sigact;
  FAR sigpendq_t *sigpend;
  irqstate_t flags;

  /* Deallocate all entries in the list of signal actions */

  flags = spin_lock_irqsave(&group->tg_lock);
  while ((sigact = (FAR sigactq_t *)sq_remfirst(&group->tg_sigactionq))
         != NULL)
    {
      spin_unlock_irqrestore(&group->tg_lock, flags);
      nxsig_release_action(sigact);
      flags = spin_lock_irqsave(&group->tg_lock);
    }

  /* Deallocate all entries in the list of pending signals */

  while ((sigpend = (FAR sigpendq_t *)sq_remfirst(&group->tg_sigpendingq))
         != NULL)
    {
      spin_unlock_irqrestore(&group->tg_lock, flags);
      nxsig_release_pendingsignal(sigpend);
      flags = spin_lock_irqsave(&group->tg_lock);
    }

  spin_unlock_irqrestore(&group->tg_lock, flags);
}
