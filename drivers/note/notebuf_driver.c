/****************************************************************************
 * drivers/note/notebuf_driver.c
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

#include <assert.h>

#include <nuttx/atomic.h>
#include <nuttx/fs/fs.h>
#include <nuttx/note/note_driver.h>
#include <nuttx/note/notebuf_driver.h>

#define NOTEBUF_MAX_NOTELEN 256

#ifdef CONFIG_DRIVERS_NOTEBUF_HPWORK
#  define NOTEBUF_HPWORK HPWORK
#else
#  define NOTEBUF_HPWORK LPWORK
#endif

#if CONFIG_DRIVERS_NOTEBUF_FLUSH_WATERMARK > 0
#  define notebuf_irqsafe(buf) ((buf)->irqsafe)
#else
#  define notebuf_irqsafe(buf) true
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void notebuf_flush(FAR void *arg)
{
  FAR struct notebuf_driver_s *buf = (FAR struct notebuf_driver_s *)arg;
  FAR struct note_driver_s *lower = buf->lower;
  struct ring_buf_s rb;
#ifdef CONFIG_DRIVERS_NOTE_STATS
  ssize_t written;
  size_t dropped = 0;
#endif

  ring_mpmc_dequeue_start(buf->ring,
                          ring_count(buf->ring), 1, &rb);
  if (rb.len1 > 0)
    {
#ifdef CONFIG_DRIVERS_NOTE_STATS
      written = lower->ops->add(lower, rb.ptr1, rb.len1, false);
      dropped += written <= 0 ? rb.len1 : rb.len1 - written;
#else
      lower->ops->add(lower, rb.ptr1, rb.len1, false);
#endif
    }

  if (rb.len2 > 0)
    {
#ifdef CONFIG_DRIVERS_NOTE_STATS
      written = lower->ops->add(lower, rb.ptr2, rb.len2, false);
      dropped += written <= 0 ? rb.len2 : rb.len2 - written;
#else
      lower->ops->add(lower, rb.ptr2, rb.len2, false);
#endif
    }

  ring_mpmc_dequeue_finish(buf->ring, &rb);

#ifdef CONFIG_DRIVERS_NOTE_STATS
  if (dropped > 0)
    {
      atomic_add_relaxed(&buf->driver.stats.drop_bytes, dropped);
    }
#endif
}

static void notebuf_flush_all(FAR struct note_driver_s *drv)
{
  FAR struct notebuf_driver_s *buf =
    (FAR struct notebuf_driver_s *)drv;
  FAR struct note_driver_s *lower = buf->lower;

  notebuf_flush(buf);

  if (lower && lower->ops->flush)
    {
      lower->ops->flush(lower);
    }
}

static ssize_t notebuf_add(FAR struct note_driver_s *drv,
                           FAR const void *note, size_t len,
                           bool noswitches)
{
  FAR struct notebuf_driver_s *buf =
    (FAR struct notebuf_driver_s *)drv;
  FAR struct note_driver_s *lower = buf->lower;
  ssize_t written = 0;

#ifdef DRIVERS_NOTEBUF_SECTION
  /* Custom linker sections may be NOLOAD (e.g. standbyram), where
   * static initializers are ineffective. Lazy-init ensures the ring
   * is usable regardless of section type. use ring_init for
   * CAS-serialized cross-core init and warm-restart recovery on
   * NOLOAD section if the ring is already initialized.
   */

  ring_init(buf->ring, CONFIG_DRIVERS_NOTEBUF_BUFSIZE);
#endif

  if (notebuf_irqsafe(buf))
    {
      notebuf_flush(buf);

      written = lower->ops->add(lower, note, len, noswitches);
      if (written == len)
        {
          return written;
        }

      if (written < 0)
        {
          written = 0;
        }

      note = (FAR const char *)note + written;
      len -= written;
    }

#ifdef CONFIG_DRIVERS_NOTEBUF_NOOVERWRITE
  written += ring_mpmc_enqueue(buf->ring, note, len);
#else
  written += ring_mpmc_overwrite(buf->ring, note, len);
#endif

#if CONFIG_DRIVERS_NOTEBUF_FLUSH_WATERMARK > 0
  if (ring_space(buf->ring) * 100 <=
      ring_capacity(buf->ring) * CONFIG_DRIVERS_NOTEBUF_FLUSH_WATERMARK)
    {
      work_queue(NOTEBUF_HPWORK, &buf->work,
                 notebuf_flush, buf, 0);
    }
#endif

#if CONFIG_DRIVERS_NOTEBUF_FLUSH_TIMEOUT > 0
  if (work_available(&buf->work))
    {
      work_queue(NOTEBUF_HPWORK, &buf->work,
                 notebuf_flush, buf,
                 MSEC2TICK(CONFIG_DRIVERS_NOTEBUF_FLUSH_TIMEOUT));
    }
#endif

  return written;
}

static int notebuf_ioctl(FAR struct file *filep, int cmd,
                         unsigned long arg)
{
  FAR struct note_driver_s *driver = filep->f_inode->i_private;
  FAR struct notebuf_driver_s *buf =
    (FAR struct notebuf_driver_s *)driver;

  switch (cmd)
    {
      case NOTE_CLEAR:
#ifdef CONFIG_DRIVERS_NOTE_STATS
        atomic_add(&driver->stats.drop_bytes, ring_reset(buf->ring));
#else
        ring_reset(buf->ring);
#endif
        return OK;

      default:
        return -ENOTTY;
    }
}

/****************************************************************************
 * Public Data
 ****************************************************************************/

const struct note_driver_ops_s g_notebuf_ops =
{
  .add = notebuf_add,
  .flush = notebuf_flush_all,
  .ioctl = notebuf_ioctl,
};
