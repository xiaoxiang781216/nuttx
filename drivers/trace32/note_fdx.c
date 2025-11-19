/****************************************************************************
 * drivers/trace32/note_fdx.c
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
#include <nuttx/note/note_driver.h>
#include <nuttx/spinlock.h>

#include "t32fdx/t32fdx.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct notefdx_s
{
  struct note_driver_s driver;
  FAR void *channel;
  bool enable;
  int loss;
};

struct notefdx_channel_s
{
  T32_FDX_BUFFER header;
  volatile T32_FDX_DATATYPE data[CONFIG_TRACE32_FDX_NOTE_BUFSIZE];
  spinlock_t lock;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static void notefdx_add(FAR struct note_driver_s *drv,
                        FAR const void *buf, size_t notelen, bool noswitche);

/****************************************************************************
 * Private Data
 ****************************************************************************/

#ifdef TRACE32_FDX_NOTE_SECTION
locate_data(CONFIG_TRACE32_FDX_NOTE_SECTION)
#endif
static struct notefdx_channel_s g_fdx_note_channel;

static const struct note_driver_ops_s g_notefdx_ops =
{
  notefdx_add,
};

struct notefdx_s g_notefdx =
{
  {
#ifdef CONFIG_SCHED_INSTRUMENTATION_FILTER
    "fdx",
    {
      {
        CONFIG_TRACE32_FDX_NOTE_FILTER_DEFAULT_MODE,
#  ifdef CONFIG_SMP
        CONFIG_TRACE32_FDX_NOTE_CPUSET
#  endif
      },
    },
#endif
    &g_notefdx_ops
  },
  NULL,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void
notefdx_taskname_handler(FAR const void *note, size_t len, FAR void *arg)
{
  FAR struct notefdx_channel_s *channel = arg;
  T32_Fdx_Send(channel, (FAR void *)note, len);
}

/****************************************************************************
 * Name: notefdx_add
 *
 * Description:
 *   Put the variable length note to fdx
 *
 * Input Parameters:
 *   buf    - The note buffer
 *   notelen - The buffer length
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void notefdx_add(FAR struct note_driver_s *drv,
                        FAR const void *buf, size_t notelen, bool noswitche)
{
  FAR struct notefdx_s *note = (FAR struct notefdx_s *)drv;
  FAR struct notefdx_channel_s *channel = note->channel;
  irqstate_t flags;

  if (channel == NULL || !T32_Fdx_Is_Enable(channel))
    {
      note->enable = false;
      return;
    }

  flags = spin_lock_irqsave(&channel->lock);

  /* first connect */

  if (!note->enable)
    {
      note->enable = true;
      sched_note_taskname(notefdx_taskname_handler, channel);
    }

  T32_Fdx_SendPoll(channel, (FAR void *)buf, notelen);
  spin_unlock_irqrestore(&channel->lock, flags);
}

/****************************************************************************
 * Name: notefdx_register
 *
 * Description:
 *   Register a trace32 fdx driver using note_driver_register
 *
 * Input Parameters:
 *   None.
 *
 * Returned Value:
 *   Zero on succress. A negated errno value is returned on a failure.
 *
 ****************************************************************************/

int notefdx_register(void)
{
  T32_Fdx_InitChannel(g_fdx_note_channel);
  T32_Fdx_EnableChannel(g_fdx_note_channel);
  g_notefdx.channel = &g_fdx_note_channel;
  return note_driver_register(&g_notefdx.driver);
}

/****************************************************************************
 * Name: T32_Fdx_GetTimebase
 *
 * Description:
 *   Get the timebase for the FDX driver
 *
 * Returned Value:
 *   The timebase for the FDX driver
 *
 * Note:
 *   This function is used to get the timebase for the FDX driver.
 *   It is called by t32fdx.c
 *
 ****************************************************************************/

unsigned long T32_Fdx_GetTimebase(void)
{
  return perf_gettime();
}
