/****************************************************************************
 * sched/signal/sig_notification.c
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

#include <inttypes.h>
#include <string.h>
#include <signal.h>
#include <assert.h>
#include <debug.h>

#include <nuttx/signal.h>

#include "sched/sched.h"
#include "signal/signal.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: nxsig_notification
 *
 * Description:
 *   Notify a client an event via either a signal or function call
 *   base on the sigev_notify field.
 *
 * Input Parameters:
 *   pid   - The task/thread ID a the client thread to be signaled.
 *   event - The instance of struct sigevent that describes how to signal
 *           the client.
 *   code  - Source: SI_USER, SI_QUEUE, SI_TIMER, SI_ASYNCIO, or SI_MESGQ
 *   work  - The work structure to queue.  Must be non-NULL if
 *           event->sigev_notify == SIGEV_THREAD.  Ignored if
 *           CONFIG_SIG_EVTHREAD is not defined.
 *
 * Returned Value:
 *   This is an internal OS interface and should not be used by applications.
 *   It follows the NuttX internal error return policy:  Zero (OK) is
 *   returned on success.  A negated errno value is returned on failure.
 *
 ****************************************************************************/

int nxsig_notification(pid_t pid, FAR struct sigevent *event,
                       int code, FAR struct sigwork_s *work)
{
  int ret = event->sigev_notify == SIGEV_NONE ? OK : -ENOSYS;

  sinfo("pid=%" PRIu16 " signo=%d code=%d sival_ptr=%p\n",
        pid, event->sigev_signo, code, event->sigev_value.sival_ptr);

  /* Notify client via a signal? */

  if (event->sigev_notify & SIGEV_SIGNAL)
    {
      siginfo_t info;
      bool      thread = false;

      /* Yes.. Create the siginfo structure */

      info.si_signo  = event->sigev_signo;
      info.si_code   = code;
      info.si_errno  = OK;
#ifdef CONFIG_SCHED_HAVE_PARENT
      info.si_pid    = this_task()->pid;
      info.si_status = OK;
#endif
      info.si_user   = NULL;

      /* Some compilers (e.g., SDCC), do not permit assignment of aggregates.
       * Use of memcpy() is overkill;  We could just copy the larger of the
       * int and FAR void * members in the union.  memcpy(), however, does
       * not require that we know which is larger.
       */

      memcpy(&info.si_value, &event->sigev_value, sizeof(union sigval));

      /* SIGEV_THREAD_ID currently used only by POSIX timer. */

      if (event->sigev_notify & SIGEV_THREAD_ID)
        {
          thread = true;
          pid = event->sigev_notify_thread_id;
        }

      /* Send the signal */

      ret = nxsig_dispatch(pid, &info, thread);
    }

#ifdef CONFIG_SIG_EVTHREAD
  /* Notify the client via a function call */

  else if (event->sigev_notify & SIGEV_THREAD)
    {
      /* The sigev_notify_function_t and worker_t types both take a single
       * argument of the same width, so they are ABI-compatible.  Use union
       * type-punning to convert the function pointer without triggering
       * -Wcast-function-type or invoking undefined behavior.  Similarly,
       * reinterpret the sigev_value as a void * argument; the notification
       * function will receive it as its union sigval parameter.
       */

      worker_t  worker = (worker_t)event->sigev_notify_function;
      FAR void *arg    = event->sigev_value.sival_ptr;

      /* Then queue the work, invoking the notification function directly */

      ret = work_queue(SIG_EVTHREAD_WORK, work, worker, arg, 0);
    }
#endif

  return ret;
}
