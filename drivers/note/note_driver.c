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
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <time.h>
#include <syslog.h>
#include <sys/param.h>

#include <nuttx/init.h>
#include <nuttx/irq.h>
#include <nuttx/sched.h>
#include <nuttx/clock.h>
#include <nuttx/clock_notifier.h>
#include <nuttx/timers/clkcnt.h>
#include <nuttx/note/note_driver.h>
#include <nuttx/note/notebuf_driver.h>
#include <nuttx/reboot_notifier.h>
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

#if defined(CONFIG_DRIVERS_NOTERAM) + defined(CONFIG_DRIVERS_NOTELOG) + \
    defined(CONFIG_DRIVERS_NOTESNAP) + defined(CONFIG_DRIVERS_NOTERTT) + \
    defined(CONFIG_SEGGER_SYSVIEW) + defined(CONFIG_DRIVERS_NOTELOWEROUT) + \
    defined(CONFIG_DRIVERS_NOTERPMSG) + defined(CONFIG_DRIVERS_NOTET32FDX) + \
    defined(CONFIG_DRIVERS_NOTENET) + defined(CONFIG_DRIVERS_NOTESTREAM) \
    > CONFIG_DRIVERS_NOTE_MAX
#  error "Maximum channel number exceeds. "
#endif

#define note_do_start(drv, tcb)                                                 \
  ((drv)->ops->start && ((drv)->ops->start(drv, tcb), true))
#define note_do_stop(drv, tcb)                                                  \
  ((drv)->ops->stop && ((drv)->ops->stop(drv, tcb), true))
#define note_do_taskname(drv, tcb)                                              \
  ((drv)->ops->taskname && ((drv)->ops->taskname(drv, tcb), true))
#define note_do_suspend(drv, tcb)                                               \
  ((drv)->ops->suspend && ((drv)->ops->suspend(drv, tcb), true))
#define note_do_resume(drv, tcb)                                                \
  ((drv)->ops->resume && ((drv)->ops->resume(drv, tcb), true))
#define note_do_ready(drv, tcb, state)                                          \
  ((drv)->ops->ready && ((drv)->ops->ready(drv, tcb, state), true))
#define note_do_cpu_start(drv, tcb, cpu)                                        \
  ((drv)->ops->cpu_start && ((drv)->ops->cpu_start(drv, tcb, cpu), true))
#define note_do_cpu_started(drv, tcb)                                           \
  ((drv)->ops->cpu_started && ((drv)->ops->cpu_started(drv, tcb), true))
#define note_do_preemption(drv, tcb, locked)                                    \
  ((drv)->ops->preemption && ((drv)->ops->preemption(drv, tcb, locked), true))
#define note_do_csection(drv, tcb, enter)                                       \
  ((drv)->ops->csection && ((drv)->ops->csection(drv, tcb, enter), true))
#define note_do_spinlock(drv, tcb, spinlock, type)                              \
  ((drv)->ops->spinlock &&                                                   \
  ((drv)->ops->spinlock(drv, tcb, spinlock, type), true))
#define note_do_syscall_enter(drv, nr, argc, ap)                                \
  ((drv)->ops->syscall_enter &&                                              \
  ((drv)->ops->syscall_enter(drv, nr, argc, ap), true))
#define note_do_syscall_leave(drv, nr, result)                                  \
  ((drv)->ops->syscall_leave &&                                              \
  ((drv)->ops->syscall_leave(drv, nr, result), true))
#define note_do_irqhandler(drv, irq, handler, enter)                            \
  ((drv)->ops->irqhandler &&                                                 \
  ((drv)->ops->irqhandler(drv, irq, handler, enter), true))
#define note_do_heap(drv, event, data, mem, size, used)                         \
  ((drv)->ops->heap && ((drv)->ops->heap(drv, event, data, mem, size, used), true))
#define note_do_wdog(drv, event, handler, arg)                                  \
  ((drv)->ops->wdog && ((drv)->ops->wdog(drv, event, handler, arg), true))
#define note_do_wqueue(drv, event, worker, arg)                                 \
  ((drv)->ops->wqueue &&                                                     \
  ((drv)->ops->wqueue(drv, event, worker, arg), true))
#define note_do_event(drv, event, tag, level, ip, buf, len)                          \
  ((drv)->ops->event && ((drv)->ops->event(drv, event, tag, level, ip, buf, len), true))
#define note_do_vprintf(drv, tag, level, ip, type, fmt, va)                                \
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

#ifdef CONFIG_SCHED_INSTRUMENTATION_FILTER
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
#endif
static int note_reboot_handler(FAR struct notifier_block *nb,
                               unsigned long action, FAR void *data);

/****************************************************************************
 * Private Data
 ****************************************************************************/

#ifdef CONFIG_SCHED_INSTRUMENTATION_FILTER
static const struct file_operations g_note_fops =
{
  .open  = note_driver_open,  /* open */
  .close = note_driver_close, /* close */
  .read  = note_driver_read,  /* read */
  .write = note_driver_write, /* write */
  .ioctl = note_driver_ioctl, /* ioctl */
  .poll  = note_driver_poll,  /* poll */
};
#endif

#if defined(CONFIG_SCHED_INSTRUMENTATION_FILTER) && \
    defined(CONFIG_SCHED_INSTRUMENTATION_IRQHANDLER)
static DEFINE_PER_CPU_BSS(unsigned int, g_note_disabled_irq_nest);
#  define g_note_disabled_irq_nest this_cpu_var(g_note_disabled_irq_nest)
#endif

#if defined(CONFIG_DRIVERS_NOTESTREAM) && defined(CONFIG_DRIVERS_NOTEBUF)

/* Buffer defined independently so it can be placed in a specific section */

static
#ifdef DRIVERS_NOTEBUF_SECTION
locate_data(CONFIG_DRIVERS_NOTEBUF_SECTION)
#endif
RING_DEFINE_BMP(g_notestream_ring, CONFIG_DRIVERS_NOTEBUF_BUFSIZE);
#define g_notestream_ring this_cpu_var_bmp(g_notestream_ring)

static DEFINE_PER_CPU_BMP(struct notebuf_driver_s, g_notebufstream) =
{
  {
#  ifdef CONFIG_SCHED_INSTRUMENTATION_FILTER
    "stream",
    {
      {
        CONFIG_DRIVERS_NOTESTREAM_FILTER_DEFAULT_MODE,
#    ifndef CONFIG_UP
        CONFIG_DRIVERS_NOTESTREAM_CPUSET,
#    endif
      },
#    ifdef CONFIG_SCHED_INSTRUMENTATION_DUMP
      {
        CONFIG_DRIVERS_NOTE_DEFAULT_LEVEL,
      },
#    endif
    },
#  endif
    &g_notebuf_ops,
  },
  (FAR struct note_driver_s *)&g_notestream,
  (FAR struct ring_s *)&g_notestream_ring,
#  ifdef CONFIG_DRIVERS_NOTESTREAM_IRQSAFE
  true,
#  endif
};

#  define g_notebufstream this_cpu_var_bmp(g_notebufstream)
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
  (FAR struct note_driver_s *)&g_notelowout,
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
#  ifdef CONFIG_DRIVERS_NOTESTREAM
#    ifdef CONFIG_DRIVERS_NOTEBUF
  (FAR struct note_driver_s *)&g_notebufstream,
#    else
  (FAR struct note_driver_s *)&g_notestream,
#    endif
#  endif
#endif
  NULL
};
#define g_note_drivers this_cpu_var_bmp(g_note_drivers)

static DEFINE_PER_CPU_BSS_BMP(spinlock_t, g_note_lock);
#define g_note_lock this_cpu_var_bmp(g_note_lock)

#if CONFIG_DRIVERS_NOTE_CLOCKID > 0
static DEFINE_PER_CPU_BSS_BMP(clock_t, g_note_perf_offset);
#  define g_note_perf_offset this_cpu_var_bmp(g_note_perf_offset)
static DEFINE_PER_CPU_BSS_BMP(struct notifier_block, g_note_clock_nb);
#  define g_note_clock_nb this_cpu_var_bmp(g_note_clock_nb)
#endif

static DEFINE_PER_CPU_BMP(struct notifier_block, g_note_reboot_nb) =
{
  note_reboot_handler
};
#define g_note_reboot_nb this_cpu_var_bmp(g_note_reboot_nb)

/****************************************************************************
 * Public Data
 ****************************************************************************/

#ifdef CONFIG_SCHED_INSTRUMENTATION_FILTER
#undef g_note_filter
DEFINE_PER_CPU_BMP(struct note_filter_s, g_note_filter);
#define g_note_filter this_cpu_var_bmp(g_note_filter)
#undef g_note_keys
DEFINE_STATIC_KEY_ARRAY_FALSE(g_note_keys, NOTE_TYPE_LAST);

/* After this point, plain 'g_note_keys' resolves to this_cpu's copy
 * for use in the hot-path sched_note_xxx functions.
 * Each CPU only reads/patches its own key array — no cross-CPU access
 * is needed because note_update_filter() runs per-CPU.
 */

#define g_note_keys REFERENCE_STATIC_KEY(g_note_keys)
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#if CONFIG_DRIVERS_NOTE_CLOCKID > 0

static void note_update_perf_offset(FAR const struct timespec *ts)
{
  unsigned long freq = perf_getfreq();
  clock_t perftime = NOTE_PERF_GETTIME();

  g_note_perf_offset =
    clkcnt_delta_time2cnt(ts->tv_nsec, freq, NSEC_PER_SEC) +
    ts->tv_sec * freq - perftime;
}

static int note_clock_notify(FAR struct notifier_block *nb,
                             unsigned long action, FAR void *data)
{
  if (action == CLOCK_REALTIME)
    {
      note_update_perf_offset((FAR const struct timespec *)data);
    }

  return 0;
}
#endif

/****************************************************************************
 * Name: note_do_add
 *
 * Description:
 *   Dispatch a formatted note to a backend's add() and update cumulative
 *   statistics based on the ssize-style return contract:
 *
 *     ret <= 0         : full drop, drop_bytes += notelen
 *     0 < ret < notelen: partial accept, bytes += ret,
 *                        drop_bytes += notelen - ret, notes += 1
 *     ret == notelen   : full accept, bytes += notelen, notes += 1
 *
 *   Stats accounting is centralized here so backends only have to honor
 *   the return contract.  Counters are updated with relaxed atomics so
 *   the producer fast path stays lock-free.
 *
 ****************************************************************************/

static inline_function ssize_t note_do_add(FAR struct note_driver_s *drv,
                                           FAR const void *note,
                                           size_t notelen,
                                           bool noswitches)
{
  ssize_t written = drv->ops->add(drv, note, notelen, noswitches);

#ifdef CONFIG_DRIVERS_NOTE_STATS
  if (written <= 0)
    {
      atomic_add_relaxed(&drv->stats.drop_bytes, notelen);
    }
  else
    {
      atomic_add_relaxed(&drv->stats.notes, 1);
      atomic_add_relaxed(&drv->stats.bytes, written);
      if (written < notelen)
        {
          atomic_add_relaxed(&drv->stats.drop_bytes,
                             notelen - written);
        }
    }
#endif

  return written;
}

#ifdef CONFIG_SCHED_INSTRUMENTATION_FILTER
/****************************************************************************
 * Name: note_update_filter
 *
 * Description:
 *   Recompute g_note_filter by AND-ing all registered per-driver
 *   filters.
 *   A note type/syscall/irq/tag is globally disabled only when EVERY
 *   driver has it disabled.
 *
 ****************************************************************************/

static void note_update_filter(void)
{
#ifdef CONFIG_SCHED_INSTRUMENTATION_SYSCALL
  FAR struct note_filter_syscall_s *syscall =
                              &g_note_filter.syscall_mask;
#endif
#ifdef CONFIG_SCHED_INSTRUMENTATION_IRQHANDLER
  FAR struct note_filter_irq_s *irq = &g_note_filter.irq_mask;
#endif
#ifdef CONFIG_SCHED_INSTRUMENTATION_DUMP
  FAR struct note_filter_tag_s *tag = &g_note_filter.tag_mask;
  FAR uint8_t *level = &g_note_filter.level.level;
#endif
  FAR uint64_t *type_mask = &g_note_filter.mode.type_mask;
  size_t i;
  size_t j;

  UNUSED(j);

  /* Step 1: Compute type_mask by AND-ing all drivers.
   * NOTE_ALL is folded into type_mask: if all drivers have
   * NOTE_ALL set, the global type_mask will have it set too,
   * and all type bits will be set to "disabled".
   */

  *type_mask = UINT64_MAX;

  for (i = 0; i < CONFIG_DRIVERS_NOTE_MAX; i++)
    {
      if (g_note_drivers[i] == NULL)
        {
          continue;
        }

      *type_mask &= g_note_drivers[i]->filter.mode.type_mask;
    }

  /* If NOTE_ALL is set in the computed type_mask, force all
   * type bits to disabled.
   */

  if (*type_mask & (1ULL << NOTE_ALL))
    {
      *type_mask = UINT64_MAX;
    }

  /* Sync global static keys from the computed type_mask.
   * Convention: bit SET in type_mask = type DISABLED (filtered out).
   * Keys are static_key_false (default NOP = disabled).
   */

  for (i = 1; i < NOTE_TYPE_LAST; i++)
    {
      bool want_enabled = !(*type_mask & (1ULL << i));
      bool enabled = static_key_enabled(&g_note_keys[i].key);

      if (want_enabled && !enabled)
        {
          static_key_enable(&g_note_keys[i].key);
        }
      else if (!want_enabled && enabled)
        {
          static_key_disable(&g_note_keys[i].key);
        }
    }

  /* Step 2: Compute sub-masks.  If the corresponding type
   * bits are disabled in the global type_mask, set the
   * sub-mask to all-filtered (0xff) directly — no need to
   * iterate drivers.
   */

#ifdef CONFIG_SCHED_INSTRUMENTATION_SYSCALL
  NOTE_FILTER_SYSCALLMASK_FILL(syscall);

  if ((*type_mask & NOTE_FILTER_MODE_FLAG_SYSCALL) !=
      NOTE_FILTER_MODE_FLAG_SYSCALL)
    {
      for (i = 0; i < CONFIG_DRIVERS_NOTE_MAX; i++)
        {
          if (g_note_drivers[i] == NULL)
            {
              continue;
            }

          for (j = 0; j < nitems(syscall->syscall_mask); j++)
            {
              syscall->syscall_mask[j] &=
                g_note_drivers[i]->filter.syscall_mask.syscall_mask[j];
            }
        }
    }
#endif

#ifdef CONFIG_SCHED_INSTRUMENTATION_IRQHANDLER
  NOTE_FILTER_IRQMASK_FILL(irq);

  if ((*type_mask & NOTE_FILTER_MODE_FLAG_IRQ) !=
      NOTE_FILTER_MODE_FLAG_IRQ)
    {
      for (i = 0; i < CONFIG_DRIVERS_NOTE_MAX; i++)
        {
          if (g_note_drivers[i] == NULL)
            {
              continue;
            }

          for (j = 0; j < nitems(irq->irq_mask); j++)
            {
              irq->irq_mask[j] &=
                g_note_drivers[i]->filter.irq_mask.irq_mask[j];
            }
        }
    }
#endif

#ifdef CONFIG_SCHED_INSTRUMENTATION_DUMP
  NOTE_FILTER_TAGMASK_FILL(tag);
  *level = 0;

  if ((*type_mask & NOTE_FILTER_MODE_FLAG_DUMP) !=
      NOTE_FILTER_MODE_FLAG_DUMP)
    {
      for (i = 0; i < CONFIG_DRIVERS_NOTE_MAX; i++)
        {
          if (g_note_drivers[i] == NULL)
            {
              continue;
            }

          for (j = 0; j < nitems(tag->tag_mask); j++)
            {
              tag->tag_mask[j] &=
                g_note_drivers[i]->filter.tag_mask.tag_mask[j];
            }

          if (g_note_drivers[i]->filter.level.level > *level)
            {
              *level = g_note_drivers[i]->filter.level.level;
            }
        }
    }
#endif
}
#else
#  define note_update_filter()
#endif /* CONFIG_SCHED_INSTRUMENTATION_FILTER */

static int note_reboot_handler(FAR struct notifier_block *nb,
                               unsigned long action,
                               FAR void *data)
{
  FAR struct note_driver_s **driver;

  for (driver = g_note_drivers; *driver; driver++)
    {
      if ((*driver)->ops->flush)
        {
          (*driver)->ops->flush(*driver);
        }
    }

  return 0;
}

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
        (FAR struct note_driver_s *)&g_notelowout;
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
#  ifdef CONFIG_DRIVERS_NOTESTREAM
#    ifdef CONFIG_DRIVERS_NOTEBUF
      g_notebufstream.lower = (FAR struct note_driver_s *)&g_notestream;
      g_notebufstream.ring =
          ring_mpmc_init(&g_notestream_ring, CONFIG_DRIVERS_NOTEBUF_BUFSIZE);
      g_note_drivers[i++] = (FAR struct note_driver_s *)&g_notebufstream;
#    else
      g_note_drivers[i++] = (FAR struct note_driver_s *)&g_notestream;
#    endif
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

#ifdef CONFIG_SCHED_INSTRUMENTATION_FILTER

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

#ifdef CONFIG_SCHED_INSTRUMENTATION_SWITCH
static void note_sync_taskname_cb(FAR const void *data,
                                  size_t len, FAR void *arg)
{
  FAR struct note_driver_s *driver = arg;
  note_do_add(driver, data, len, false);
}
#endif

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

  /* Backends get first chance: this lets a stacked backend (e.g. notebuf)
   * handle commands that need to consult its lower layer (NOTE_CLEAR,
   * NOTE_GETSTATS combined view, NOTE_CLEARSTATS).  Anything the backend
   * does not recognize falls through to the common handler below.
   */

  if (driver->ops != NULL && driver->ops->ioctl != NULL)
    {
      ret = driver->ops->ioctl(filep, cmd, arg);
      if (ret != -ENOTTY)
        {
          return ret;
        }

      ret = OK;
    }

#ifdef CONFIG_SCHED_INSTRUMENTATION_SWITCH
  if (cmd == NOTE_SYNCTASKNAME)
    {
      note_taskname_foreach(note_sync_taskname_cb, driver);
      return ret;
    }
#endif

  irq_mask = spin_lock_irqsave_notrace(&g_note_lock);
  switch (cmd)
    {
#ifdef CONFIG_SCHED_INSTRUMENTATION_FILTER
      case NOTE_GETFILTER:
        {
          FAR struct note_filter_mode_s *mode =
                         (FAR struct note_filter_mode_s *)arg;
          mode->type_mask = driver->filter.mode.type_mask;
#ifndef CONFIG_UP
          mode->cpuset = driver->filter.mode.cpuset;
#endif
        }
        break;

      case NOTE_SETFILTER:
        {
          FAR struct note_filter_mode_s *mode =
                        (FAR struct note_filter_mode_s *)arg;
          driver->filter.mode.type_mask = mode->type_mask;
#ifndef CONFIG_UP
          driver->filter.mode.cpuset = mode->cpuset;
#endif
          note_update_filter();
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
          note_update_filter();
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
          note_update_filter();
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
          note_update_filter();
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
          note_update_filter();
        }
        break;
#endif

      case NOTE_FLUSH:
        spin_unlock_irqrestore_notrace(&g_note_lock, irq_mask);
        if (driver->ops->flush)
          {
            driver->ops->flush(driver);
          }

        return OK;

#ifdef CONFIG_DRIVERS_NOTE_STATS
      /* NOTE_GETSTATS / NOTE_CLEARSTATS
       *   Read or clear the cumulative producer counters maintained by
       *   note_do_add().  Available on every backend uniformly; stacked
       *   backends (notebuf) override these in their ops->ioctl to merge
       *   the upper- and lower-layer counters before this fallthrough.
       */

      case NOTE_GETSTATS:
        {
          FAR struct note_stats_s *out = (FAR struct note_stats_s *)arg;
          out->notes      = atomic_read(&driver->stats.notes);
          out->bytes      = atomic_read(&driver->stats.bytes);
          out->drop_bytes = atomic_read(&driver->stats.drop_bytes);
        }
        break;

      case NOTE_CLEARSTATS:
        atomic_set(&driver->stats.notes, 0);
        atomic_set(&driver->stats.bytes, 0);
        atomic_set(&driver->stats.drop_bytes, 0);
        break;
#endif

      default:
        ret = -ENOTTY;
        break;
    }

  spin_unlock_irqrestore_notrace(&g_note_lock, irq_mask);

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

#endif /* CONFIG_SCHED_INSTRUMENTATION_FILTER */

/****************************************************************************
 * Name: note_driver_isenabled
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

static inline int note_driver_isenabled(FAR struct note_driver_s *driver)
{
#ifdef CONFIG_SCHED_INSTRUMENTATION_FILTER
  if (NOTE_FILTER_TYPEMASK_ISSET(NOTE_ALL, &driver->filter.mode))
    {
      return false;
    }

#ifndef CONFIG_UP
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
 * Name: note_driver_isenabled_type
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

static inline int
note_driver_isenabled_type(FAR struct note_driver_s *driver, int type)
{
#ifdef CONFIG_SCHED_INSTRUMENTATION_FILTER
  if (!note_driver_isenabled(driver))
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
 * Name: note_spinlock
 *
 * Description:
 *   Common spinlock note logic
 *
 ****************************************************************************/

#ifdef CONFIG_SCHED_INSTRUMENTATION_SPINLOCKS
static void note_spinlock(FAR volatile void *spinlock, int type)
{
  FAR struct tcb_s *tcb = running_task();
  FAR struct note_driver_s **driver;
  struct note_spinlock_s note;
  bool formatted = false;

  note_drivers_initialize();
  for (driver = g_note_drivers; *driver; driver++)
    {
      if (!note_driver_isenabled_type(*driver, type))
        {
          continue;
        }

      if (note_do_spinlock(*driver, tcb, spinlock, type))
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
          note.nsp_value = (uint8_t)(uintptr_t)
            ((FAR volatile spinlock_t *)spinlock)->lock;
        }

      /* Add the note to circular buffer */

      note_do_add(*driver, &note, note.nsp_cmn.nc_length, true);
    }
}
#endif

/****************************************************************************
 * Name: note_driver_isenabled_syscall
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
static inline int
note_driver_isenabled_syscall(FAR struct note_driver_s *driver, int type,
                              int nr)
{
#ifdef CONFIG_SCHED_INSTRUMENTATION_FILTER
  if (!note_driver_isenabled_type(driver, type))
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

static void note_vsyscall_enter(int nr, int argc, va_list ap)
{
  FAR struct tcb_s *tcb = running_task();
  FAR struct note_driver_s **driver;
  struct note_syscall_enter_s note;
  unsigned int length = 0;
  bool formatted = false;

  note_drivers_initialize();
  for (driver = g_note_drivers; *driver; driver++)
    {
      va_list copy;

      if (!note_driver_isenabled_syscall(*driver, NOTE_SYSCALL_ENTER, nr))
        {
          continue;
        }

      va_copy(copy, ap);
      if (note_do_syscall_enter(*driver, nr, argc, &copy))
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

      note_do_add(*driver, &note, length, false);
    }
}

/****************************************************************************
 * Name: note_syscall_leave
 *
 * Description:
 *   Record a system call leave note into all enabled note drivers.
 *
 * Input Parameters:
 *   nr     - The system call number
 *   result - The system call return value
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void note_syscall_leave(int nr, uintptr_t result)
{
  FAR struct tcb_s *tcb = running_task();
  FAR struct note_driver_s **driver;
  struct note_syscall_leave_s note;
  bool formatted = false;

  note_drivers_initialize();
  for (driver = g_note_drivers; *driver; driver++)
    {
      if (!note_driver_isenabled_syscall(*driver, NOTE_SYSCALL_LEAVE, nr))
        {
          continue;
        }

      if (note_do_syscall_leave(*driver, nr, result))
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

      note_do_add(*driver, &note, note.nsc_cmn.nc_length, false);
    }
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
static inline int note_driver_isenabled_irq(FAR struct note_driver_s *driver,
                                            int irq, bool enter)
{
#ifdef CONFIG_SCHED_INSTRUMENTATION_FILTER
  int type = NOTE_IRQ_ENTER + !enter;

  if (!note_driver_isenabled_type(driver, type))
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
 * Name: note_driver_isenabled_dump
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
static inline int
note_driver_isenabled_dump(FAR struct note_driver_s *driver, uint8_t tag,
                           uint8_t level, int type)
{
#  ifdef CONFIG_SYSLOG_TO_SCHED_NOTE
  if (tag == NXTAG_LOG)
    {
      /* For NXTAG_LOG, use the syslog level mask */

      return (g_syslog_mask[NXTAG_GLOBAL] & LOG_MASK(level)) != 0;
    }

#endif

#  ifdef CONFIG_SCHED_INSTRUMENTATION_FILTER
  if (!note_driver_isenabled_type(driver, type))
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

#ifdef CONFIG_DRIVERS_NOTE_TEXT_DUMP

/****************************************************************************
 * Name: note_dump_header
 ****************************************************************************/

static int note_dump_header(FAR struct lib_outstream_s *s,
                            FAR const struct note_common_s *note)
{
  struct timespec ts;
  char buf[TASK_NAME_SIZE];
  pid_t pid;
  int ret;

  NOTE_PERF_CONVERT(note->nc_systime, &ts);
  pid = note->nc_pid;
#ifdef CONFIG_SMP
  int cpu = note->nc_cpu;
#else
  int cpu = 0;
#endif

  nxtask_namepool_get(pid, buf, TASK_NAME_SIZE);

  ret = lib_sprintf(s, "%8s-%-3u [%d] %3" PRIuTM ".%09lu: ",
                    buf, get_pid(pid), cpu,
                    ts.tv_sec, ts.tv_nsec);

  return ret;
}

#if defined(CONFIG_SCHED_INSTRUMENTATION_SWITCH) || \
    defined(CONFIG_SCHED_INSTRUMENTATION_IRQHANDLER)
/****************************************************************************
 * Name: note_dump_sched_switch
 ****************************************************************************/

static int note_dump_sched_switch(FAR struct lib_outstream_s *s,
                                  FAR struct note_dump_context_s *ctx)
{
  char current_buf[TASK_NAME_SIZE];
  char next_buf[TASK_NAME_SIZE];
  uint8_t current_priority;
  uint8_t next_priority;
  pid_t current_pid;
  pid_t next_pid;
  int ret;

  current_pid = ctx->current_pid;
  next_pid = ctx->next_pid;

  current_priority = ctx->current_priority;
  next_priority = ctx->next_priority;

  nxtask_namepool_get(current_pid, current_buf, TASK_NAME_SIZE);
  nxtask_namepool_get(next_pid, next_buf, TASK_NAME_SIZE);

  ret = lib_sprintf(s, "sched_switch: prev_comm=%s prev_pid=%u "
                    "prev_prio=%u prev_state=%c ==> "
                    "next_comm=%s next_pid=%u next_prio=%u\n",
                    current_buf, get_pid(current_pid),
                    current_priority, get_task_state(ctx->current_state),
                    next_buf, get_pid(next_pid),
                    next_priority);

  ctx->current_pid = ctx->next_pid;
  ctx->current_priority = ctx->next_priority;
  ctx->pendingswitch = false;
  return ret;
}
#endif

/****************************************************************************
 * Name: note_dump_printf
 ****************************************************************************/

#ifdef CONFIG_SCHED_INSTRUMENTATION_DUMP
static int note_dump_printf(FAR struct lib_outstream_s *s,
                            FAR struct note_printf_s *note)
{
  size_t ret = 0;

  if (note->npt_type == 0)
    {
      ret = lib_osprintf(s, (FAR const char *)note->npt_fmt, note->npt_data);
    }
  else
    {
      size_t count = NOTE_PRINTF_GET_COUNT(note->npt_type);
      char fmt[128];
      size_t i;

      fmt[0] = '\0';
      ret += lib_sprintf(s, "%p", (FAR void *)note->npt_fmt);
      for (i = 0; i < count; i++)
        {
          int type = NOTE_PRINTF_GET_TYPE(note->npt_type, i);

          switch (type)
            {
              case NOTE_PRINTF_UINT32:
                {
                  strcat(fmt, " %u");
                }
                break;
              case NOTE_PRINTF_UINT64:
                {
                  strcat(fmt, " %llu");
                }
                break;
              case NOTE_PRINTF_STRING:
                {
                  strcat(fmt, " %s");
                }
                break;
              case NOTE_PRINTF_DOUBLE:
                {
                  strcat(fmt, " %f");
                }
            }
        }

        ret += lib_osprintf(s, fmt, note->npt_data);
    }

  return ret;
}
#endif

#endif /* CONFIG_DRIVERS_NOTE_TEXT_DUMP */

/****************************************************************************
 * Name: note_type_size
 *
 * Description:
 *   Return the exact byte size of a fixed-length note type, or zero for a
 *   variable-length type (where only nc_length >= the common header can be
 *   checked).  The sizes mirror the write side so a header whose type is
 *   fixed but whose length disagrees can be rejected during resync.
 *
 ****************************************************************************/

static inline_function size_t note_type_size(uint8_t type)
{
  switch (type)
    {
      case NOTE_STOP:
      case NOTE_RESUME:
      case NOTE_READY:
        return sizeof(struct note_common_s);
      case NOTE_SUSPEND:
        return sizeof(struct note_suspend_s);
      case NOTE_CPU_START:
        return sizeof(struct note_cpu_start_s);
      case NOTE_CPU_STARTED:
        return sizeof(struct note_cpu_started_s);
      case NOTE_PREEMPT_LOCK:
      case NOTE_PREEMPT_UNLOCK:
        return sizeof(struct note_preempt_s);
      case NOTE_CSECTION_ENTER:
      case NOTE_CSECTION_LEAVE:
        return sizeof(struct note_csection_s);
      case NOTE_SPINLOCK_LOCK:
      case NOTE_SPINLOCK_LOCKED:
      case NOTE_SPINLOCK_UNLOCK:
      case NOTE_SPINLOCK_ABORT:
        return sizeof(struct note_spinlock_s);
      case NOTE_SYSCALL_LEAVE:
        return sizeof(struct note_syscall_leave_s);
      case NOTE_IRQ_ENTER:
      case NOTE_IRQ_LEAVE:
        return sizeof(struct note_irqhandler_s);
      case NOTE_WDOG_START:
      case NOTE_WDOG_CANCEL:
      case NOTE_WDOG_ENTER:
      case NOTE_WDOG_LEAVE:
        return sizeof(struct note_wdog_s);
      case NOTE_WQUEUE_START:
      case NOTE_WQUEUE_CANCEL:
      case NOTE_WQUEUE_ENTER:
      case NOTE_WQUEUE_LEAVE:
        return sizeof(struct note_wqueue_s);
      case NOTE_HEAP_ADD:
      case NOTE_HEAP_REMOVE:
      case NOTE_HEAP_ALLOC:
      case NOTE_HEAP_FREE:
        return sizeof(struct note_heap_s);
      default:
        return 0;
    }
}

/****************************************************************************
 * Name: note_is_valid
 *
 * Description:
 *   Check whether a common note header read at some ring offset is
 *   plausible: a real type, a CPU within range, and a length that fits the
 *   remaining bytes.  For a fixed-length type the length must match exactly.
 *
 ****************************************************************************/

static inline_function bool
note_is_valid(FAR const struct note_common_s *hdr)
{
  size_t size;

  if (hdr->nc_type <= NOTE_ALL || hdr->nc_type >= NOTE_TYPE_LAST ||
      hdr->nc_cpu >= CONFIG_NCPUS ||
      hdr->nc_length < sizeof(struct note_common_s))
    {
      return false;
    }

  size = note_type_size(hdr->nc_type);
  return size == 0 || hdr->nc_length == size;
}

/****************************************************************************
 * Name: note_read
 ****************************************************************************/

static inline_function ssize_t
note_read(FAR struct note_dump_stream_s *stream)
{
  FAR struct note_common_s *cmn = &stream->note.cmn;
  uint32_t start;
  uint32_t base;
  uint32_t len;
  int scan;

  do
    {
      /* Scan forward for the first position whose header validates.  A
       * fragment left by an overwrite cannot exceed one note, so bound the
       * scan by the maximum note length.
       */

      for (scan = 0; scan < NOTE_BUFFER_SIZE; scan++)
        {
          base = stream->pos;
          len = ring_mpmc_peek(stream->ring, (FAR uint8_t *)cmn,
                               sizeof(*cmn), &stream->pos);
          if (len < sizeof(*cmn))
            {
              /* Less than a whole header remains; nothing left to dump. */

              return 0;
            }

          if (note_is_valid(cmn))
            {
              break;
            }

          /* No valid header here.  The peek reads from the live cons_tail
           * when base fell behind it, so the real read start is
           * stream->pos - len.
           */

          start = ring_wrap(stream->pos - len);
          if (base != RING_IDX_OLDEST && start != base)
            {
              /* An overwrite evicted the scanned bytes, re-anchor the scan
               * to the new cons_tail and restart the fragment-length budget.
               */

              stream->pos = start;
              scan = -1;
            }
          else
            {
              stream->pos = ring_wrap(start + 1);
            }
        }

      if (scan == NOTE_BUFFER_SIZE)
        {
          return -EAGAIN;
        }

      /* stream->pos now sits at the payload; read it from there.  A short
       * read means the whole note is not present, so nothing is left.
       */

      start = stream->pos;
      len = cmn->nc_length - sizeof(*cmn);
      if (ring_mpmc_peek(stream->ring, stream->note.data, len,
                         &stream->pos) != len)
        {
          return 0;
        }

      /* If an overwrite advanced cons_tail between the header and payload
       * peeks, need rescan.
       */
    }
  while (stream->pos != ring_wrap(start + len));

  return cmn->nc_length;
}

/****************************************************************************
 * Name: note_dump_gets
 ****************************************************************************/

static ssize_t note_dump_gets(FAR struct lib_instream_s *self,
                              FAR void *buffer, size_t len)
{
  FAR struct note_dump_stream_s *stream =
                              (FAR struct note_dump_stream_s *)self;
  FAR char *dest = buffer;
  FAR char *end = dest + len;
  ssize_t ret = 0;

  /* Emit as many whole notes as fit into the caller's buffer.  Binary mode
   * hands back the raw notes unchanged; text mode formats each note into
   * ftrace text.  A note that would not fit is left for the next call so it
   * is never split; the caller must provide a buffer large enough for at
   * least one note.
   */

  for (; ; )
    {
      uint32_t saved = stream->pos;
      ssize_t notelen;

      ret = note_read(stream);
      if (ret <= 0)
        {
          break;
        }

#ifdef CONFIG_DRIVERS_NOTE_TEXT_DUMP
      if (!stream->binary)
        {
          struct lib_memoutstream_s out;

          /* note_dump_one() returns the full formatted length even when the
           * stream clamps it, so a length past the remaining space means
           * the whole note did not fit.
           */

          lib_memoutstream(&out, dest, end - dest);
          notelen = note_dump_one(stream, &stream->note, &out.common);
          if (notelen > out.buflen)
            {
              /* Leave this note for the next call so it is never split. */

              stream->pos = saved;
              break;
            }
        }
      else
#endif
        {
          notelen = stream->note.cmn.nc_length;
          if (dest + notelen > end)
            {
              /* Leave this note for the next call so it is never split. */

              stream->pos = saved;
              break;
            }

          memcpy(dest, &stream->note, notelen);
        }

      dest += notelen;
    }

  /* If no note was emitted, propagate the last read error (for example
   * -EAGAIN at the end of the ring) so the caller can tell end-of-data
   * apart from a short fill.
   */

  return dest != buffer ? dest - (FAR char *)buffer : ret;
}

/****************************************************************************
 * Name: note_dump_getc
 ****************************************************************************/

static int note_dump_getc(FAR struct lib_instream_s *self)
{
  /* This stream only yields formatted text through gets(); a single-byte
   * read has no meaning here and must not consume a note.
   */

  UNUSED(self);
  return -EINVAL;
}

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
#if CONFIG_DRIVERS_NOTE_CLOCKID > 0
  if (g_note_perf_offset == 0 && OSINIT_HW_READY())
    {
      struct timespec ts;

      if (clock_gettime(CONFIG_DRIVERS_NOTE_CLOCKID, &ts) == 0)
        {
          note_update_perf_offset(&ts);

          g_note_clock_nb.notifier_call = note_clock_notify;
          register_clock_notifier(&g_note_clock_nb);
        }
    }

  note->nc_systime = perftime + g_note_perf_offset;
#else
  note->nc_systime = perftime;
#endif
}

/****************************************************************************
 * Name: note_dump_stream
 ****************************************************************************/

void note_dump_stream(FAR struct note_dump_stream_s *stream,
                      FAR struct ring_s *ring, bool binary)
{
  int cpu;

#ifndef CONFIG_DRIVERS_NOTE_TEXT_DUMP
  /* Without the text formatter built in, only binary dumps are possible. */

  DEBUGASSERT(binary);
#endif

  /* Wire the embedded non-consuming ring input stream. */

  stream->ring        = ring;
  stream->binary      = binary;
  stream->pos         = RING_IDX_OLDEST;
  stream->common.getc = note_dump_getc;
  stream->common.gets = note_dump_gets;
  stream->common.nget = 0;

  /* Initialize the trace dump context */

  for (cpu = 0; cpu < CONFIG_SMP_NCPUS; cpu++)
    {
      stream->ctx[cpu].intr_nest = 0;
      stream->ctx[cpu].pendingswitch = false;
      stream->ctx[cpu].current_state = TSTATE_TASK_RUNNING;
      stream->ctx[cpu].current_pid = -1;
      stream->ctx[cpu].next_pid = -1;
      stream->ctx[cpu].current_priority = -1;
      stream->ctx[cpu].next_priority = -1;
    }
}

#ifdef CONFIG_DRIVERS_NOTE_TEXT_DUMP

/****************************************************************************
 * Name: note_dump_one
 ****************************************************************************/

int note_dump_one(FAR struct note_dump_stream_s *stream,
                  FAR const struct note_max_s *p,
                  FAR struct lib_outstream_s *s)
{
  FAR const struct note_common_s *note = &p->cmn;
  FAR struct note_dump_context_s *ctx;
#ifdef CONFIG_SCHED_INSTRUMENTATION_SWITCH
  char buf[TASK_NAME_SIZE];
#endif
#ifdef CONFIG_SMP
  int cpu = note->nc_cpu;
#else
  int cpu = 0;
#endif
  int ret = 0;
  pid_t pid;

  ctx = &stream->ctx[cpu];
  pid = note->nc_pid;

  if (ctx->current_pid < 0)
    {
      ctx->current_pid = pid;
    }

  /* Output one note */

  switch (note->nc_type)
    {
#ifdef CONFIG_SCHED_INSTRUMENTATION_SWITCH
    case NOTE_START:
      {
        nxtask_namepool_get(pid, buf, TASK_NAME_SIZE);
        ret += note_dump_header(s, note);
        ret += lib_sprintf(s, "sched_wakeup_new: comm=%s pid=%d "
                           "target_cpu=%d\n",
                           buf, get_pid(pid), cpu);
      }
      break;

    case NOTE_STOP:
      {
        /* This note informs the task to be stopped.
         * Change current task state for the succeeding NOTE_RESUME.
         */

        ctx->current_state = 0;
      }
      break;

    case NOTE_TASKNAME:
      {
        FAR const char *name =
          (FAR const char *)p + sizeof(struct note_common_s);

        ret += note_dump_header(s, note);
        ret += lib_sprintf(s, "task_rename: pid=%d comm=%s\n",
                           get_pid(pid), name);
      }
      break;

    case NOTE_SUSPEND:
      {
        FAR struct note_suspend_s *nsu = (FAR struct note_suspend_s *)p;
        int i;

        UNUSED(i);

        /* This note informs the task to be suspended.
         * Preserve the information for the succeeding NOTE_RESUME.
         */

        ctx->current_state = nsu->nsu_state;

#if CONFIG_SCHED_INSTRUMENTATION_SWITCH_BACKTRACE > 0
        ret += lib_sprintf(s, "   <-");
        for (i = 0; i < CONFIG_SCHED_INSTRUMENTATION_SWITCH_BACKTRACE; i++)
          {
            if (nsu->nsu_backtrace[i] == 0)
              {
                break;
              }

            ret += lib_sprintf(s, " 0x%" PRIxPTR, nsu->nsu_backtrace[i]);
          }

        ret += lib_sprintf(s, "\n");
#endif
      }
      break;

    case NOTE_RESUME:
      {
        /* This note informs the task to be resumed.
         * The task switch timing depends on the running context.
         */

        ctx->next_pid = pid;
        ctx->next_priority = note->nc_priority;

        if (ctx->intr_nest == 0)
          {
            /* If not in the interrupt context, the task switch is
             * executed immediately.
             */

            ret += note_dump_header(s, note);
            ret += note_dump_sched_switch(s, ctx);
          }
        else
          {
            /* If in the interrupt context, the task switch is postponed
             * until leaving the interrupt handler.
             */

            nxtask_namepool_get(ctx->next_pid, buf, TASK_NAME_SIZE);
            ret += note_dump_header(s, note);
            ret += lib_sprintf(s, "sched_waking: comm=%s "
                               "pid=%d target_cpu=%d\n",
                               buf, get_pid(ctx->next_pid), cpu);
            ctx->pendingswitch = true;
          }
      }
      break;

    case NOTE_READY:
      {
        /* This note informs the task to be ready to run.
         * Output sched_waking using the note's own pid/priority,
         * which identifies the task that just became runnable.
         */

        nxtask_namepool_get(pid, buf, TASK_NAME_SIZE);
        ret += note_dump_header(s, note);
        ret += lib_sprintf(s, "sched_waking: comm=%s "
                           "pid=%d prio=%d target_cpu=%d\n",
                           buf, get_pid(pid), note->nc_priority, cpu);
      }
      break;
#endif

#ifdef CONFIG_SCHED_INSTRUMENTATION_SYSCALL
    case NOTE_SYSCALL_ENTER:
      {
        FAR struct note_syscall_enter_s *nsc;
        int i;

        UNUSED(i);
        nsc = (FAR struct note_syscall_enter_s *)p;
        if (nsc->nsc_nr < CONFIG_SYS_RESERVED ||
            nsc->nsc_nr >= SYS_maxsyscall)
          {
            break;
          }

        ret += note_dump_header(s, note);
        ret += lib_sprintf(s, "sys_%s(",
                           g_funcnames[nsc->nsc_nr - CONFIG_SYS_RESERVED]);

#ifdef CONFIG_SCHED_INSTRUMENTATION_FILTER_SYSCALL_ARGS
        for (i = 0; i < nsc->nsc_argc; i++)
          {
            uintptr_t arg;
            arg = nsc->nsc_args[i];
            if (i == 0)
              {
                ret += lib_sprintf(s, "arg%d: 0x%" PRIxPTR, i, arg);
              }
            else
              {
                ret += lib_sprintf(s, ", arg%d: 0x%" PRIxPTR, i, arg);
              }
          }
#endif

        ret += lib_sprintf(s, ")\n");
      }
      break;

    case NOTE_SYSCALL_LEAVE:
      {
        FAR struct note_syscall_leave_s *nsc;
        uintptr_t result;

        nsc = (FAR struct note_syscall_leave_s *)p;
        if (nsc->nsc_nr < CONFIG_SYS_RESERVED ||
            nsc->nsc_nr >= SYS_maxsyscall)
          {
            break;
          }

        ret += note_dump_header(s, note);
        result = nsc->nsc_result;
        ret += lib_sprintf(s, "sys_%s -> 0x%" PRIxPTR "\n",
                           g_funcnames[nsc->nsc_nr - CONFIG_SYS_RESERVED],
                           result);
      }
      break;
#endif

#ifdef CONFIG_SCHED_INSTRUMENTATION_IRQHANDLER
    case NOTE_IRQ_ENTER:
      {
        FAR struct note_irqhandler_s *nih;

        nih = (FAR struct note_irqhandler_s *)p;
        ret += note_dump_header(s, note);
        ret += lib_sprintf(s, "irq_handler_entry: irq=%u name=%pS\n",
                           nih->nih_irq, (FAR void *)nih->nih_handler);
        ctx->intr_nest++;
      }
      break;

    case NOTE_IRQ_LEAVE:
      {
        FAR struct note_irqhandler_s *nih;

        nih = (FAR struct note_irqhandler_s *)p;
        ret += note_dump_header(s, note);
        ret += lib_sprintf(s, "irq_handler_exit: irq=%u ret=handled\n",
                           nih->nih_irq);
        ctx->intr_nest--;

        if (ctx->intr_nest <= 0)
          {
            ctx->intr_nest = 0;
            if (ctx->pendingswitch)
              {
                /* If the pending task switch exists, it is executed here */

                ret += note_dump_header(s, note);
                ret += note_dump_sched_switch(s, ctx);
              }
          }
      }
      break;
#endif

#ifdef CONFIG_SCHED_INSTRUMENTATION_WDOG
    case NOTE_WDOG_START:
    case NOTE_WDOG_CANCEL:
      {
        FAR struct note_wdog_s *nw;

        nw = (FAR struct note_wdog_s *)p;
        ret += note_dump_header(s, note);
        ret += lib_sprintf(s, "tracing_mark_write: I|%d|"
                           "wdog_%s: handler=%pS arg=0x%" PRIxPTR "\n",
                           pid,
                           note->nc_type == NOTE_WDOG_START
                           ? "start" : "cancel",
                           (FAR void *)nw->handler, nw->arg);
      }
      break;
    case NOTE_WDOG_ENTER:
    case NOTE_WDOG_LEAVE:
      {
        FAR struct note_wdog_s *nw;

        nw = (FAR struct note_wdog_s *)p;
        ret += note_dump_header(s, note);
        ret += lib_sprintf(s, "tracing_mark_write: %c|%d|"
                           "wdog: handler=%pS arg=0x%" PRIxPTR "\n",
                           note->nc_type == NOTE_WDOG_ENTER ? 'B' : 'E',
                           pid, (FAR void *)nw->handler, nw->arg);
      }
      break;
#endif

#ifdef CONFIG_SCHED_INSTRUMENTATION_CSECTION
    case NOTE_CSECTION_ENTER:
    case NOTE_CSECTION_LEAVE:
      {
        struct note_csection_s *ncs;
        ncs = (FAR struct note_csection_s *)p;
        ret += note_dump_header(s, &ncs->ncs_cmn);
        ret += lib_sprintf(s, "tracing_mark_write: %c|%d|critical_section\n",
                           note->nc_type == NOTE_CSECTION_ENTER ?
                           'B' : 'E', pid);
      }
      break;
#endif

#ifdef CONFIG_SCHED_INSTRUMENTATION_PREEMPTION
    case NOTE_PREEMPT_LOCK:
    case NOTE_PREEMPT_UNLOCK:
      {
        struct note_preempt_s *npr;
        int16_t count;
        npr = (FAR struct note_preempt_s *)p;
        count = npr->npr_count;
        ret += note_dump_header(s, &npr->npr_cmn);
        ret += lib_sprintf(s,  "tracing_mark_write: %c|%d|sched_lock:%d\n",
                          note->nc_type == NOTE_PREEMPT_LOCK ?
                          'B' : 'E', pid, count);
      }
      break;
#endif

#ifdef CONFIG_SCHED_INSTRUMENTATION_DUMP
    case NOTE_DUMP_PRINTF:
      {
        FAR struct note_printf_s *npt;

        npt = (FAR struct note_printf_s *)p;
        ret += note_dump_header(s, &npt->npt_cmn);
        ret += lib_sprintf(s, "tracing_mark_write: I|%d|", pid);
        ret += note_dump_printf(s, npt);
        lib_stream_putc(s, '\n');
        ret++;
      }
      break;
    case NOTE_DUMP_BEGIN:
    case NOTE_DUMP_END:
      {
        FAR struct note_event_s *nbi = (FAR struct note_event_s *)p;
        char c = note->nc_type == NOTE_DUMP_BEGIN ? 'B' : 'E';
        int len = note->nc_length - SIZEOF_NOTE_EVENT(0);
        uintptr_t ip;

        ip = nbi->nev_ip;
        ret += note_dump_header(s, &nbi->nev_cmn);
        if (len > 0)
          {
            ret += lib_sprintf(s, "tracing_mark_write: %c|%d|%.*s\n",
                               c, pid, len, (FAR const char *)nbi->nev_data);
          }
        else
          {
            ret += lib_sprintf(s, "tracing_mark_write: %c|%d|%pS\n",
                               c, pid, (FAR void *)ip);
          }
      }
      break;
    case NOTE_DUMP_MARK:
      {
        FAR struct note_event_s *nbi = (FAR struct note_event_s *)p;
        int len = note->nc_length - SIZEOF_NOTE_EVENT(0);

        ret += note_dump_header(s, &nbi->nev_cmn);
        ret += lib_sprintf(s, "tracing_mark_write: I|%d|%.*s\n",
                           pid, len, (FAR const char *)nbi->nev_data);
      }
      break;
      case NOTE_DUMP_BINARY:
      {
        FAR struct note_event_s *nbi = (FAR struct note_event_s *)p;
        int len = note->nc_length - SIZEOF_NOTE_EVENT(0);
        int i;

        ret += note_dump_header(s, &nbi->nev_cmn);
        ret += lib_sprintf(s, "tracing_mark_write: I|%d|", pid);

        for (i = 0; i < len; i++)
          {
            ret += lib_sprintf(s, "%02x ", nbi->nev_data[i]);
          }

        lib_stream_putc(s, '\n');
        ret++;
      }
      break;
    case NOTE_DUMP_COUNTER:
      {
        FAR struct note_event_s *nbi = (FAR struct note_event_s *)p;
        FAR struct note_counter_s *counter;
        int64_t value;
        counter = (FAR struct note_counter_s *)nbi->nev_data;

        /* nev_data is not word aligned, copy the wide field out first. */

        memcpy(&value, &counter->value, sizeof(value));
        ret += note_dump_header(s, &nbi->nev_cmn);
        ret += lib_sprintf(s, "tracing_mark_write: C|%d|%s|%" PRId64 "\n",
                           pid, counter->name, value);
      }
      break;
    case NOTE_DUMP_THREADTIME:
      {
        FAR struct note_event_s *nev = (FAR struct note_event_s *)p;
        FAR struct note_threadtime_s *nts = (FAR struct note_threadtime_s *)
                                            nev->nev_data;
        clock_t elapsed;

        /* nev_data is not word aligned, copy the wide field out first. */

        memcpy(&elapsed, &nts->elapsed, sizeof(elapsed));
        ret += note_dump_header(s, &nev->nev_cmn);
        ret += lib_sprintf(s, "tracing_mark_write: I|%d|elapsed:%llu\n",
                           pid, (unsigned long long)elapsed);
      }
      break;
    case NOTE_DUMP_ASYNC_BEGIN:
    case NOTE_DUMP_ASYNC_END:
      {
        FAR struct note_event_s *nev = (FAR struct note_event_s *)p;
        FAR struct note_async_s *nas = (FAR struct note_async_s *)
                                       nev->nev_data;
        size_t total = note->nc_length - SIZEOF_NOTE_EVENT(0)
                         - sizeof(struct note_async_s);
        int32_t cookie;
        size_t nlen;

        if (note->nc_length <= SIZEOF_NOTE_EVENT(0)
              + sizeof(struct note_async_s))
          {
            break;
          }

        /* nev_data is not word aligned, copy the wide field out first. */

        memcpy(&cookie, &nas->cookie, sizeof(cookie));
        nlen = strnlen(nas->names, total) + 1;
        if (nlen > total)
          {
            nlen = total;
          }

        ret += note_dump_header(s, &nev->nev_cmn);
        if (nlen < total)
          {
            ret += lib_sprintf(s, "tracing_mark_write: "
                               "%c|%d|%s|%s|%" PRId32 "\n",
                               note->nc_type == NOTE_DUMP_ASYNC_BEGIN
                               ? 'G' : 'H',
                               pid, nas->names,
                               nas->names + nlen, cookie);
          }
        else
          {
            ret += lib_sprintf(s, "tracing_mark_write: "
                               "%c|%d|%s|%" PRId32 "\n",
                               note->nc_type == NOTE_DUMP_ASYNC_BEGIN
                               ? 'S' : 'F',
                               pid, nas->names, cookie);
          }
      }
      break;
#endif
#ifdef CONFIG_SCHED_INSTRUMENTATION_WQUEUE
    case NOTE_WQUEUE_START:
    case NOTE_WQUEUE_CANCEL:
      {
        FAR struct note_wqueue_s *nwq;

        nwq = (FAR struct note_wqueue_s *)p;
        ret += note_dump_header(s, note);
        ret += lib_sprintf(s, "tracing_mark_write: I|%d|"
                           "wqueue_%s: worker=%pS arg=0x%" PRIxPTR "\n",
                           pid, note->nc_type == NOTE_WQUEUE_START
                           ? "enqueue" : "cancel",
                           (FAR void *)nwq->worker, nwq->arg);
      }
      break;
    case NOTE_WQUEUE_ENTER:
    case NOTE_WQUEUE_LEAVE:
      {
        FAR struct note_wqueue_s *nwq;

        nwq = (FAR struct note_wqueue_s *)p;
        ret += note_dump_header(s, note);
        ret += lib_sprintf(s, "tracing_mark_write: %c|%d|"
                           "wqueue: worker=%pS arg=0x%" PRIxPTR "\n",
                           note->nc_type == NOTE_WQUEUE_ENTER ? 'B' : 'E',
                           pid, (FAR void *)nwq->worker, nwq->arg);
      }
      break;
#endif
#ifdef CONFIG_SCHED_INSTRUMENTATION_HEAP
    case NOTE_HEAP_ADD:
    case NOTE_HEAP_REMOVE:
    case NOTE_HEAP_ALLOC:
    case NOTE_HEAP_FREE:
      {
        FAR struct note_heap_s *nmm = (FAR struct note_heap_s *)p;
        FAR const char *name[] =
          {
            "add", "remove", "malloc", "free"
          };

        ret += note_dump_header(s, &nmm->nhp_cmn);
        ret += lib_sprintf(s, "tracing_mark_write: C|%d|Heap Usage|%zu|%s"
                           ": heap: 0x%" PRIxPTR
                           " size: %zu, address: 0x%" PRIxPTR "\n",
                           pid, nmm->used,
                           name[note->nc_type - NOTE_HEAP_ADD],
                           nmm->heap, nmm->size, nmm->mem);
      }
      break;
#endif
    default:
      break;
    }

  /* Return the length of the processed note */

  return ret;
}

#endif /* CONFIG_DRIVERS_NOTE_TEXT_DUMP */

#ifdef CONFIG_SCHED_INSTRUMENTATION

/****************************************************************************
 * Name: note_add
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

void note_add(FAR const void *data, size_t len)
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
          if (!note_driver_isenabled_type(*driver, note->nc_type))
            {
              continue;
            }

#ifdef CONFIG_SCHED_INSTRUMENTATION_DUMP
          if (note->nc_type > NOTE_DUMP_PRINTF &&
              note->nc_type <= NOTE_DUMP_ASYNC_END)
            {
              FAR struct note_event_s *nev =
                  (FAR struct note_event_s *)note;
              if (!note_driver_isenabled_dump(*driver, nev->nev_tag,
                                              nev->nev_level, note->nc_type))
                {
                  continue;
                }
            }
          else if (note->nc_type == NOTE_DUMP_PRINTF)
            {
              FAR struct note_printf_s *npt =
                  (FAR struct note_printf_s *)note;
              if (!note_driver_isenabled_dump(*driver, npt->npt_tag,
                                              npt->npt_level, note->nc_type))
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

          note_do_add(*driver, note, notelen, false);
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

/****************************************************************************
 * Name: note_format_taskname
 *
 * Description:
 *   Format the task-name payload shared by NOTE_START and NOTE_TASKNAME
 *   events.  The caller supplies the pre-allocated note buffer, the owning
 *   TCB (may be NULL when emitting on behalf of the name pool), the task
 *   name string and the concrete event type.
 *
 * Returned Value:
 *   Total length of the formatted note, suitable for note_do_add().
 *
 ****************************************************************************/

static unsigned int
note_format_taskname(FAR struct note_startalloc_s *note,
                     FAR struct tcb_s *tcb, FAR const char *name,
                     uint8_t type)
{
  unsigned int length;

#if CONFIG_TASK_NAME_SIZE > 0
  int namelen = 0;

  if (name != NULL && name[0] != '\0')
    {
      namelen = strlen(name);
    }

  DEBUGASSERT(namelen <= CONFIG_TASK_NAME_SIZE);
  strlcpy(note->nsa_name, name != NULL ? name : "",
          sizeof(note->nsa_name));
  length = SIZEOF_NOTE_START(namelen + 1);
#else
  UNUSED(name);
  length = SIZEOF_NOTE_START(0);
#endif

  note_common(tcb, &note->nsa_cmn, length, type);
  return length;
}

void note_start(FAR struct tcb_s *tcb)
{
  struct note_startalloc_s note;
  unsigned int length;
  FAR struct note_driver_s **driver;
  bool formatted = false;

  nxtask_namepool_record(tcb->pid, tcb->name);

  note_drivers_initialize();
  for (driver = g_note_drivers; *driver; driver++)
    {
      if (!note_driver_isenabled_type(*driver, NOTE_START))
        {
          continue;
        }

      if (note_do_start(*driver, tcb))
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
          length = note_format_taskname(&note, tcb, tcb->name, NOTE_START);
        }

      /* Add the note to circular buffer */

      note_do_add(*driver, &note, length, false);
    }
}

void note_stop(FAR struct tcb_s *tcb)
{
  struct note_stop_s note;
  FAR struct note_driver_s **driver;
  bool formatted = false;

  note_drivers_initialize();
  for (driver = g_note_drivers; *driver; driver++)
    {
      if (!note_driver_isenabled_type(*driver, NOTE_STOP))
        {
          continue;
        }

      if (note_do_stop(*driver, tcb))
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

      note_do_add(*driver, &note, note.nsp_cmn.nc_length, false);
    }
}

static void note_taskname_foreach_handler(pid_t pid, FAR const char *name,
                                          FAR void *arg)
{
  FAR struct note_taskname_foreach_s *ctx = arg;
  struct note_startalloc_s note;
  size_t length;

  length = note_format_taskname(&note, NULL, name, NOTE_TASKNAME);
  note.nsa_cmn.nc_pid = pid;
  ctx->handler(&note, length, ctx->arg);
}

void note_taskname_foreach(void (FAR *handler)(FAR const void *,
                                               size_t, FAR void *),
                           FAR void *arg)
{
  struct note_taskname_foreach_s ctx;

  ctx.handler = handler;
  ctx.arg = arg;
  nxtask_namepool_foreach(note_taskname_foreach_handler, &ctx);
}

void note_taskname(FAR struct tcb_s *tcb)
{
  struct note_startalloc_s note;
  unsigned int length;
  FAR struct note_driver_s **driver;
  bool formatted = false;

  nxtask_namepool_record(tcb->pid, tcb->name);

  note_drivers_initialize();
  for (driver = g_note_drivers; *driver; driver++)
    {
      if (!note_driver_isenabled_type(*driver, NOTE_TASKNAME))
        {
          continue;
        }

      if (note_do_taskname(*driver, tcb))
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
          length = note_format_taskname(&note, tcb, tcb->name,
                                        NOTE_TASKNAME);
        }

      note_do_add(*driver, &note, length, false);
    }
}

void note_suspend(FAR struct tcb_s *tcb)
{
  struct note_suspend_s note;
  FAR struct note_driver_s **driver;
#if CONFIG_SCHED_INSTRUMENTATION_SWITCH_BACKTRACE > 0
  int n;
#endif
  bool formatted = false;

  note_drivers_initialize();
  for (driver = g_note_drivers; *driver; driver++)
    {
      if (!note_driver_isenabled_type(*driver, NOTE_SUSPEND))
        {
          continue;
        }

      if (note_do_suspend(*driver, tcb))
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
          note_common(tcb, &note.nsu_cmn,
                      sizeof(struct note_suspend_s), NOTE_SUSPEND);
          note.nsu_state = tcb->task_state;
#if CONFIG_SCHED_INSTRUMENTATION_SWITCH_BACKTRACE > 0
          n = sched_backtrace(tcb->pid,
                (FAR void **)note.nsu_backtrace,
                CONFIG_SCHED_INSTRUMENTATION_SWITCH_BACKTRACE,
                2);
          if (n < CONFIG_SCHED_INSTRUMENTATION_SWITCH_BACKTRACE)
            {
              memset(&note.nsu_backtrace[n], 0,
                (CONFIG_SCHED_INSTRUMENTATION_SWITCH_BACKTRACE
                 - n) * sizeof(uintptr_t));
            }
#endif
        }

      /* Add the note to circular buffer */

      note_do_add(*driver, &note, note.nsu_cmn.nc_length, true);
    }
}

void note_resume(FAR struct tcb_s *tcb)
{
  struct note_resume_s note;
  FAR struct note_driver_s **driver;
  bool formatted = false;

  note_drivers_initialize();
  for (driver = g_note_drivers; *driver; driver++)
    {
      if (!note_driver_isenabled_type(*driver, NOTE_RESUME))
        {
          continue;
        }

      if (note_do_resume(*driver, tcb))
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

      note_do_add(*driver, &note, note.nre_cmn.nc_length, true);
    }
}

#ifdef CONFIG_SMP
void note_cpu_start(FAR struct tcb_s *tcb, int cpu)
{
  struct note_cpu_start_s note;
  FAR struct note_driver_s **driver;
  bool formatted = false;

  note_drivers_initialize();
  for (driver = g_note_drivers; *driver; driver++)
    {
      if (!note_driver_isenabled_type(*driver, NOTE_CPU_START))
        {
          continue;
        }

      if (note_do_cpu_start(*driver, tcb, cpu))
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

      note_do_add(*driver, &note, note.ncs_cmn.nc_length, false);
    }
}

void note_cpu_started(FAR struct tcb_s *tcb)
{
  struct note_cpu_started_s note;
  FAR struct note_driver_s **driver;
  bool formatted = false;

  note_drivers_initialize();
  for (driver = g_note_drivers; *driver; driver++)
    {
      if (!note_driver_isenabled_type(*driver, NOTE_CPU_STARTED))
        {
          continue;
        }

      if (note_do_cpu_started(*driver, tcb))
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

      note_do_add(*driver, &note, note.ncs_cmn.nc_length, false);
    }
}
#endif /* CONFIG_SMP */

/****************************************************************************
 * Name: note_ready
 *
 * Description:
 *   Record a task state transition to ready-to-run.
 *
 * Input Parameters:
 *   tcb   - The TCB of the task
 *   state - Target state (running/ready/pending)
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void note_ready(FAR struct tcb_s *tcb, uint8_t state)
{
  FAR struct note_driver_s **driver;
  struct note_common_s note;
  bool formatted = false;

  note_drivers_initialize();
  for (driver = g_note_drivers; *driver; driver++)
    {
      if (!note_driver_isenabled_type(*driver, NOTE_READY))
        {
          continue;
        }

      if (note_do_ready(*driver, tcb, state))
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
          note_common(tcb, &note, sizeof(note),
                      NOTE_READY);
        }

      /* Add the note to circular buffer */

      note_do_add(*driver, &note, note.nc_length, true);
    }
}
#endif /* CONFIG_SCHED_INSTRUMENTATION_SWITCH */

#ifdef CONFIG_SCHED_INSTRUMENTATION_PREEMPTION
void note_preemption(FAR struct tcb_s *tcb, bool locked)
{
  struct note_preempt_s note;
  FAR struct note_driver_s **driver;
  bool formatted = false;
  irqstate_t flags;

  note_drivers_initialize();
  flags = spin_lock_irqsave_notrace(&g_note_lock);
  for (driver = g_note_drivers; *driver; driver++)
    {
      if (!note_driver_isenabled_type(*driver, NOTE_PREEMPT_LOCK + !locked))
        {
          continue;
        }

      if (note_do_preemption(*driver, tcb, locked))
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
                      NOTE_PREEMPT_LOCK + !locked);
          note.npr_count = tcb->lockcount;
        }

      /* Add the note to circular buffer */

      note_do_add(*driver, &note, note.npr_cmn.nc_length, true);
    }

  spin_unlock_irqrestore_notrace(&g_note_lock, flags);
}
#endif

#ifdef CONFIG_SCHED_INSTRUMENTATION_CSECTION
void note_csection(FAR struct tcb_s *tcb, bool enter)
{
  struct note_csection_s note;
  FAR struct note_driver_s **driver;
  bool formatted = false;

  note_drivers_initialize();
  for (driver = g_note_drivers; *driver; driver++)
    {
      if (!note_driver_isenabled_type(*driver, NOTE_CSECTION_ENTER + !enter))
        {
          continue;
        }

      if (note_do_csection(*driver, tcb, enter))
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
        }

      /* Add the note to circular buffer */

      note_do_add(*driver, &note, note.ncs_cmn.nc_length, true);
    }
}
#endif

/****************************************************************************
 * Name: sched_note_spinlock_xxx
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
__jumpfunc__ void sched_note_spinlock_lock(FAR volatile void *spinlock)
{
  if (static_branch_unlikely(&g_note_keys[NOTE_SPINLOCK_LOCK]))
    {
      note_spinlock(spinlock, NOTE_SPINLOCK_LOCK);
    }
}

__jumpfunc__ void sched_note_spinlock_locked(FAR volatile void *spinlock)
{
  if (static_branch_unlikely(&g_note_keys[NOTE_SPINLOCK_LOCKED]))
    {
      note_spinlock(spinlock, NOTE_SPINLOCK_LOCKED);
    }
}

__jumpfunc__ void sched_note_spinlock_unlock(FAR volatile void *spinlock)
{
  if (static_branch_unlikely(&g_note_keys[NOTE_SPINLOCK_UNLOCK]))
    {
      note_spinlock(spinlock, NOTE_SPINLOCK_UNLOCK);
    }
}

__jumpfunc__ void sched_note_spinlock_abort(FAR volatile void *spinlock)
{
  if (static_branch_unlikely(&g_note_keys[NOTE_SPINLOCK_ABORT]))
    {
      note_spinlock(spinlock, NOTE_SPINLOCK_ABORT);
    }
}
#endif

#ifdef CONFIG_SCHED_INSTRUMENTATION_SYSCALL
__jumpfunc__ void sched_note_syscall_enter(int nr, int argc, ...)
{
  va_list ap;

  if (static_branch_unlikely(&g_note_keys[NOTE_SYSCALL_ENTER]) &&
      !note_filter_syscall(nr))
    {
      va_start(ap, argc);
      note_vsyscall_enter(nr, argc, ap);
      va_end(ap);
    }
}

__jumpfunc__ void sched_note_syscall_leave(int nr, uintptr_t result)
{
  if (static_branch_unlikely(&g_note_keys[NOTE_SYSCALL_LEAVE]) &&
      !note_filter_syscall(nr))
    {
      note_syscall_leave(nr, result);
    }
}
#endif

#ifdef CONFIG_SCHED_INSTRUMENTATION_IRQHANDLER
void note_irqhandler(int irq, FAR void *handler, bool enter)
{
  struct note_irqhandler_s note;
  FAR struct note_driver_s **driver;
  bool formatted = false;
  FAR struct tcb_s *tcb = enter ? running_task() : this_task();

  note_drivers_initialize();
  for (driver = g_note_drivers; *driver; driver++)
    {
      if (!note_driver_isenabled_irq(*driver, irq, enter))
        {
          continue;
        }

      if (note_do_irqhandler(*driver, irq, handler, enter))
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

      /* Add the note to circular buffer.
       * Use noswitches=true because IRQ handler instrumentation must not
       * trigger context switches (e.g. via poll_notify -> nxsem_post).
       */

      note_do_add(*driver, &note, note.nih_cmn.nc_length, true);
    }
}
#endif

#ifdef CONFIG_SCHED_INSTRUMENTATION_WDOG
void note_wdog(uint8_t event, FAR void *handler, FAR const void *arg)
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
      if (!note_driver_isenabled_type(*driver, event))
        {
          continue;
        }

      if (note_do_wdog(*driver, event, handler, arg))
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

      note_do_add(*driver, &note, note.nwd_cmn.nc_length, false);
    }

  leave_critical_section_notrace(flags);
}
#endif

#ifdef CONFIG_SCHED_INSTRUMENTATION_WQUEUE
/****************************************************************************
 * Name: note_wqueue
 *
 * Description:
 *   Record a work queue item start or end.
 *
 * Input Parameters:
 *   event   - NOTE_WQUEUE_START or NOTE_WQUEUE_END
 *   worker  - The worker function address
 *   arg     - The worker function argument
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void note_wqueue(uint8_t event, FAR void *worker, FAR const void *arg)
{
  FAR struct note_driver_s **driver;
  struct note_wqueue_s note;
  bool formatted = false;
  FAR struct tcb_s *tcb = this_task();

  note_drivers_initialize();
  for (driver = g_note_drivers; *driver; driver++)
    {
      if (!note_driver_isenabled_type(*driver, event))
        {
          continue;
        }

      if (note_do_wqueue(*driver, event, worker, arg))
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
          note_common(tcb, &note.nwq_cmn, sizeof(note), event);
          note.worker = (uintptr_t)worker;
          note.arg = (uintptr_t)arg;
        }

      /* Add the note to circular buffer */

      note_do_add(*driver, &note, note.nwq_cmn.nc_length, false);
    }
}
#endif

#ifdef CONFIG_SCHED_INSTRUMENTATION_HEAP
void note_heap(uint8_t event, FAR void *heap, FAR void *mem,
               size_t size, size_t used)
{
  FAR struct note_driver_s **driver;
  struct note_heap_s note;
  bool formatted = false;
  FAR struct tcb_s *tcb = running_task();

  note_drivers_initialize();
  for (driver = g_note_drivers; *driver; driver++)
    {
      if (!note_driver_isenabled_type(*driver, event))
        {
          continue;
        }

      if (note_do_heap(*driver, event, heap, mem, size, used))
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

      note_do_add(*driver, &note, note.nhp_cmn.nc_length, false);
    }
}
#endif

#ifdef CONFIG_SCHED_INSTRUMENTATION_DUMP
size_t note_driver_event_ip(FAR struct note_driver_s *driver, uint8_t event,
                            uint8_t tag, uint8_t level, uintptr_t ip,
                            FAR const void *buf, size_t len)
{
  FAR struct tcb_s *tcb = running_task();
  FAR struct note_event_s *note;
  char data[NOTE_BUFFER_SIZE];
  unsigned int length = 0;

  if (note_do_event(driver, event, tag, level, ip, buf, len))
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

  note_do_add(driver, note, length, false);
  return length - SIZEOF_NOTE_EVENT(0);
}

void note_driver_vprintf_ip(FAR struct note_driver_s *driver, uint8_t tag,
                            uint8_t level, uintptr_t ip, uintptr_t type,
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

  if (note_do_vprintf(driver, tag, level, ip, type, fmt, *va))
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
      if ((ssize_t)next < 0)
        {
          return;
        }
    }

  length = SIZEOF_NOTE_PRINTF(next);
  note_common(tcb, &note->npt_cmn, length, NOTE_DUMP_PRINTF);
  note->npt_ip = ip;
  note->npt_tag = tag;
  note->npt_fmt = (uintptr_t)fmt;
  note->npt_type = type;
  note->npt_level = level;

  /* Add the note to circular buffer */

  note_do_add(driver, note, length, false);
}

void note_driver_printf_ip(FAR struct note_driver_s *driver, uint8_t tag,
                           uint8_t level, uintptr_t ip, uintptr_t type,
                           FAR const char *fmt, ...)
{
  va_list va;
  va_start(va, fmt);
  note_driver_vprintf_ip(driver, tag, level, ip, type, fmt, &va);
  va_end(va);
}

void note_event_ip(uint8_t event, uint8_t tag, uint8_t level, uintptr_t ip,
                   FAR const void *buf, size_t len)
{
  FAR struct note_driver_s **driver;

  note_drivers_initialize();
  if (static_key_enabled(&REFERENCE_STATIC_KEY(g_note_keys)[event].key) &&
      !note_filter_dump(tag))
    {
      for (driver = g_note_drivers; *driver; driver++)
        {
          if (!note_driver_isenabled_dump(*driver, tag, level, event))
            {
              continue;
            }

          note_driver_event_ip(*driver, event, tag, level, ip, buf, len);
        }
    }
}

__jumpfunc__ void note_vprintf_ip(uint8_t tag, uint8_t level, uintptr_t ip,
                                  FAR const char *fmt, uintptr_t type,
                                  FAR va_list *va)
{
  FAR struct note_driver_s **driver;

  note_drivers_initialize();
  if (static_branch_unlikely(&g_note_keys[NOTE_DUMP_PRINTF]) &&
      !note_filter_dump(tag))
    {
      for (driver = g_note_drivers; *driver; driver++)
        {
          va_list copy;
          if (!note_driver_isenabled_dump(*driver, tag, level,
                                         NOTE_DUMP_PRINTF))
            {
              continue;
            }

          va_copy(copy, *va);
          note_driver_vprintf_ip(*driver, tag, level, ip, type, fmt, &copy);
          va_end(copy);
        }
    }
}

void note_printf_ip(uint8_t tag, uint8_t level, uintptr_t ip,
                    FAR const char *fmt, uintptr_t type, ...)
{
  va_list va;
  va_start(va, type);
  note_vprintf_ip(tag, level, ip, fmt, type, &va);
  va_end(va);
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
  register_reboot_notifier(&g_note_reboot_nb);
  note_update_filter();

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
          note_update_filter();
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
          note_update_filter();
          break;
        }
    }

  spin_unlock_irqrestore_notrace(&g_note_lock, irq_mask);
}
