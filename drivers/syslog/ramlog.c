/****************************************************************************
 * drivers/syslog/ramlog.c
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

#include <sys/types.h>
#include <sys/ioctl.h>

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <poll.h>
#include <errno.h>
#include <assert.h>
#include <debug.h>
#include <ctype.h>
#include <sys/boardctl.h>

#include <nuttx/nuttx.h>
#include <nuttx/arch.h>
#include <nuttx/kmalloc.h>
#include <nuttx/spinlock.h>
#include <nuttx/semaphore.h>
#include <nuttx/fs/fs.h>
#include <nuttx/fs/ioctl.h>
#include <nuttx/syslog/ramlog.h>
#include <nuttx/compiler.h>
#include <nuttx/list.h>
#include <nuttx/irq.h>
#include <nuttx/ring.h>
#include <nuttx/ratelimit.h>

#ifdef CONFIG_BINARY_SYSLOG
#  include <nuttx/sched_note.h>
#endif

#ifdef CONFIG_RAMLOG_DUMP_ON_PANIC
#  include <nuttx/panic_notifier.h>
#endif

#ifdef CONFIG_RAMLOG

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifdef CONFIG_BINARY_SYSLOG
#  define RAMLOG_BINARY true
#else
#  define RAMLOG_BINARY false
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct ramlog_user_s
{
  dq_entry_t        rl_entry;      /* The dq entry of reader */
  uint32_t          rl_read;       /* The read index (per-reader position) */
  uint32_t          rl_threashold; /* The threashold of the reader */
  sem_t             rl_readsem;    /* Per-reader wait semaphore */
#ifdef CONFIG_BINARY_SYSLOG
  struct note_dump_stream_s rl_stream; /* Per-reader frame-aware stream */
#endif

  /* The following the poll structures of threads waiting for driver events.
   * The 'struct pollfd' reference for each open is also retained in the
   * f_priv field of the 'struct file'.
   */

  FAR struct pollfd *rl_fds;
};

struct ramlog_dev_s
{
  FAR struct ring_s       *rl_ring;        /* The ring buffer */
  spinlock_t               rl_lock;        /* The lock for ramlog */
  dq_queue_t               rl_queue;       /* The dq of ramlog_user_s */
  struct ratelimit_state_s rl_ratelimit;   /* The ratelimit for ramlog */
#if CONFIG_RAMLOG_POLLTIMEOUT_MS > 0
  struct wdog_s            rl_wdog;        /* The wdog for poll timeout */
#endif
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/* Helper functions */

static void    ramlog_pollnotify(FAR struct ramlog_dev_s *priv,
                                 pollevent_t events);
#if CONFIG_RAMLOG_POLLTIMEOUT_MS > 0
static void    ramlog_timeout_handler(wdparm_t arg);
#endif

/* Character driver methods */

static int     ramlog_file_open(FAR struct file *filep);
static int     ramlog_file_close(FAR struct file *filep);
static ssize_t ramlog_file_read(FAR struct file *filep, FAR char *buffer,
                                size_t buflen);
static ssize_t ramlog_file_write(FAR struct file *filep,
                                 FAR const char *buffer, size_t buflen);
static int     ramlog_file_ioctl(FAR struct file *filep, int cmd,
                                 unsigned long arg);
static int     ramlog_file_poll(FAR struct file *filep,
                                FAR struct pollfd *fds, bool setup);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct file_operations g_ramlogfops =
{
  ramlog_file_open,  /* open */
  ramlog_file_close, /* close */
  ramlog_file_read,  /* read */
  ramlog_file_write, /* write */
  NULL,              /* seek */
  ramlog_file_ioctl, /* ioctl */
  NULL,              /* truncate */
  ramlog_file_poll   /* poll */
};

/* This is the pre-allocated buffer used for the console RAM log and/or
 * for the syslogging function.
 */

#ifdef CONFIG_RAMLOG_SYSLOG

/* Ring buffer for syslog. When RAMLOG_BUFFER_SECTION is configured,
 * the buffer is placed in a specific linker section (e.g. .noinit)
 * for warm-restart persistence.
 */

static char g_ramlog_blob[CONFIG_RAMLOG_BUFSIZE]
#ifdef RAMLOG_BUFFER_SECTION
locate_data(CONFIG_RAMLOG_BUFFER_SECTION)
#endif
aligned_data(RING_BLOB_ALIGN);

static struct ring_s g_ramlog_ring;

/* This is the device structure for the console or syslogging function.  The
 * ring handle is embedded and the buffer is bound lazily in ramlog_addbuf();
 * the fields are all zero-init (ramlog_dev_s carries no function pointers),
 * so a BSS instance is safe even though ramlog_putc can run before the
 * driver registration logic executes.
 */

static DEFINE_PER_CPU_BSS_BMP(struct ramlog_dev_s, g_ramlog_dev);
#  define g_ramlog_dev this_cpu_var_bmp(g_ramlog_dev)
#endif

#ifdef CONFIG_RAMLOG_DUMP_ON_PANIC
static int ramlog_panic_notifier(FAR struct notifier_block *nb,
                                 unsigned long action,
                                 FAR void *data);

static DEFINE_PER_CPU_BMP(struct notifier_block, g_ramlog_panic_nb) =
{
  .notifier_call = ramlog_panic_notifier,
  .priority      = INT_MIN,
};
#  define g_ramlog_panic_nb this_cpu_var_bmp(g_ramlog_panic_nb)
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: ramlog_is_blocking
 ****************************************************************************/

static inline_function bool ramlog_is_blocking(FAR struct file *filep)
{
  if (up_interrupt_context() ||
      (filep && filep->f_oflags & O_NONBLOCK))
    {
      return false;
    }

  return true;
}

/****************************************************************************
 * Name: ramlog_get_ring
 *
 * Description:
 *   Bind the syslog instance to the single g_ramlog_ring handle, and that
 *   handle to g_ramlog_blob.  Both are lazy because ramlog_putc() can run
 *   before any registration logic does, so g_ramlog_dev must stay a plain
 *   BSS instance with no initializer to carry the pointer.
 *
 *   The pointer store is per-CPU and always writes the same value, so
 *   repeating it costs nothing and races with nobody.  ring_mpmc_init() is
 *   idempotent through the handle's boot-scoped guard - and the handle being
 *   one global rather than one per CPU is exactly what makes that guard
 *   effective here.
 *
 *   Devices from ramlog_register() are bound at registration and pass
 *   through untouched.
 *
 ****************************************************************************/

static inline_function
FAR struct ring_s *ramlog_get_ring(FAR struct ramlog_dev_s *priv)
{
#ifdef CONFIG_RAMLOG_SYSLOG
  if (priv->rl_ring == NULL && priv == &g_ramlog_dev)
    {
      ring_mpmc_init(&g_ramlog_ring, g_ramlog_blob, sizeof(g_ramlog_blob),
                     CONFIG_RAMLOG_RECOVER_SIZE);
      priv->rl_ring = &g_ramlog_ring;
    }
#endif

  return priv->rl_ring;
}

#ifdef CONFIG_RAMLOG_DUMP_ON_PANIC
/****************************************************************************
 * Name: ramlog_dump
 *
 * Description:
 *   Dump the RAMLOG content to serial output using up_nputs.
 *   This function is designed to work in panic context with minimal
 *   stack usage by directly outputting from the ramlog buffer.
 *
 ****************************************************************************/

static void ramlog_dump(void)
{
  FAR struct ring_s *r = &g_ramlog_ring;
  struct ring_buf_s buf;

  if (!ring_is_ready(r))
    {
      return;
    }

  up_puts("\r\n========== RAMLOG DUMP BEGIN ==========\r\n");

  ring_mpmc_dequeue_start(r, UINT32_MAX, 1, &buf);

#if CONFIG_RAMLOG_DUMP_ON_PANIC_SIZE > 0
  if (buf.cnt > CONFIG_RAMLOG_DUMP_ON_PANIC_SIZE)
    {
      up_puts("[TRUNCATED]\r\n");
    }
#endif

  if (buf.cnt == 0)
    {
      up_puts("[EMPTY]\r\n");
    }
  else
    {
      up_nputs(buf.ptr1, buf.len1);
      if (buf.len2 > 0)
        {
          up_nputs(buf.ptr2, buf.len2);
        }
    }

  up_puts("\r\n========== RAMLOG DUMP END ==========\r\n");
}

/****************************************************************************
 * Name: ramlog_panic_notifier
 *
 * Description:
 *   Panic notifier callback to dump RAMLOG content.
 *
 ****************************************************************************/

static int ramlog_panic_notifier(FAR struct notifier_block *nb,
                                 unsigned long action,
                                 FAR void *data)
{
  UNUSED(nb);
  UNUSED(data);

  if (action == PANIC_KERNEL_FINAL
#if defined(CONFIG_RAMLOG_DUMP_ON_PANIC_CPUID) && CONFIG_RAMLOG_DUMP_ON_PANIC_CPUID >= 0
      && this_cpu() == CONFIG_RAMLOG_DUMP_ON_PANIC_CPUID
#endif
  )
    {
      ramlog_dump();
    }

  return 0;
}
#endif

#if CONFIG_RAMLOG_POLLTIMEOUT_MS > 0
static inline_function
void ramlog_timeout_reset(FAR struct ramlog_dev_s *dev)
{
  /* We need locked to ensure the wdog status and dq status aligned. */

  irqstate_t flags = spin_lock_irqsave(&dev->rl_lock);

  if (dq_empty(&dev->rl_queue))
    {
      wd_cancel(&dev->rl_wdog);
    }
  else
    {
      wd_start(&dev->rl_wdog, MSEC2TICK(CONFIG_RAMLOG_POLLTIMEOUT_MS),
               ramlog_timeout_handler, (wdparm_t)dev);
    }

  spin_unlock_irqrestore(&dev->rl_lock, flags);
}

static void ramlog_timeout_handler(wdparm_t arg)
{
  FAR struct ramlog_dev_s *dev = (FAR struct ramlog_dev_s *)arg;

  if (ring_count(dev->rl_ring) > 0)
    {
      ramlog_pollnotify(dev, POLLIN);
    }

  ramlog_timeout_reset(dev);
}
#else
#  define ramlog_timeout_reset(dev)
#endif

/****************************************************************************
 * Name: ramlog_unread
 ****************************************************************************/

static inline_function
uint32_t ramlog_unread(FAR struct ramlog_dev_s *priv,
                       FAR struct ramlog_user_s *upriv)
{
  return ring_mpmc_peek(priv->rl_ring, NULL, UINT32_MAX, &upriv->rl_read);
}

/****************************************************************************
 * Name: ramlog_pollnotify
 ****************************************************************************/

static void ramlog_pollnotify(FAR struct ramlog_dev_s *priv,
                              pollevent_t events)
{
  FAR dq_entry_t *entry;
  irqstate_t flags;

  /* This function may be called from an interrupt handler */

  flags = spin_lock_irqsave_nopreempt(&priv->rl_lock);

  dq_for_every(&priv->rl_queue, entry)
    {
      FAR struct ramlog_user_s *upriv =
        container_of(entry, struct ramlog_user_s, rl_entry);

      if ((events & POLLIN) &&
          ramlog_unread(priv, upriv) >= upriv->rl_threashold)
        {
          poll_notify(&upriv->rl_fds, 1, POLLIN);
          nxsem_post(&upriv->rl_readsem);
        }

      if (events & POLLOUT)
        {
          poll_notify(&upriv->rl_fds, 1, POLLOUT);
        }
    }

  spin_unlock_irqrestore_nopreempt(&priv->rl_lock, flags);
}

/****************************************************************************
 * Name: ramlog_addbuf
 *
 * Description:
 *   Write data to the ramlog using ring.h MPMC APIs.
 *   Overwrite mode uses ring_mpmc_overwrite.
 *   Non-overwrite mode uses ring_mpmc_enqueue (non-blocking).
 *
 ****************************************************************************/

static ssize_t ramlog_addbuf(FAR struct ramlog_dev_s *priv,
                             FAR const char *buffer, size_t len)
{
  FAR struct ring_s *r = ramlog_get_ring(priv);
  size_t written = 0;

  if (ratelimit_islimited(&priv->rl_ratelimit))
    {
      return len;
    }

#ifdef CONFIG_RAMLOG_NOOVERWRITE

  /* Non-overwrite mode: enqueue as much as possible, discard the rest */

  written = ring_mpmc_enqueue(r, buffer, len);
#else
  /* Overwrite mode: always succeeds, discards oldest data */

  written = len;

  if (len > ring_capacity(r))
    {
      buffer += len - ring_capacity(r);
      len = ring_capacity(r);
    }

  ring_mpmc_overwrite(r, buffer, len);
#endif

  /* Notify readers if any data was written */

  if (written > 0)
    {
      ramlog_pollnotify(priv, POLLIN);
    }

  ramlog_timeout_reset(priv);
  return written;
}

#ifdef CONFIG_BINARY_SYSLOG

/****************************************************************************
 * Name: ramlog_binary_read
 ****************************************************************************/

static ssize_t ramlog_binary_read(FAR struct ramlog_dev_s *priv,
                                  FAR struct ramlog_user_s *upriv,
                                  FAR char *buffer, size_t len)
{
  ssize_t nread;

  nread = lib_stream_gets(&upriv->rl_stream.common, buffer, len);

  /* Sync the reader position so ramlog_unread() and pollnotify() report
   * the correct amount of data still pending for this reader.
   */

  upriv->rl_read = upriv->rl_stream.pos;

  return nread;
}

#endif /* CONFIG_BINARY_SYSLOG */

/****************************************************************************
 * Name: ramlog_file_read
 *
 * Description:
 *   Read from the ramlog ring buffer.
 *
 *   Overwrite mode: reader only advances its own rl_read cursor without
 *   touching cons_tail. The writer is the sole entity that pushes
 *   cons_tail (via overwrite discard). This allows multiple independent
 *   readers to each maintain their own position.
 *
 *   Non-overwrite mode: uses MC dequeue which advances cons_tail,
 *   implementing clear-on-read semantics. Slower readers get clamped.
 *
 ****************************************************************************/

static ssize_t ramlog_file_read(FAR struct file *filep, FAR char *buffer,
                                size_t len)
{
  FAR struct inode *inode = filep->f_inode;
  FAR struct ramlog_dev_s *priv = inode->i_private;
  FAR struct ramlog_user_s *upriv = filep->f_priv;
#ifndef CONFIG_BINARY_SYSLOG
  FAR struct ring_s *r = priv->rl_ring;
#endif
#ifdef CONFIG_RAMLOG_NOOVERWRITE
  FAR dq_entry_t *entry;
  irqstate_t flags;
  uint32_t minread;
#endif
  ssize_t nread;
  int ret;

  /* Loop until data is available or we must return */

  for (; ; )
    {
#ifdef CONFIG_BINARY_SYSLOG
      nread = ramlog_binary_read(priv, upriv, buffer, len);
#else
      nread = ring_mpmc_peek(r, buffer, len, &upriv->rl_read);
#endif
      if (nread > 0)
        {
          break;
        }

      /* No data available */

      if (!ramlog_is_blocking(filep))
        {
          return 0;
        }

      ret = nxsem_wait(&upriv->rl_readsem);
      if (ret < 0)
        {
          return ret;
        }
    }

  /* Non-overwrite: advance cons_tail to the lowest rl_read of all
   * readers, freeing buffer space for new log data.
   */

#ifdef CONFIG_RAMLOG_NOOVERWRITE
  minread = upriv->rl_read;

  flags = spin_lock_irqsave(&priv->rl_lock);
  dq_for_every(&priv->rl_queue, entry)
    {
      FAR struct ramlog_user_s *u = (FAR struct ramlog_user_s *)entry;

      if (ring_is_valid_cons(r, minread, u->rl_read))
        {
          minread = u->rl_read;
        }
    }

  spin_unlock_irqrestore(&priv->rl_lock, flags);
  ring_mpmc_skipto(r, minread);
#endif

  return nread;
}

/****************************************************************************
 * Name: ramlog_file_write
 ****************************************************************************/

static ssize_t ramlog_file_write(FAR struct file *filep,
                                 FAR const char *buffer, size_t len)
{
  FAR struct inode *inode = filep->f_inode;
  FAR struct ramlog_dev_s *priv = inode->i_private;

  return ramlog_addbuf(priv, buffer, len);
}

/****************************************************************************
 * Name: ramlog_file_ioctl
 ****************************************************************************/

static int ramlog_file_ioctl(FAR struct file *filep, int cmd,
                             unsigned long arg)
{
  FAR struct inode *inode = filep->f_inode;
  FAR struct ramlog_dev_s *priv = inode->i_private;
  FAR struct ramlog_user_s *upriv = filep->f_priv;
  int ret = 0;

  switch (cmd)
    {
      case FIONREAD:
        *(FAR int *)((uintptr_t)arg) = ramlog_unread(priv, upriv);
        break;
#ifdef CONFIG_BINARY_SYSLOG
      case NOTE_GETREADMODE:
        if (arg == 0)
          {
            ret = -EINVAL;
          }
        else
          {
            *(FAR unsigned int *)arg = upriv->rl_stream.binary ?
                                       NOTE_MODE_READ_BINARY :
                                       NOTE_MODE_READ_ASCII;
          }
        break;
      case NOTE_SETREADMODE:
        if (arg == 0)
          {
            ret = -EINVAL;
          }
        else
          {
            upriv->rl_stream.binary = (arg == NOTE_MODE_READ_BINARY);
          }
        break;
#endif
      case PIPEIOC_POLLINTHRD:
        upriv->rl_threashold = (uint32_t)arg;
        break;
      case PIPEIOC_GETSIZE:
        if (arg == 0)
          {
            ret = -EINVAL;
          }
        else
          {
            *(FAR unsigned int *)arg = ring_capacity(priv->rl_ring);
          }
        break;
      case SYSLOGIOC_CLEAR:
        ring_mpmc_skip(priv->rl_ring, ring_count(priv->rl_ring));
        break;

      case SYSLOGIOC_SETRATELIMIT:
        if (arg == 0)
          {
            ret = -EINVAL;
          }
        else
          {
            FAR struct syslog_ratelimit_s *limit =
              (FAR struct syslog_ratelimit_s *)arg;

            priv->rl_ratelimit.interval = limit->interval;
            priv->rl_ratelimit.burst = limit->burst;
            ret = 0;
          }
        break;

      case SYSLOGIOC_GETRATELIMIT:
        if (arg == 0)
          {
            ret = -EINVAL;
          }
        else
          {
            FAR struct syslog_ratelimit_s *limit =
              (FAR struct syslog_ratelimit_s *)arg;

            limit->interval = priv->rl_ratelimit.interval;
            limit->burst = priv->rl_ratelimit.burst;
            ret = 0;
          }
        break;

      default:
        ret = -ENOTTY;
        break;
    }

  return ret;
}

/****************************************************************************
 * Name: ramlog_file_poll
 ****************************************************************************/

static int ramlog_file_poll(FAR struct file *filep, FAR struct pollfd *fds,
                            bool setup)
{
  FAR struct inode *inode = filep->f_inode;
  FAR struct ramlog_dev_s *priv = inode->i_private;
  FAR struct ramlog_user_s *upriv = filep->f_priv;
  pollevent_t eventset = 0;
  irqstate_t flags;

  /* Get exclusive access to the poll structures */

  flags = spin_lock_irqsave(&priv->rl_lock);

  /* Are we setting up the poll?  Or tearing it down? */

  if (setup)
    {
      /* This is a request to set up the poll.  Find an available
       * slot for the poll structure reference.
       */

      if (!upriv->rl_fds)
        {
          upriv->rl_fds = fds;
          fds->priv     = &upriv->rl_fds;
        }

      /* Should immediately notify on any of the requested events? */

      /* Check if the receive buffer is not empty. */

      if (ramlog_unread(priv, upriv) >= upriv->rl_threashold)
        {
          eventset |= POLLIN;
        }

#ifdef CONFIG_RAMLOG_NOOVERWRITE
      if (ring_space(priv->rl_ring) > 0)
#endif
        {
          eventset |= POLLOUT;
        }

      spin_unlock_irqrestore(&priv->rl_lock, flags);
      poll_notify(&fds, 1, eventset);
    }
  else if (fds->priv)
    {
      /* This is a request to tear down the poll. */

      FAR struct pollfd **slot = (FAR struct pollfd **)fds->priv;

      /* Remove all memory of the poll setup */

      *slot     = NULL;
      fds->priv = NULL;
      spin_unlock_irqrestore(&priv->rl_lock, flags);
    }
  else
    {
      spin_unlock_irqrestore(&priv->rl_lock, flags);
    }

  ramlog_timeout_reset(priv);
  return 0;
}

/****************************************************************************
 * Name: ramlog_file_open
 ****************************************************************************/

static int ramlog_file_open(FAR struct file *filep)
{
  FAR struct inode *inode = filep->f_inode;
  FAR struct ramlog_dev_s *priv = inode->i_private;
  FAR struct ramlog_user_s *upriv;
  FAR struct ring_s *r = ramlog_get_ring(priv);
  irqstate_t flags;

  upriv = kmm_zalloc(sizeof(struct ramlog_user_s));
  if (upriv == NULL)
    {
      return -ENOMEM;
    }

  upriv->rl_threashold = CONFIG_RAMLOG_POLLTHRESHOLD;
  upriv->rl_read = RING_IDX_OLDEST;
  ring_mpmc_peek(r, NULL, UINT32_MAX, &upriv->rl_read);

#ifdef CONFIG_BINARY_SYSLOG
  note_dump_stream(&upriv->rl_stream, priv->rl_ring, RAMLOG_BINARY);
#endif

  nxsem_init(&upriv->rl_readsem, 0, 0);

  flags = spin_lock_irqsave(&priv->rl_lock);
  dq_addlast(&upriv->rl_entry, &priv->rl_queue);
  spin_unlock_irqrestore(&priv->rl_lock, flags);

  filep->f_priv = upriv;
  return 0;
}

/****************************************************************************
 * Name: ramlog_file_close
 ****************************************************************************/

static int ramlog_file_close(FAR struct file *filep)
{
  FAR struct inode *inode = filep->f_inode;
  FAR struct ramlog_dev_s *priv = inode->i_private;
  FAR struct ramlog_user_s *upriv = filep->f_priv;
  irqstate_t flags;

  flags = spin_lock_irqsave(&priv->rl_lock);
  dq_rem(&upriv->rl_entry, &priv->rl_queue);
  spin_unlock_irqrestore(&priv->rl_lock, flags);

  nxsem_destroy(&upriv->rl_readsem);
  kmm_free(upriv);
  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: ramlog_register
 *
 * Description:
 *   Create the RAM logging device and register it at the specified path.
 *
 ****************************************************************************/

int ramlog_register(FAR const char *devpath, FAR char *buffer, size_t buflen)
{
  FAR struct ramlog_dev_s *priv;
  int ret;

  /* Allocate a RAM logging device structure */

  priv = kmm_zalloc(sizeof(struct ramlog_dev_s) + sizeof(struct ring_s));
  if (priv == NULL)
    {
      return -ENOMEM;
    }

  priv->rl_ring = (FAR struct ring_s *)(priv + 1);

  ring_mpmc_init(priv->rl_ring, buffer, buflen, 0);

  spin_lock_init(&priv->rl_lock);

  ret = register_driver(devpath, &g_ramlogfops, 0666, priv);
  if (ret < 0)
    {
      kmm_free(priv);
    }

  return ret;
}

/****************************************************************************
 * Name: ramlog_syslog_register
 *
 * Description:
 *   Use a pre-allocated RAM logging device and register it at the path
 *   specified by CONFIG_RAMLOG_SYSLOG
 *
 ****************************************************************************/

#ifdef CONFIG_RAMLOG_SYSLOG
void ramlog_syslog_register(void)
{
#ifdef CONFIG_RAMLOG_DUMP_ON_PANIC
  panic_notifier_chain_register(&g_ramlog_panic_nb);
#endif

  /* Register the syslog character driver */

  register_driver(CONFIG_RAMLOG_DEVPATH, &g_ramlogfops, 0666, &g_ramlog_dev);
}
#endif

/****************************************************************************
 * Name: ramlog_putc
 *
 * Description:
 *   This is the low-level system logging interface.
 *
 ****************************************************************************/

#ifdef CONFIG_RAMLOG_SYSLOG
int ramlog_putc(FAR syslog_channel_t *channel, int ch)
{
  char cch = ch;

  UNUSED(channel);

  /* Add the character to the RAMLOG */

  ramlog_addbuf(&g_ramlog_dev, &cch, 1);

  /* Return the character added on success */

  return ch;
}

ssize_t ramlog_write(FAR syslog_channel_t *channel,
                     FAR const char *buffer, size_t buflen)
{
  return ramlog_addbuf(&g_ramlog_dev, buffer, buflen);
}
#endif

#endif /* CONFIG_RAMLOG */
