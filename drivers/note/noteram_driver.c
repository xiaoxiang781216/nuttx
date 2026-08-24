/****************************************************************************
 * drivers/note/noteram_driver.c
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

#include <stdint.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sched.h>
#include <fcntl.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <poll.h>

#include <nuttx/atomic.h>
#include <nuttx/spinlock.h>
#include <nuttx/sched.h>
#include <nuttx/sched_note.h>
#include <nuttx/kmalloc.h>
#include <nuttx/note/note_driver.h>
#include <nuttx/note/noteram_driver.h>
#include <nuttx/nuttx.h>
#include <nuttx/ratelimit.h>
#include <nuttx/ring.h>
#include <nuttx/fs/fs.h>
#include <nuttx/streams.h>
#include <nuttx/queue.h>

#include "note_driver.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct noteram_driver_s
{
  struct note_driver_s driver;
  atomic_t overwrite;
  FAR struct ring_s *ring;
  atomic_t count_ref;       /* Reference count, init 1 at creation */
  bool dynamic;             /* true if dynamically allocated */
  struct ratelimit_state_s ratelimit;
  dq_queue_t poll_list;     /* Registered poll users list */
  rspinlock_t lock;
  atomic_t poll_ref;        /* Poll notify in-flight reference */
#if CONFIG_DRIVERS_NOTERAM_POLLTIMEOUT_MS > 0
  struct wdog_s wdog;
#endif
};

/* The structure to hold the context data of trace dump */

struct noteram_dump_context_s
{
  struct note_dump_stream_s stream;  /* Per-file parse/read context */
  unsigned int threshold;            /* Per-file poll threshold */
  FAR struct pollfd *pfd;            /* Per-file pollfd pointer */
  dq_entry_t poll_node;              /* Poll list node */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int noteram_open(FAR struct file *filep);
static int noteram_close(FAR struct file *filep);
static ssize_t noteram_read(FAR struct file *filep,
                            FAR char *buffer, size_t buflen);
static int noteram_ioctl(FAR struct file *filep, int cmd, unsigned long arg);
static int noteram_poll(FAR struct file *filep, FAR struct pollfd *fds,
                        bool setup);
static ssize_t noteram_add(FAR struct note_driver_s *drv,
                           FAR const void *note, size_t len,
                           bool noswitches);

#if CONFIG_DRIVERS_NOTERAM_POLLTIMEOUT_MS > 0
static void noteram_timeout_handler(wdparm_t arg);
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

static char g_noteram_blob[CONFIG_DRIVERS_NOTERAM_BUFSIZE]
#ifdef DRIVERS_NOTERAM_SECTION
locate_data(CONFIG_DRIVERS_NOTERAM_SECTION)
#endif
aligned_data(RING_BLOB_ALIGN);

static struct ring_s g_noteram_ring;

static const struct note_driver_ops_s g_noteram_ops =
{
  .add = noteram_add,
  .open = noteram_open,
  .close = noteram_close,
  .read = noteram_read,
  .ioctl = noteram_ioctl,
  .poll = noteram_poll,
};

/****************************************************************************
 * Public Data
 ****************************************************************************/

#undef g_noteram_driver
DEFINE_PER_CPU_BMP(struct noteram_driver_s, g_noteram_driver) =
{
  {
#ifdef CONFIG_SCHED_INSTRUMENTATION_FILTER
    "ram",
    {
      {
        CONFIG_DRIVERS_NOTERAM_FILTER_DEFAULT_MODE,
#  ifndef CONFIG_UP
        CONFIG_DRIVERS_NOTERAM_CPUSET
#  endif
      },

#  ifdef CONFIG_SCHED_INSTRUMENTATION_DUMP
      {
        CONFIG_DRIVERS_NOTE_DEFAULT_LEVEL,
      },
#  endif
    },
#endif
    &g_noteram_ops
  },
#ifdef CONFIG_DRIVERS_NOTERAM_DEFAULT_NOOVERWRITE
  NOTE_MODE_OVERWRITE_DISABLE,
#else
  NOTE_MODE_OVERWRITE_ENABLE,
#endif
  &g_noteram_ring,
};
#define g_noteram_driver this_cpu_var_bmp(g_noteram_driver)

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: noteram_free
 ****************************************************************************/

static void noteram_free(FAR struct noteram_driver_s *drv)
{
  if (drv->dynamic)
    {
      kmm_free(drv->ring->blob);
    }

  if (drv != &g_noteram_driver)
    {
      kmm_free(drv);
    }
}

/****************************************************************************
 * Name: noteram_deinitialize_by_name
 ****************************************************************************/

static int noteram_deinitialize_by_name(FAR const char *name)
{
  FAR struct noteram_driver_s *drv;
  char devpath[PATH_MAX];
  struct file filep;
  int ret;

  snprintf(devpath, sizeof(devpath), "/dev/note/%s", name);
  ret = file_open(&filep, devpath, O_RDONLY);
  if (ret < 0)
    {
      return ret;
    }

  drv = (FAR struct noteram_driver_s *)filep.f_inode->i_private;
  file_close(&filep);
  noteram_deinitialize(&drv->driver);
  return OK;
}

/****************************************************************************
 * Name: noteram_unread_length
 ****************************************************************************/

static uint32_t noteram_unread_length(FAR struct noteram_driver_s *drv,
                                      uint32_t read)
{
  return ring_mpmc_peek(drv->ring, NULL, ring_capacity(drv->ring), &read);
}

#if CONFIG_DRIVERS_NOTERAM_POLLTIMEOUT_MS > 0
static inline_function
void noteram_timeout_reset(FAR struct noteram_driver_s *drv)
{
  if (dq_empty(&drv->poll_list) || ring_count(drv->ring) == 0)
    {
      wd_cancel(&drv->wdog);
    }
  else
    {
      wd_start(&drv->wdog, MSEC2TICK(CONFIG_DRIVERS_NOTERAM_POLLTIMEOUT_MS),
               noteram_timeout_handler, (wdparm_t)drv);
    }
}

static void noteram_timeout_handler(wdparm_t arg)
{
  FAR struct noteram_driver_s *drv = (FAR struct noteram_driver_s *)arg;
  irqstate_t flags = rspin_lock_irqsave_notrace(&drv->lock);
  FAR struct noteram_dump_context_s *ctx;

  atomic_add(&drv->poll_ref, 1);
  dq_for_every_entry(&drv->poll_list, ctx,
                     struct noteram_dump_context_s, poll_node)
    {
      FAR struct note_dump_stream_s *stream = &ctx->stream;

      /* Relaxed read of ctx->stream.pos and ctx->threshold:
       * we already hold drv lock which serializes with
       * poll_list add/remove, so ctx stays valid.  A stale value
       * only causes a harmless extra or missed notification.
       */

      if (ctx->pfd &&
          noteram_unread_length(drv, stream->pos) >= ctx->threshold)
        {
          rspin_unlock_irqrestore_notrace(&drv->lock, flags);
          poll_notify(&ctx->pfd, 1, POLLIN);
          flags = rspin_lock_irqsave_notrace(&drv->lock);
        }
    }

  atomic_sub(&drv->poll_ref, 1);

  rspin_unlock_irqrestore_notrace(&drv->lock, flags);

  noteram_timeout_reset(drv);
}
#else
#  define noteram_timeout_reset(drv)
#endif /* CONFIG_RAMLOG_POLLTIMEOUT_MS > 0 */

/****************************************************************************
 * Name: noteram_open
 ****************************************************************************/

static int noteram_open(FAR struct file *filep)
{
  FAR struct noteram_driver_s *drv = (FAR struct noteram_driver_s *)
                                     filep->f_inode->i_private;
  FAR struct noteram_dump_context_s *ctx;

  if (drv == &g_noteram_driver)
    {
      ring_mpmc_init(drv->ring, g_noteram_blob, sizeof(g_noteram_blob), 0);
    }

  atomic_add(&drv->count_ref, 1);

  ctx = kmm_zalloc(sizeof(*ctx));
  if (ctx == NULL)
    {
      if (atomic_sub(&drv->count_ref, 1) == 1)
        {
          noteram_free(drv);
          return -ENOMEM;
        }

      return -ENOMEM;
    }

  ctx->threshold = sizeof(struct note_common_s);

  filep->f_priv = ctx;
#ifdef CONFIG_DRIVERS_NOTE_TEXT_DUMP
  note_dump_stream(&ctx->stream, drv->ring, false);
#else
  note_dump_stream(&ctx->stream, drv->ring, true);
#endif
  return OK;
}

int noteram_close(FAR struct file *filep)
{
  FAR struct noteram_driver_s *drv = filep->f_inode->i_private;
  FAR struct noteram_dump_context_s *ctx = filep->f_priv;

  if (ctx->pfd != NULL)
    {
      irqstate_t flags;

      flags = rspin_lock_irqsave_notrace(&drv->lock);

      while (atomic_read(&drv->poll_ref) > 0)
        {
          rspin_unlock_irqrestore_notrace(&drv->lock, flags);
          sched_yield();
          flags = rspin_lock_irqsave_notrace(&drv->lock);
        }

      ctx->pfd = NULL;
      dq_rem(&ctx->poll_node, &drv->poll_list);
      rspin_unlock_irqrestore_notrace(&drv->lock, flags);
    }

  filep->f_priv = NULL;
  kmm_free(ctx);

  if (atomic_sub(&drv->count_ref, 1) == 1)
    {
      noteram_free(drv);
    }

  return OK;
}

/****************************************************************************
 * Name: noteram_read
 ****************************************************************************/

static ssize_t noteram_read(FAR struct file *filep, FAR char *buffer,
                            size_t buflen)
{
  FAR struct noteram_dump_context_s *ctx = filep->f_priv;

  return lib_stream_gets(&ctx->stream, buffer, buflen);
}

/****************************************************************************
 * Name: noteram_ioctl
 ****************************************************************************/

static int noteram_ioctl(FAR struct file *filep, int cmd, unsigned long arg)
{
  FAR struct noteram_dump_context_s *ctx = filep->f_priv;
  FAR struct noteram_driver_s *drv = filep->f_inode->i_private;
  int ret = -ENOTTY;

  /* Handle the ioctl commands */

  switch (cmd)
    {
      /* NOTE_CLEAR
       *      - Clear all contents of the circular buffer
       *        Argument: Ignored
       */

      case NOTE_CLEAR:
        {
          int32_t expect = NOTE_MODE_OVERWRITE_OVERFLOW;

#ifdef CONFIG_DRIVERS_NOTE_STATS
          atomic_add_relaxed(&drv->driver.stats.drop_bytes,
                             ring_mpmc_skip(drv->ring, RING_SIZE_MAX));
#else
          ring_mpmc_skip(drv->ring, RING_SIZE_MAX);
#endif
          atomic_cmpxchg(&drv->overwrite, &expect,
                         NOTE_MODE_OVERWRITE_DISABLE);

          ret = OK;
        }
        break;

      /* NOTE_GETOVERWRITE
       *      - Get overwrite mode
       *        Argument: A writable pointer to unsigned int
       */

      case NOTE_GETOVERWRITE:
        if (arg == 0)
          {
            ret = -EINVAL;
          }
        else
          {
            *(FAR unsigned int *)arg = atomic_read(&drv->overwrite);
            ret = OK;
          }
        break;

      /* NOTE_SETOVERWRITE
       *      - Set overwrite mode
       *        Argument: A read-only pointer to unsigned int
       */

      case NOTE_SETOVERWRITE:
        if (arg == 0)
          {
            ret = -EINVAL;
          }
        else
          {
            atomic_set(&drv->overwrite, *(FAR unsigned int *)arg);
            ret = OK;
          }
        break;

      /* NOTE_GETREADMODE
       *      - Get read mode
       *        Argument: A writable pointer to unsigned int
       */

      case NOTE_GETREADMODE:
        if (arg == 0)
          {
            ret = -EINVAL;
          }
        else
          {
            *(FAR unsigned int *)arg = ctx->stream.binary ?
                                       NOTE_MODE_READ_BINARY :
                                       NOTE_MODE_READ_ASCII;
            ret = OK;
          }
        break;

      /* NOTE_SETREADMODE
       *      - Set read mode
       *        Argument: A read-only pointer to unsigned int
       */

      case NOTE_SETREADMODE:
        if (arg == 0)
          {
            ret = -EINVAL;
          }
        else
          {
            ctx->stream.binary = (arg == NOTE_MODE_READ_BINARY);
            ret = OK;
          }
        break;

      case NOTE_GETRATELIMIT:
        if (arg == 0)
          {
            ret = -EINVAL;
          }
        else
          {
            FAR struct note_ratelimit_s *limit =
              (FAR struct note_ratelimit_s *)arg;

            ratelimit_getlimit(&drv->ratelimit, &limit->interval,
                               &limit->burst);
            ret = OK;
          }
        break;

      case NOTE_SETRATELIMIT:
        if (arg == 0)
          {
            ret = -EINVAL;
          }
        else
          {
            FAR struct note_ratelimit_s *limit =
              (FAR struct note_ratelimit_s *)arg;

            ratelimit_setlimit(&drv->ratelimit, limit->interval,
                               limit->burst);
            ret = OK;
          }
        break;

      case FIONREAD:
        if (arg == 0)
          {
            ret = -EINVAL;
          }
        else
          {
            *(FAR unsigned int *)arg =
              noteram_unread_length(drv, ctx->stream.pos);
            ret = OK;
          }
        break;

      case PIPEIOC_POLLINTHRD:
        ctx->threshold = (uint32_t)arg;
        ret = OK;
        break;

      case PIPEIOC_GETSIZE:
        if (arg == 0)
          {
            ret = -EINVAL;
          }
        else
          {
            *(FAR unsigned int *)arg = ring_capacity(drv->ring);
            ret = OK;
          }
        break;

      case NOTE_CREATE:
        {
          FAR struct noteram_config_s *config =
            (FAR struct noteram_config_s *)arg;

          if (config == NULL || config->devname == NULL ||
              config->bufsize == 0)
            {
              ret = -EINVAL;
            }
          else
            {
              FAR struct note_driver_s *new_drv;
              new_drv = noteram_initialize(config->devname,
                                           config->bufsize,
                                           config->overwrite,
                                           true);
              ret = new_drv != NULL ? OK : -ENOMEM;
            }
        }
        break;

      case NOTE_DESTROY:
        {
          FAR const char *devname = (FAR const char *)arg;

          if (devname == NULL)
            {
              ret = -EINVAL;
            }
          else
            {
              ret = noteram_deinitialize_by_name(devname);
            }
        }
        break;

      default:
        break;
    }

  return ret;
}

/****************************************************************************
 * Name: noteram_poll
 ****************************************************************************/

static int noteram_poll(FAR struct file *filep, FAR struct pollfd *fds,
                        bool setup)
{
  int ret = 0;
  FAR struct inode *inode;
  FAR struct noteram_driver_s *drv;
  FAR struct noteram_dump_context_s *ctx;
  irqstate_t flags;

  DEBUGASSERT(filep != NULL && fds != NULL);
  inode = filep->f_inode;

  DEBUGASSERT(inode != NULL && inode->i_private != NULL);
  drv = inode->i_private;
  ctx = filep->f_priv;

  /* Ignore waits that do not include POLLIN */

  if ((fds->events & POLLIN) == 0)
    {
      return -EDEADLK;
    }

  flags = rspin_lock_irqsave_notrace(&drv->lock);

  while (atomic_read(&drv->poll_ref) > 0)
    {
      rspin_unlock_irqrestore_notrace(&drv->lock, flags);
      sched_yield();
      flags = rspin_lock_irqsave_notrace(&drv->lock);
    }

  if (setup)
    {
      FAR struct note_dump_stream_s *stream = &ctx->stream;

      if (ctx->pfd != NULL)
        {
          rspin_unlock_irqrestore_notrace(&drv->lock, flags);
          return -EBUSY;
        }

      ctx->pfd = fds;
      dq_addlast(&ctx->poll_node, &drv->poll_list);

      if (noteram_unread_length(drv, stream->pos) >= ctx->threshold)
        {
          rspin_unlock_irqrestore_notrace(&drv->lock, flags);
          poll_notify(&fds, 1, POLLIN);
        }
      else
        {
          rspin_unlock_irqrestore_notrace(&drv->lock, flags);
        }
    }
  else
    {
      if (ctx->pfd != NULL)
        {
          ctx->pfd = NULL;
          dq_rem(&ctx->poll_node, &drv->poll_list);
        }

      rspin_unlock_irqrestore_notrace(&drv->lock, flags);
    }

  noteram_timeout_reset(drv);

  return ret;
}

/****************************************************************************
 * Name: noteram_add
 *
 * Description:
 *   Add the variable length note to the transport layer
 *
 * Input Parameters:
 *   note    - The note buffer
 *   notelen - The buffer length
 *   noswitches - True: Can't do context switches now.
 *
 * Returned Value:
 *   None
 *
 * Assumptions:
 *   We are within a critical section.
 *
 ****************************************************************************/

static ssize_t noteram_add(FAR struct note_driver_s *driver,
                           FAR const void *note, size_t notelen,
                           bool noswitches)
{
  FAR struct noteram_driver_s *drv = (FAR struct noteram_driver_s *)driver;
  irqstate_t flags;
  size_t added;

  if (drv == &g_noteram_driver)
    {
      ring_mpmc_init(drv->ring, g_noteram_blob, sizeof(g_noteram_blob), 0);
    }

  if (ratelimit_islimited(&drv->ratelimit))
    {
      return -ENOSPC;
    }

  switch (atomic_read(&drv->overwrite))
    {
      case NOTE_MODE_OVERWRITE_OVERFLOW:
        return -ENOSPC;

      case NOTE_MODE_OVERWRITE_DISABLE:
        added = ring_mpmc_enqueue(drv->ring, note, notelen);
        if (added < notelen)
          {
            atomic_set(&drv->overwrite, NOTE_MODE_OVERWRITE_OVERFLOW);
          }
        break;

      default:
        {
#ifdef CONFIG_DRIVERS_NOTE_STATS
          uint32_t evicted = ring_mpmc_overwrite(drv->ring, note, notelen);

          if (evicted != 0)
            {
              atomic_add_relaxed(&drv->driver.stats.drop_bytes, evicted);
            }
#else
          ring_mpmc_overwrite(drv->ring, note, notelen);
#endif
          added = MIN(notelen, ring_capacity(drv->ring));
        }
        break;
    }

  if (!noswitches && !dq_empty(&drv->poll_list))
    {
      FAR struct noteram_dump_context_s *ctx;

      flags = rspin_lock_irqsave_notrace(&drv->lock);
      atomic_add(&drv->poll_ref, 1);

      dq_for_every_entry(&drv->poll_list, ctx,
                          struct noteram_dump_context_s, poll_node)
        {
          FAR struct note_dump_stream_s *stream = &ctx->stream;

          if (ctx->pfd &&
              noteram_unread_length(drv, stream->pos) >= ctx->threshold)
            {
              rspin_unlock_irqrestore_notrace(&drv->lock, flags);
              poll_notify(&ctx->pfd, 1, POLLIN);
              flags = rspin_lock_irqsave_notrace(&drv->lock);
            }
        }

      atomic_sub(&drv->poll_ref, 1);
      rspin_unlock_irqrestore_notrace(&drv->lock, flags);
    }

  noteram_timeout_reset(drv);

  return added;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: noteram_initialize_with_buffer
 *
 * Description:
 *   Allocate a noteram driver plus its ring handle, initialize the ring in
 *   place over the caller-supplied blob, and register it.  The blob is
 *   treated as caller-owned (outlives the driver, not freed on deinit).
 *   noteram_initialize() reuses this and flips drv->dynamic to true so its
 *   self-allocated blob is freed on deinit.
 *
 * Input Parameters:
 *  devname: The name of the Noteram device
 *  buffer: The blob (header + data) the ring operates on
 *  bufsize: The total blob size in bytes (header + data)
 *  overwrite: The overwrite mode
 *  regnote: If register the note driver
 *
 * Returned Value:
 *   The note driver instance on success; NULL on failure.
 *
 ****************************************************************************/

FAR struct note_driver_s *
noteram_initialize_with_buffer(FAR const char *devname,
                               FAR void *buffer, size_t bufsize,
                               bool overwrite, bool regnote)
{
  FAR struct noteram_driver_s *drv;
  size_t len = 1;
  int ret;

  if (devname)
    {
      len += strlen(devname);
    }

  drv = kmm_zalloc(sizeof(*drv) + sizeof(struct ring_s) + len);
  if (drv == NULL)
    {
      return NULL;
    }

  drv->ring = (FAR struct ring_s *)(drv + 1);

  atomic_set(&drv->count_ref, 1);

  if (devname)
    {
      memcpy(drv->ring + 1, devname, len);
    }

#ifdef CONFIG_SCHED_INSTRUMENTATION_FILTER
  drv->driver.name = (FAR const char *)(drv->ring + 1);
  drv->driver.filter.mode.type_mask =
    CONFIG_DRIVERS_NOTERAM_FILTER_DEFAULT_MODE;

#  ifndef CONFIG_UP
  drv->driver.filter.mode.cpuset =
    CONFIG_DRIVERS_NOTERAM_CPUSET;
#  endif

#  ifdef CONFIG_SCHED_INSTRUMENTATION_DUMP
  drv->driver.filter.level.level = CONFIG_DRIVERS_NOTE_DEFAULT_LEVEL;
#  endif
#endif

  drv->driver.ops = &g_noteram_ops;

  ring_mpmc_init(drv->ring, buffer, bufsize, 0);

  atomic_set(&drv->overwrite, overwrite);
  dq_init(&drv->poll_list);

  if (regnote)
    {
      ret = note_driver_register(&drv->driver);
      if (ret < 0)
        {
          kmm_free(drv);
          return NULL;
        }
    }

  return &drv->driver;
}

/****************************************************************************
 * Name: noteram_initialize
 *
 * Description:
 *   Register a serial driver at /dev/note/ram that can be used by an
 *   application to read data from the circular note buffer.  The shm buffer
 *   is allocated here on the driver's behalf and freed on deinit.
 *
 * Input Parameters:
 *  devname: The name of the Noteram device
 *  bufsize: The total blob size in bytes (header + data), same meaning as
 *           in noteram_initialize_with_buffer
 *  overwrite: The overwrite mode
 *  regnote: If register the note driver
 *
 * Returned Value:
 *   Zero on succress. A negated errno value is returned on a failure.
 *
 ****************************************************************************/

FAR struct note_driver_s *
noteram_initialize(FAR const char *devname, size_t bufsize,
                   bool overwrite, bool regnote)
{
  FAR struct note_driver_s *drv;
  FAR void *buffer;

  buffer = kmm_memalign(RING_BLOB_ALIGN, bufsize);
  if (buffer == NULL)
    {
      return NULL;
    }

  drv = noteram_initialize_with_buffer(devname, buffer, bufsize,
                                       overwrite, regnote);
  if (drv == NULL)
    {
      kmm_free(buffer);
      return NULL;
    }

  container_of(drv, struct noteram_driver_s, driver)->dynamic = true;

  return drv;
}

/****************************************************************************
 * Name: noteram_deinitialize
 *
 * Description:
 *   Unregister noteram driver.
 *
 * Input Parameters:
 *  drv: the ptr of note_driver_s.
 *
 * Returned Value:
 *  None.
 *
 ****************************************************************************/

void noteram_deinitialize(struct note_driver_s *drv)
{
  FAR struct noteram_driver_s *noteram_drv;

  if (drv == NULL)
    {
      return;
    }

  noteram_drv = container_of(drv, struct noteram_driver_s, driver);

  note_driver_unregister(drv);

  if (atomic_sub(&noteram_drv->count_ref, 1) == 1)
    {
      noteram_free(noteram_drv);
    }
}
