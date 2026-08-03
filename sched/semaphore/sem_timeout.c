/****************************************************************************
 * sched/semaphore/sem_timeout.c
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
#include <nuttx/nuttx.h>

#include <errno.h>

#include <nuttx/sched.h>

#include <nuttx/irq.h>
#include <nuttx/spinlock.h>

#include "semaphore/semaphore.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: nxsem_timeout
 *
 * Description:
 *   This function is called if the timeout elapses before before a
 *   semaphore is acquired.
 *
 * Input Parameters:
 *   arg - The argument that was provided when the timeout was configured.
 *
 * Returned Value:
 *   None
 *
 * Assumptions:
 *   - Called from the context of the timer interrupt handler.
 *
 ****************************************************************************/

uint64_t nxsem_timeout(FAR const hrtimer_t *timer, uint64_t expired)
{
  FAR struct tcb_s *wtcb  = container_of(timer, struct tcb_s, waittimer);
  irqstate_t        flags = enter_critical_section();

  /* It is also possible that an interrupt/context switch beat us to the
   * punch and already changed the task's state.
   */

  if (expired == timer->expired && wtcb->task_state == TSTATE_WAIT_SEM)
    {
      /* Cancel the semaphore wait */

      nxsem_wait_irq(wtcb, ETIMEDOUT);
    }

  leave_critical_section(flags);
  return 0;
}
