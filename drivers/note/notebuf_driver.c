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

#include <nuttx/note/notebuf_driver.h>

#define NOTEBUF_MAX_NOTELEN 256

#ifdef CONFIG_DRIVERS_NOTEBUF_HPWORK
#  define NOTEBUF_HPWORK HPWORK
#else
#  define NOTEBUF_HPWORK LPWORK
#endif

#ifndef CONFIG_DRIVERS_NOTEBUF_FLUSH_DELAY
#  define CONFIG_DRIVERS_NOTEBUF_FLUSH_DELAY 0
#endif

#if CONFIG_DRIVERS_NOTEBUF_FLUSH_DELAY > 0
#  define notebuf_irqsafe(buf) ((buf)->irqsafe)
#else
#  define notebuf_irqsafe(buf) true
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void notebuf_skip(FAR struct notebuf_driver_s *buf, size_t len)
{
#ifndef CONFIG_DRIVERS_NOTEBUF_NOOVERWRITE
  irqstate_t flags;
  uint8_t skip;

  flags = spin_lock_irqsave_notrace(&buf->lock);
  while (ring_space(&buf->ring) < len)
    {
      if (ring_mpsc_dequeue(&buf->ring, &skip, 1) == 0)
        {
          break;
        }

      if (skip > 1)
        {
          ring_mpsc_dequeue_skip(&buf->ring, skip - 1);
        }
    }

  spin_unlock_irqrestore_notrace(&buf->lock, flags);
#endif
}

static void notebuf_flush(FAR void *arg)
{
  FAR struct notebuf_driver_s *buf = (FAR struct notebuf_driver_s *)arg;
  FAR struct note_driver_s *lower = buf->lower;

#ifdef CONFIG_DRIVERS_NOTEBUF_NOOVERWRITE
  irqstate_t flags;
  uint8_t note[NOTEBUF_MAX_NOTELEN];

  flags = spin_lock_irqsave_notrace(&buf->lock);
  while (ring_mpsc_dequeue(&buf->ring, note, 1) > 0)
    {
      uint8_t notelen = note[0];

      notelen = ring_mpsc_dequeue(&buf->ring, &note[1], notelen - 1);

      spin_unlock_irqrestore_notrace(&buf->lock, flags);
      lower->ops->add(lower, note, notelen + 1, false);
      flags = spin_lock_irqsave_notrace(&buf->lock);
    }

  spin_unlock_irqrestore_notrace(&buf->lock, flags);
#else
  struct ring_buf_s rb;

  ring_mpsc_dequeue_start(&buf->ring,
                          ring_count(&buf->ring), 1, &rb);
  if (rb.len1 > 0)
    {
      lower->ops->add(lower, rb.ptr1, rb.len1, false);
    }

  if (rb.len2 > 0)
    {
      lower->ops->add(lower, rb.ptr2, rb.len2, false);
    }

  ring_mpsc_dequeue_finish(&buf->ring, &rb);
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

static int notebuf_add(FAR struct note_driver_s *drv,
                       FAR const void *note, size_t len,
                       bool noswitches)
{
  FAR struct notebuf_driver_s *buf =
    (FAR struct notebuf_driver_s *)drv;
  FAR struct note_driver_s *lower = buf->lower;

  if (notebuf_irqsafe(buf))
    {
      notebuf_flush(buf);
      if (lower->ops->add(lower, note, len, noswitches) >= 0)
        {
          return 0;
        }
    }

  notebuf_skip(buf, len);
  ring_mpsc_enqueue(&buf->ring, note, len);

#if CONFIG_DRIVERS_NOTEBUF_FLUSH_DELAY > 0
  if (!noswitches)
    {
      work_queue(NOTEBUF_HPWORK, &buf->work,
                 notebuf_flush, buf,
                 USEC2TICK(CONFIG_DRIVERS_NOTEBUF_FLUSH_DELAY));
    }
#endif

  return 0;
}

/****************************************************************************
 * Public Data
 ****************************************************************************/

const struct note_driver_ops_s g_notebuf_ops =
{
  .add = notebuf_add,
  .flush = notebuf_flush_all,
};
