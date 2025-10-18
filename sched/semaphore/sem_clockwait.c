/****************************************************************************
 * sched/semaphore/sem_clockwait.c
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

int nxsem_clockwait_slow(FAR sem_t *sem, clockid_t clockid,
                    FAR const struct timespec *abstime)
{
  FAR struct tcb_s *rtcb = this_task();
  irqstate_t flags;
  int ret = -EINVAL;

  DEBUGASSERT(sem != NULL && abstime != NULL);
  DEBUGASSERT(up_interrupt_context() == false);

  /* We will disable interrupts until we have completed the semaphore
   * wait.  We need to do this (as opposed to just disabling pre-emption)
   * because there could be interrupt handlers that are asynchronously
   * posting semaphores and to prevent race conditions with watchdog
   * timeout.  This is not too bad because interrupts will be re-
   * enabled while we are blocked waiting for the semaphore.
   */

  if (abstime->tv_nsec >= 0 && abstime->tv_nsec < 1000000000)
    {
      flags = enter_critical_section();

      if (clockid == CLOCK_REALTIME)
        {
          wd_start_realtime(&rtcb->waitdog, abstime,
                            nxsem_timeout, (uintptr_t)rtcb);
        }
      else
        {
          wd_start_abstime(&rtcb->waitdog, abstime,
                            nxsem_timeout, (uintptr_t)rtcb);
        }

      /* Now perform the blocking wait.  If nxsem_wait() fails, the
       * negated errno value will be returned below.
       */

      ret = nxsem_wait_slow(sem);

      leave_critical_section(flags);

      /* Stop the watchdog timer */

      wd_cancel(&rtcb->waitdog);
    }

  return ret;
}
