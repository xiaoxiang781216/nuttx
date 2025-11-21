/****************************************************************************
 * drivers/trace32/syslog_fdx.c
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

#include <errno.h>
#include <nuttx/spinlock.h>
#include <nuttx/syslog/syslog.h>

#include "t32fdx/t32fdx.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct syslogfdx_channel_s
{
  T32_FDX_BUFFER header;
  T32_FDX_DATATYPE data[CONFIG_SYSLOG_FDX_BUFSIZE];
  spinlock_t lock;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct syslogfdx_channel_s g_fdx_syslog_channel =
{
  .header.transferchannel = 0x01,
  .header.size = CONFIG_SYSLOG_FDX_BUFSIZE,
  .lock = SP_UNLOCKED,
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int syslog_fdx_putc(FAR syslog_channel_t *channel, int ch)
{
  irqstate_t flags;
  char c = ch;

  if (!T32_Fdx_Is_Enable(&g_fdx_syslog_channel))
    {
      return -EIO;
    }

  flags = spin_lock_irqsave(&g_fdx_syslog_channel.lock);
  T32_Fdx_Send(&g_fdx_syslog_channel, &c, 1);
  spin_unlock_irqrestore(&g_fdx_syslog_channel.lock, flags);
  return ch;
}

ssize_t syslog_fdx_write(FAR syslog_channel_t *channel,
                         FAR const char *buffer, size_t buflen)
{
  irqstate_t flags;
  int ret;

  if (!T32_Fdx_Is_Enable(&g_fdx_syslog_channel))
    {
      return -EIO;
    }

  flags = spin_lock_irqsave(&g_fdx_syslog_channel.lock);
  ret = T32_Fdx_Send(&g_fdx_syslog_channel, (FAR void *)buffer, buflen);
  spin_unlock_irqrestore(&g_fdx_syslog_channel.lock, flags);
  return ret > 0 ? ret : -EIO;
}
