/****************************************************************************
 * sched/timer/timer_settime.c
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
#include <time.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#include <nuttx/irq.h>
#include <nuttx/nuttx.h>

#include "clock/clock.h"
#include "timer/timer.h"

#ifndef CONFIG_DISABLE_POSIX_TIMERS

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: timer_timespec_validate
 *
 * Description:
 *   Validate that a timespec has legal values.
 *
 * Input Parameters:
 *   ts - Pointer to the timespec to validate.
 *
 * Returned Value:
 *   true if the timespec is valid, false otherwise.
 *
 ****************************************************************************/

static inline_function bool
timer_timespec_validate(FAR const struct timespec *ts)
{
  DEBUGASSERT(ts);
  return ts->tv_sec >= 0 && ts->tv_nsec >= 0 &&
         ts->tv_nsec < NSEC_PER_SEC;
}

static inline_function bool
timer_delay_validate(FAR const struct timespec *ts)
{
  DEBUGASSERT(ts);
  return ts->tv_sec < HRTIMER_MAX_DELAY / NSEC_PER_SEC;
}

/****************************************************************************
 * Name: timer_signotify
 *
 * Description:
 *   This function basically re-implements nxsig_queue() so that the si_code
 *   can be correctly set to SI_TIMER
 *
 * Input Parameters:
 *   timer - A reference to the POSIX timer that just timed out
 *
 * Returned Value:
 *   None
 *
 * Assumptions:
 *   This function executes within the critical sections.
 *
 ****************************************************************************/

static inline void timer_signotify(FAR struct posix_timer_s *timer)
{
#ifdef CONFIG_SIG_EVTHREAD
  DEBUGVERIFY(nxsig_notification(timer->pt_owner, &timer->pt_event,
                                 SI_TIMER, &timer->pt_work));
#else
  DEBUGVERIFY(nxsig_notification(timer->pt_owner, &timer->pt_event,
                                 SI_TIMER, NULL));
#endif
}

/****************************************************************************
 * Name: timer_timeout
 *
 * Description:
 *   This function is called if the timeout elapses before the condition is
 *   signaled.
 *
 * Input Parameters:
 *   hrtimer - The hrtimer pointer
 *   expired - Expired time in nanoseconds.
 *
 * Assumptions:
 *   This function executes in the context of the timer interrupt.
 *   The hrtimer is not NULL.
 *
 ****************************************************************************/

static uint64_t timer_timeout(FAR const hrtimer_t *hrtimer, uint64_t expired)
{
  FAR struct posix_timer_s *timer = container_of(hrtimer,
                                                 struct posix_timer_s,
                                                 pt_timer);
  uint64_t delay      = clock_systime_nsec() - expired;
  uint64_t next_delay = 0u;

  DEBUGASSERT(timer);

  /* Send the specified signal to the specified task. Increment the
   * reference count on the timer first so that will not be deleted until
   * after the signal handler returns.
   * If the timer->pt_crefs <= 0, it means the timer has been deleted by the
   * user, we should return immediately.
   */

  if (timer->pt_crefs > 0u)
    {
      timer->pt_crefs++;
      timer_signotify(timer);

      /* Release the reference. timer_release will return nonzero if
       * the timer was not deleted. The thread that decreases the reference
       * count to zero will become the timer owner.
       */

      if (timer_release(timer, true) && timer->pt_delay)
        {
          /* If this is a repetitive timer, then restart the timer.
           * First, calculate the number of timer overruns and the next
           * expected tick.
           * The next expired tick frame can be computed as align up:
           * frame <- (elapsed_ticks + pt_delay) / pt_delay
           * For instance:
           *  |   pt_delay   |   pt_delay   |   pt_delay   | ... |
           *  ^ expected                    ^ ticks        ^ next
           * In this case, frame equals 3.
           * Then, pt_overrun <- frame - 1 and
           * the next expected <- expected + frame * pt_delay.
           * Assumption of correctness:
           * (delay + timer->pt_delay) should not overflow.
           */

          uint64_t frame = (delay + timer->pt_delay) / timer->pt_delay;
          next_delay     = frame * timer->pt_delay;
          atomic_set(&timer->pt_overrun, (int32_t)(frame - 1u));
          DEBUGASSERT(frame <= INT_MAX + 1u);

          /* To avoid the periodical hrtimer starving,
           * we have to round-up the minimal interval to NSEC_PER_TICK.
           */

          next_delay = next_delay < NSEC_PER_TICK ?
                       NSEC_PER_TICK : next_delay;
        }
    }

  DEBUGASSERT(next_delay <= HRTIMER_MAX_DELAY);

  return next_delay;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: timer_settime
 *
 * Description:
 *   The timer_settime() function sets the time until the next expiration of
 *   the timer specified by timerid from the it_value member of the value
 *   argument and arm the timer if the it_value member of value is non-zero.
 *   If the specified timer was already armed when timer_settime() is
 *   called, this call will reset the time until next expiration to the
 *   value specified. If the it_value member of value is zero, the timer
 *   will be disarmed. The effect of disarming or resetting a timer with
 *   pending expiration notifications is unspecified.
 *
 *   If the flag TIMER_ABSTIME is not set in the argument flags,
 *   timer_settime() will behave as if the time until next expiration is set
 *   to be equal to the interval specified by the it_value member of value.
 *   That is, the timer will expire in it_value nanoseconds from when the
 *   call is made. If the flag TIMER_ABSTIME is set in the argument flags,
 *   timer_settime() will behave as if the time until next expiration is set
 *   to be equal to the difference between the absolute time specified by
 *   the it_value member of value and the current value of the clock
 *   associated with timerid.  That is, the timer will expire when the clock
 *   reaches the value specified by the it_value member of value.  If the
 *   specified time has already passed, the function will succeed and the
 *   expiration notification will be made.
 *
 *   The reload value of the timer will be set to the value specified by the
 *   it_interval member of value.  When a timer is armed with a non-zero
 *   it_interval, a periodic (or repetitive) timer is specified.
 *
 *   Time values that are between two consecutive non-negative integer
 *   multiples of the resolution of the specified timer will be rounded up
 *   to the larger multiple of the resolution. Quantization error will not
 *   cause the timer to expire earlier than the rounded time value.
 *
 *   If the argument ovalue is not NULL, the timer_settime() function will
 *   store, in the location referenced by ovalue, a value representing the
 *   previous amount of time before the timer would have expired, or zero if
 *   the timer was disarmed, together with the previous timer reload value.
 *   Timers will not expire before their scheduled time.
 *
 * Input Parameters:
 *   timerid - The pre-thread timer, previously created by the call to
 *     timer_create(), to be be set.
 *   flags - Specifies characteristics of the timer (see above)
 *   value - Specifies the timer value to set
 *   ovalue - A location in which to return the time remaining from the
 *     previous timer setting.
 *
 * Returned Value:
 *   If the timer_settime() succeeds, a value of 0 (OK) will be returned.
 *   If an error occurs, the value -1 (ERROR) will be returned, and errno set
 *   to indicate the error.
 *
 *   EINVAL - The timerid argument does not correspond to an ID returned by
 *     timer_create() but not yet deleted by timer_delete().
 *   EINVAL - A value structure specified a nanosecond value less than zero
 *     or greater than or equal to 1000 million, and the it_value member of
 *     that structure did not specify zero seconds and nanoseconds.
 *
 * Assumptions:
 *
 ****************************************************************************/

int timer_settime(timer_t timerid, int flags,
                  FAR const struct itimerspec *value,
                  FAR struct itimerspec *ovalue)
{
  FAR struct posix_timer_s *timer = timer_gethandle(timerid);
  int ret = -EINVAL;

  /* Some sanity checks */

  if (timer != NULL && value != NULL &&
      timer_timespec_validate(&value->it_interval) &&
      timer_timespec_validate(&value->it_value) &&
      timer_delay_validate(&value->it_interval))
    {
      ret = OK;

      if (ovalue)
        {
          /* Get the remaining time */

          clock_nsec2time(&ovalue->it_value,
                          hrtimer_gettime(&timer->pt_timer));
          clock_nsec2time(&ovalue->it_interval, timer->pt_delay);
        }

      /* Disarm the timer and wait the callback to finish. */

      hrtimer_cancel_sync(&timer->pt_timer);

      /* Now we have the ownership of the timer, we can modify the
       * timer without any lock.
       */

      /* Cancel any pending notification */

      nxsig_cancel_notification(&timer->pt_work);

      /* If the it_value member of value is zero, the timer will not be
       * re-armed
       */

      if (value->it_value.tv_sec > 0 || value->it_value.tv_nsec > 0)
        {
          uint64_t delay = 0u;

          /* Setup up any repetitive timer */

          if (value->it_interval.tv_sec > 0 ||
              value->it_interval.tv_nsec > 0)
            {
              timer->pt_delay = clock_time2nsec(&value->it_interval);
            }
          else
            {
              timer->pt_delay = 0u;
            }

          /* Check if abstime is selected */

          if ((flags & TIMER_ABSTIME) != 0)
            {
              /* Calculate a delay corresponding to the
               * absolute time in 'value'.
               */

              struct timespec now;
              struct timespec reltime;

              nxclock_gettime(timer->pt_clock, &now);
              clock_timespec_subtract(&value->it_value, &now, &reltime);
              delay = clock_time2nsec(&reltime);
            }
          else
            {
              /* Calculate a delay assuming that 'value' holds the
               * relative time to wait.
               */

              delay = clock_time2nsec(&value->it_value);
            }

          /* Then start the timer */

          hrtimer_start(&timer->pt_timer, timer_timeout, delay,
                        HRTIMER_MODE_REL);
        }
    }

  if (ret < 0)
    {
      set_errno(-ret);
      ret = ERROR;
    }

  return ret;
}

#endif /* CONFIG_DISABLE_POSIX_TIMERS */
