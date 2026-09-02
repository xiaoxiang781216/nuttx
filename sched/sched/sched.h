/****************************************************************************
 * sched/sched/sched.h
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

#ifndef __SCHED_SCHED_SCHED_H
#define __SCHED_SCHED_SCHED_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>
#include <stdbool.h>
#include <sched.h>

#include <nuttx/arch.h>
#include <nuttx/clock.h>
#include <nuttx/hrtimer.h>
#include <nuttx/queue.h>
#include <nuttx/kmalloc.h>
#include <nuttx/spinlock.h>
#include <nuttx/tls.h>
#include <nuttx/trace.h>

#include "clock/clock.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define PIDHASH(pid)             ((pid) & (g_npidhash - 1))

/* This set of all CPUs */

#define SCHED_ALL_CPUS           ((1 << CONFIG_SMP_NCPUS) - 1)

/* The state of a task is indicated both by the task_state field of the TCB
 * and by a series of task lists.  All of these tasks lists are declared
 * below. Although it is not always necessary, most of these lists are
 * prioritized so that common list handling logic can be used (only the
 * g_readytorun, the g_pendingtasks, and the g_waitingforsemaphore lists
 * need to be prioritized).
 */

#define list_readytorun()        (&g_readytorun)
#ifndef CONFIG_SMP
#  define list_pendingtasks()      (&g_pendingtasks)
#endif
#define list_waitingforsignal()  (&g_waitingforsignal)
#define list_waitingforfill()    (&g_waitingforfill)
#define list_stoppedtasks()      (&g_stoppedtasks)

/* These are macros to access the current CPU and the current task on a CPU.
 * These macros are intended to support a future SMP implementation.
 * NOTE: this_task() for SMP is implemented in sched_thistask.c
 */

/* List attribute flags */

#define TLIST_ATTR_PRIORITIZED   (1 << 0) /* Bit 0: List is prioritized */
#define TLIST_ATTR_INDEXED       (1 << 1) /* Bit 1: List is indexed by CPU */
#define TLIST_ATTR_RUNNABLE      (1 << 2) /* Bit 2: List includes running tasks */
#define TLIST_ATTR_OFFSET        (1 << 3) /* Bit 3: Pointer of task list is offset */

#define __TLIST_ATTR(s)          g_tasklisttable[s].attr
#define TLIST_ISPRIORITIZED(s)   ((__TLIST_ATTR(s) & TLIST_ATTR_PRIORITIZED) != 0)
#define TLIST_ISINDEXED(s)       ((__TLIST_ATTR(s) & TLIST_ATTR_INDEXED) != 0)
#define TLIST_ISRUNNABLE(s)      ((__TLIST_ATTR(s) & TLIST_ATTR_RUNNABLE) != 0)
#define TLIST_ISOFFSET(s)        ((__TLIST_ATTR(s) & TLIST_ATTR_OFFSET) != 0)

#define __TLIST_HEAD(t) \
  (TLIST_ISOFFSET((t)->task_state) ? (FAR dq_queue_t *)((FAR uint8_t *)((t)->waitobj) + \
  (uintptr_t)g_tasklisttable[(t)->task_state].list) : g_tasklisttable[(t)->task_state].list)

#ifdef CONFIG_SMP
#  define TLIST_HEAD(t,c) \
    ((TLIST_ISINDEXED((t)->task_state)) ? (&(__TLIST_HEAD(t))[c]) : __TLIST_HEAD(t))
#  define TLIST_BLOCKED(t)       __TLIST_HEAD(t)
#else
#  define TLIST_HEAD(t)          __TLIST_HEAD(t)
#  define TLIST_BLOCKED(t)       __TLIST_HEAD(t)
#endif

#ifdef CONFIG_SCHED_CRITMONITOR_MAXTIME_PANIC
#  define CRITMONITOR_PANIC(tag, id, fmt, ...) \
          do \
            { \
              _alert(fmt, ##__VA_ARGS__); \
              trace_diag(tag, LOG_EMERG, id, fmt, ##__VA_ARGS__); \
              PANIC(); \
            } \
          while(0)
#else
#  define CRITMONITOR_PANIC(tag, id, fmt, ...) \
          do \
            { \
              _alert(fmt, ##__VA_ARGS__); \
              trace_diag(tag, LOG_EMERG, id, fmt, ##__VA_ARGS__); \
            } \
          while(0)
#endif

/* Elapsed times are measured with the perf counter, but the monitor
 * thresholds (SCHED_CRITMONITOR_MAXTIME_xxx and friends) are configured in
 * microseconds.  The perf frequency is only known at runtime, so convert
 * here instead of hard-coding counter units in the configuration.
 *
 * US2COUNT rounds up: a slow perf counter (e.g. 16.384kHz, ~61us per count)
 * would otherwise truncate any sub-resolution threshold to 0 counts, which
 * means "report every time" and would drown the log in false positives.
 * Rounding up also keeps 0 reserved for the configured "always report"
 * value.
 */

#define PERF_US2COUNT(us) \
        div_round_up((clock_t)(us) * perf_getfreq(), USEC_PER_SEC)

#define PERF_COUNT2US(count) \
        ((uintmax_t)(count) * USEC_PER_SEC / perf_getfreq())

/* True when at least one monitor needs a converted threshold. */

#define CRITMONITOR_HAVE_MAXTIME \
        (CONFIG_SCHED_CRITMONITOR_MAXTIME_THREAD > 0 || \
         CONFIG_SCHED_CRITMONITOR_MAXTIME_WQUEUE > 0 || \
         CONFIG_SCHED_CRITMONITOR_MAXTIME_PREEMPTION > 0 || \
         CONFIG_SCHED_CRITMONITOR_MAXTIME_IRQDISABLE > 0 || \
         CONFIG_SCHED_CRITMONITOR_MAXTIME_BUSYWAIT > 0 || \
         CONFIG_SCHED_CRITMONITOR_MAXTIME_IRQ > 0 || \
         CONFIG_SCHED_CRITMONITOR_MAXTIME_WDOG > 0)

/****************************************************************************
 * Public Type Definitions
 ****************************************************************************/

/* This structure defines an element of the g_tasklisttable[].  This table
 * is used to map a task_state enumeration to the corresponding task list.
 */

struct tasklist_s
{
  DSEG dq_queue_t *list; /* Pointer to the task list */
  int attr;              /* List attribute flags */
};

/* This enumeration defines smp schedule task switch rule */

enum task_deliver_e
{
  SWITCH_NONE   = 0, /* No schedule switch pending */
  SWITCH_HIGHER = 1, /* Higher priority task needs to be scheduled in */
  SWITCH_EQUAL       /* Higher or equal priority task needs to be scheduled
                      * in
                      */
};

typedef struct tasklist_s tasklist_table_t[NUM_TASK_STATES];

/* One slot per enabled SCHED_CRITMONITOR_MAXTIME_xxx setting, converted to
 * perf counter units by nxsched_critmon_initialize().  The slots carry the
 * same "> 0" guard as the monitors that read them, so a disabled threshold
 * costs neither RAM nor code.
 */

enum critmon_maxtime_e
{
#if CONFIG_SCHED_CRITMONITOR_MAXTIME_THREAD > 0
  CRITMON_THREAD,
#endif
#if CONFIG_SCHED_CRITMONITOR_MAXTIME_WQUEUE > 0
  CRITMON_WQUEUE,
#endif
#if CONFIG_SCHED_CRITMONITOR_MAXTIME_PREEMPTION > 0
  CRITMON_PREEMPTION,
#endif
#if CONFIG_SCHED_CRITMONITOR_MAXTIME_IRQDISABLE > 0
  CRITMON_IRQDISABLE,
#endif
#if CONFIG_SCHED_CRITMONITOR_MAXTIME_BUSYWAIT > 0
  CRITMON_BUSYWAIT,
#endif
#if CONFIG_SCHED_CRITMONITOR_MAXTIME_IRQ > 0
  CRITMON_IRQ,
#endif
#if CONFIG_SCHED_CRITMONITOR_MAXTIME_WDOG > 0
  CRITMON_WDOG,
#endif
  CRITMON_MAXTIME_COUNT
};

/****************************************************************************
 * Public Data
 ****************************************************************************/

/* Declared in sched_critmonitor.c ******************************************/

/* The enabled SCHED_CRITMONITOR_MAXTIME_xxx thresholds, converted once to
 * perf counter units by nxsched_critmon_initialize().
 */

#if CRITMONITOR_HAVE_MAXTIME
extern clock_t g_critmon_maxtime[CRITMON_MAXTIME_COUNT];
#endif

/* Declared in nx_start.c ***************************************************/

/* The state of a task is indicated both by the task_state field of the TCB
 * and by a series of task lists.  All of these tasks lists are declared
 * below. Although it is not always necessary, most of these lists are
 * prioritized so that common list handling logic can be used (only the
 * g_readytorun, the g_pendingtasks, and the g_waitingforsemaphore lists
 * need to be prioritized).
 */

#ifdef CONFIG_SMP
/* g_delivertasks is used to indicate that a task switch is scheduled for
 * another cpu to be processed.
 */

DECLARE_PER_CPU_SMP(enum task_deliver_e, g_delivertasks);

/* This is the list of idle tasks */

DECLARE_PER_CPU(FAR struct tcb_s, g_idletcb);

#endif

/* This is the list of all tasks that are ready-to-run, but cannot be placed
 * in the g_readytorun list because:  (1) They are higher priority than the
 * currently active task at the head of the g_readytorun list, and (2) the
 * currently active task has disabled pre-emption.
 */

#ifndef CONFIG_SMP
DECLARE_PER_CPU_BMP(dq_queue_t, g_pendingtasks);
#define g_pendingtasks this_cpu_var_bmp(g_pendingtasks)
#endif

/* This is the list of all tasks that are blocked waiting for a signal */

DECLARE_PER_CPU_BMP(dq_queue_t, g_waitingforsignal);
#define g_waitingforsignal this_cpu_var_bmp(g_waitingforsignal)

/* This is the list of all tasks that are blocking waiting for a page fill */

#ifdef CONFIG_LEGACY_PAGING
DECLARE_PER_CPU_BMP(dq_queue_t, g_waitingforfill);
#define g_waitingforfill this_cpu_var_bmp(g_waitingforfill)
#endif

/* This is the list of all tasks that have been stopped
 * via SIGSTOP or SIGTSTP
 */

#ifdef CONFIG_SIG_SIGSTOP_ACTION
DECLARE_PER_CPU_BMP(dq_queue_t, g_stoppedtasks);
#define g_stoppedtasks this_cpu_var_bmp(g_stoppedtasks)
#endif

/* This is the value of the last process ID assigned to a task */

DECLARE_PER_CPU_BMP(volatile pid_t, g_lastpid);
#define g_lastpid this_cpu_var_bmp(g_lastpid)

/* The following hash table is used for two things:
 *
 * 1. This hash table greatly speeds the determination of a new unique
 *    process ID for a task, and
 * 2. Is used to quickly map a process ID into a TCB.
 */

DECLARE_PER_CPU_BMP(FAR struct tcb_s **, g_pidhash);
DECLARE_PER_CPU_BMP(volatile int, g_npidhash);
DECLARE_PER_CPU_BMP(spinlock_t, g_pidhashlock);
#define g_pidhash this_cpu_var_bmp(g_pidhash)
#define g_npidhash this_cpu_var_bmp(g_npidhash)
#define g_pidhashlock this_cpu_var_bmp(g_pidhashlock)

/* This is a table of task lists.  This table is indexed by the task stat
 * enumeration type (tstate_t) and provides a pointer to the associated
 * static task list (if there is one) as well as a a set of attribute flags
 * indicating properties of the list, for example, if the list is an
 * ordered list or not.
 */

DECLARE_PER_CPU_BMP(tasklist_table_t, g_tasklisttable);
#define g_tasklisttable this_cpu_var_bmp(g_tasklisttable)

#ifndef CONFIG_SCHED_CPULOAD_NONE
/* This is the total number of clock tick counts.  Essentially the
 * 'denominator' for all CPU load calculations.
 */

DECLARE_PER_CPU_BMP(volatile clock_t, g_cpuload_total);
#define g_cpuload_total this_cpu_var_bmp(g_cpuload_total)
#endif

/* Declared in sched_lock.c *************************************************/

/* Pre-emption is disabled via the interface sched_lock(). sched_lock()
 * works by preventing context switches from the currently executing tasks.
 * This prevents other tasks from running (without disabling interrupts) and
 * gives the currently executing task exclusive access to the (single) CPU
 * resources. Thus, sched_lock() and its companion, sched_unlock(), are
 * used to implement some critical sections.
 *
 * In the single CPU case, Pre-emption is disabled using a simple lockcount
 * in the TCB. When the scheduling is locked, the lockcount is incremented;
 * when the scheduler is unlocked, the lockcount is decremented. If the
 * lockcount for the task at the head of the g_readytorun list has a
 * lockcount > 0, then pre-emption is disabled.
 *
 * No special protection is required since only the executing task can
 * modify its lockcount.
 */

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

void nxsched_process_tick(void);

int nxthread_create(FAR const char *name, int ttype, int priority,
                    FAR void *stack_addr, int stack_size, main_t entry,
                    FAR char * const argv[], FAR char * const envp[]);

/* Task list manipulation functions */

bool nxsched_add_readytorun(FAR struct tcb_s *btcb);
bool nxsched_remove_readytorun(FAR struct tcb_s *rtcb);
void nxsched_remove_self(FAR struct tcb_s *tcb);
void nxsched_add_blocked(FAR struct tcb_s *btcb, tstate_t task_state);
void nxsched_remove_blocked(FAR struct tcb_s *btcb);
int  nxsched_set_priority(FAR struct tcb_s *tcb, int sched_priority);

/* nxsched_set_running_priority() reprioritizes a running task.  It updates
 * the ready-to-run list and may trigger a context switch.  It takes its own
 * critical section internally, so callers do not need to hold one (nesting
 * is harmless if they already do).
 */

void nxsched_set_running_priority(FAR struct tcb_s *tcb, int sched_priority);
#ifndef CONFIG_SMP
bool nxsched_merge_pending(void);
bool nxsched_reprioritize_rtr(FAR struct tcb_s *tcb, int priority);
#endif

/****************************************************************************
 * Name:  nxsched_release_pid
 *
 * Description:
 *   When a task is destroyed, this function must be called to make its
 *   process ID available for reuse.
 *
 ****************************************************************************/

void nxsched_release_pid(pid_t pid);

/* Priority inheritance support */

#ifdef CONFIG_PRIORITY_INHERITANCE
int  nxsched_reprioritize(FAR struct tcb_s *tcb, int sched_priority);
#else
#  define nxsched_reprioritize(tcb,sched_priority) \
     nxsched_set_priority(tcb,sched_priority)
#endif

/* Support for tickless operation */

void nxsched_reassess_timer(void);

#if defined(CONFIG_SCHED_TICKLESS) && \
    (CONFIG_RR_INTERVAL > 0 || defined(CONFIG_SCHED_SPORADIC))
void nxsched_timer_cancel(void);
void nxsched_timer_start(clock_t now, clock_t delay);
#else
#  define nxsched_timer_cancel()
#  define nxsched_timer_start(now, delay)
#endif

/* Scheduler policy support */

#if CONFIG_RR_INTERVAL > 0
clock_t nxsched_process_roundrobin(FAR struct tcb_s *tcb, clock_t ticks,
                                   bool noswitches);
#  ifdef CONFIG_SCHED_TICKLESS
void nxsched_suspend_roundrobin(FAR struct tcb_s *tcb);
void nxsched_resume_roundrobin(FAR struct tcb_s *tcb);
#  endif
#endif

void nxsched_switch(FAR struct tcb_s *tcb, FAR struct tcb_s *rtcb);

#ifdef CONFIG_SCHED_SPORADIC
int  nxsched_validate_sporadic(FAR const struct sched_param *param,
                                FAR clock_t *repl_ticks,
                                FAR clock_t *budget_ticks);
int  nxsched_initialize_sporadic(FAR struct tcb_s *tcb);
int  nxsched_start_sporadic(FAR struct tcb_s *tcb);
int  nxsched_stop_sporadic(FAR struct tcb_s *tcb);
int  nxsched_reset_sporadic(FAR struct tcb_s *tcb);
int  nxsched_resume_sporadic(FAR struct tcb_s *tcb);
int  nxsched_suspend_sporadic(FAR struct tcb_s *tcb);
clock_t nxsched_process_sporadic(FAR struct tcb_s *tcb, clock_t ticks,
                                 bool noswitches);
void nxsched_sporadic_lowpriority(FAR struct tcb_s *tcb);
#endif

#ifdef CONFIG_SIG_SIGSTOP_ACTION
void nxsched_suspend(FAR struct tcb_s *tcb);
#endif

#if defined(CONFIG_STACKCHECK_MARGIN) && \
           (CONFIG_STACKCHECK_MARGIN != -1)
void nxsched_checkstackoverflow(FAR struct tcb_s *tcb);
#else
#  define nxsched_checkstackoverflow(tcb)
#endif

#ifdef CONFIG_SMP
bool nxsched_switch_running(int cpu, bool switch_equal);
void nxsched_process_delivered(int cpu);
#else
#  define nxsched_select_cpu(a)     (0)
#endif

#define nxsched_islocked_tcb(tcb)   ((tcb)->lockcount > 0)

/* CPU load measurement support */

#if defined(CONFIG_SCHED_CPULOAD_SYSCLK) || \
    defined (CONFIG_SCHED_CPULOAD_CRITMONITOR)
void nxsched_process_taskload_ticks(FAR struct tcb_s *tcb, clock_t ticks);
void nxsched_process_cpuload_ticks(clock_t ticks);
#define nxsched_process_cpuload() nxsched_process_cpuload_ticks(1)
#endif

void nxsched_suspend_scheduler(FAR struct tcb_s *tcb);
void nxsched_resume_scheduler(FAR struct tcb_s *tcb);

/* Critical section monitor */

#ifdef CONFIG_SCHED_CRITMONITOR
void nxsched_suspend_critmon(FAR struct tcb_s *tcb);
void nxsched_resume_critmon(FAR struct tcb_s *tcb);
void nxsched_update_critmon(FAR struct tcb_s *tcb);
#endif

#if CONFIG_SCHED_CRITMONITOR_MAXTIME_PREEMPTION >= 0
void nxsched_critmon_preemption(FAR struct tcb_s *tcb, bool state,
                                FAR void *caller);
#else
#  define nxsched_critmon_preemption(t, s, c)
#endif

#if CRITMONITOR_HAVE_MAXTIME
void nxsched_critmon_initialize(void);
#else
#  define nxsched_critmon_initialize()
#endif

#if CONFIG_SCHED_CRITMONITOR_MAXTIME_IRQDISABLE >= 0 || \
    defined(CONFIG_SCHED_INSTRUMENTATION_CSECTION)
void restore_critical_section(uint16_t count);
#elif defined(CONFIG_SMP)
#  define restore_critical_section(count) rspin_restorelock(&g_schedlock, count)
#else
#  define restore_critical_section(count)
#endif

/* Obtain TLS from kernel */

static inline_function
FAR struct tls_info_s *nxsched_get_tls(FAR struct tcb_s *tcb)
{
  return (FAR struct tls_info_s *)tcb->stack_alloc_ptr;
}

static inline_function
FAR char **nxsched_get_stackargs(FAR struct tcb_s *tcb)
{
  return nxsched_get_tls(tcb)->tl_argv;
}

/****************************************************************************
 * Inline functions
 ****************************************************************************/

bool nxsched_add_prioritized(FAR struct tcb_s *tcb, DSEG dq_queue_t *list);

#ifdef CONFIG_SMP

/* Try to switch the head of the ready-to-run list to active on "target_cpu".
 * "cpu" is "this_cpu()", and passed only for optimization.
 */

static inline_function bool
nxsched_deliver_task(int cpu, int target_cpu,
                     enum task_deliver_e priority)
{
  bool ret = false;

  /* If there is already a schedule interrupt pending, there is
   * no need to do anything now.
   */

  if (per_cpu_var_smp(g_delivertasks, target_cpu) != priority)
    {
      if (cpu == target_cpu)
        {
          ret = nxsched_switch_running(cpu, priority == SWITCH_EQUAL);
        }
      else
        {
          per_cpu_var_smp(g_delivertasks, target_cpu) = priority;
          up_send_smp_sched(target_cpu);
        }
    }

  return ret;
}

static inline_function int nxsched_select_cpu(cpu_set_t affinity)
{
  uint8_t minprio;
  int cpu;
  int i;

  minprio = SCHED_PRIORITY_MAX;
  cpu     = 0xff;

  for (i = 0; i < CONFIG_SMP_NCPUS; i++)
    {
      /* Is the thread permitted to run on this CPU? */

      if ((affinity & (1 << i)) != 0)
        {
          FAR struct tcb_s *rtcb = current_task(i);

          /* If this CPU is executing its IDLE task, then use it.  The
           * IDLE task is always the last task in the assigned task list.
           */

          if (is_idle_task(rtcb))
            {
              /* The IDLE task should always be assigned to this CPU and have
               * a priority of zero.
               */

              DEBUGASSERT(rtcb->sched_priority == 0);
              return i;
            }
          else if (rtcb->sched_priority <= minprio)
            {
              DEBUGASSERT(rtcb->sched_priority > 0);
              minprio = rtcb->sched_priority;
              cpu = i;
            }
        }
    }

  DEBUGASSERT(cpu != 0xff);
  return cpu;
}
#endif

/****************************************************************************
 * Name: tcb_timer_start
 *
 * Description:
 *   Start the wait timer with a relative delay in nanoseconds.  Returns
 *   the absolute expiration time so the caller can compute the remaining
 *   time if needed.
 *
 ****************************************************************************/

static inline_function
uint64_t tcb_timer_start(FAR struct tcb_s *tcb, uint64_t delay,
                         hrtimer_entry_t func)
{
  uint64_t expected = delay + clock_systime_nsec();
  hrtimer_start(&tcb->waittimer, func, expected, HRTIMER_MODE_ABS);
  return expected;
}

/****************************************************************************
 * Name: tcb_timer_start_ticks
 *
 * Description:
 *   Start the wait timer with a relative delay in ticks.  The delay is
 *   clamped to HRTIMER_MAX_DELAY / NSEC_PER_TICK.  Returns the absolute
 *   expiration time so the caller can compute the remaining time if needed.
 *
 ****************************************************************************/

static inline_function
uint64_t tcb_timer_start_ticks(FAR struct tcb_s *tcb, clock_t delay,
                               hrtimer_entry_t func)
{
  uint64_t ticks = (uint64_t)delay > HRTIMER_MAX_DELAY / NSEC_PER_TICK ?
                   HRTIMER_MAX_DELAY / NSEC_PER_TICK : (uint64_t)delay;
  return tcb_timer_start(tcb, TICK2NSEC(ticks), func);
}

/****************************************************************************
 * Name: tcb_timer_start_abstime
 *
 * Description:
 *   Start the wait timer with an absolute time (timespec).  If the time
 *   is already in the past, clamp to the current time so the timer fires
 *   immediately.  Returns the absolute expiration time in nanoseconds.
 *
 ****************************************************************************/

static inline_function
uint64_t tcb_timer_start_abstime(FAR struct tcb_s *tcb, clockid_t clockid,
                                 FAR const struct timespec *abstime,
                                 hrtimer_entry_t func)
{
  uint64_t now = clock_systime_nsec();
  uint64_t expected;

  if (clockid == CLOCK_REALTIME)
    {
#ifdef CONFIG_CLOCK_TIMEKEEPING
      struct timespec rt;
      nxclock_gettime(CLOCK_REALTIME, &rt);
      clock_timespec_subtract(abstime, &rt, &rt);
      expected = clock_time2nsec(&rt) + now;
#else
      struct timespec ts;
      clock_timespec_subtract(abstime, &g_basetime, &ts);
      expected = clock_time2nsec(&ts);
      expected = HRTIMER_TIME_BEFORE(now, expected) ? expected : now;
#endif
    }
  else
    {
      DEBUGASSERT(abstime->tv_sec >= 0);
      expected = clock_time2nsec(abstime);
      expected = HRTIMER_TIME_BEFORE(now, expected) ? expected : now;
    }

  hrtimer_start(&tcb->waittimer, func, expected, HRTIMER_MODE_ABS);
  return expected;
}

/****************************************************************************
 * Name: tcb_timer_cancel
 *
 * Description:
 *   Cancel the wait timer asynchronously.  If a callback is in-flight on
 *   another CPU, it will fail the state check and no-op.
 *
 ****************************************************************************/

static inline_function
void tcb_timer_cancel(FAR struct tcb_s *tcb)
{
  hrtimer_cancel(&tcb->waittimer);
  tcb->waittimer.expired += INT64_MAX;
}

/****************************************************************************
 * Name: tcb_timer_cancel_sync
 *
 * Description:
 *   Cancel the wait timer and synchronously wait for any in-flight
 *   callback to complete.
 *
 ****************************************************************************/

static inline_function
void tcb_timer_cancel_sync(FAR struct tcb_s *tcb)
{
  hrtimer_cancel_sync(&tcb->waittimer);
}

#endif /* __SCHED_SCHED_SCHED_H */
