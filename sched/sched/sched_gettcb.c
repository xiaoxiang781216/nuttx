/****************************************************************************
 * sched/sched/sched_gettcb.c
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

#include <sys/types.h>

#include <sched.h>

#include <nuttx/irq.h>
#include <nuttx/semaphore.h>

#include "sched/sched.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: nxsched_get_tcb/nxsched_put_tcb/nxsched_get_tcb_by_index
 *
 * Description:
 *   Given a task ID,
 *   Obtain a valid TCB and increment the corresponding reference count to
 *   prevent it from being released. nxsched_get_tcb and nxsched_put_tcb
 *   must be called in pairs to ensure the proper release of the TCB.
 *
 ****************************************************************************/

FAR struct tcb_s *nxsched_get_tcb(pid_t pid)
{
  FAR struct tcb_s *ret = NULL;
  irqstate_t flags;
  int hash_ndx;

  flags = spin_lock_irqsave_notrace(&g_pidhashlock);
  if (g_pidhash != NULL && pid >= 0)
    {
      /* The test and the return setup should be atomic.  This still does
       * not provide proper protection if the recipient of the TCB does not
       * also protect against the task associated with the TCB from
       * terminating asynchronously.
       */

      /* Get the hash_ndx associated with the pid */

      hash_ndx = PIDHASH(pid);

      /* Verify that the correct TCB was found. */

      if (g_pidhash[hash_ndx] != NULL && pid == g_pidhash[hash_ndx]->pid)
        {
          /* Return the TCB associated with this pid (if any) */

          ret = g_pidhash[hash_ndx];
        }
    }

  if (ret && ret != running_task())
    {
      if (!up_interrupt_context())
        {
          /* If we are in the thread context, after obtaining a reference to
           * another task, we may not be able to release this reference
           * immediately. The purpose of refs is also to prevent the
           * situation where this_task is killed and thus unable to release
           * the references to other tasks. We need to record the total
           * number of references that this_task makes to other tasks.
           */

          atomic_fetch_add(&this_task()->refs, 1);
        }

      atomic_fetch_add(&ret->refs, 1);
    }

  spin_unlock_irqrestore_notrace(&g_pidhashlock, flags);

  return ret;
}

void nxsched_put_tcb(FAR struct tcb_s *tcb)
{
  if (tcb && tcb != running_task())
    {
      DEBUGASSERT(atomic_read(&tcb->refs) > 0);

      /* tcb may in EXIT_PROCESSING */

      if (atomic_fetch_sub(&tcb->refs, 1) == 1u)
        {
          nxsem_post(&tcb->exit_sem);
        }

      if (!up_interrupt_context())
        {
          tcb = this_task();

          DEBUGASSERT(atomic_read(&tcb->refs) > 0);

          /* this_task may be killed and in KILL_PROCESSING */

          if (atomic_fetch_sub(&tcb->refs, 1) == 1u &&
              (atomic_read(&tcb->flags) & TCB_FLAG_KILL_PROCESSING))
            {
              nxsem_post(&tcb->exit_sem);

              /* If the TCB is already in the exiting state, we
               * should allow the exiting task to execute normally first.
               * We stop the execution here.
               *
               * If it continues to run, it may happen that we obtain
               * the TCB again and it gets killed.
               */

              for (; ; )
                {
                  usleep(1000);
                }
            }
        }
    }
}

FAR struct tcb_s *nxsched_get_tcb_by_index(int index)
{
  FAR struct tcb_s *ret = NULL;
  irqstate_t flags;

  flags = spin_lock_irqsave_notrace(&g_pidhashlock);
  ret = g_pidhash[index];

  if (ret && ret != running_task())
    {
      if (!up_interrupt_context())
        {
          /* If we are in the thread context, after obtaining a reference to
           * another task, we may not be able to release this reference
           * immediately. The purpose of refs is also to prevent the
           * situation where this_task is killed and thus unable to release
           * the references to other tasks. We need to record the total
           * number of references that this_task makes to other tasks.
           */

          atomic_fetch_add(&this_task()->refs, 1);
        }

      atomic_fetch_add(&ret->refs, 1);
    }

  spin_unlock_irqrestore_notrace(&g_pidhashlock, flags);

  return ret;
}

FAR struct tcb_s *nxsched_get_childtcb(FAR struct tcb_s *parent)
{
  FAR struct tcb_s *ret = NULL;
  irqstate_t flags;
  int hash_ndx;

  /* The test and the return setup should be atomic.  This still does
   * not provide proper protection if the recipient of the TCB does not
   * also protect against the task associated with the TCB from
   * terminating asynchronously.
   */

  flags = spin_lock_irqsave_notrace(&g_pidhashlock);

  for (hash_ndx = 0; hash_ndx < g_npidhash; hash_ndx++)
    {
      /* Verify that the correct TCB was found. */

      if (g_pidhash[hash_ndx] &&
          parent->group->tg_pid == g_pidhash[hash_ndx]->group->tg_ppid)
        {
          /* Return the TCB associated with this pid (if any) */

          ret = g_pidhash[hash_ndx];
          break;
        }
    }

  if (ret && ret != running_task())
    {
      if (!up_interrupt_context())
        {
          /* If we are in the thread context, after obtaining a reference to
           * another task, we may not be able to release this reference
           * immediately. The purpose of refs is also to prevent the
           * situation where this_task is killed and thus unable to release
           * the references to other tasks. We need to record the total
           * number of references that this_task makes to other tasks.
           */

          atomic_fetch_add(&this_task()->refs, 1);
        }

      atomic_fetch_add(&ret->refs, 1);
    }

  spin_unlock_irqrestore_notrace(&g_pidhashlock, flags);

  return ret;
}
