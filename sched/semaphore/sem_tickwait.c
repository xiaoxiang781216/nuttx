/****************************************************************************
 * sched/semaphore/sem_tickwait.c
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

#include <stdint.h>
#include <unistd.h>
#include <time.h>
#include <assert.h>
#include <errno.h>
#include <debug.h>

#include <nuttx/irq.h>
#include <nuttx/arch.h>
#include <nuttx/wdog.h>

#include "sched/sched.h"
#include "clock/clock.h"
#include "semaphore/semaphore.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: nxsem_tickwait_slow
 *
 * Description:
 *   This function is a lighter weight version of sem_timedwait().  It is
 *   non-standard and intended only for use within the RTOS.
 *
 * Input Parameters:
 *   sem     - Semaphore object
 *   delay   - Ticks to wait from the start time until the semaphore is
 *             posted.  If ticks is zero, then this function is equivalent
 *             to sem_trywait().
 *
 * Returned Value:
 *   This is an internal OS interface, not available to applications, and
 *   hence follows the NuttX internal error return policy:  Zero (OK) is
 *   returned on success.  A negated errno value is returned on failure:
 *
 *     -ETIMEDOUT is returned on the timeout condition.
 *     -ECANCELED may be returned if the thread is canceled while waiting.
 *
 ****************************************************************************/

int nxsem_tickwait_slow(FAR sem_t *sem, clock_t delay)
{
  FAR struct tcb_s *rtcb = this_task();
  irqstate_t flags;
  int ret = -EINVAL;

  DEBUGASSERT(sem != NULL && up_interrupt_context() == false);

  /* Try to take the semaphore without waiting. */

  if (delay != 0u)
    {
      /* Start the watchdog with interrupts still disabled */

      flags = enter_critical_section();

      wd_start(&rtcb->waitdog, delay, nxsem_timeout, (uintptr_t)rtcb);

      /* Now perform the blocking wait */

      ret = nxsem_wait_slow(sem);

      /* We can now restore interrupts */

      leave_critical_section(flags);

      /* Stop the watchdog timer */

      wd_cancel(&rtcb->waitdog);
    }

  return ret;
}
