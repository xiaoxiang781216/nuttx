/****************************************************************************
 * drivers/note/notesnap_driver.c
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

#include <assert.h>
#include <errno.h>
#include <nuttx/atomic.h>
#include <nuttx/note/note_driver.h>
#include <nuttx/panic_notifier.h>
#include <nuttx/ring.h>
#include <nuttx/sched_note.h>
#include <nuttx/spinlock.h>

#include "note_driver.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct notesnap_s
{
  struct note_driver_s  driver;
  FAR struct ring_s   * ring;
  spinlock_t            lock;
  struct notifier_block nb;
  atomic_t              dumping;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static ssize_t notesnap_add(FAR struct note_driver_s *drv,
                            FAR const void *note, size_t notelen,
                            bool noswitches);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static RING_DEFINE_BMP(g_notesnap_buffer, CONFIG_DRIVERS_NOTESNAP_BUFSIZE);
#define g_notesnap_buffer this_cpu_var_bmp(g_notesnap_buffer)

static const struct note_driver_ops_s g_notesnap_ops =
{
  .add = notesnap_add,
};

/****************************************************************************
 * Public Data
 ****************************************************************************/

DEFINE_PER_CPU_BMP(struct notesnap_s, g_notesnap) =
{
  {
#ifdef CONFIG_SCHED_INSTRUMENTATION_FILTER
    "snap",
    {
      {
        CONFIG_DRIVERS_NOTESNAP_FILTER_DEFAULT_MODE,
#  ifndef CONFIG_UP
        CONFIG_DRIVERS_NOTESNAP_CPUSET
#  endif
      },

#  ifdef CONFIG_SCHED_INSTRUMENTATION_DUMP
      {
        CONFIG_DRIVERS_NOTE_DEFAULT_LEVEL,
      },
#  endif
    },
#endif
    &g_notesnap_ops
  },
  RING_BASE(&g_notesnap_buffer),
  SP_UNLOCKED,
};
#define g_notesnap this_cpu_var_bmp(g_notesnap)

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: notesnap_add
 *
 * Description:
 *   Store a raw binary note into the per-CPU ring buffer.  When the ring
 *   is full, the oldest whole note is dropped to make room (notebuf model),
 *   so the ring always holds the most recent notes.
 *
 ****************************************************************************/

static ssize_t notesnap_add(FAR struct note_driver_s *drv,
                            FAR const void *note, size_t notelen,
                            bool noswitches)
{
  FAR struct notesnap_s *snap = (FAR struct notesnap_s *)drv;
  irqstate_t flags;
  uint8_t skip;

  if (atomic_read(&snap->dumping))
    {
      return -EBUSY;
    }

  flags = spin_lock_irqsave_notrace(&snap->lock);

  /* Drop oldest whole notes until there is room for the new one. The first
   * byte of each note is its total length, so dequeue that byte and skip
   * the rest of the record to stay record-aligned.
   */

  while (ring_space(snap->ring) < notelen)
    {
      if (ring_spsc_dequeue(snap->ring, &skip, 1) == 0)
        {
          break;
        }

      if (skip > 1)
        {
          ring_spsc_skip(snap->ring, skip - 1);
        }
    }

  /* Cast to uint8_t * so the ring macro derives an element size of 1. */

  ring_spsc_enqueue(snap->ring, (FAR uint8_t *)note, notelen);

  spin_unlock_irqrestore_notrace(&snap->lock, flags);
  return notelen;
}

#ifdef CONFIG_DRIVERS_NOTESNAP_CRASH_DUMP
static void notesnap_dump_with_stream(FAR struct lib_outstream_s *stream)
{
  FAR struct notesnap_s *snap = &g_notesnap;
  struct note_dump_context_s ctx;
  struct note_max_s note;
  uint32_t index = 0;
  uint8_t len;

  /* Stop recording while dumping */

  atomic_set(&snap->dumping, true);

  note_dump_context_init(&ctx);

  /* Walk every note from the oldest (cons_tail) to the newest without
   * advancing the ring, so the snapshot survives the dump.
   */

  while (ring_spsc_peek(snap->ring, &len, 1, &index) != 0)
    {
      /* Peek the length byte that prefixes the next note. */

      DEBUGASSERT(len > 0);

      /* Peek the whole note (the peek copies across any wrap-around). */

      index -= 1;
      DEBUGVERIFY(ring_spsc_peek(snap->ring, (FAR uint8_t *)&note,
                                 len, &index) == len);

      note_dump_one(&note, stream, &ctx);
    }

  atomic_set(&snap->dumping, false);
}

static int notesnap_notifier(FAR struct notifier_block *nb,
                             unsigned long action, FAR void *data)
{
  if (action == PANIC_KERNEL)
    {
      struct lib_syslograwstream_s stream;

      lib_syslograwstream_open(&stream);
      notesnap_dump_with_stream(&stream.common);
      lib_syslograwstream_close(&stream);
    }

  return 0;
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: notesnap_notify_register
 ****************************************************************************/

#ifdef CONFIG_DRIVERS_NOTESNAP_CRASH_DUMP
void notesnap_notify_register(void)
{
  g_notesnap.nb.notifier_call = notesnap_notifier;
  panic_notifier_chain_register(&g_notesnap.nb);
}
#endif
