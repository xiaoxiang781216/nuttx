/****************************************************************************
 * drivers/rpmsg/rpmsg_note.c
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
#include <debug.h>
#include <execinfo.h>

#include <nuttx/sched_note.h>
#include <nuttx/spinlock.h>
#include <nuttx/note/note_driver.h>
#include <nuttx/note/noteram_driver.h>
#include <nuttx/rpmsg/rpmsg.h>

#include "rpmsg_procfs.h"

/****************************************************************************
 * Private data
 ****************************************************************************/

static FAR struct note_driver_s *g_rpmsg_note_drv;
static spinlock_t g_rpmsg_note_lock = SP_UNLOCKED;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: rpmsg_note_trace
 ****************************************************************************/

void rpmsg_note_trace(FAR const char *name, bool bt, FAR const void *buf,
                      size_t len, FAR const char *format, ...)
{
  va_list ap;

  va_start(ap, format);
  rpmsg_note_vtrace(name, bt, buf, len, format, ap);
  va_end(ap);
}

/****************************************************************************
 * Name: rpmsg_note_vtrace
 ****************************************************************************/

void rpmsg_note_vtrace(FAR const char *name, bool bt, FAR const void *buf,
                       size_t len, FAR const char *format, va_list ap)
{
  irqstate_t flags;

  if (rpmsg_procfs_note_allow(name))
    {
#if defined(CONFIG_RPMSG_NOTE_BACKTRACE) && CONFIG_RPMSG_NOTE_BACKTRACE > 0
      if (bt)
        {
          FAR void *buffer[CONFIG_RPMSG_NOTE_BACKTRACE];
          char buf[BACKTRACE_BUFFER_SIZE(CONFIG_RPMSG_NOTE_BACKTRACE)];
          struct va_format vaf;
          vaf.fmt = format;
          vaf.va = &ap;

          backtrace(buffer, CONFIG_RPMSG_NOTE_BACKTRACE);
          backtrace_format(buf, sizeof(buf), buffer,
                          CONFIG_RPMSG_NOTE_BACKTRACE);
          flags = spin_lock_irqsave(&g_rpmsg_note_lock);
          note_driver_printf(g_rpmsg_note_drv, NOTE_TAG_RPMSG,
                            "%pV, %s", &vaf, buf);
        }
      else
#endif
        {
          flags = spin_lock_irqsave(&g_rpmsg_note_lock);
          note_driver_vprintf(g_rpmsg_note_drv, NOTE_TAG_RPMSG, format, &ap);
        }

      while (len > 0)
        {
          size_t written = note_driver_binary(g_rpmsg_note_drv,
                                              NOTE_TAG_RPMSG, buf, len);
          buf += written;
          len -= written;
        }

      spin_unlock_irqrestore(&g_rpmsg_note_lock, flags);
    }
}

/****************************************************************************
 * Name: rpmsg_note_initialize
 ****************************************************************************/

int rpmsg_note_initialize(void)
{
  g_rpmsg_note_drv = noteram_initialize("/dev/note/rpmsg",
                                        CONFIG_RPMSG_NOTE_BUFSIZE,
                                        true, true, true);
  if (g_rpmsg_note_drv == NULL)
    {
      rpmsgerr("failed to initialize note driver.\n");
      return -ENOMEM;
    }

  return 0;
}
