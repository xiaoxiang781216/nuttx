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
#include <syslog.h>

#include <nuttx/init.h>
#include <nuttx/irq.h>
#include <nuttx/sched.h>
#include <nuttx/clock.h>
#include <nuttx/timers/clkcnt.h>
#include <nuttx/note/note_driver.h>
#include <nuttx/syslog/syslog.h>
#include <nuttx/sched_note.h>
#include <nuttx/spinlock.h>
#include <nuttx/streams.h>
#include <nuttx/fs/fs.h>

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

struct note_taskname_foreach_s
{
  CODE void (*handler)(FAR const void *, size_t, FAR void *);
  FAR void *arg;
};

typedef struct note_driver_s *
note_drivers_t[CONFIG_DRIVERS_NOTE_MAX + 1];

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int note_driver_open(FAR struct file *filep);
static int note_driver_close(FAR struct file *filep);
static ssize_t note_driver_read(FAR struct file *filep,
                                FAR char *buffer, size_t buflen);
static ssize_t note_driver_write(FAR struct file *filep,
                                 FAR const char *buffer, size_t buflen);
static int note_driver_ioctl(FAR struct file *filep, int cmd,
                             unsigned long arg);
static int note_driver_poll(FAR struct file *filep, FAR struct pollfd *fds,
                            bool setup);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct file_operations g_note_fops =
{
  .open  = note_driver_open,  /* open */
  .close = note_driver_close, /* close */
  .read  = note_driver_read,  /* read */
  .write = note_driver_write, /* write */
  .ioctl = note_driver_ioctl, /* ioctl */
  .poll  = note_driver_poll,  /* poll */
};

#if defined(CONFIG_SCHED_INSTRUMENTATION_FILTER) && \
    defined(CONFIG_SCHED_INSTRUMENTATION_IRQHANDLER)
static DEFINE_PER_CPU_BSS(unsigned int, g_note_disabled_irq_nest);
#  define g_note_disabled_irq_nest this_cpu_var(g_note_disabled_irq_nest)
#endif

FAR static DEFINE_PER_CPU_BMP(note_drivers_t, g_note_drivers) =
{
#ifndef CONFIG_BMP
#  ifdef CONFIG_DRIVERS_NOTERAM
  (FAR struct note_driver_s *)&g_noteram_driver,
#  endif
#  ifdef CONFIG_DRIVERS_NOTELOG
  (FAR struct note_driver_s *)&g_notelog_driver,
#  endif
#  ifdef CONFIG_DRIVERS_NOTELOWEROUT
  (FAR struct note_driver_s *)&g_notestream_lowerout,
#  endif
#  ifdef CONFIG_DRIVERS_NOTERPMSG
  (FAR struct note_driver_s *)&g_noterpmsg_driver,
#  endif
#  ifdef CONFIG_DRIVERS_NOTET32FDX
  (FAR struct note_driver_s *)&g_notefdx,
#  endif
#  ifdef CONFIG_DRIVERS_NOTESNAP
  (FAR struct note_driver_s *)&g_notesnap,
#  endif
#endif
#ifdef CONFIG_DRIVERS_NOTENET
  (FAR struct note_driver_s *)&g_notenet,
#endif
  NULL
};
#define g_note_drivers this_cpu_var_bmp(g_note_drivers)

static DEFINE_PER_CPU_BSS_BMP(spinlock_t, g_note_lock);
#define g_note_lock this_cpu_var_bmp(g_note_lock)

#if CONFIG_DRIVERS_NOTE_CLOCKID >= 0
static DEFINE_PER_CPU_BSS_BMP(clock_t, g_note_perf_offset);
#  define g_note_perf_offset this_cpu_var_bmp(g_note_perf_offset)
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#ifdef CONFIG_BMP
static void note_drivers_initialize(void)
{
  int i = 0;

  if (g_note_drivers[0] == NULL)
    {
#  ifdef CONFIG_DRIVERS_NOTERAM
      g_note_drivers[i++] = (FAR struct note_driver_s *)&g_noteram_driver;
#  endif
#  ifdef CONFIG_DRIVERS_NOTELOG
      g_note_drivers[i++] = (FAR struct note_driver_s *)&g_notelog_driver;
#  endif
#  ifdef CONFIG_DRIVERS_NOTELOWEROUT
      g_note_drivers[i++] =
        (FAR struct note_driver_s *)&g_notestream_lowerout;
#  endif
#  ifdef CONFIG_DRIVERS_NOTERPMSG
      g_note_drivers[i++] = (FAR struct note_driver_s *)&g_noterpmsg_driver;
#  endif
#  ifdef CONFIG_DRIVERS_NOTET32FDX
      g_note_drivers[i++] = (FAR struct note_driver_s *)&g_notefdx;
#  endif
#  ifdef CONFIG_DRIVERS_NOTESNAP
      g_note_drivers[i++] = (FAR struct note_driver_s *)&g_notesnap;
#  endif
    }
}
#else
#  define note_drivers_initialize()
#endif

/****************************************************************************
 * Name: note_driver_fs_register
 *
 * Description:
 *   Open.the note driver.
 *
 ****************************************************************************/

static int note_driver_fs_register(FAR struct note_driver_s *driver)
{
#ifdef CONFIG_SCHED_INSTRUMENTATION_FILTER
  char devpath[32];
  int ret = 0;

  if (driver->name != NULL && driver->name[0] != '\0')
    {
      snprintf(devpath, sizeof(devpath), "/dev/note/%s", driver->name);

      /* Use driver's private fops if available, otherwise use common fops */

      ret = register_driver(devpath, &g_note_fops, 0666, driver);
    }

  return ret;
#else
  return 0;
#endif
}

/****************************************************************************
 * Name: note_driver_fs_unregister
 *
 * Description:
 *   Open.the note driver.
 *
 ****************************************************************************/

static void note_driver_fs_unregister(FAR struct note_driver_s *driver)
{
#ifdef CONFIG_SCHED_INSTRUMENTATION_FILTER
  char devpath[32];

  snprintf(devpath, sizeof(devpath), "/dev/note/%s", driver->name);
  unregister_driver(devpath);
#endif
}

/****************************************************************************
 * Name: note_driver_open
 *
 * Description:
 *   Open.the note driver.
 *
 ****************************************************************************/

static int note_driver_open(FAR struct file *filep)
{
  FAR struct note_driver_s *driver = filep->f_inode->i_private;

  if (driver != NULL && driver->ops != NULL &&
      driver->ops->open != NULL)
    {
      return driver->ops->open(filep);
    }

  return 0;
}

/****************************************************************************
 * Name: note_driver_close
 *
 * Description:
 *   Close the note driver.
 *
 ****************************************************************************/

static int note_driver_close(FAR struct file *filep)
{
  FAR struct note_driver_s *driver = filep->f_inode->i_private;

  if (driver != NULL && driver->ops != NULL &&
      driver->ops->close != NULL)
    {
      return driver->ops->close(filep);
    }

  return 0;
}

/****************************************************************************
 * Name: note_driver_read
 *
 * Description:
 *   Read from the note driver.
 *
 ****************************************************************************/

static ssize_t note_driver_read(FAR struct file *filep, FAR char *buffer,
                                size_t len)
{
  FAR struct note_driver_s *driver = filep->f_inode->i_private;

  if (driver != NULL && driver->ops != NULL &&
      driver->ops->read != NULL)
    {
      return driver->ops->read(filep, buffer, len);
    }

  return -ENOSYS;
}

/****************************************************************************
 * Name: note_driver_write
 *
 * Description:
 *   Write to the note driver.
 *
 ****************************************************************************/

static ssize_t note_driver_write(FAR struct file *filep,
                                 FAR const char *buffer, size_t len)
{
  FAR struct note_driver_s *driver = filep->f_inode->i_private;

  if (driver != NULL && driver->ops != NULL &&
      driver->ops->write != NULL)
    {
      return driver->ops->write(filep, buffer, len);
    }

  return -ENOSYS;
}

/****************************************************************************
 * Name: note_driver_ioctl
 *
 * Description:
 *   Common ioctl handler for note filter control.
 *
 ****************************************************************************/

static int note_driver_ioctl(FAR struct file *filep, int cmd,
                             unsigned long arg)
{
  int ret = OK;
  irqstate_t irq_mask;
  FAR struct note_driver_s *driver = filep->f_inode->i_private;

  irq_mask = spin_lock_irqsave_notrace(&g_note_lock);
  switch (cmd)
    {
#ifdef CONFIG_SCHED_INSTRUMENTATION_FILTER
      case NOTE_GETFILTER:
        {
          FAR struct note_filter_mode_s *mode =
                         (FAR struct note_filter_mode_s *)arg;
          mode->type_mask = driver->filter.mode.type_mask;
#ifdef CONFIG_SMP
          mode->cpuset = driver->filter.mode.cpuset;
#endif
        }
        break;

      case NOTE_SETFILTER:
        {
          FAR struct note_filter_mode_s *mode =
                        (FAR struct note_filter_mode_s *)arg;
          driver->filter.mode.type_mask = mode->type_mask;
#ifdef CONFIG_SMP
          driver->filter.mode.cpuset = mode->cpuset;
#endif
        }
        break;
#endif

#ifdef CONFIG_SCHED_INSTRUMENTATION_SYSCALL
      case NOTE_GETSYSCALLFILTER:
        {
          FAR struct note_filter_syscall_s *filter =
                        (FAR struct note_filter_syscall_s *)arg;
          *filter = driver->filter.syscall_mask;
        }
        break;

      case NOTE_SETSYSCALLFILTER:
        {
          FAR struct note_filter_syscall_s *filter =
                        (FAR struct note_filter_syscall_s *)arg;
          driver->filter.syscall_mask = *filter;
        }
        break;
#endif

#ifdef CONFIG_SCHED_INSTRUMENTATION_IRQHANDLER
      case NOTE_GETIRQFILTER:
        {
          FAR struct note_filter_irq_s *filter =
                        (FAR struct note_filter_irq_s *)arg;
          *filter = driver->filter.irq_mask;
        }
        break;

      case NOTE_SETIRQFILTER:
        {
          FAR struct note_filter_irq_s *filter =
                        (FAR struct note_filter_irq_s *)arg;
          driver->filter.irq_mask = *filter;
        }
        break;
#endif

#ifdef CONFIG_SCHED_INSTRUMENTATION_DUMP
      case NOTE_GETTAGFILTER:
        {
          FAR struct note_filter_tag_s *filter =
                        (FAR struct note_filter_tag_s *)arg;
          *filter = driver->filter.tag_mask;
        }
        break;

      case NOTE_SETTAGFILTER:
        {
          FAR struct note_filter_tag_s *filter =
                        (FAR struct note_filter_tag_s *)arg;
          driver->filter.tag_mask = *filter;
        }
        break;
#endif

#ifdef CONFIG_SCHED_INSTRUMENTATION_DUMP
      case NOTE_GETLEVEL:
        {
          FAR struct note_filter_level_s *level =
                        (FAR struct note_filter_level_s *)arg;
          level->level = driver->filter.level.level;
        }
        break;

      case NOTE_SETLEVEL:
        {
          FAR struct note_filter_level_s *level =
                        (FAR struct note_filter_level_s *)arg;
          driver->filter.level.level = level->level;
        }
        break;
#endif

      default:
        ret = -ENOTTY;
        break;
    }

  spin_unlock_irqrestore_notrace(&g_note_lock, irq_mask);

  if (ret == -ENOTTY && driver->ops != NULL && driver->ops->ioctl != NULL)
    {
      /* Let the driver handle special ioctls */

      ret = driver->ops->ioctl(filep, cmd, arg);
    }

  return ret;
}

/****************************************************************************
 * Name: note_driver_poll
 *
 * Description:
 *   Poll the note driver.
 *
 ****************************************************************************/

static int note_driver_poll(FAR struct file *filep, FAR struct pollfd *fds,
                            bool setup)
{
  FAR struct note_driver_s *driver = filep->f_inode->i_private;

  if (driver != NULL && driver->ops != NULL &&
      driver->ops->poll != NULL)
    {
      return driver->ops->poll(filep, fds, setup);
    }

  return -ENOSYS;
}

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
#  ifdef CONFIG_SYSLOG_TO_SCHED_NOTE
  if (tag == NOTE_TAG_LOG)
    {
      /* For NOTE_TAG_LOG, use the syslog level mask */

      return (g_syslog_mask & LOG_MASK(level)) != 0;
    }

#endif

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

  note_drivers_initialize();
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

      /* The note data no longer has alignment padding. */

      data += notelen;
      len -= notelen;
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

  nxtask_namepool_record(tcb->pid, tcb->name);

  note_drivers_initialize();
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

  note_drivers_initialize();
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

static void note_taskname_handler(pid_t pid, FAR const char *name,
                                  FAR void *arg)
{
  FAR struct note_taskname_foreach_s *ctx = arg;
  struct note_startalloc_s note;
  size_t length;
  int namelen;

  namelen = strlen(name);
  strlcpy(note.nsa_name, name, sizeof(note.nsa_name));
  length = SIZEOF_NOTE_START(namelen + 1);
  note_common(NULL, &note.nsa_cmn, length, NOTE_TASKNAME);
  note.nsa_cmn.nc_pid = pid;
  ctx->handler(&note, length, ctx->arg);
}

void sched_note_taskname(void (FAR *handler)(FAR const void *,
                                             size_t, FAR void *),
                         FAR void *arg)
{
  struct note_taskname_foreach_s ctx;

  ctx.handler = handler;
  ctx.arg = arg;
  nxtask_namepool_foreach(note_taskname_handler, &ctx);
}

void sched_note_suspend(FAR struct tcb_s *tcb)
{
  struct note_suspend_s note;
  FAR struct note_driver_s **driver;
  bool formatted = false;

  note_drivers_initialize();
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

  note_drivers_initialize();
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

  note_drivers_initialize();
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

  note_drivers_initialize();
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

  note_drivers_initialize();
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

  note_drivers_initialize();
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

  note_drivers_initialize();
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
          note.nsp_value = (uint8_t)(uintptr_t)spinlock->lock;
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

  note_drivers_initialize();
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

  note_drivers_initialize();
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

  note_drivers_initialize();
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

  note_drivers_initialize();
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

  note_drivers_initialize();
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
  note->npt_fmt = (uintptr_t)fmt;
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

  note_drivers_initialize();
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

  note_drivers_initialize();
  for (driver = g_note_drivers; *driver; driver++)
    {
      va_list copy;
      if (!note_isenabled_dump(*driver, tag, level, NOTE_DUMP_PRINTF))
        {
          continue;
        }

      va_copy(copy, *va);
      note_driver_vprintf_ip(*driver, tag, level, ip, type, fmt, &copy);
      va_end(copy);
    }
}
#endif /* CONFIG_SCHED_INSTRUMENTATION_DUMP */

/****************************************************************************
 * Name: note_driver_initialize
 ****************************************************************************/

int note_driver_initialize(void)
{
  int i;
  int ret = 0;

  note_drivers_initialize();
  for (i = 0; i < CONFIG_DRIVERS_NOTE_MAX; i++)
    {
      if (g_note_drivers[i] != NULL)
        {
          ret = note_driver_fs_register(g_note_drivers[i]);
          if (ret < 0)
            {
              break;
            }
        }
    }

  return ret;
}

/****************************************************************************
 * Name: note_driver_register
 ****************************************************************************/

int note_driver_register(FAR struct note_driver_s *driver)
{
  int i;
  irqstate_t irq_mask;

  DEBUGASSERT(driver);
  irq_mask = spin_lock_irqsave_notrace(&g_note_lock);

  /* Find an empty slot to register the driver */

  note_drivers_initialize();
  for (i = 0; i < CONFIG_DRIVERS_NOTE_MAX; i++)
    {
      if (g_note_drivers[i] == NULL)
        {
          g_note_drivers[i] = driver;
          break;
        }
    }

  spin_unlock_irqrestore_notrace(&g_note_lock, irq_mask);

  if (i == CONFIG_DRIVERS_NOTE_MAX)
    {
      return -ENOSPC;
    }

  /* Register device node /dev/note/xxx */

  return note_driver_fs_register(driver);
}

/****************************************************************************
 * Name: note_driver_unregister
 ****************************************************************************/

void note_driver_unregister(FAR struct note_driver_s *driver)
{
  int i;
  irqstate_t irq_mask;

  DEBUGASSERT(driver);

  note_drivers_initialize();
  irq_mask = spin_lock_irqsave_notrace(&g_note_lock);
  for (i = 0; i < CONFIG_DRIVERS_NOTE_MAX; i++)
    {
      if (g_note_drivers[i] == driver)
        {
          note_driver_fs_unregister(driver);
          g_note_drivers[i] = NULL;
          break;
        }
    }

  spin_unlock_irqrestore_notrace(&g_note_lock, irq_mask);
}
