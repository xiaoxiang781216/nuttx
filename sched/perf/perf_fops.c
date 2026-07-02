/****************************************************************************
 * sched/perf/perf_fops.c
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

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>

#include <nuttx/atomic.h>
#include <nuttx/circbuf.h>
#include <nuttx/fs/fs.h>
#include <nuttx/kmalloc.h>
#include <nuttx/mutex.h>
#include <nuttx/perf.h>
#include <nuttx/sched.h>

#include "perf.h"

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int perf_close(FAR struct file *filep);
static ssize_t perf_read(FAR struct file *filep, FAR char *buffer,
                         size_t len);
static int perf_ioctl(FAR struct file *filep, int cmd, unsigned long arg);
static int perf_poll(FAR struct file *filep, FAR struct pollfd *fds,
                     bool setup);
#ifdef CONFIG_FS_FILEMAP
static int perf_mmap(FAR struct file *filep,
                     FAR struct mm_map_entry_s *map);
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct file_operations g_perf_fops =
{
  .close = perf_close, /* close */
  .read  = perf_read,  /* read */
  .ioctl = perf_ioctl, /* ioctl */
  .poll  = perf_poll,  /* poll */
#ifdef CONFIG_FS_FILEMAP
  .mmap  = perf_mmap   /* mmap */
#endif
};

static struct inode g_perf_inode =
{
  NULL,                   /* i_parent */
  NULL,                   /* i_peer */
  NULL,                   /* i_child */
  1,                      /* i_crefs */
  FSNODEFLAG_TYPE_DRIVER, /* i_flags */
  {
    &g_perf_fops          /* u */
  }
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: perf_event_for_child
 *
 * Description:
 *   Handle func for each event for the same group leader
 *
 * Input Parameters:
 *   event - Perf event
 *   func  - Executive function
 *
 * Returned Value:
 *   Execution result
 *
 ****************************************************************************/

static int perf_event_for_child(FAR struct perf_event_s *event,
                                nxsched_cpu_call_t func)
{
  FAR struct perf_event_s *child_event;
  int ret;

  ret = func(event);
  if (ret < 0)
    {
      return ret;
    }

  nxmutex_lock(&event->child_mutex);

  list_for_every_entry(&event->child_list, child_event,
                       struct perf_event_s, child_list)
    {
      ret = func(child_event);
      if (ret < 0)
        {
          break;
        }
    }

  nxmutex_unlock(&event->child_mutex);
  return ret;
}

/****************************************************************************
 * Name: perf_event_for_group
 *
 * Description:
 *   Handle func for each event in the group
 *
 * Input Parameters:
 *   event - Perf event
 *   func  - Executive function
 *
 * Returned Value:
 *   Execution result
 *
 ****************************************************************************/

static int perf_event_for_group(FAR struct perf_event_s *event,
                                nxsched_cpu_call_t func)
{
  FAR struct perf_event_s *sub_event;
  int ret;

  event = event->group_leader;
  ret = perf_event_for_child(event, func);
  if (ret < 0)
    {
      return ret;
    }

  list_for_every_entry(&event->sibling_list, sub_event,
                       struct perf_event_s, group_node)
    {
      ret = perf_event_for_child(sub_event, func);
      if (ret < 0)
        {
          return ret;
        }
    }

  return OK;
}

/****************************************************************************
 * Name: perf_read_event_count
 *
 * Description:
 *   Perf event read event count
 *
 * Input Parameters:
 *   event       - Perf event
 *
 * Returned Value:
 *   Count result
 *
 ****************************************************************************/

static uint64_t perf_read_event_count(FAR struct perf_event_s *event)
{
  FAR struct perf_event_s *child_event;
  uint64_t count;

  perf_function_call(event, perf_get_event_count);
  count = PERF_GET_COUNT(event);

  nxmutex_lock(&event->child_mutex);

  list_for_every_entry(&event->child_list, child_event,
                       struct perf_event_s, child_list)
    {
      perf_function_call(child_event, perf_get_event_count);
      count += atomic64_read(&child_event->count);
    }

  nxmutex_unlock(&event->child_mutex);
  return count;
}

/****************************************************************************
 * Name: perf_read_one
 *
 * Description:
 *   Perf event read result from one event
 *
 * Input Parameters:
 *   event       - Perf event
 *   buffer      - Read buffer
 *   read_format - Read format for result data
 *
 * Returned Value:
 *   Read result
 *
 ****************************************************************************/

static ssize_t perf_read_one(FAR struct perf_event_s *event,
                             FAR char *buffer, uint64_t read_format)
{
  FAR uint64_t *data = (FAR uint64_t *)buffer;
  ssize_t n = 0;

  data[n++] = perf_read_event_count(event);
  if (read_format & PERF_FORMAT_TOTAL_TIME_ENABLED)
    {
      /* TODO */

      data[n++] = 0;
    }

  if (read_format & PERF_FORMAT_TOTAL_TIME_RUNNING)
    {
      /* TODO */

      data[n++] = 0;
    }

  if (read_format & PERF_FORMAT_ID)
    {
      data[n++] = perf_get_eventid(event);
    }

  if (read_format & PERF_FORMAT_LOST)
    {
      /* TODO */

      data[n++] = 0;
    }

  return n * sizeof(uint64_t);
}

/****************************************************************************
 * Name: perf_read_group_one
 *
 * Description:
 *   Perf event read result from group
 *
 * Input Parameters:
 *   event       - Perf event
 *   buffer      - Read buffer
 *   read_format - Read format for result data
 *
 * Returned Value:
 *   Read result
 *
 ****************************************************************************/

static ssize_t perf_read_group_one(FAR struct perf_event_s *event,
                                   FAR uint64_t *data,
                                   uint64_t read_format)
{
  FAR struct perf_event_s *sub_event;
  ssize_t n = 0;

  if (read_format & PERF_FORMAT_TOTAL_TIME_ENABLED)
    {
      /* TODO */

      data[n++] += 0;
    }

  if (read_format & PERF_FORMAT_TOTAL_TIME_RUNNING)
    {
      /* TODO */

      data[n++] += 0;
    }

  data[n++] = perf_read_event_count(event);
  if (read_format & PERF_FORMAT_ID)
    {
      data[n++] = perf_get_eventid(event);
    }

  if (read_format & PERF_FORMAT_LOST)
    {
      /* TODO */

      data[n++] = 0;
    }

  list_for_every_entry(&event->sibling_list, sub_event,
                       struct perf_event_s, group_node)
    {
      data[n++] += perf_read_event_count(sub_event);
      if (read_format & PERF_FORMAT_ID)
        {
          data[n++] = perf_get_eventid(sub_event);
        }

      if (read_format & PERF_FORMAT_LOST)
        {
          /* TODO */

          data[n++] = 0;
        }
    }

  return n * sizeof(uint64_t);
}

/****************************************************************************
 * Name: perf_read_group
 *
 * Description:
 *   Perf event read result from group
 *
 * Input Parameters:
 *   event       - Perf event
 *   buffer      - Read buffer
 *   read_format - Read format for result data
 *
 * Returned Value:
 *   Read result
 *
 ****************************************************************************/

static ssize_t perf_read_group(FAR struct perf_event_s *event,
                               FAR char *buffer, uint64_t read_format)
{
  FAR struct perf_event_s *group_leader = event->group_leader;
  FAR struct perf_event_s *child_event;
  FAR uint64_t *data = (FAR uint64_t *)buffer;
  ssize_t ret;

  data[0] = 1 + group_leader->sibling_num;

  ret = perf_read_group_one(group_leader, data, read_format);

  nxmutex_lock(&group_leader->child_mutex);

  list_for_every_entry(&event->child_list, child_event,
                       struct perf_event_s, child_list)
    {
      ret = perf_read_group_one(child_event, data, read_format);
    }

  nxmutex_unlock(&group_leader->child_mutex);
  return ret;
}

/****************************************************************************
 * Name: perf_close
 *
 * Description:
 *   Perf event close
 *
 * Input Parameters:
 *   filep - File point
 *
 * Returned Value:
 *   Close result
 *
 ****************************************************************************/

static int perf_close(FAR struct file *filep)
{
  FAR struct perf_event_s *event = filep->f_priv;
  FAR struct perf_event_context_s *ctx;

  ASSERT(event != NULL);

  /* Serialize against a concurrent perf_event_task_exit() on another CPU
   * that may be tearing down (and freeing) the very context this event is
   * bound to.  Two things must happen under the teardown lock:
   *
   *   1. Re-read event->ctx *inside* the lock.  The monitored task's exit
   *      path clears event->ctx to NULL for fd-owned events before we get
   *      here; reading it outside the lock is a TOCTOU race where we could
   *      observe a non-NULL ctx that is freed a moment later, then fire a
   *      cross-CPU detach IPI dereferencing freed memory.
   *
   *   2. Hold the lock across perf_context_detach() (which may issue a
   *      synchronous cross-CPU IPI): the peer exit path must also take this
   *      lock before it can free the ctx/event, so nothing is freed while
   *      our IPI is in flight.
   *
   * perf_close() runs in blockable task context (fd close), so the sleeping
   * mutex is safe.  Lock order: teardown -> child_mutex -> ctx->lock.
   */

  perf_teardown_lock();

  ctx = event->ctx;
  if (ctx != NULL)
    {
      perf_context_detach(event, ctx);
    }

  perf_free_event(event);

  perf_teardown_unlock();
  return OK;
}

/****************************************************************************
 * Name: perf_read
 *
 * Description:
 *   Perf event read
 *
 * Input Parameters:
 *   filep  - File point
 *   buffer - Read buffer
 *   len    - Read data len
 *
 * Returned Value:
 *   Read data len
 *
 ****************************************************************************/

static ssize_t perf_read(FAR struct file *filep, FAR char *buffer,
                         size_t len)
{
  FAR struct perf_event_s *event = filep->f_priv;
  uint64_t read_format;
  ssize_t ret;

  ASSERT(event != NULL);

  read_format = event->attr.read_format;

  /* Reading a count issues a cross-CPU IPI (perf_function_call ->
   * perf_get_event_count) that dereferences event->ctx to route to the
   * owning CPU.  Serialize against a concurrent perf_event_task_exit() that
   * could free that ctx mid-read.  Blockable task context; lock order:
   * teardown -> child_mutex (taken inside perf_read_*) -> ctx->lock.
   */

  perf_teardown_lock();

  if (read_format & PERF_FORMAT_GROUP)
    {
      ret = perf_read_group(event, buffer, read_format);
    }
  else
    {
      ret = perf_read_one(event, buffer, read_format);
    }

  perf_teardown_unlock();

  return ret;
}

/****************************************************************************
 * Name: perf_ioctl
 *
 * Description:
 *   Perf event ioctl
 *
 * Input Parameters:
 *   filep  - File point
 *   cmd    - Command id
 *   arg    - Command arg
 *
 * Returned Value:
 *   Ioctl result
 *
 ****************************************************************************/

static int perf_ioctl(FAR struct file *filep, int cmd, unsigned long arg)
{
  FAR struct perf_event_s *event = filep->f_priv;
  nxsched_cpu_call_t func = NULL;
  int ret = OK;

  ASSERT(event != NULL);

  switch (cmd)
    {
      case PERF_EVENT_IOC_ENABLE:
        func = perf_event_enable;
        break;
      case PERF_EVENT_IOC_DISABLE:
        func = perf_event_disable;
        break;
      case PERF_EVENT_IOC_RESET:
        func = perf_event_reset;
        break;
      case PERF_EVENT_IOC_SET_OUTPUT:
        {
          FAR struct file *out_filep;
          FAR struct perf_event_s *out_event;
          int fd = (int) arg;

          ret = file_get(fd, &out_filep);
          if (ret < 0)
            {
              return -EINVAL;
            }

          out_event = out_filep->f_priv;

          if (!out_event->buf)
            {
              return -EINVAL;
            }

          perf_buffer_set_output(event, out_event);
          return OK;
        }

      case PERF_EVENT_IOC_ID:
        {
          if (!arg)
            {
              return -EINVAL;
            }

          *(uint64_t *)arg = perf_get_eventid(event);
        }

        return OK;
      default:
        return -EINVAL;
    }

  /* enable/disable dispatch to the owning CPU via a cross-CPU IPI
   * (perf_function_call) that dereferences event->ctx.  Serialize against a
   * concurrent perf_event_task_exit() freeing that ctx.  reset is a local
   * atomic set (no IPI) but is harmless to serialize.  Blockable task
   * context; lock order: teardown -> child_mutex -> ctx->lock.
   */

  perf_teardown_lock();

  if (arg & PERF_IOC_FLAG_GROUP)
    {
      ret = perf_event_for_group(event, func);
    }
  else
    {
      ret = perf_event_for_child(event, func);
    }

  perf_teardown_unlock();

  return ret;
}

/****************************************************************************
 * Name: perf_poll
 *
 * Description:
 *   Perf event poll
 *
 * Input Parameters:
 *   filep  - File point
 *   fds    - Poll fds
 *   setup  - Setup or close
 *
 * Returned Value:
 *   Poll result
 *
 ****************************************************************************/

static int perf_poll(FAR struct file *filep, FAR struct pollfd *fds,
                     bool setup)
{
  FAR struct perf_event_s *event = filep->f_priv;
  irqstate_t flags;

  if ((fds->events & POLLIN) == 0)
    {
      return -EDEADLK;
    }

  if (!event || !event->buf)
    {
      return -EINVAL;
    }

  flags = spin_lock_irqsave(&event->buf->lock);

  if (event->state == PERF_EVENT_STATE_EXIT)
    {
      spin_unlock_irqrestore(&event->buf->lock, flags);
      return -ESRCH;
    }

  if (setup)
    {
      if (event->pfd)
        {
          spin_unlock_irqrestore(&event->buf->lock, flags);
          return -EBUSY;
        }

      event->pfd = fds;

      if (circbuf_used(&(event->buf->rb)) > 0)
        {
          spin_unlock_irqrestore(&event->buf->lock, flags);
          poll_notify(&event->pfd, 1, POLLIN);
          return 0;
        }
    }
  else
    {
      event->pfd = NULL;
    }

  spin_unlock_irqrestore(&event->buf->lock, flags);
  return 0;
}

#ifdef CONFIG_FS_FILEMAP
static int perf_mmap(FAR struct file *filep,
                     FAR struct mm_map_entry_s *map)
{
  FAR struct perf_event_s *event = filep->f_priv;

  if (!event->buf)
    {
      size_t buf_size = map->length;
      FAR void *buf;

      buf = kumm_zalloc(sizeof(struct perf_buffer_s) + buf_size);
      if (buf == NULL)
        {
          return -ENOMEM;
        }

      event->buf = buf;
      event->buf->ref_count = 1;
      circbuf_init(&event->buf->rb, buf + sizeof(struct perf_buffer_s),
                   buf_size);
      spin_lock_init(&event->buf->lock);
    }

  map->vaddr = event->buf;
  return 0;
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: perf_install_fd
 *
 * Description:
 *   Allocate a file descriptor bound to the perf event so that user space
 *   can operate on it through the perf file operations.  Called from
 *   perf_event_open() in the core framework; the fd/inode binding is kept
 *   private to this file so the core stays file-descriptor agnostic.
 *
 * Input Parameters:
 *   event - Perf event
 *   flags - Open flags from perf_event_open()
 *
 * Returned Value:
 *   Allocated file descriptor on success, a negated errno on failure.
 *
 ****************************************************************************/

int perf_install_fd(FAR struct perf_event_s *event, unsigned long flags)
{
  return file_allocate_from_inode(&g_perf_inode, O_RDONLY | flags,
                                  0, event, 0);
}
