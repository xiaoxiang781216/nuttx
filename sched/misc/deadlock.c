/****************************************************************************
 * sched/misc/deadlock.c
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

#include <nuttx/mutex.h>
#include <nuttx/sched.h>
#include <nuttx/spinlock.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

struct deadlock_info_s
{
  FAR pid_t *holders;
  size_t arraylen;
  size_t holdercnt;
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: getholder
 ****************************************************************************/

static pid_t getholder(FAR struct tcb_s *tcb)
{
  if (tcb == NULL)
    {
      return INVALID_PROCESS_ID;
    }

  /* Spinlock deadlock only occurs in RUNNING threads,
   * while mutex deadlock only occurs in WAIT_SEM threads.
   */

  if (tcb->task_state == TSTATE_WAIT_SEM)
    {
      FAR sem_t *sem = tcb->waitobj;
      if (sem != NULL && (sem->flags & SEM_TYPE_MUTEX) != 0)
        {
          return nxmutex_get_holder((FAR mutex_t *)sem);
        }
    }
#ifdef CONFIG_SPINLOCK_DEBUG
  else if (tcb->wait_spinlock)
    {
      return spinlock_get_holder(tcb->wait_spinlock);
    }
#endif

  return INVALID_PROCESS_ID;
}

/****************************************************************************
 * Name: collect_deadlock
 ****************************************************************************/

static void collect_deadlock(FAR struct tcb_s *tcb, FAR void *arg)
{
  FAR struct deadlock_info_s *info = arg;
  pid_t holder = getholder(tcb);
  size_t index;

  if (holder == INVALID_PROCESS_ID)
    {
      return;
    }

  /* Check previous deadlock holder list. */

  for (index = 0; index < info->holdercnt; index++)
    {
      if (info->holders[index] == tcb->pid)
        {
          return;
        }
    }

  /* Append the holders for this tcb to list. */

  for (index = info->holdercnt; index < info->arraylen; index++)
    {
      size_t i;

      /* Check if this holder is already held. */

      for (i = info->holdercnt; i < index; i++)
        {
          if (info->holders[i] == holder)
            {
              info->holdercnt = index;
              return;
            }
        }

      /* Add holder to list and continue to holder's holder. */

      info->holders[index] = holder;
      tcb = nxsched_get_tcb(holder);
      DEBUGASSERT(tcb != NULL);
      holder = getholder(tcb);
      nxsched_put_tcb(tcb);
      if (holder == INVALID_PROCESS_ID)
        {
          /* If this holder isn't waiting for lock, it's over. */

          break;
        }
    }

  /* If no deadlock, clear the holders of this tcb. */

  memset(&info->holders[info->holdercnt], 0,
         (info->arraylen - info->holdercnt) * sizeof(pid_t));
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: nxsched_collect_deadlock
 *
 * Description:
 *   Check if there is a deadlock and get the thread pid of the deadlock.
 *
 * Input parameters:
 *   pid   - The array to store the thread pid of the deadlock.
 *   count - The size of the pid array.
 *
 * Returned Value:
 *   The number of thread deadlocks.
 *
 ****************************************************************************/

size_t nxsched_collect_deadlock(FAR pid_t *pid, size_t count)
{
  struct deadlock_info_s info;

  info.holders = pid;
  info.arraylen = count;
  info.holdercnt = 0;
  nxsched_foreach(collect_deadlock, &info);
  return info.holdercnt;
}
