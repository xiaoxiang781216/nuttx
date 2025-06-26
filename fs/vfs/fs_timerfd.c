/****************************************************************************
 * fs/vfs/fs_timerfd.c
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
#include <stdio.h>
#include <poll.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <debug.h>

#include <nuttx/irq.h>
#include <nuttx/wdog.h>
#include <nuttx/atomic.h>
#include <nuttx/spinlock.h>

#include <sys/ioctl.h>
#include <sys/timerfd.h>

#include "clock/clock.h"
#include "inode/inode.h"
#include "fs_heap.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef struct timerfd_waiter_sem_s
{
  sem_t sem;
  FAR struct timerfd_waiter_sem_s *next;
} timerfd_waiter_sem_t;

/* This structure describes the internal state of the driver */

struct timerfd_priv_s
{
  spinlock_t                lock;    /* Lock to protect the driver state */
  FAR timerfd_waiter_sem_t *rdsems;  /* List of blocking readers */
  int                       clock;   /* Clock to use as the timing base */
  int                       delay;   /* If non-zero, used to reset repetitive
                                      * timers */
  struct wdog_s             wdog;    /* The watchdog that provides the timing */
  timerfd_t                 counter; /* timerfd counter */
  atomic_t                  crefs;   /* References counts on timerfd (max: 255) */

  /* The following is a list if poll structures of threads waiting for
   * driver events.
   */

#ifdef CONFIG_TIMER_FD_POLL
  FAR struct pollfd *fds[CONFIG_TIMER_FD_NPOLLWAITERS];
#endif
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int timerfd_open(FAR struct file *filep);
static int timerfd_close(FAR struct file *filep);

static ssize_t timerfd_read(FAR struct file *filep, FAR char *buffer,
                            size_t len);
#ifdef CONFIG_TIMER_FD_POLL
static int timerfd_poll(FAR struct file *filep, FAR struct pollfd *fds,
                        bool setup);
#endif

static int timerfd_blocking_io(FAR struct timerfd_priv_s *dev,
                               FAR timerfd_waiter_sem_t  *sem,
                               FAR timerfd_waiter_sem_t **slist,
                               FAR irqstate_t *intflags);

static FAR struct timerfd_priv_s *timerfd_allocdev(void);
static void timerfd_destroy(FAR struct timerfd_priv_s *dev);

static void timerfd_timeout(wdparm_t arg);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct file_operations g_timerfd_fops =
{
  timerfd_open,  /* open */
  timerfd_close, /* close */
  timerfd_read,  /* read */
  NULL,          /* write */
  NULL,          /* seek */
  NULL,          /* ioctl */
  NULL,          /* mmap */
  NULL,          /* truncate */
#ifdef CONFIG_TIMER_FD_POLL
  timerfd_poll   /* poll */
#endif
};

static struct inode g_timerfd_inode =
{
  NULL,                   /* i_parent */
  NULL,                   /* i_peer */
  NULL,                   /* i_child */
  1,                      /* i_crefs */
  FSNODEFLAG_TYPE_DRIVER, /* i_flags */
  {
    &g_timerfd_fops       /* u */
  }
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static FAR struct timerfd_priv_s *timerfd_allocdev(void)
{
  FAR struct timerfd_priv_s *dev;

  dev = (FAR struct timerfd_priv_s *)
    fs_heap_zalloc(sizeof(struct timerfd_priv_s));
  if (dev)
    {
      /* Initialize the private structure */

      spin_lock_init(&dev->lock);
      atomic_set(&dev->crefs, 1);
    }

  return dev;
}

static void timerfd_destroy(FAR struct timerfd_priv_s *dev)
{
  wd_cancel(&dev->wdog);
  fs_heap_free(dev);
}

static int timerfd_open(FAR struct file *filep)
{
  FAR struct timerfd_priv_s *priv = filep->f_priv;

  /* Get exclusive access to the device structures */

  atomic_fetch_add(&priv->crefs, 1);
  return OK;
}

static int timerfd_close(FAR struct file *filep)
{
  FAR struct timerfd_priv_s *priv = filep->f_priv;

  /* Decrement the references to the driver.  If the reference count will
   * decrement to 0, then uninitialize the driver.
   */

  if (atomic_fetch_sub(&priv->crefs, 1) > 1)
    {
      /* Just decrement the reference count and release the semaphore */

      return OK;
    }

  /* Re-create the path to the driver. */

  finfo("destroy\n");

  timerfd_destroy(priv);
  return OK;
}

static int timerfd_blocking_io(FAR struct timerfd_priv_s *dev,
                               FAR  timerfd_waiter_sem_t *sem,
                               FAR timerfd_waiter_sem_t **slist,
                               FAR irqstate_t *intflags)
{
  int ret;

  sem->next = *slist;
  *slist = sem;

  /* Wait for timerfd to notify */

  spin_unlock_irqrestore(&dev->lock, *intflags);
  ret = nxsem_wait(&sem->sem);
  *intflags = spin_lock_irqsave(&dev->lock);
  if (ret < 0)
    {
      FAR timerfd_waiter_sem_t *cur_sem;

      cur_sem = *slist;
      if (cur_sem == sem)
        {
          *slist = sem->next;
        }
      else
        {
          while (cur_sem)
            {
              if (cur_sem->next == sem)
                {
                  cur_sem->next = sem->next;
                  break;
                }
            }
        }
    }

  return ret;
}

static ssize_t timerfd_read(FAR struct file *filep, FAR char *buffer,
                            size_t len)
{
  FAR struct timerfd_priv_s *dev = filep->f_priv;
  timerfd_waiter_sem_t sem;
  irqstate_t intflags;
  ssize_t ret;

  if (len < sizeof(timerfd_t) || buffer == NULL)
    {
      return -EINVAL;
    }

  nxsem_init(&sem.sem, 0, 0);

  /* Ensure that interrupts are disabled and we do not lose counts
   * if expiration occurs after read, but before setting counter
   * to zero
   */

  intflags = spin_lock_irqsave(&dev->lock);

  /* Wait for an incoming event */

  if (dev->counter == 0)
    {
      if (filep->f_oflags & O_NONBLOCK)
        {
          spin_unlock_irqrestore(&dev->lock, intflags);
          nxsem_destroy(&sem.sem);
          return -EAGAIN;
        }

      do
        {
          ret = timerfd_blocking_io(dev, &sem, &dev->rdsems, &intflags);
          if (ret < 0)
            {
              spin_unlock_irqrestore(&dev->lock, intflags);
              nxsem_destroy(&sem.sem);
              return ret;
            }
        }
      while (dev->counter == 0);
    }

  *(FAR timerfd_t *)buffer = dev->counter;
  dev->counter = 0;
  spin_unlock_irqrestore(&dev->lock, intflags);
  nxsem_destroy(&sem.sem);

  return sizeof(timerfd_t);
}

#ifdef CONFIG_TIMER_FD_POLL
static int timerfd_poll(FAR struct file *filep, FAR struct pollfd *fds,
                        bool setup)
{
  FAR struct timerfd_priv_s *dev = filep->f_priv;
  irqstate_t intflags;
  int ret = OK;
  int i;

  intflags = spin_lock_irqsave(&dev->lock);
  if (!setup)
    {
      /* This is a request to tear down the poll. */

      FAR struct pollfd **slot = (FAR struct pollfd **)fds->priv;

      /* Remove all memory of the poll setup */

      *slot     = NULL;
      fds->priv = NULL;
      goto out;
    }

  /* This is a request to set up the poll. Find an available
   * slot for the poll structure reference
   */

  for (i = 0; i < CONFIG_TIMER_FD_NPOLLWAITERS; i++)
    {
      /* Find an available slot */

      if (!dev->fds[i])
        {
          /* Bind the poll structure and this slot */

          dev->fds[i] = fds;
          fds->priv   = &dev->fds[i];
          break;
        }
    }

  if (i >= CONFIG_TIMER_FD_NPOLLWAITERS)
    {
      fds->priv = NULL;
      ret       = -EBUSY;
      goto out;
    }

  /* Notify the POLLIN event if the counter is not zero */

  if (dev->counter > 0)
    {
      spin_unlock_irqrestore(&dev->lock, intflags);
#ifdef CONFIG_TIMER_FD_POLL
      poll_notify(&fds, 1, POLLIN);
#endif
      return OK;
    }

out:
  spin_unlock_irqrestore(&dev->lock, intflags);
  return ret;
}
#endif

static void timerfd_timeout(wdparm_t arg)
{
  FAR struct timerfd_priv_s *dev = (FAR struct timerfd_priv_s *)arg;
  FAR timerfd_waiter_sem_t *cur_sem;
  irqstate_t intflags;

  /* Disable interrupts to ensure that expiration counter is accessed
   * atomically
   */

  intflags = spin_lock_irqsave_nopreempt(&dev->lock);

  /* Increment timer expiration counter */

  dev->counter++;

  /* If this is a repetitive timer, then restart the watchdog */

  if (dev->delay > 0)
    {
      wd_start(&dev->wdog, dev->delay, timerfd_timeout, arg);
    }

#ifdef CONFIG_TIMER_FD_POLL
  /* Notify all poll/select waiters */

  poll_notify(dev->fds, CONFIG_TIMER_FD_NPOLLWAITERS, POLLIN);
#endif

  /* Notify all of the waiting readers */

  cur_sem = dev->rdsems;
  while (cur_sem != NULL)
    {
      nxsem_post(&cur_sem->sem);
      cur_sem = cur_sem->next;
    }

  dev->rdsems = NULL;
  spin_unlock_irqrestore_nopreempt(&dev->lock, intflags);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int timerfd_create(int clockid, int flags)
{
  FAR struct timerfd_priv_s *new_dev;
  int new_fd;
  int ret;

  /* Sanity checks. */

  if ((clockid != CLOCK_REALTIME &&
       clockid != CLOCK_MONOTONIC &&
       clockid != CLOCK_BOOTTIME) ||
      (flags & ~(TFD_NONBLOCK | TFD_CLOEXEC)) != 0)
    {
      ret = -EINVAL;
      goto errout;
    }

  /* Allocate instance data for this driver */

  new_dev = timerfd_allocdev();
  if (new_dev == NULL)
    {
      /* Failed to allocate new device */

      ret = -ENOMEM;
      goto errout;
    }

  /* Initialize the timer instance */

  new_dev->clock = clockid;
  new_fd = file_allocate_from_inode(&g_timerfd_inode, O_RDONLY | flags,
                                    0, new_dev, 0);
  if (new_fd < 0)
    {
      ret = new_fd;
      goto errout_with_dev;
    }

  /* Device is ready for use */

  return new_fd;

errout_with_dev:
  timerfd_destroy(new_dev);
errout:
  set_errno(-ret);
  return ERROR;
}

int timerfd_settime(int fd, int flags,
                    FAR const struct itimerspec *new_value,
                    FAR struct itimerspec *old_value)
{
  FAR struct timerfd_priv_s *dev;
  FAR struct file *filep;
  irqstate_t intflags;
  sclock_t delay;
  int ret;

  /* Some sanity checks */

  if (!new_value)
    {
      ret = -EFAULT;
      goto errout;
    }

  if ((flags & ~TFD_TIMER_ABSTIME) != 0)
    {
      ret = -EINVAL;
      goto errout;
    }

  /* Get file pointer by file descriptor */

  ret = file_get(fd, &filep);
  if (ret < 0)
    {
      goto errout;
    }

  if (filep->f_inode->u.i_ops != &g_timerfd_fops)
    {
      goto errout_with_filep;
    }

  dev = (FAR struct timerfd_priv_s *)filep->f_priv;

  /* Disable interrupts here to ensure that expiration counter is accessed
   * atomicaly.
   */

  intflags = spin_lock_irqsave(&dev->lock);
  if (old_value)
    {
      /* Get the number of ticks before the underlying watchdog expires */

      delay = wd_gettime(&dev->wdog);

      /* Convert that to a struct timespec and return it */

      clock_ticks2time(&old_value->it_value, delay);
      clock_ticks2time(&old_value->it_interval, dev->delay);
    }

  /* Disarm the timer (in case the timer was already armed when
   * timerfd_settime() is called).
   */

  wd_cancel(&dev->wdog);

  /* Clear expiration counter */

  dev->counter = 0;

  /* If the it_value member of value is zero, the timer will not be
   * re-armed
   */

  if (new_value->it_value.tv_sec <= 0 && new_value->it_value.tv_nsec <= 0)
    {
      spin_unlock_irqrestore(&dev->lock, intflags);
      file_put(filep);
      return OK;
    }

  /* Setup up any repetitive timer */

  delay = clock_time2ticks(&new_value->it_interval);
  dev->delay = delay;

  /* We need to disable timer interrupts through the following section so
   * that the system timer is stable.
   */

  /* Check if abstime is selected */

  if ((flags & TFD_TIMER_ABSTIME) != 0)
    {
      /* Calculate a delay corresponding to the absolute time in 'value' */

      clock_abstime2ticks(dev->clock, &new_value->it_value, &delay);
    }
  else
    {
      /* Calculate a delay assuming that 'value' holds the relative time
       * to wait.  We have internal knowledge that clock_time2ticks always
       * returns success.
       */

      delay = clock_time2ticks(&new_value->it_value);
    }

  /* If the time is in the past or now, then set up the next interval
   * instead (assuming a repetitive timer).
   */

  if (delay <= 0)
    {
      delay = dev->delay;
    }

  /* Then start the watchdog */

  ret = wd_start(&dev->wdog, delay, timerfd_timeout, (wdparm_t)dev);
  spin_unlock_irqrestore(&dev->lock, intflags);
  if (ret < 0)
    {
      goto errout_with_filep;
    }

  file_put(filep);
  return OK;

errout_with_filep:
  file_put(filep);
errout:
  set_errno(-ret);
  return ERROR;
}

int timerfd_gettime(int fd, FAR struct itimerspec *curr_value)
{
  FAR struct timerfd_priv_s *dev;
  FAR struct file *filep;
  sclock_t ticks;
  int ret;

  /* Some sanity checks */

  if (!curr_value)
    {
      ret = -EFAULT;
      goto errout;
    }

  /* Get file pointer by file descriptor */

  ret = file_get(fd, &filep);
  if (ret < 0)
    {
      goto errout;
    }

  if (filep->f_inode->u.i_ops != &g_timerfd_fops)
    {
      file_put(filep);
      goto errout;
    }

  dev = (FAR struct timerfd_priv_s *)filep->f_priv;

  /* Get the number of ticks before the underlying watchdog expires */

  ticks = wd_gettime(&dev->wdog);

  /* Convert that to a struct timespec and return it */

  clock_ticks2time(&curr_value->it_value, ticks);
  clock_ticks2time(&curr_value->it_interval, dev->delay);
  file_put(filep);
  return OK;

errout:
  set_errno(-ret);
  return ERROR;
}
