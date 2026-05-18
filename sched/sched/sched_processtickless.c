/****************************************************************************
 * sched/sched/sched_processtickless.c
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
#include <nuttx/compiler.h>

#include <time.h>
#include <sys/param.h>
#include <assert.h>
#include <debug.h>

#if CONFIG_RR_INTERVAL > 0
#  include <sched.h>
#  include <nuttx/arch.h>
#endif

#include "sched/sched.h"
#include "wdog/wdog.h"
#include "hrtimer/hrtimer.h"
#include "clock/clock.h"
#include "nuttx/seqlock.h"

#ifdef CONFIG_CLOCK_TIMEKEEPING
#  include "clock/clock_timekeeping.h"
#endif

#ifdef CONFIG_SCHED_TICKLESS

/****************************************************************************
 * Public Data
 ****************************************************************************/

#ifdef CONFIG_SCHED_TICKLESS_LIMIT_MAX_SLEEP
/* By default, the RTOS tickless logic assumes that range of times that can
 * be represented by the underlying hardware time is so large that no special
 * precautions need to taken.  That is not always the case.  If there is a
 * limit to the maximum timing interval that be represented by the timer,
 * then that limit must be respected.
 *
 * If CONFIG_SCHED_TICKLESS_LIMIT_MAX_SLEEP is defined, then a 32-bit global
 * variable called g_oneshot_maxticks variable is enabled. The variable
 * is initialized by platform-specific logic at runtime to the maximum delay
 * that the timer can wait (in microseconds).  The RTOS tickless logic will
 * then limit all requested delays to this value (in ticks).
 */

DEFINE_PER_CPU_BMP(uint32_t, g_oneshot_maxticks) = UINT32_MAX;
#define g_oneshot_maxticks this_cpu_var_bmp(g_oneshot_maxticks)
#endif

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

#if CONFIG_RR_INTERVAL > 0 || defined(CONFIG_SCHED_SPORADIC)
static clock_t nxsched_cpu_scheduler(int cpu, clock_t ticks,
                                     clock_t elapsed, bool noswitches);
#endif
static clock_t nxsched_process_scheduler(clock_t ticks, clock_t elapsed,
                                         bool noswitches);
static clock_t nxsched_process_event(clock_t ticks, bool noswitches);

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* This is the tick that the timer was stopped.  All future times are
 * calculated against this time.  It must be valid at all times when
 * the timer is not running.
 */

static DEFINE_PER_CPU_BSS_BMP(clock_t, g_timer_tick);
#define g_timer_tick this_cpu_var_bmp(g_timer_tick)

/* HRTimer for scheduler event. */

static DEFINE_PER_CPU_BSS_BMP(hrtimer_t, g_sched_event);
#define g_sched_event this_cpu_var_bmp(g_sched_event)

static DEFINE_PER_CPU_BSS_BMP(bool, g_in_sched_expiration);
#define g_in_sched_expiration this_cpu_var_bmp(g_in_sched_expiration)

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#if !defined(CONFIG_CLOCK_TIMEKEEPING) && !defined(CONFIG_ALARM_ARCH) && \
    !defined(CONFIG_TIMER_ARCH)
int up_timer_gettick(FAR clock_t *ticks)
{
  struct timespec ts;
  int ret;
  ret = up_timer_gettime(&ts);
  *ticks = clock_time2ticks_floor(&ts);
  return ret;
}
#endif

static clock_t nxsched_process_event(clock_t ticks, bool noswitches)
{
  clock_t elapsed = 0u;

  /* Ensure the g_timer_tick is monotonic. */

  if (clock_compare(g_timer_tick, ticks))
    {
      /* Calculate the elapsed time and update clock tickbase. */

      elapsed      = ticks - g_timer_tick;
      g_timer_tick = ticks;
    }

  /* Process the timer ticks and set up the next interval (or not) */

  return nxsched_process_scheduler(ticks, elapsed, noswitches);
}

static uint64_t nxsched_hrtimer_expiration(const hrtimer_t *timer,
                                           uint64_t expired)
{
  clock_t    delay;
  clock_t    now   = div_const(expired, NSEC_PER_TICK);
  irqstate_t flags = enter_critical_section();

  g_in_sched_expiration = true;
  delay = nxsched_process_event(now, false);
  g_in_sched_expiration = false;

  leave_critical_section(flags);

  return delay != CLOCK_MAX ? TICK2NSEC((uint64_t)delay) : 0u;
}

/****************************************************************************
 * Name:  nxsched_cpu_scheduler
 *
 * Description:
 *   Check for operations specific to scheduling policy of the currently
 *   active task on a single CPU.
 *
 * Input Parameters:
 *   cpu     - The CPU that we are performing the scheduler operations on.
 *   ticks   - The number of ticks that represent current time.
 *   elapsed - The number of ticks that have elapsed on the interval timer.
 *   noswitches - True: Can't do context switches now.
 *
 * Returned Value:
 *   The number if ticks remaining until the next time slice expires.
 *   CLOCK_MAX is returned if there is no time slicing (i.e., the task at
 *   the head of the ready-to-run list does not support round robin
 *   scheduling).
 *
 *   The value one may returned under certain circumstances that probably
 *   can't happen.  The value one is the minimal timer setup and it means
 *   that a context switch is needed now, but cannot be performed because
 *   noswitches == true.
 *
 ****************************************************************************/

#if CONFIG_RR_INTERVAL > 0 || defined(CONFIG_SCHED_SPORADIC)
static clock_t nxsched_cpu_scheduler(int cpu, clock_t ticks,
                                     clock_t elapsed, bool noswitches)
{
  FAR struct tcb_s *rtcb = current_task(cpu);
  FAR struct tcb_s *ntcb = current_task(cpu);
  clock_t ret = CLOCK_MAX;

#if CONFIG_RR_INTERVAL > 0
  /* Check if the currently executing task uses round robin scheduling. */

  if ((atomic_read(&rtcb->flags) & TCB_FLAG_POLICY_MASK) ==
      TCB_FLAG_SCHED_RR)
    {
      /* Yes, check if the currently executing task has exceeded its
       * timeslice.
       */

      ret = nxsched_process_roundrobin(rtcb, elapsed, noswitches);
    }
#endif

#ifdef CONFIG_SCHED_SPORADIC
  /* Check if the currently executing task uses sporadic scheduling. */

  if ((atomic_read(&rtcb->flags) & TCB_FLAG_POLICY_MASK) ==
      TCB_FLAG_SCHED_SPORADIC)
    {
      FAR struct sporadic_s *sporadic = rtcb->sporadic;
      DEBUGASSERT(sporadic);

      /* Save the last time that the scheduler ran.  This time was saved
       * higher in the calling hierarchy but cannot be applied until here.
       * That is because there are cases that context switches may occur
       * between then and before we get here.  So we can't positive of
       * which task TCB to save the time in until we are here and
       * committed to updating the scheduler for this TCB.
       */

      sporadic->eventtime = ticks;

      /* Yes, check if the currently executing task has exceeded its
       * budget.
       */

      ret = nxsched_process_sporadic(rtcb, elapsed, noswitches);
    }
#endif

  /* If a context switch occurred, then need to return delay remaining for
   * the new task at the head of the ready to run list.
   */

  ntcb = current_task(cpu);

  /* Check if the new task at the head of the ready-to-run has changed. */

  if (rtcb != ntcb)
    {
      /* Recurse just to get the correct return value */

      return nxsched_process_scheduler(ticks, 0, true);
    }

  /* Returning zero means that there is no interesting event to be timed */

  return ret;
}
#endif

/****************************************************************************
 * Name: nxsched_process_scheduler
 *
 * Description:
 *   Check for operations specific to scheduling policy of the currently
 *   active task on a single CPU.
 *
 * Input Parameters:
 *   ticks - The number of ticks that represent current time.
 *   elapsed - The number of ticks that have elapsed on the interval timer.
 *   noswitches - True: Can't do context switches now.
 *
 * Returned Value:
 *   The number if ticks remaining until the next time slice expires.
 *   CLOCK_MAX is returned if there is no time slicing (i.e., the task at
 *   the head of the ready-to-run list does not support round robin
 *   scheduling).
 *
 *   The value one may returned under certain circumstances that probably
 *   can't happen.  The value one is the minimal timer setup and it means
 *   that a context switch is needed now, but cannot be performed because
 *   noswitches == true.
 *
 ****************************************************************************/

static inline_function
clock_t nxsched_process_scheduler(clock_t ticks, clock_t elapsed,
                                  bool noswitches)
{
  clock_t minslice = CLOCK_MAX;
#if CONFIG_RR_INTERVAL > 0 || defined(CONFIG_SCHED_SPORADIC)
  irqstate_t flags = enter_critical_section();
  clock_t timeslice;
  int i;

  /* Single CPU case:
   * For nested interrupts, higher IRQs may interrupt nxsched_cpu_scheduler()
   * but nxsched_cpu_scheduler() requires that interrupts be disabled.
   * We are in ISR context, no meaning we are disabled the interrupts.
   *
   * SMP case:
   * enter_critical_section() does much more than just disable interrupts on
   * the local CPU; it also manages spinlocks to assure the stability of the
   * TCB that we are manipulating.
   */

  /* Perform scheduler operations on all CPUs */

  for (i = 0; i < CONFIG_SMP_NCPUS; i++)
    {
      timeslice = nxsched_cpu_scheduler(i, ticks, elapsed, noswitches);
      minslice  = MIN(timeslice, minslice);
    }

  leave_critical_section(flags);
#endif

  return minslice;
}

/****************************************************************************
 * Name:  nxsched_timer_start
 *
 * Description:
 *   Start the interval timer.
 *
 * Input Parameters:
 *   ticks - The number of ticks defining the timer interval to setup.
 *   interval - The number of ticks to use when setting up the next timer.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static inline_function
void nxsched_timer_start(clock_t ticks, clock_t interval)
{
  if (interval != CLOCK_MAX)
    {
      DEBUGASSERT((uint64_t)interval <= UINT32_MAX &&
                  (uint64_t)interval <= HRTIMER_MAX_DELAY /
                                        NSEC_PER_TICK);
      hrtimer_start(&g_sched_event, nxsched_hrtimer_expiration,
                    TICK2NSEC((uint64_t)ticks + interval),
                    HRTIMER_MODE_ABS);
    }
  else
    {
      hrtimer_cancel(&g_sched_event);
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: nxsched_tick_expiration
 *
 * Description:
 *   If CONFIG_SCHED_TICKLESS is defined, then this function is provided by
 *   the RTOS base code and called from platform-specific code when the
 *   interval timer used to implement the tick-less OS expires.
 *
 * Input Parameters:
 *   None.
 *
 * Returned Value:
 *   Base code implementation assumes that this function is called from
 *   interrupt handling logic with interrupts disabled.
 *
 * Note:
 *   The current SCHED_RR implementation has an issue: if a round-robin task
 *   is preempted, its timeslice counter does not decrement properly.
 *   Therefore, we must trigger the scheduler on each timer expiration to
 *   minimize the occurrence of this problem.
 *   This workaround can be removed once the SCHED_RR behavior is fixed.
 *
 ****************************************************************************/

void nxsched_process_timer(void)
{
  uint64_t        nsec;
  struct timespec ts =
    {
      0
    };

  up_timer_gettime(&ts);
  nsec = clock_time2nsec(&ts);

  hrtimer_process(nsec);

#  if CONFIG_RR_INTERVAL > 0
  /* Workaround for SCHED_RR, see the note. */

  clock_t    now   = div_const(nsec, NSEC_PER_TICK);
  irqstate_t flags = enter_critical_section();
  nxsched_timer_start(now, nxsched_process_event(now, true));
  leave_critical_section(flags);
#  endif
}

/****************************************************************************
 * Name:  nxsched_reassess_timer
 *
 * Description:
 *   It is necessary to re-assess the timer interval in several
 *   circumstances:
 *
 *   - If the watchdog at the head of the expiration list changes (or if its
 *     delay changes.  This can occur as a consequence of the actions of
 *     hrtimer_start() or hrtimer_cancel().
 *   - When pre-emption is re-enabled.  A previous time slice may have
 *     expired while pre-emption was enabled and now needs to be executed.
 *
 *   In the original design, it was also planned that
 *   nxsched_reassess_timer() be called whenever there was a change at the
 *   head of the ready-to-run list.  That call was intended to establish a
 *   new time-slice for the newly activated task or to stop the timer if
 *   time-slicing is no longer needed.  However, it turns out that that
 *   solution is too fragile:  The system is too vulnerable at the time
 *   that the ready-to-run list is modified in order to muck with timers.
 *
 *   The kludge/work-around is simple to keep the timer running all of the
 *   time with an interval of no more than the timeslice interval.  If we
 *   do this, then there is really no need to do anything on context
 *   switches.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 * Note:
 *   This function is called from the critical section
 *
 ****************************************************************************/

void nxsched_reassess_timer(void)
{
  clock_t next;
  clock_t now;

  up_timer_gettick(&now);
  next = nxsched_process_event(now, true);

  if (!g_in_sched_expiration)
    {
      nxsched_timer_start(now, next);
    }
}

/****************************************************************************
 * Name:  nxsched_get_next_expired
 *
 * Description:
 *   Get the time remaining until the next timer expiration.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   The time remaining until the next timer expiration.
 *
 * Note:
 *   There is two special returned value
 *   Zero     : The timer already expired or in processing.
 *   CLOCK_MAX: There is no working timer.
 *
 ****************************************************************************/

clock_t nxsched_get_next_expired(void)
{
  return wd_get_next_expire(clock_systime_ticks());
}

#endif /* CONFIG_SCHED_TICKLESS */
