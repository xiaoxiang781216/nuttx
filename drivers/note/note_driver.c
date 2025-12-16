/****************************************************************************
 * drivers/note/note_driver.c
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

#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <time.h>

#include <nuttx/init.h>
#include <nuttx/irq.h>
#include <nuttx/sched.h>
#include <nuttx/clock.h>
#include <nuttx/timers/clkcnt.h>
#include <nuttx/note/note_driver.h>
#include <nuttx/sched_note.h>
#include <nuttx/spinlock.h>
#include <nuttx/streams.h>

#include "note_driver.h"
#include "sched/sched.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#if defined(CONFIG_DRIVERS_NOTERAM) +  defined(CONFIG_DRIVERS_NOTELOG) + \
    defined(CONFIG_DRIVERS_NOTESNAP) + defined(CONFIG_DRIVERS_NOTERTT) + \
    defined(CONFIG_SEGGER_SYSVIEW) > CONFIG_DRIVERS_NOTE_MAX
#  error "Maximum channel number exceeds. "
#endif

#define note_add(drv, note, notelen, noswitches)                             \
  ((drv)->ops->add(drv, note, notelen, noswitches))
#define note_start(drv, tcb)                                                 \
  ((drv)->ops->start && ((drv)->ops->start(drv, tcb), true))
#define note_stop(drv, tcb)                                                  \
  ((drv)->ops->stop && ((drv)->ops->stop(drv, tcb), true))
#define note_suspend(drv, tcb)                                               \
  ((drv)->ops->suspend && ((drv)->ops->suspend(drv, tcb), true))
#define note_resume(drv, tcb)                                                \
  ((drv)->ops->resume && ((drv)->ops->resume(drv, tcb), true))
#define note_cpu_start(drv, tcb, cpu)                                        \
  ((drv)->ops->cpu_start && ((drv)->ops->cpu_start(drv, tcb, cpu), true))
#define note_cpu_started(drv, tcb)                                           \
  ((drv)->ops->cpu_started && ((drv)->ops->cpu_started(drv, tcb), true))
#define note_preemption(drv, tcb, locked)                                    \
  ((drv)->ops->preemption && ((drv)->ops->preemption(drv, tcb, locked), true))
#define note_csection(drv, tcb, enter)                                       \
  ((drv)->ops->csection && ((drv)->ops->csection(drv, tcb, enter), true))
#define note_spinlock(drv, tcb, spinlock, type)                              \
  ((drv)->ops->spinlock &&                                                   \
  ((drv)->ops->spinlock(drv, tcb, spinlock, type), true))
#define note_syscall_enter(drv, nr, argc, ap)                                \
  ((drv)->ops->syscall_enter &&                                              \
  ((drv)->ops->syscall_enter(drv, nr, argc, ap), true))
#define note_syscall_leave(drv, nr, result)                                  \
  ((drv)->ops->syscall_leave &&                                              \
  ((drv)->ops->syscall_leave(drv, nr, result), true))
#define note_irqhandler(drv, irq, handler, enter)                            \
  ((drv)->ops->irqhandler &&                                                 \
  ((drv)->ops->irqhandler(drv, irq, handler, enter), true))
#define note_heap(drv, event, data, mem, size, used)                         \
  ((drv)->ops->heap && ((drv)->ops->heap(drv, event, data, mem, size, used), true))
#define note_wdog(drv, event, handler, arg)                                  \
  ((drv)->ops->wdog && ((drv)->ops->wdog(drv, event, handler, arg), true))
#define note_event(drv, tag, level, ip, event, buf, len)                          \
  ((drv)->ops->event && ((drv)->ops->event(drv, tag, level, ip, event, buf, len), true))
#define note_vprintf(drv, tag, level, ip, type, fmt, va)                                \
  ((drv)->ops->vprintf && ((drv)->ops->vprintf(drv, tag, level, ip, type, fmt, va), true))

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct note_startalloc_s
{
  struct note_common_s nsa_cmn; /* Common note parameters */
#if CONFIG_TASK_NAME_SIZE > 0
  char nsa_name[CONFIG_TASK_NAME_SIZE + 1];
#endif
};

#if CONFIG_TASK_NAME_SIZE > 0
#  define SIZEOF_NOTE_START(n) (sizeof(struct note_common_s) + (n))
#else
#  define SIZEOF_NOTE_START(n) (sizeof(struct note_common_s))
#endif

#if CONFIG_DRIVERS_NOTE_TASKNAME_BUFSIZE > 0
struct note_taskname_info_s
{
  pid_t pid;
  uint8_t size;
  char name[1];
};

struct note_taskname_s
{
  size_t head;
  size_t tail;
  char buffer[CONFIG_DRIVERS_NOTE_TASKNAME_BUFSIZE];
};
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

#if defined(CONFIG_SCHED_INSTRUMENTATION_FILTER) && \
    defined(CONFIG_SCHED_INSTRUMENTATION_IRQHANDLER)
static DEFINE_PER_CPU_BSS(unsigned int, g_note_disabled_irq_nest);
#  define g_note_disabled_irq_nest this_cpu_var(g_note_disabled_irq_nest)
#endif

FAR static struct note_driver_s *
  g_note_drivers[CONFIG_DRIVERS_NOTE_MAX + 1] =
{
#ifdef CONFIG_DRIVERS_NOTERAM
  (FAR struct note_driver_s *)&g_noteram_driver,
#endif
#ifdef CONFIG_DRIVERS_NOTELOG
  (FAR struct note_driver_s *)&g_notelog_driver,
#endif
#ifdef CONFIG_DRIVERS_NOTELOWEROUT
  (FAR struct note_driver_s *)&g_notestream_lowerout,
#endif
#ifdef CONFIG_DRIVERS_NOTERPMSG
  (FAR struct note_driver_s *)&g_noterpmsg_driver,
#endif
#ifdef CONFIG_DRIVERS_NOTET32FDX
  (FAR struct note_driver_s *)&g_notefdx,
#endif
#ifdef CONFIG_DRIVERS_NOTESNAP
  (FAR struct note_driver_s *)&g_notesnap,
#endif
  NULL
};

#if CONFIG_DRIVERS_NOTE_TASKNAME_BUFSIZE > 0 && \
    defined(CONFIG_SCHED_INSTRUMENTATION_SWITCH)
static struct note_taskname_s g_note_taskname;
#endif

#if defined(CONFIG_SCHED_INSTRUMENTATION_FILTER)
static spinlock_t g_note_lock;
#endif

#if CONFIG_DRIVERS_NOTE_CLOCKID >= 0
static clock_t g_note_perf_offset;
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: note_isenabled
 *
 * Description:
 *   Check whether the instrumentation is enabled.
 *
 * Input Parameters:
 *   driver - The channel of note driver
 *
 * Returned Value:
 *   True is returned if the instrumentation is enabled.
 *
 ****************************************************************************/

static inline int note_isenabled(FAR struct note_driver_s *driver)
{
#ifdef CONFIG_SCHED_INSTRUMENTATION_FILTER
  if (NOTE_FILTER_TYPEMASK_ISSET(NOTE_ALL, &driver->filter.mode))
    {
      return false;
    }

#ifdef CONFIG_SMP
  /* Ignore notes that are not in the set of monitored CPUs */

  if (CPU_ISSET(this_cpu(), &driver->filter.mode.cpuset) == 0)
    {
      /* Not in the set of monitored CPUs.  Do not log the note. */

      return false;
    }
#endif
#endif

  return true;
}

/****************************************************************************
 * Name: note_isenabled_type
 *
 * Description:
 *   Check whether the instrumentation is enabled.
 *
 * Input Parameters:
 *   driver - The channel of note driver
 *   note   - The common note structure to use
 *
 * Returned Value:
 *   True is returned if the instrumentation is enabled.
 *
 ****************************************************************************/

static inline int note_isenabled_type(FAR struct note_driver_s *driver,
                                      int type)
{
#ifdef CONFIG_SCHED_INSTRUMENTATION_FILTER
  if (!note_isenabled(driver))
    {
      return false;
    }

  if (NOTE_FILTER_TYPEMASK_ISSET(type, &driver->filter.mode))
    {
      return false;
    }
#endif

  return true;
}

/****************************************************************************
 * Name: note_isenabled_syscall
 *
 * Description:
 *   Check whether the syscall instrumentation is enabled.
 *
 * Input Parameters:
 *   driver - The channel of note driver
 *   nr - syscall number
 *
 * Returned Value:
 *   True is returned if the instrumentation is enabled.
 *
 ****************************************************************************/

#ifdef CONFIG_SCHED_INSTRUMENTATION_SYSCALL
static inline int note_isenabled_syscall(FAR struct note_driver_s *driver,
                                         int type, int nr)
{
#ifdef CONFIG_SCHED_INSTRUMENTATION_FILTER
  if (!note_isenabled_type(driver, type))
    {
      return false;
    }

  /* Exclude the case of syscall called by the interrupt handler which is
   * not traced.
   */

  if (up_interrupt_context())
    {
#ifdef CONFIG_SCHED_INSTRUMENTATION_IRQHANDLER
      if (g_note_disabled_irq_nest > 0)
        {
          return false;
        }
#else
      return false;
#endif
    }

  /* If the syscall trace is disabled or the syscall number is masked,
   * do nothing.
   */

  if (NOTE_FILTER_SYSCALLMASK_ISSET(nr - CONFIG_SYS_RESERVED,
                                    &driver->filter.syscall_mask))
    {
      return false;
    }
#endif

  return true;
}
#endif

/****************************************************************************
 * Name: note_isenabled_irqhandler
 *
 * Description:
 *   Check whether the interrupt handler instrumentation is enabled.
 *
 * Input Parameters:
 *   driver - The channel of note driver
 *   irq   - IRQ number
 *   enter - interrupt enter/leave flag
 *
 * Returned Value:
 *   True is returned if the instrumentation is enabled.
 *
 ****************************************************************************/

#ifdef CONFIG_SCHED_INSTRUMENTATION_IRQHANDLER
static inline int note_isenabled_irq(FAR struct note_driver_s *driver,
                                     int irq, bool enter)
{
#ifdef CONFIG_SCHED_INSTRUMENTATION_FILTER
  int type = NOTE_IRQ_ENTER + enter;

  if (!note_isenabled_type(driver, type))
    {
      return false;
    }

  /* If the IRQ trace is disabled or the IRQ number is masked, disable
   * subsequent syscall traces until leaving the interrupt handler
   */

  if (NOTE_FILTER_IRQMASK_ISSET(irq, &driver->filter.irq_mask))
    {
      if (enter)
        {
          g_note_disabled_irq_nest++;
        }
      else
        {
          g_note_disabled_irq_nest--;
        }

      return false;
    }
#endif

  return true;
}
#endif

/****************************************************************************
 * Name: note_isenabled_dump
 *
 * Description:
 *   Check whether the dump instrumentation is enabled.
 *
 * Input Parameters:
 *   driver - The channel of note driver
 *   tag: The dump instrumentation tag
 *
 * Returned Value:
 *   True is returned if the instrumentation is enabled.
 *
 ****************************************************************************/

#ifdef CONFIG_SCHED_INSTRUMENTATION_DUMP
static inline int note_isenabled_dump(FAR struct note_driver_s *driver,
                                      uint8_t tag, uint8_t level, int type)
{
#  ifdef CONFIG_SCHED_INSTRUMENTATION_FILTER
  if (!note_isenabled_type(driver, type))
    {
      return false;
    }

  /* If the dump trace is disabled, do nothing. */

  if (NOTE_FILTER_TAGMASK_ISSET(tag, &driver->filter.tag_mask))
    {
      return false;
    }

  if (level > driver->filter.level.level)
    {
      return false;
    }
#  endif

  return true;
}
#endif

#if defined(CONFIG_SCHED_INSTRUMENTATION_SWITCH) && \
    (CONFIG_DRIVERS_NOTE_TASKNAME_BUFSIZE > 0)
/****************************************************************************
 * Name: note_find_taskname
 *
 * Description:
 *   Find task name info corresponding to the specified PID
 *
 * Input Parameters:
 *   PID - Task ID
 *
 * Returned Value:
 *   Pointer to the task name info
 *   If the corresponding info doesn't exist in the buffer, NULL is returned.
 *
 ****************************************************************************/

static FAR struct note_taskname_info_s *note_find_taskname(pid_t pid)
{
  FAR struct note_taskname_info_s *ti;
  int n = g_note_taskname.tail;

  while (n != g_note_taskname.head)
    {
      ti = (FAR struct note_taskname_info_s *)
            &g_note_taskname.buffer[n];
      if (ti->pid == pid)
        {
          return ti;
        }

      n += ti->size;
      if (n >= CONFIG_DRIVERS_NOTE_TASKNAME_BUFSIZE)
        {
          n -= CONFIG_DRIVERS_NOTE_TASKNAME_BUFSIZE;
        }
    }

  return NULL;
}

/****************************************************************************
 * Name: note_record_taskname
 *
 * Description:
 *   Record the task name info of the specified task
 *
 * Input Parameters:
 *   PID - Task ID
 *   name - task name
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void note_record_taskname(pid_t pid, FAR const char *name)
{
  FAR struct note_taskname_info_s *ti;
  size_t tilen;
  size_t namelen;
  size_t skiplen;
  size_t remain;

  namelen = strlen(name);
  DEBUGASSERT(namelen <= CONFIG_TASK_NAME_SIZE);
  tilen = sizeof(struct note_taskname_info_s) + namelen;
  DEBUGASSERT(tilen <= UCHAR_MAX);

  skiplen = CONFIG_DRIVERS_NOTE_TASKNAME_BUFSIZE - g_note_taskname.head;
  if (skiplen >= tilen + sizeof(struct note_taskname_info_s))
    {
      skiplen = 0; /* Have enough space at the tail - needn't skip */
    }

  if (g_note_taskname.head >= g_note_taskname.tail)
    {
      remain = CONFIG_DRIVERS_NOTE_TASKNAME_BUFSIZE -
               (g_note_taskname.head - g_note_taskname.tail);
    }
  else
    {
      remain = g_note_taskname.tail - g_note_taskname.head;
    }

  while (skiplen + tilen >= remain)
    {
      /* No enough space, drop the old info */

      ti = (FAR struct note_taskname_info_s *)
            &g_note_taskname.buffer[g_note_taskname.tail];
      g_note_taskname.tail = (g_note_taskname.tail + ti->size) %
                             CONFIG_DRIVERS_NOTE_TASKNAME_BUFSIZE;
      remain += ti->size;
    }

  if (skiplen)
    {
      /* Fill the skipped region with an invalid info */

      ti = (FAR struct note_taskname_info_s *)
            &g_note_taskname.buffer[g_note_taskname.head];
      ti->size = skiplen;
      ti->pid = INVALID_PROCESS_ID;
      ti->name[0] = '\0';

      /* Move to the begin of circle buffer */

      g_note_taskname.head = 0;
    }

  ti = (FAR struct note_taskname_info_s *)
        &g_note_taskname.buffer[g_note_taskname.head];
  ti->size = NOTE_ALIGN(tilen);
  ti->pid = pid;
  strlcpy(ti->name, name, namelen + 1);
  g_note_taskname.head += ti->size;
}

#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: note_common
 *
 * Description:
 *   Fill in some of the common fields in the note structure.
 *
 * Input Parameters:
 *   tcb    - The TCB containing the information
 *   note   - The common note structure to use
 *   length - The total lengthof the note structure
 *   type   - The type of the note
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void note_common(FAR struct tcb_s *tcb, FAR struct note_common_s *note,
                 uint8_t length, uint8_t type)
{
  clock_t perftime;

  /* Save all of the common fields */

  note->nc_length = length;
  note->nc_type   = type;

#ifdef CONFIG_DRIVERS_NOTE_CPUID
  note->nc_cpu    = CONFIG_DRIVERS_NOTE_CPUID;
#else
  note->nc_cpu    = this_cpu();
#endif

  if (tcb == NULL)
    {
      note->nc_priority = CONFIG_INIT_PRIORITY;
      note->nc_pid = 0;
    }
  else
    {
      note->nc_priority = tcb->sched_priority;
      note->nc_pid = tcb->pid;
    }

  perftime = NOTE_PERF_GETTIME();
#if CONFIG_DRIVERS_NOTE_CLOCKID >= 0
  if (g_note_perf_offset == 0 && OSINIT_HW_READY())
    {
      struct timespec ts;
      unsigned long freq;

      clock_gettime(CONFIG_DRIVERS_NOTE_CLOCKID, &ts);
      freq = perf_getfreq();

      g_note_perf_offset =
        clkcnt_delta_time2cnt(ts.tv_nsec, freq, NSEC_PER_SEC) +
        ts.tv_sec * freq - perftime;
    }

  note->nc_systime = perftime + g_note_perf_offset;
#else
  note->nc_systime = perftime;
#endif
}

#ifdef CONFIG_SCHED_INSTRUMENTATION

/****************************************************************************
 * Name: sched_note_add
 *
 * Description:
 *   Forward rpmsg note data to individual channels.This process does
 *   not require filtering
 *
 * Input Parameters:
 *   data - The forward note data.
 *   len - The len of forward note data.
 *
 * Returned Value:
 *   None
 *
 * Assumptions:
 *   We are within a critical section.
 *
 ****************************************************************************/

void sched_note_add(FAR const void *data, size_t len)
{
  DEBUGASSERT(data);

  while (len >= sizeof(struct note_common_s))
    {
      FAR struct note_common_s *note = (FAR struct note_common_s *)data;
      size_t notelen = note->nc_length;
      FAR struct note_driver_s **driver;

      DEBUGASSERT(notelen >= sizeof(struct note_common_s) &&
                  len >= notelen);
      for (driver = g_note_drivers; *driver; driver++)
        {
          if (!note_isenabled_type(*driver, note->nc_type))
            {
              continue;
            }

#ifdef CONFIG_SCHED_INSTRUMENTATION_DUMP
          if (note->nc_type > NOTE_DUMP_PRINTF &&
              note->nc_type <= NOTE_DUMP_THREADTIME)
            {
              FAR struct note_event_s *nev =
                  (FAR struct note_event_s *)note;
              if (!note_isenabled_dump(*driver, nev->nev_tag, nev->nev_level,
                                       note->nc_type))
                {
                  continue;
                }
            }
          else if (note->nc_type == NOTE_DUMP_PRINTF)
            {
              FAR struct note_printf_s *npt =
                  (FAR struct note_printf_s *)note;
              if (!note_isenabled_dump(*driver, npt->npt_tag, npt->npt_level,
                                       note->nc_type))
                {
                  continue;
                }
            }
#endif

          if ((*driver)->ops->add == NULL)
            {
              continue;
            }

          /* Add the note to circular buffer */

          note_add(*driver, note, notelen, false);
        }

      /* The note data from note rpmsg is aligned. When parsing
       * the data, it is necessary to align the notelen.
       */

      data += NOTE_ALIGN(notelen);
      len -= NOTE_ALIGN(notelen);
    }
}
#endif

#ifdef CONFIG_SCHED_INSTRUMENTATION_SWITCH
/****************************************************************************
 * Name: sched_note_*
 *
 * Description:
 *   These are the hooks into the scheduling instrumentation logic.  Each
 *   simply formats the note associated with the schedule event and adds
 *   that note to the circular buffer.
 *
 * Input Parameters:
 *   tcb - The TCB of the thread.
 *
 * Returned Value:
 *   None
 *
 * Assumptions:
 *   We are within a critical section.
 *
 ****************************************************************************/

void sched_note_start(FAR struct tcb_s *tcb)
{
  struct note_startalloc_s note;
  unsigned int length;
  FAR struct note_driver_s **driver;
  bool formatted = false;

#if CONFIG_TASK_NAME_SIZE > 0
  int namelen = 0;
#endif

#if CONFIG_DRIVERS_NOTE_TASKNAME_BUFSIZE > 0
  note_record_taskname(tcb->pid, tcb->name);
#endif

  for (driver = g_note_drivers; *driver; driver++)
    {
      if (!note_isenabled_type(*driver, NOTE_START))
        {
          continue;
        }

      if (note_start(*driver, tcb))
        {
          continue;
        }

      if ((*driver)->ops->add == NULL)
        {
          continue;
        }

      if (!formatted)
        {
          formatted = true;

          /* Copy the task name (if possible) and
           * get the length of the note
           */

#if CONFIG_TASK_NAME_SIZE > 0
          if (tcb->name[0] != '\0')
            {
              namelen = strlen(tcb->name);
            }

          DEBUGASSERT(namelen <= CONFIG_TASK_NAME_SIZE);
          strlcpy(note.nsa_name, tcb->name, sizeof(note.nsa_name));

          length = SIZEOF_NOTE_START(namelen + 1);
#else
          length = SIZEOF_NOTE_START(0);
#endif

          /* Finish formatting the note */

          note_common(tcb, &note.nsa_cmn, length, NOTE_START);
        }

      /* Add the note to circular buffer */

      note_add(*driver, &note, length, false);
    }
}

void sched_note_stop(FAR struct tcb_s *tcb)
{
  struct note_stop_s note;
  FAR struct note_driver_s **driver;
  bool formatted = false;

  for (driver = g_note_drivers; *driver; driver++)
    {
      if (!note_isenabled_type(*driver, NOTE_STOP))
        {
          continue;
        }

      if (note_stop(*driver, tcb))
        {
          continue;
        }

      if ((*driver)->ops->add == NULL)
        {
          continue;
        }

      /* Format the note */

      if (!formatted)
        {
          formatted = true;
          note_common(tcb, &note.nsp_cmn, sizeof(struct note_stop_s),
                      NOTE_STOP);
        }

      /* Add the note to circular buffer */

      note_add(*driver, &note, note.nsp_cmn.nc_length, false);
    }
}

void sched_note_taskname(void (FAR *handler)(FAR const void *,
                                             size_t, FAR void *),
                         FAR void *arg)
{
#if CONFIG_DRIVERS_NOTE_TASKNAME_BUFSIZE > 0
  FAR struct note_taskname_info_s *ti;
  struct note_startalloc_s note;
  int n = g_note_taskname.tail;
  size_t length;
  int namelen;

  while (n != g_note_taskname.head)
    {
      ti = (FAR struct note_taskname_info_s *)
            &g_note_taskname.buffer[n];

      if (ti->pid != INVALID_PROCESS_ID)
        {
          namelen = strlen(ti->name);
          strlcpy(note.nsa_name, ti->name, sizeof(note.nsa_name));
          length = SIZEOF_NOTE_START(namelen + 1);
          note_common(NULL, &note.nsa_cmn, length, NOTE_TASKNAME);
          note.nsa_cmn.nc_pid = ti->pid;
          handler((FAR struct note_common_s *)&note, length, arg);
        }

      n += ti->size;
      if (n >= CONFIG_DRIVERS_NOTE_TASKNAME_BUFSIZE)
        {
          n -= CONFIG_DRIVERS_NOTE_TASKNAME_BUFSIZE;
        }
    }
#endif
}

void sched_note_suspend(FAR struct tcb_s *tcb)
{
  struct note_suspend_s note;
  FAR struct note_driver_s **driver;
  bool formatted = false;

  for (driver = g_note_drivers; *driver; driver++)
    {
      if (!note_isenabled_type(*driver, NOTE_SUSPEND))
        {
          continue;
        }

      if (note_suspend(*driver, tcb))
        {
          continue;
        }

      if ((*driver)->ops->add == NULL)
        {
          continue;
        }

      /* Format the note */

      if (!formatted)
        {
          formatted = true;
          note_common(tcb, &note.nsu_cmn, sizeof(struct note_suspend_s),
                      NOTE_SUSPEND);
          note.nsu_state = tcb->task_state;
        }

      /* Add the note to circular buffer */

      note_add(*driver, &note, note.nsu_cmn.nc_length, true);
    }
}

void sched_note_resume(FAR struct tcb_s *tcb)
{
  struct note_resume_s note;
  FAR struct note_driver_s **driver;
  bool formatted = false;

  for (driver = g_note_drivers; *driver; driver++)
    {
      if (!note_isenabled_type(*driver, NOTE_RESUME))
        {
          continue;
        }

      if (note_resume(*driver, tcb))
        {
          continue;
        }

      if ((*driver)->ops->add == NULL)
        {
          continue;
        }

      /* Format the note */

      if (!formatted)
        {
          formatted = true;
          note_common(tcb, &note.nre_cmn, sizeof(struct note_resume_s),
                      NOTE_RESUME);
        }

      /* Add the note to circular buffer */

      note_add(*driver, &note, note.nre_cmn.nc_length, true);
    }
}

#ifdef CONFIG_SMP
void sched_note_cpu_start(FAR struct tcb_s *tcb, int cpu)
{
  struct note_cpu_start_s note;
  FAR struct note_driver_s **driver;
  bool formatted = false;

  for (driver = g_note_drivers; *driver; driver++)
    {
      if (!note_isenabled_type(*driver, NOTE_CPU_START))
        {
          continue;
        }

      if (note_cpu_start(*driver, tcb, cpu))
        {
          continue;
        }

      if ((*driver)->ops->add == NULL)
        {
          continue;
        }

      /* Format the note */

      if (!formatted)
        {
          formatted = true;
          note_common(tcb, &note.ncs_cmn, sizeof(struct note_cpu_start_s),
                      NOTE_CPU_START);
          note.ncs_target = (uint8_t)cpu;
        }

      /* Add the note to circular buffer */

      note_add(*driver, &note, note.ncs_cmn.nc_length, false);
    }
}

void sched_note_cpu_started(FAR struct tcb_s *tcb)
{
  struct note_cpu_started_s note;
  FAR struct note_driver_s **driver;
  bool formatted = false;

  for (driver = g_note_drivers; *driver; driver++)
    {
      if (!note_isenabled_type(*driver, NOTE_CPU_STARTED))
        {
          continue;
        }

      if (note_cpu_started(*driver, tcb))
        {
          continue;
        }

      if ((*driver)->ops->add == NULL)
        {
          continue;
        }

      /* Format the note */

      if (!formatted)
        {
          formatted = true;
          note_common(tcb, &note.ncs_cmn, sizeof(struct note_cpu_started_s),
                      NOTE_CPU_STARTED);
        }

      /* Add the note to circular buffer */

      note_add(*driver, &note, note.ncs_cmn.nc_length, false);
    }
}
#endif /* CONFIG_SMP */
#endif /* CONFIG_SCHED_INSTRUMENTATION_SWITCH */

#ifdef CONFIG_SCHED_INSTRUMENTATION_PREEMPTION
void sched_note_preemption(FAR struct tcb_s *tcb, bool locked)
{
  struct note_preempt_s note;
  FAR struct note_driver_s **driver;
  bool formatted = false;
  irqstate_t flags;

  flags = spin_lock_irqsave_notrace(&g_note_lock);
  for (driver = g_note_drivers; *driver; driver++)
    {
      if (!note_isenabled_type(*driver, NOTE_PREEMPT_LOCK))
        {
          continue;
        }

      if (note_preemption(*driver, tcb, locked))
        {
          continue;
        }

      if ((*driver)->ops->add == NULL)
        {
          continue;
        }

      /* Format the note */

      if (!formatted)
        {
          formatted = true;
          note_common(tcb, &note.npr_cmn, sizeof(struct note_preempt_s),
                      locked ? NOTE_PREEMPT_LOCK : NOTE_PREEMPT_UNLOCK);
          note.npr_count = tcb->lockcount;
        }

      /* Add the note to circular buffer */

      note_add(*driver, &note, note.npr_cmn.nc_length, true);
    }

  spin_unlock_irqrestore_notrace(&g_note_lock, flags);
}
#endif

#ifdef CONFIG_SCHED_INSTRUMENTATION_CSECTION
void sched_note_csection(FAR struct tcb_s *tcb, bool enter)
{
  struct note_csection_s note;
  FAR struct note_driver_s **driver;
  bool formatted = false;

  for (driver = g_note_drivers; *driver; driver++)
    {
      if (!note_isenabled_type(*driver, NOTE_CSECTION_ENTER + enter))
        {
          continue;
        }

      if (note_csection(*driver, tcb, enter))
        {
          continue;
        }

      if ((*driver)->ops->add == NULL)
        {
          continue;
        }

      /* Format the note */

      if (!formatted)
        {
          formatted = true;
          note_common(tcb, &note.ncs_cmn, sizeof(struct note_csection_s),
                      enter ? NOTE_CSECTION_ENTER : NOTE_CSECTION_LEAVE);
#ifdef CONFIG_SMP
          note.ncs_count = tcb->irqcount;
#endif
        }

      /* Add the note to circular buffer */

      note_add(*driver, &note, note.ncs_cmn.nc_length, true);
    }
}
#endif

/****************************************************************************
 * Name: sched_note_spinlock
 *
 * Description:
 *   Common logic for NOTE_SPINLOCK, NOTE_SPINLOCKED, and NOTE_SPINUNLOCK
 *
 * Input Parameters:
 *   tcb  - The TCB containing the information
 *   note - The common note structure to use
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

#ifdef CONFIG_SCHED_INSTRUMENTATION_SPINLOCKS
void sched_note_spinlock(FAR volatile spinlock_t *spinlock,
                         int type)
{
  FAR struct tcb_s *tcb = running_task();
  struct note_spinlock_s note;
  FAR struct note_driver_s **driver;
  bool formatted = false;

  for (driver = g_note_drivers; *driver; driver++)
    {
      if (!note_isenabled_type(*driver, type))
        {
          continue;
        }

      if (note_spinlock(*driver, tcb, spinlock, type))
        {
          continue;
        }

      if ((*driver)->ops->add == NULL)
        {
          continue;
        }

      /* Format the note */

      if (!formatted)
        {
          formatted = true;
          note_common(tcb, &note.nsp_cmn, sizeof(struct note_spinlock_s),
                      type);
          note.nsp_spinlock = (uintptr_t)spinlock;
          note.nsp_value = (uint8_t)spinlock->lock;
        }

      /* Add the note to circular buffer */

      note_add(*driver, &note, note.nsp_cmn.nc_length, true);
    }
}
#endif

#ifdef CONFIG_SCHED_INSTRUMENTATION_SYSCALL
void sched_note_syscall_enter(int nr, int argc, ...)
{
  struct note_syscall_enter_s note;
  FAR struct note_driver_s **driver;
  bool formatted = false;
  FAR struct tcb_s *tcb = running_task();
  unsigned int length = 0;
  va_list ap;

  va_start(ap, argc);
  for (driver = g_note_drivers; *driver; driver++)
    {
      va_list copy;

      if (!note_isenabled_syscall(*driver, NOTE_SYSCALL_ENTER, nr))
        {
          continue;
        }

      va_copy(copy, ap);
      if (note_syscall_enter(*driver, nr, argc, &copy))
        {
          va_end(copy);
          continue;
        }

      if ((*driver)->ops->add == NULL)
        {
          va_end(copy);
          continue;
        }

      /* Format the note */

      if (!formatted)
        {
          int i;
          UNUSED(i);
          formatted = true;
          length = SIZEOF_NOTE_SYSCALL_ENTER(argc);
          note_common(tcb, &note.nsc_cmn, length, NOTE_SYSCALL_ENTER);
          DEBUGASSERT(nr <= UCHAR_MAX);
          note.nsc_nr = nr;

#ifdef CONFIG_SCHED_INSTRUMENTATION_FILTER_SYSCALL_ARGS
          /* If needed, retrieve the given syscall arguments */

          DEBUGASSERT(argc <= MAX_SYSCALL_ARGS);
          note.nsc_argc = argc;

          for (i = 0; i < argc; i++)
            {
              uintptr_t arg;
              arg = (uintptr_t)va_arg(copy, uintptr_t);
              note.nsc_args[i] = arg;
            }
#endif
        }

      va_end(copy);

      /* Add the note to circular buffer */

      note_add(*driver, &note, length, false);
    }

    va_end(ap);
}

void sched_note_syscall_leave(int nr, uintptr_t result)
{
  struct note_syscall_leave_s note;
  FAR struct note_driver_s **driver;
  bool formatted = false;
  FAR struct tcb_s *tcb = running_task();

  for (driver = g_note_drivers; *driver; driver++)
    {
      if (!note_isenabled_syscall(*driver, NOTE_SYSCALL_LEAVE, nr))
        {
          continue;
        }

      if (note_syscall_leave(*driver, nr, result))
        {
          continue;
        }

      if ((*driver)->ops->add == NULL)
        {
          continue;
        }

      /* Format the note */

      if (!formatted)
        {
          formatted = true;
          note_common(tcb, &note.nsc_cmn,
                      sizeof(struct note_syscall_leave_s),
                      NOTE_SYSCALL_LEAVE);
          DEBUGASSERT(nr <= UCHAR_MAX);
          note.nsc_nr = nr;
          note.nsc_result = result;
        }

      /* Add the note to circular buffer */

      note_add(*driver, &note, note.nsc_cmn.nc_length, false);
    }
}
#endif

#ifdef CONFIG_SCHED_INSTRUMENTATION_IRQHANDLER
void sched_note_irqhandler(int irq, FAR void *handler, bool enter)
{
  struct note_irqhandler_s note;
  FAR struct note_driver_s **driver;
  bool formatted = false;
  FAR struct tcb_s *tcb = enter ? running_task() : this_task();

  for (driver = g_note_drivers; *driver; driver++)
    {
      if (!note_isenabled_irq(*driver, irq, enter))
        {
          continue;
        }

      if (note_irqhandler(*driver, irq, handler, enter))
        {
          continue;
        }

      if ((*driver)->ops->add == NULL)
        {
          continue;
        }

      if (!formatted)
        {
          formatted = true;
          note_common(tcb, &note.nih_cmn, sizeof(struct note_irqhandler_s),
                      enter ? NOTE_IRQ_ENTER : NOTE_IRQ_LEAVE);
          note.nih_irq = irq;
          note.nih_handler = (uintptr_t)handler;
        }

      /* Add the note to circular buffer */

      note_add(*driver, &note, note.nih_cmn.nc_length, false);
    }
}
#endif

#ifdef CONFIG_SCHED_INSTRUMENTATION_WDOG
void sched_note_wdog(uint8_t event, FAR void *handler, FAR const void *arg)
{
  FAR struct note_driver_s **driver;
  struct note_wdog_s note;
  bool formatted = false;
  FAR struct tcb_s *tcb = this_task();
  irqstate_t flags;

  flags = enter_critical_section_notrace();
  for (driver = g_note_drivers; *driver; driver++)
    {
      if (!note_isenabled_type(*driver, event))
        {
          continue;
        }

      if (note_wdog(*driver, event, handler, arg))
        {
          continue;
        }

      if ((*driver)->ops->add == NULL)
        {
          continue;
        }

      if (!formatted)
        {
          formatted = true;
          note_common(tcb, &note.nwd_cmn, sizeof(note), event);
          note.handler = (uintptr_t)handler;
          note.arg = (uintptr_t)arg;
        }

      /* Add the note to circular buffer */

      note_add(*driver, &note, note.nwd_cmn.nc_length, false);
    }

  leave_critical_section_notrace(flags);
}
#endif

#ifdef CONFIG_SCHED_INSTRUMENTATION_HEAP
void sched_note_heap(uint8_t event, FAR void *heap, FAR void *mem,
                     size_t size, size_t used)
{
  FAR struct note_driver_s **driver;
  struct note_heap_s note;
  bool formatted = false;
  FAR struct tcb_s *tcb = running_task();

  for (driver = g_note_drivers; *driver; driver++)
    {
      if (!note_isenabled_type(*driver, event))
        {
          continue;
        }

      if (note_heap(*driver, event, heap, mem, size, used))
        {
          continue;
        }

      if ((*driver)->ops->add == NULL)
        {
          continue;
        }

      if (!formatted)
        {
          formatted = true;
          note_common(tcb, &note.nhp_cmn, sizeof(note), event);
          note.heap = (uintptr_t)heap;
          note.mem = (uintptr_t)mem;
          note.size = size;
          note.used = used;
        }

      /* Add the note to circular buffer */

      note_add(*driver, &note, note.nhp_cmn.nc_length, false);
    }
}
#endif

#ifdef CONFIG_SCHED_INSTRUMENTATION_DUMP
size_t note_driver_event_ip(FAR struct note_driver_s *driver, uint8_t tag,
                            uint8_t level, uintptr_t ip, uint8_t event,
                            FAR const void *buf, size_t len)
{
  FAR struct tcb_s *tcb = running_task();
  FAR struct note_event_s *note;
  char data[NOTE_BUFFER_SIZE];
  unsigned int length = 0;

  if (note_event(driver, tag, level, ip, event, buf, len))
    {
      return length;
    }

  if (driver->ops->add == NULL)
    {
      return length;
    }

  /* Format the note */

  note = (FAR struct note_event_s *)data;
  length = SIZEOF_NOTE_EVENT(len);
  if (length >= sizeof(data))
    {
      length = sizeof(data) - 1;
    }

  note_common(tcb, &note->nev_cmn, length, event);
  note->nev_ip = ip;
  note->nev_tag = tag;
  note->nev_level = level;
  if (buf != NULL)
    {
      memcpy(note->nev_data, buf, length - SIZEOF_NOTE_EVENT(0));
    }

  /* Add the note to circular buffer */

  note_add(driver, note, length, false);
  return length - SIZEOF_NOTE_EVENT(0);
}

void note_driver_vprintf_ip(FAR struct note_driver_s *driver, uint8_t tag,
                            uint8_t level, uintptr_t ip, uint64_t type,
                            FAR const char *fmt, FAR va_list *va)
{
  FAR struct tcb_s *tcb = running_task();
  FAR struct note_printf_s *note;
  uint8_t data[NOTE_BUFFER_SIZE];
  size_t length = 0;

  /* Format the note */

  begin_packed_struct union
    {
      int i;
      long l;
#ifdef CONFIG_HAVE_LONG_LONG
      long long ll;
#endif
      intmax_t im;
      size_t sz;
      ptrdiff_t ptr;
      FAR void *p;
      FAR const char *s;
#ifdef CONFIG_HAVE_DOUBLE
      double d;
#  ifdef CONFIG_HAVE_LONG_DOUBLE
      long double ld;
#  endif
#endif
    }

  end_packed_struct *var;
  size_t next = 0;
  note = (FAR struct note_printf_s *)data;
  length = sizeof(data) - SIZEOF_NOTE_PRINTF(0);

  if (note_vprintf(driver, tag, level, ip, type, fmt, *va))
    {
      return;
    }

  if (driver->ops->add == NULL)
    {
      return;
    }

  if (type)
    {
      size_t count = NOTE_PRINTF_GET_COUNT(type);
      size_t i;

      for (i = 0; i < count; i++)
        {
          var = (FAR void *)&note->npt_data[next];
          switch (NOTE_PRINTF_GET_TYPE(type, i))
            {
              case NOTE_PRINTF_UINT32:
                {
                  var->i = va_arg(*va, int);
                  if (next + sizeof(var->i) > length)
                    {
                      break;
                    }

                  next += sizeof(var->i);
                }
              break;
              case NOTE_PRINTF_UINT64:
                {
                  if (next + sizeof(var->ll) > length)
                    {
                      break;
                    }

                  var->ll = va_arg(*va, long long);
                  next += sizeof(var->ll);
                }
              break;
              case NOTE_PRINTF_STRING:
                {
                  size_t len;
                  var->s = va_arg(*va, FAR const char *);
                  if (var->s == NULL)
                    {
                      var->s = "(null)";
                    }

                  len = strlen(var->s) + 1;
                  if (next + len > length)
                    {
                      len = length - next;
                    }

                  strlcpy(note->npt_data + next, var->s, len);
                  next += len;
                }
              break;
#ifdef CONFIG_HAVE_DOUBLE
            case NOTE_PRINTF_DOUBLE:
              {
                var->d = va_arg(*va, double);
                if (next + sizeof(var->d) > length)
                  {
                    break;
                  }

                next += sizeof(var->d);
              }
            break;
#endif
            }
        }
    }
  else
    {
      next = lib_bsprintf(note->npt_data, length, fmt, *va);
      DEBUGASSERT(next < sizeof(data) - sizeof(*note));
    }

  length = SIZEOF_NOTE_PRINTF(next);
  note_common(tcb, &note->npt_cmn, length, NOTE_DUMP_PRINTF);
  note->npt_ip = ip;
  note->npt_tag = tag;
  note->npt_fmt = fmt;
  note->npt_type = type;
  note->npt_level = level;

  /* Add the note to circular buffer */

  note_add(driver, note, length, false);
}

void note_driver_printf_ip(FAR struct note_driver_s *driver, uint8_t tag,
                           uint8_t level, uintptr_t ip, uint64_t type,
                           FAR const char *fmt, ...)
{
  va_list va;
  va_start(va, fmt);
  note_driver_vprintf_ip(driver, tag, level, ip, type, fmt, &va);
  va_end(va);
}

void sched_note_event_ip(uint8_t tag, uint8_t level, uintptr_t ip,
                         uint8_t event, FAR const void *buf,
                         size_t len)
{
  FAR struct note_driver_s **driver;

  for (driver = g_note_drivers; *driver; driver++)
    {
      if (!note_isenabled_dump(*driver, tag, level, event))
        {
          continue;
        }

      note_driver_event_ip(*driver, tag, level, ip, event, buf, len);
    }
}

void sched_note_vprintf_ip(uint8_t tag, uint8_t level, uintptr_t ip,
                           FAR const char *fmt, uint64_t type,
                           FAR va_list *va)
{
  FAR struct note_driver_s **driver;

  for (driver = g_note_drivers; *driver; driver++)
    {
      if (!note_isenabled_dump(*driver, tag, level, NOTE_DUMP_PRINTF))
        {
          continue;
        }

      note_driver_vprintf_ip(*driver, tag, level, ip, type, fmt, va);
    }
}
#endif /* CONFIG_SCHED_INSTRUMENTATION_DUMP */

#ifdef CONFIG_SCHED_INSTRUMENTATION_FILTER

/****************************************************************************
 * Name: note_filter_mode
 *
 * Description:
 *   Set and get note filter mode.
 *   (Same as NOTECTL_GETMODE / NOTECTL_SETMODE ioctls)
 *
 * Input Parameters:
 *   oldm - A writable pointer to struct note_filter_mode_s to get current
 *          filter mode
 *          If 0, no data is written.
 *   newm - A read-only pointer to struct note_filter_mode_s which holds the
 *          new filter mode
 *          If 0, the filter mode is not updated.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void note_filter_mode(FAR struct note_filter_named_mode_s *oldm,
                      FAR struct note_filter_named_mode_s *newm)
{
  irqstate_t irq_mask;
  FAR struct note_driver_s **driver;

  irq_mask = spin_lock_irqsave_notrace(&g_note_lock);

  if (oldm != NULL)
    {
      for (driver = g_note_drivers; *driver; driver++)
        {
          if (oldm->name[0] == '\0')
            {
              oldm->mode = (*driver)->filter.mode;
              strlcpy(oldm->name, (*driver)->name, NAME_MAX);
              break;
            }
          else if (strcmp((*driver)->name, oldm->name) == 0)
            {
              oldm->mode = (*driver)->filter.mode;
              break;
            }
        }
    }

  if (newm != NULL)
    {
      for (driver = g_note_drivers; *driver; driver++)
        {
          if (newm->name[0] == '\0')
            {
               (*driver)->filter.mode = newm->mode;
            }
          else if (0 == strcmp((*driver)->name, newm->name))
            {
               (*driver)->filter.mode = newm->mode;
               break;
            }
        }
    }

  spin_unlock_irqrestore_notrace(&g_note_lock, irq_mask);
}

/****************************************************************************
 * Name: note_filter_syscall
 *
 * Description:
 *   Set and get syscall filter setting
 *   (Same as NOTECTL_GETSYSCALLFILTER / NOTECTL_SETSYSCALLFILTER ioctls)
 *
 * Input Parameters:
 *   oldf - A writable pointer to struct note_filter_syscall_s to get
 *          current syscall filter setting
 *          If 0, no data is written.
 *   newf - A read-only pointer to struct note_filter_syscall_s of the
 *          new syscall filter setting
 *          If 0, the setting is not updated.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

#ifdef CONFIG_SCHED_INSTRUMENTATION_SYSCALL
void note_filter_syscall(FAR struct note_filter_named_syscall_s *oldf,
                         FAR struct note_filter_named_syscall_s *newf)
{
  irqstate_t irq_mask;
  FAR struct note_driver_s **driver;

  irq_mask = spin_lock_irqsave_notrace(&g_note_lock);

  if (oldf != NULL)
    {
      for (driver = g_note_drivers; *driver; driver++)
        {
          if (oldf->name[0] == '\0')
            {
              oldf->syscall_mask = (*driver)->filter.syscall_mask;
              strlcpy(oldf->name, (*driver)->name, NAME_MAX);
              break;
            }
          else if (strcmp((*driver)->name, oldf->name) == 0)
            {
              oldf->syscall_mask = (*driver)->filter.syscall_mask;
              break;
            }
        }
    }

  if (newf != NULL)
    {
      for (driver = g_note_drivers; *driver; driver++)
        {
          if (newf->name[0] == '\0')
            {
               (*driver)->filter.syscall_mask = newf->syscall_mask;
            }
          else if (0 == strcmp((*driver)->name, newf->name))
            {
               (*driver)->filter.syscall_mask = newf->syscall_mask;
               break;
            }
        }
    }

  spin_unlock_irqrestore_notrace(&g_note_lock, irq_mask);
}
#endif

/****************************************************************************
 * Name: note_filter_irq
 *
 * Description:
 *   Set and get IRQ filter setting
 *   (Same as NOTECTL_GETIRQFILTER / NOTECTL_SETIRQFILTER ioctls)
 *
 * Input Parameters:
 *   oldf - A writable pointer to struct note_filter_irq_s to get
 *          current IRQ filter setting
 *          If 0, no data is written.
 *   newf - A read-only pointer to struct note_filter_irq_s of the new
 *          IRQ filter setting
 *          If 0, the setting is not updated.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

#ifdef CONFIG_SCHED_INSTRUMENTATION_IRQHANDLER
void note_filter_irq(FAR struct note_filter_named_irq_s *oldf,
                     FAR struct note_filter_named_irq_s *newf)
{
  irqstate_t irq_mask;
  FAR struct note_driver_s **driver;

  irq_mask = spin_lock_irqsave_notrace(&g_note_lock);

  if (oldf != NULL)
    {
      for (driver = g_note_drivers; *driver; driver++)
        {
          if (oldf->name[0] == '\0')
            {
              oldf->irq_mask = (*driver)->filter.irq_mask;
              strlcpy(oldf->name, (*driver)->name, NAME_MAX);
              break;
            }
          else if (strcmp((*driver)->name, oldf->name) == 0)
            {
              oldf->irq_mask = (*driver)->filter.irq_mask;
              break;
            }
        }
    }

  if (newf != NULL)
    {
      for (driver = g_note_drivers; *driver; driver++)
        {
          if (newf->name[0] == '\0')
            {
               (*driver)->filter.irq_mask = newf->irq_mask;
            }
          else if (0 == strcmp((*driver)->name, newf->name))
            {
               (*driver)->filter.irq_mask = newf->irq_mask;
               break;
            }
        }
    }

  spin_unlock_irqrestore_notrace(&g_note_lock, irq_mask);
}
#endif

/****************************************************************************
 * Name: note_filter_tag
 *
 * Description:
 *   Set and get tag filter setting
 *   (Same as NOTECTL_GETDUMPFILTER / NOTECTL_SETDUMPFILTER ioctls)
 *
 * Input Parameters:
 *   oldf - A writable pointer to struct note_filter_tag_s to get
 *          current dump filter setting
 *          If 0, no data is written.
 *   newf - A read-only pointer to struct note_filter_tag_s of the
 *          new dump filter setting
 *          If 0, the setting is not updated.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

#ifdef CONFIG_SCHED_INSTRUMENTATION_DUMP
void note_filter_tag(FAR struct note_filter_named_tag_s *oldf,
                     FAR struct note_filter_named_tag_s *newf)
{
  FAR struct note_driver_s **driver;
  irqstate_t irq_mask;

  irq_mask = spin_lock_irqsave_notrace(&g_note_lock);

  if (oldf != NULL)
    {
      for (driver = g_note_drivers; *driver; driver++)
        {
          if (oldf->name[0] == '\0')
            {
              oldf->tag_mask = (*driver)->filter.tag_mask;
              strlcpy(oldf->name, (*driver)->name, NAME_MAX);
              break;
            }
          else if (strcmp((*driver)->name, oldf->name) == 0)
            {
              oldf->tag_mask = (*driver)->filter.tag_mask;
              break;
            }
        }
    }

  if (newf != NULL)
    {
      for (driver = g_note_drivers; *driver; driver++)
        {
          if (newf->name[0] == '\0')
            {
               (*driver)->filter.tag_mask = newf->tag_mask;
            }
          else if (0 == strcmp((*driver)->name, newf->name))
            {
               (*driver)->filter.tag_mask = newf->tag_mask;
               break;
            }
        }
    }

  spin_unlock_irqrestore_notrace(&g_note_lock, irq_mask);
}
#endif

#endif /* CONFIG_SCHED_INSTRUMENTATION_FILTER */

/****************************************************************************
 * Name: note_get_taskname
 *
 * Description:
 *   Get the task name string of the specified PID
 *
 * Input Parameters:
 *   PID - Task ID
 *   buf - A writable buffer to hold the task name
 *   len - The length of the buffer
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void note_get_taskname(pid_t pid, FAR char *buf, size_t len)
{
#if CONFIG_TASK_NAME_SIZE > 0
  FAR struct tcb_s *tcb = nxsched_get_tcb(pid);

  if (tcb != NULL)
    {
      strlcpy(buf, tcb->name, len);
      nxsched_put_tcb(tcb);
    }
  else
    {
#  if defined(CONFIG_SCHED_INSTRUMENTATION_SWITCH) && \
      (CONFIG_DRIVERS_NOTE_TASKNAME_BUFSIZE > 0)
      FAR struct note_taskname_info_s *ti = note_find_taskname(pid);

      if (ti != NULL)
        {
          strlcpy(buf, ti->name, len);
        }
      else
        {
          strlcpy(buf, "<noname>", len);
        }
#  else
      strlcpy(buf, "<noname>", len);
#  endif
    }
#else
  strlcpy(buf, "<noname>", len);
#endif
}

/****************************************************************************
 * Name: note_driver_register
 ****************************************************************************/

int note_driver_register(FAR struct note_driver_s *driver)
{
  int i;

  DEBUGASSERT(driver);
  for (i = 0; i < CONFIG_DRIVERS_NOTE_MAX; i++)
    {
      if (g_note_drivers[i] == NULL)
        {
          g_note_drivers[i] = driver;
          return OK;
        }
    }

  return -ENOMEM;
}
