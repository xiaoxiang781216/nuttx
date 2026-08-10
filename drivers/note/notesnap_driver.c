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
#include <string.h>
#include <sys/param.h>
#include <nuttx/kmalloc.h>
#include <nuttx/nuttx.h>
#include <nuttx/note/note_driver.h>
#include <nuttx/note/notesnap_driver.h>
#include <nuttx/panic_notifier.h>
#include <nuttx/ring.h>
#include <nuttx/sched_note.h>
#include <nuttx/syslog/syslog.h>

#include "note_driver.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct notesnap_s
{
  struct note_driver_s  driver;
  FAR struct ring_s   * ring;
  struct notifier_block nb;
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
  &g_notesnap_buffer.base,
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

  /* ring_mpmc_overwrite is CAS-based and reentrancy-safe, so no lock is
   * needed even if an interrupt preempts a task-context add on the same
   * CPU.  Cast to uint8_t * so the ring macro derives an element size of
   * one; the dump path resyncs past any fragment a wrap may leave behind.
   */

  ring_mpmc_overwrite(snap->ring, (FAR const uint8_t *)note, notelen);
  return MIN(notelen, ring_capacity(snap->ring));
}

#ifdef CONFIG_DRIVERS_NOTESNAP_CRASH_DUMP
static int notesnap_notifier(FAR struct notifier_block *nb,
                             unsigned long action, FAR void *data)
{
  FAR struct notesnap_s *snap = container_of(nb, struct notesnap_s, nb);
  struct note_dump_stream_s stream;
  char buf[NOTE_BUFFER_SIZE];
  ssize_t n;

  if (action != PANIC_KERNEL)
    {
      return 0;
    }

  /* Print the formatted notes from the non-consuming dump stream to
   * syslog.  The overwrite ring keeps moving under the reader, so the dump
   * stream resyncs past any fragment a wrap leaves behind; each gets()
   * fills the buffer with as many whole notes as fit.
   */

  note_dump_stream(&stream, snap->ring, false);

  while ((n = lib_stream_gets(&stream, buf, sizeof(buf))) > 0)
    {
      syslog_write(buf, n);
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

/****************************************************************************
 * Name: notesnap_initialize
 *
 * Description:
 *   Dynamically create a notesnap instance over a ring buffer allocated
 *   from the kernel heap.  The instance keeps the most recent raw binary
 *   notes (dump-only, like the static per-CPU snapshot).
 *
 *   The instance is intentionally NOT registered into the global note
 *   dispatch array: it is a private sink fed directly by its owner via the
 *   note_driver_*() helpers and dumped from its own ring, so registering it
 *   would only leak unrelated global trace traffic into this buffer.
 *
 * Input Parameters:
 *   devname: The name of the notesnap device (must not be NULL)
 *   bufsize: The size in bytes of the circular note buffer
 *
 * Returned Value:
 *   The note driver handle on success; NULL on failure.
 *
 ****************************************************************************/

FAR struct note_driver_s *
notesnap_initialize(FAR const char *devname, size_t bufsize)
{
  FAR struct notesnap_s *snap;
  size_t len;

  if (devname == NULL || bufsize == 0)
    {
      return NULL;
    }

  len = strlen(devname) + 1;
  snap = kmm_zalloc(sizeof(*snap) + len);
  if (snap == NULL)
    {
      return NULL;
    }

  memcpy(snap + 1, devname, len);
  snap->ring = ring_alloc(bufsize, 1);
  if (snap->ring == NULL)
    {
      kmm_free(snap);
      return NULL;
    }

#ifdef CONFIG_SCHED_INSTRUMENTATION_FILTER
  snap->driver.name = (FAR const char *)(snap + 1);
  snap->driver.filter.mode.type_mask =
    CONFIG_DRIVERS_NOTESNAP_FILTER_DEFAULT_MODE;
#  ifndef CONFIG_UP
  snap->driver.filter.mode.cpuset = CONFIG_DRIVERS_NOTESNAP_CPUSET;
#  endif
#  ifdef CONFIG_SCHED_INSTRUMENTATION_DUMP
  snap->driver.filter.level.level = CONFIG_DRIVERS_NOTE_DEFAULT_LEVEL;
#  endif
#endif

  snap->driver.ops = &g_notesnap_ops;

#ifdef CONFIG_DRIVERS_NOTESNAP_CRASH_DUMP
  snap->nb.notifier_call = notesnap_notifier;
  panic_notifier_chain_register(&snap->nb);
#endif

  return &snap->driver;
}

/****************************************************************************
 * Name: notesnap_deinitialize
 *
 * Description:
 *   Free a notesnap instance created by notesnap_initialize().
 *
 * Input Parameters:
 *   drv: The note driver handle returned by notesnap_initialize()
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

void notesnap_deinitialize(FAR struct note_driver_s *drv)
{
  FAR struct notesnap_s *snap;

  if (drv == NULL)
    {
      return;
    }

  snap = container_of(drv, struct notesnap_s, driver);

#ifdef CONFIG_DRIVERS_NOTESNAP_CRASH_DUMP
  panic_notifier_chain_unregister(&snap->nb);
#endif

  ring_free(snap->ring);
  kmm_free(snap);
}
