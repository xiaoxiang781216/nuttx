/****************************************************************************
 * sched/sched/sched_critmonitor.c
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
#include <nuttx/sched_note.h>
#include <nuttx/trace.h>

#include <sys/types.h>
#include <sched.h>
#include <assert.h>
#include <debug.h>
#include <string.h>
#include <time.h>

#include "sched/sched.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#if CONFIG_SCHED_CRITMONITOR_MAXTIME_PREEMPTION > 0
#  define CHECK_PREEMPTION(tcb, elapsed) \
     do \
       { \
         if (!is_idle_task(tcb) && \
             elapsed > g_critmon_maxtime[CRITMON_PREEMPTION]) \
           { \
             CRITMONITOR_PANIC(NXTAG_SCHED, \
                               DIAG_SCHED_PREEMPTION_TIMEOUT, \
                               "Type:PREEMPTION,Pid:%d," \
                               "Elapsed:%juus,Threshold:%juus", \
                               (tcb)->pid, PERF_COUNT2US(elapsed), \
                               (uintmax_t) \
                               CONFIG_SCHED_CRITMONITOR_MAXTIME_PREEMPTION); \
           } \
       } \
     while (0)
#else
#  define CHECK_PREEMPTION(tcb, elapsed)
#endif

#if CONFIG_SCHED_CRITMONITOR_MAXTIME_IRQDISABLE > 0
#  define CHECK_IRQDIS(tcb, elapsed) \
     do \
       { \
         if (!is_idle_task(tcb) && \
             elapsed > g_critmon_maxtime[CRITMON_IRQDISABLE]) \
           { \
             CRITMONITOR_PANIC(NXTAG_SCHED, \
                               DIAG_SCHED_IRQDISABLE_TIMEOUT, \
                               "Type:IRQDISABLE,Pid:%d," \
                               "Elapsed:%juus,Threshold:%juus", \
                               (tcb)->pid, PERF_COUNT2US(elapsed), \
                               (uintmax_t) \
                               CONFIG_SCHED_CRITMONITOR_MAXTIME_IRQDISABLE); \
           } \
       } \
     while (0)
#else
#  define CHECK_IRQDIS(tcb, elapsed)
#endif

#if CONFIG_SCHED_CRITMONITOR_MAXTIME_BUSYWAIT > 0
#  define CHECK_BUSYWAIT(tcb, elapsed) \
     do \
       { \
         if (!is_idle_task(tcb) && \
             elapsed > g_critmon_maxtime[CRITMON_BUSYWAIT]) \
           { \
             CRITMONITOR_PANIC(NXTAG_SCHED, \
                               DIAG_SCHED_BUSYWAIT_TIMEOUT, \
                               "Type:BUSYWAIT,Pid:%d," \
                               "Elapsed:%juus,Threshold:%juus", \
                               (tcb)->pid, PERF_COUNT2US(elapsed), \
                               (uintmax_t) \
                               CONFIG_SCHED_CRITMONITOR_MAXTIME_BUSYWAIT); \
           } \
       } \
     while (0)
#else
#  define CHECK_BUSYWAIT(tcb, elapsed)
#endif

#if CONFIG_SCHED_CRITMONITOR_MAXTIME_THREAD > 0
#  define CHECK_THREAD(tcb, elapsed) \
     do \
       { \
         if (!is_idle_task(tcb) && \
             elapsed > g_critmon_maxtime[CRITMON_THREAD]) \
           { \
             CRITMONITOR_PANIC(NXTAG_SCHED, \
                               DIAG_SCHED_THREAD_TIMEOUT, \
                               "Type:THREAD,Pid:%d,Name:%s," \
                               "Elapsed:%juus,Threshold:%juus", \
                               (tcb)->pid, get_task_name(tcb), \
                               PERF_COUNT2US(elapsed), \
                               (uintmax_t) \
                               CONFIG_SCHED_CRITMONITOR_MAXTIME_THREAD); \
           } \
       } \
     while (0)
#else
#  define CHECK_THREAD(tcb, elapsed)
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

#if CONFIG_SCHED_CRITMONITOR_MAXTIME_PREEMPTION >= 0
static spinlock_t g_crimonitor_lock = SP_UNLOCKED;
#endif

/****************************************************************************
 * Public Data
 ****************************************************************************/

/* Maximum time with pre-emption disabled or with IRQs disabled. */

#if CONFIG_SCHED_CRITMONITOR_MAXTIME_PREEMPTION >= 0
DEFINE_PER_CPU_BSS(clock_t, g_premp_max);
#  define g_premp_max this_cpu_var(g_premp_max)
#endif

#if CONFIG_SCHED_CRITMONITOR_MAXTIME_IRQDISABLE >= 0
DEFINE_PER_CPU_BSS(clock_t, g_irqdis_max);
#  define g_irqdis_max this_cpu_var(g_irqdis_max)
#endif

#if CONFIG_SCHED_CRITMONITOR_MAXTIME_BUSYWAIT >= 0
DEFINE_PER_CPU_BSS(clock_t, g_busywait_max);
#  define g_busywait_max this_cpu_var(g_busywait_max)
DEFINE_PER_CPU_BSS(clock_t, g_busywait_total);
#  define g_busywait_total this_cpu_var(g_busywait_total)
#endif

/* Report thresholds in perf counter units, indexed by enum
 * critmon_maxtime_e, filled in by nxsched_critmon_initialize().
 */

#if CRITMONITOR_HAVE_MAXTIME
clock_t g_critmon_maxtime[CRITMON_MAXTIME_COUNT];
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: nxsched_critmon_cpuload
 *
 * Description:
 *   Update the running time of all running threads when switching threads
 *
 * Input Parameters:
 *   tcb   - The task that we are performing the load operations on.
 *   current - The current time
 *   tick - The ticks that we process in this cpuload.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

#ifdef CONFIG_SCHED_CPULOAD_CRITMONITOR
static void nxsched_critmon_cpuload(FAR struct tcb_s *tcb, clock_t current,
                                    clock_t tick)
{
  irqstate_t flags;
  int i;
  UNUSED(i);

  /* Update the cpuload of the thread ready to be suspended */

  flags = enter_critical_section();
  nxsched_process_taskload_ticks(tcb, tick);

  /* Update the cpuload of threads running on other CPUs */

#  ifdef CONFIG_SMP
  for (i = 0; i < CPU_SETSIZE; i++)
    {
      FAR struct tcb_s *rtcb = current_task(i);

      if (tcb->cpu == rtcb->cpu)
        {
          continue;
        }

      nxsched_process_taskload_ticks(rtcb, tick);

      /* Update start time, avoid repeated statistics when the next call */

      rtcb->run_start = current;
    }
#  endif

  leave_critical_section(flags);
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: nxsched_critmon_initialize
 *
 * Description:
 *   Convert the configured SCHED_CRITMONITOR_MAXTIME_xxx thresholds from
 *   microseconds to perf counter units.  Done once at boot so that the
 *   monitors, which sit on hot paths such as irq_dispatch() and the work
 *   queue loop, only need a load and a compare.
 *
 * Assumptions:
 *   Called from nx_start(), right after clock_initialize() has run
 *   up_timer_initialize() and made perf_getfreq() meaningful.
 *
 ****************************************************************************/

#if CRITMONITOR_HAVE_MAXTIME
void nxsched_critmon_initialize(void)
{
  /* A slot only exists when its setting is positive, so PERF_US2COUNT()
   * never sees the 0 ("disabled") or -1 ("not configured") sentinels, which
   * its unsigned arithmetic would turn into a huge wrapped value.
   */

#  if CONFIG_SCHED_CRITMONITOR_MAXTIME_THREAD > 0
  g_critmon_maxtime[CRITMON_THREAD] =
    PERF_US2COUNT(CONFIG_SCHED_CRITMONITOR_MAXTIME_THREAD);
#  endif
#  if CONFIG_SCHED_CRITMONITOR_MAXTIME_WQUEUE > 0
  g_critmon_maxtime[CRITMON_WQUEUE] =
    PERF_US2COUNT(CONFIG_SCHED_CRITMONITOR_MAXTIME_WQUEUE);
#  endif
#  if CONFIG_SCHED_CRITMONITOR_MAXTIME_PREEMPTION > 0
  g_critmon_maxtime[CRITMON_PREEMPTION] =
    PERF_US2COUNT(CONFIG_SCHED_CRITMONITOR_MAXTIME_PREEMPTION);
#  endif
#  if CONFIG_SCHED_CRITMONITOR_MAXTIME_IRQDISABLE > 0
  g_critmon_maxtime[CRITMON_IRQDISABLE] =
    PERF_US2COUNT(CONFIG_SCHED_CRITMONITOR_MAXTIME_IRQDISABLE);
#  endif
#  if CONFIG_SCHED_CRITMONITOR_MAXTIME_BUSYWAIT > 0
  g_critmon_maxtime[CRITMON_BUSYWAIT] =
    PERF_US2COUNT(CONFIG_SCHED_CRITMONITOR_MAXTIME_BUSYWAIT);
#  endif
#  if CONFIG_SCHED_CRITMONITOR_MAXTIME_IRQ > 0
  g_critmon_maxtime[CRITMON_IRQ] =
    PERF_US2COUNT(CONFIG_SCHED_CRITMONITOR_MAXTIME_IRQ);
#  endif
#  if CONFIG_SCHED_CRITMONITOR_MAXTIME_WDOG > 0
  g_critmon_maxtime[CRITMON_WDOG] =
    PERF_US2COUNT(CONFIG_SCHED_CRITMONITOR_MAXTIME_WDOG);
#  endif
}
#endif

/****************************************************************************
 * Name: nxsched_critmon_preemption
 *
 * Description:
 *   Called when there is any change in pre-emptible state of a thread.
 *
 * Assumptions:
 *   - Called with IRQs disabled.
 *   - Never called from an interrupt handler
 *   - Caller is the address of the function that is changing the pre-emption
 *
 ****************************************************************************/

#if CONFIG_SCHED_CRITMONITOR_MAXTIME_PREEMPTION >= 0
void nxsched_critmon_preemption(FAR struct tcb_s *tcb, bool state,
                                FAR void *caller)
{
  clock_t current = perf_gettime_raw();
  irqstate_t flags;

  flags = spin_lock_irqsave_notrace(&g_crimonitor_lock);

  /* Are we enabling or disabling pre-emption */

  if (state)
    {
      /* Disabling.. Save the thread start time */

      tcb->premp_start  = current;
      tcb->premp_caller = caller;
    }
  else
    {
      /* Re-enabling.. Check for the max elapsed time */

      clock_t elapsed = current - tcb->premp_start;

      if (elapsed > tcb->premp_max)
        {
          tcb->premp_max        = elapsed;
          tcb->premp_max_caller = tcb->premp_caller;
          CHECK_PREEMPTION(tcb, elapsed);
        }

      /* Check for the global max elapsed time */

      if (elapsed > g_premp_max)
        {
          g_premp_max = elapsed;
        }
    }

  spin_unlock_irqrestore_notrace(&g_crimonitor_lock, flags);
}
#endif /* CONFIG_SCHED_CRITMONITOR_MAXTIME_PREEMPTION >= 0 */

/****************************************************************************
 * Name: nxsched_critmon_irqdisable
 *
 * Description:
 *   Called by irq_save/irq_restore wrappers when a thread disables or
 *   re-enables interrupts.  Covers all IRQ-disabled paths including
 *   enter_critical_section, spin_lock_irqsave, rspin_lock_irqsave, etc.
 *
 * Assumptions:
 *   - Called with interrupts already disabled (on entry) or about to be
 *     re-enabled (on exit).
 *   - Might be called from an interrupt handler
 *
 ****************************************************************************/

#if CONFIG_SCHED_CRITMONITOR_MAXTIME_IRQDISABLE >= 0
void nxsched_critmon_irqdisable(bool state, bool force, FAR void *caller)
{
  FAR struct tcb_s *tcb = running_task();
  clock_t current;

  /* IRQ duration is tracked in irqinfo;
   * critmon records the interrupt-disable time in thread context only.
   * If tcb is NULL, this means the thread has exited, exited thread
   * is recorded in nxtask_exit().
   */

  if (up_interrupt_context() || tcb == NULL ||
      (!force && state && tcb->irqdis_count++ != 0) ||
      (!force && !state && --tcb->irqdis_count != 0))
    {
      return;
    }

  /* Call perf_gettime_raw only tcb->irqdis_count == 0, prevent recursion. */

  current = perf_gettime_raw();

  /* Are we disabling or enabling interrupts? */

  if (state)
    {
      /* Disabling... Save the start time. */

      tcb->irqdis_start  = current;
      tcb->irqdis_caller = caller;
    }
  else
    {
      /* Enabling.. Check for the max elapsed time */

      clock_t elapsed = current - tcb->irqdis_start;

      if (elapsed > tcb->irqdis_max)
        {
          tcb->irqdis_max        = elapsed;
          tcb->irqdis_max_caller = tcb->irqdis_caller;
          CHECK_IRQDIS(tcb, elapsed);
        }

      /* Check for the global max elapsed time */

      if (elapsed > g_irqdis_max)
        {
          g_irqdis_max = elapsed;
        }
    }
}
#endif /* CONFIG_SCHED_CRITMONITOR_MAXTIME_IRQDISABLE >= 0 */

/****************************************************************************
 * Name: nxsched_critmon_busywait
 *
 * Description:
 *   Called when a thread tries to acquire a spinlock (busy-waiting) or
 *   has successfully acquired it.
 *
 * Assumptions:
 *   - Called before acquiring or immediately after acquiring a spinlock.
 *   - Might be called from an interrupt handler.
 *
 ****************************************************************************/

#if CONFIG_SCHED_CRITMONITOR_MAXTIME_BUSYWAIT >= 0
void nxsched_critmon_busywait(bool state, FAR void *caller)
{
  FAR struct tcb_s *tcb = this_task();
  clock_t current       = perf_gettime_raw();

  /* Are we busy waiting for a spinlock? */

  if (state)
    {
      /* Waiting... Save the start time. */

      tcb->busywait_start  = current;
      tcb->busywait_caller = caller;
    }
  else
    {
      /* Acquired spinlock... Check for the max elapsed time */

      clock_t elapsed = current - tcb->busywait_start;

      if (elapsed > tcb->busywait_max)
        {
          tcb->busywait_max        = elapsed;
          tcb->busywait_max_caller = tcb->busywait_caller;
          CHECK_BUSYWAIT(tcb, elapsed);
        }

      /* Check for the global max elapsed time */

      if (elapsed > g_busywait_max)
        {
          g_busywait_max = elapsed;
        }

      /* Update thread-level and cpu-level busywait */

      tcb->busywait_total   += elapsed;
      g_busywait_total += elapsed;
    }
}
#endif /* CONFIG_SCHED_CRITMONITOR_MAXTIME_BUSYWAIT >= 0 */

/****************************************************************************
 * Name: nxsched_suspend_critmon
 *
 * Description:
 *   Called when a thread suspends execution, perhaps terminating an
 *   IRQ-disabled or non-preemptible state.
 *
 * Assumptions:
 *   - Called with IRQs disabled.
 *   - Might be called from an interrupt handler
 *
 ****************************************************************************/

void nxsched_suspend_critmon(FAR struct tcb_s *tcb)
{
#ifdef CONFIG_SCHED_INSTRUMENTATION_THREADTIME
  static clock_t threshold = CLOCK_MAX;
#endif

  clock_t current = perf_gettime_raw();
  clock_t elapsed = current - tcb->run_start;

#ifdef CONFIG_SCHED_CPULOAD_CRITMONITOR
  nxsched_critmon_cpuload(tcb, current, elapsed);
#endif

#ifdef CONFIG_SCHED_INSTRUMENTATION_THREADTIME
  if (threshold == CLOCK_MAX)
    {
      threshold = PERF_US2COUNT(
        CONFIG_SCHED_INSTRUMENTATION_THREAD_RUNTIME_DURATION);
    }

  if (!is_idle_task(tcb) && elapsed > threshold)
    {
      clock_t us = PERF_COUNT2US(elapsed);
      sched_note_threadtime(us);
    }
#endif

#if CONFIG_SCHED_CRITMONITOR_MAXTIME_THREAD >= 0
  tcb->run_time += elapsed;
  if (elapsed > tcb->run_max)
    {
      tcb->run_max = elapsed;
      CHECK_THREAD(tcb, elapsed);
    }
#endif

#if CONFIG_SCHED_CRITMONITOR_MAXTIME_PREEMPTION >= 0

  /* Did this task disable preemption? */

  if (tcb->lockcount > 0)
    {
      /* Possibly re-enabling.. Check for the max elapsed time */

      elapsed = current - tcb->premp_start;
      if (elapsed > tcb->premp_max)
        {
          tcb->premp_max        = elapsed;
          tcb->premp_max_caller = tcb->premp_caller;
          CHECK_PREEMPTION(tcb, elapsed);
        }

      if (elapsed > g_premp_max)
        {
          g_premp_max = elapsed;
        }
    }

#endif /* CONFIG_SCHED_CRITMONITOR_MAXTIME_PREEMPTION */

#if CONFIG_SCHED_CRITMONITOR_MAXTIME_IRQDISABLE >= 0

  /* Did this task disable interrupts? If so, accumulate the elapsed
   * time before context switch so that sleep time is not counted.
   */

  if (tcb->irqdis_count > 0)
    {
      elapsed = current - tcb->irqdis_start;
      if (elapsed > tcb->irqdis_max)
        {
          tcb->irqdis_max        = elapsed;
          tcb->irqdis_max_caller = tcb->irqdis_caller;
          CHECK_IRQDIS(tcb, elapsed);
        }

      if (elapsed > g_irqdis_max)
        {
          g_irqdis_max = elapsed;
        }
    }

#endif /* CONFIG_SCHED_CRITMONITOR_MAXTIME_IRQDISABLE */
}

/****************************************************************************
 * Name: nxsched_resume_critmon
 *
 * Description:
 *   Called when a thread resumes execution, perhaps re-establishing an
 *   IRQ-disabled or non-pre-emptible state.
 *
 * Assumptions:
 *   - Called with IRQs disabled.
 *   - Might be called from an interrupt handler
 *
 ****************************************************************************/

void nxsched_resume_critmon(FAR struct tcb_s *tcb)
{
  clock_t current = perf_gettime_raw();

  UNUSED(current);

#if CONFIG_SCHED_CRITMONITOR_MAXTIME_THREAD >= 0
  tcb->run_start = current;
#endif

#if CONFIG_SCHED_CRITMONITOR_MAXTIME_PREEMPTION >= 0

  /* Did this task disable pre-emption? */

  if (tcb->lockcount > 0)
    {
      /* Yes.. Save the start time */

      tcb->premp_start = current;
    }
#endif /* CONFIG_SCHED_CRITMONITOR_MAXTIME_PREEMPTION */

#if CONFIG_SCHED_CRITMONITOR_MAXTIME_IRQDISABLE >= 0

  /* Did this task disable interrupts? Reset the start time so that
   * sleep time is not counted toward irq-disable duration.
   */

  if (tcb->irqdis_count > 0)
    {
      tcb->irqdis_start = current;
    }
#endif /* CONFIG_SCHED_CRITMONITOR_MAXTIME_IRQDISABLE */
}

void nxsched_update_critmon(FAR struct tcb_s *tcb)
{
  irqstate_t flags = enter_critical_section();
  clock_t current = perf_gettime_raw();
  clock_t elapsed = current - tcb->run_start;

  if (tcb->task_state == TSTATE_TASK_RUNNING)
    {
#ifdef CONFIG_SCHED_CPULOAD_CRITMONITOR
      nxsched_process_taskload_ticks(tcb, elapsed);
#endif

      tcb->run_start = current;
      tcb->run_time += elapsed;
      if (elapsed > tcb->run_max)
        {
          tcb->run_max = elapsed;
          CHECK_THREAD(tcb, elapsed);
        }
    }

  leave_critical_section(flags);
}
