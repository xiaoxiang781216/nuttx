/****************************************************************************
 * sched/semaphore/sem_protect.c
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
#include <errno.h>

#include "sched/sched.h"
#include "semaphore/semaphore.h"

#ifdef CONFIG_PRIORITY_PROTECT

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: nxsem_protect_wait
 *
 * Description:
 *   This function attempts to lock the protected semaphore, set the
 *   holder tcb priority to ceiling priority.
 *
 *   This is an internal OS interface.  It is functionally equivalent to
 *   sem_wait except that:
 *
 *   - It is not a cancellation point, and
 *   - It does not modify the errno value.
 *
 * Input Parameters:
 *   sem - Semaphore descriptor.
 *
 * Returned Value:
 *   This is an internal OS interface and should not be used by applications.
 *   It follows the NuttX internal error return policy:  Zero (OK) is
 *   returned on success.  A negated errno value is returned on failure.
 *   Possible returned errors:
 *
 *   - EINVAL:  Invalid attempt to get the semaphore
 *
 ****************************************************************************/

int nxsem_protect_wait(FAR sem_t *sem)
{
  if ((sem->flags & SEM_PRIO_MASK) == SEM_PRIO_PROTECT)
    {
      FAR struct tcb_s *rtcb = this_task();

      if (rtcb->sched_priority > sem->ceiling)
        {
          return -EINVAL;
        }

      sem->saved = rtcb->sched_priority;
      rtcb->sched_priority = sem->ceiling;
    }

  return OK;
}

/****************************************************************************
 * Name: nxsem_protect_post
 *
 * Description:
 *   This function handles priority restoration for semaphores with
 *   PTHREAD_PRIO_PROTECT protocol. It implements a clever technique to
 *   ensure that when a thread unlocks a PRIO_PROTECT mutex, it is not
 *   moved to the tail of the scheduling queue at its priority level.
 *
 *   The key insight: by temporarily boosting the priority and then
 *   immediately restoring it, we trigger the scheduler to re-evaluate
 *   the thread's position in the ready queue without actually changing
 *   its execution priority.
 *
 *   Workflow:
 *   1. Temporarily set priority to (saved_priority + 1) - This causes
 *      the scheduler to keep the thread at the head of ready queue
 *   2. Immediately restore to saved_priority - Thread maintains original
 *      priority but keeps its queue position
 *   3. This satisfies POSIX requirement: "When a thread unlocks a mutex
 *      with PRIO_PROTECT protocol, it shall not be moved to the tail of
 *      the scheduling queue at its priority"
 *
 * Input Parameters:
 *   sem - Pointer to the semaphore structure
 *
 * Returned Value:
 *   None
 *
 * Assumptions:
 *   - Called from appropriate context with proper synchronization
 *   - sem->saved contains the original priority to restore
 *   - This technique relies on the specific behavior of NuttX scheduler
 *
 ****************************************************************************/

void nxsem_protect_post(FAR sem_t *sem)
{
  if (sem->saved > 0)
    {
      if (sem->saved < SCHED_PRIORITY_MAX)
        {
          /* Temporary priority boost to trigger scheduler re-evaluation
           * and prevent moving to the tail of the scheduling queue
           */

          nxsched_set_priority(this_task(), sem->saved + 1);

          /* Restore original priority but keep queue position */

          this_task()->sched_priority = sem->saved;
        }
      else
        {
          /* Already at max priority, just trigger scheduler re-evaluation */

          nxsched_set_priority(this_task(), sem->saved);
        }

      /* Clear saved priority */

      sem->saved = 0;
    }
}

#endif /* CONFIG_PRIORITY_PROTECT */
