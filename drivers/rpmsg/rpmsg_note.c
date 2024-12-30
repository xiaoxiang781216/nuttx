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

#include <nuttx/sched_note.h>
#include <nuttx/note/note_driver.h>
#include <nuttx/note/noteram_driver.h>
#include <nuttx/rpmsg/rpmsg_note.h>

/****************************************************************************
 * Private data
 ****************************************************************************/

static FAR struct note_driver_s *g_rpmsg_note_drv;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: rpmsg_note_receive
 ****************************************************************************/

void rpmsg_note_receive(FAR const struct rpmsg_hdr *hdr,
                        FAR const struct rpmsg_endpoint *ept)
{
  DEBUGASSERT(hdr != NULL && ept != NULL);
  sched_note_printf(NOTE_TAG_RPMSG,
                    "Recv:src:%u dst:%u len:%d ept_cb:%p\n",
                    hdr->src, hdr->dst, hdr->len, ept->cb);
}

/****************************************************************************
 * Name: rpmsg_note_send
 ****************************************************************************/

void rpmsg_note_send(FAR struct rpmsg_device *rdev,
                     uint32_t src, uint32_t dst,
                     const void *data, int len)
{
  DEBUGASSERT(rdev != NULL);
  sched_note_printf(NOTE_TAG_RPMSG,
                    "Send:rdev:%p src:%" PRIu32 " dst:%" PRIu32 " len:%d\n",
                    rdev, src, dst, len);
}

/****************************************************************************
 * Name: rpmsg_note_initialize
 ****************************************************************************/

int rpmsg_note_initialize(void)
{
  g_rpmsg_note_drv = noteram_initialize("/dev/note/rpmsg",
                                        CONFIG_RPMSG_NOTE_BUFSIZE,
                                        true, true);
  if (g_rpmsg_note_drv == NULL)
    {
      rpmsgerr("failed to initialize note driver.\n");
      return -ENOMEM;
    }

  NOTE_FILTER_TAGMASK_ZERO(&g_rpmsg_note_drv->filter.tag_mask);
  NOTE_FILTER_TAGMASK_SET(NOTE_TAG_RPMSG,
                          &g_rpmsg_note_drv->filter.tag_mask);

  g_rpmsg_note_drv->filter.mode.flag = NOTE_FILTER_MODE_FLAG_ENABLE |
                                       NOTE_FILTER_MODE_FLAG_DUMP;

  return 0;
}
