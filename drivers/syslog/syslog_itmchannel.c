/****************************************************************************
 * drivers/syslog/syslog_itmchannel.c
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

#include <nuttx/coresight/coresight_itm.h>
#include <nuttx/syslog/syslog.h>

#include "syslog.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Private Functions Prototypes
 ****************************************************************************/

static int syslog_itm_putc(FAR struct syslog_channel_s *channel, int ch);
static ssize_t syslog_itm_write(FAR struct syslog_channel_s *channel,
                                FAR const char *buffer, size_t buflen);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static FAR struct itm_port_s *g_syslog_itm_port;

static const struct syslog_channel_ops_s g_itm_channel_ops =
{
  syslog_itm_putc,
  syslog_itm_putc,
  NULL,
  syslog_itm_write,
  syslog_itm_write
};

static struct syslog_channel_s g_itm_channel =
{
  &g_itm_channel_ops
#  ifdef CONFIG_SYSLOG_IOCTL
  , "itm"
  , false
#  endif
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int syslog_itm_putc(FAR struct syslog_channel_s *channel, int ch)
{
  return itm_send(g_syslog_itm_port, &ch, 1);
}

ssize_t syslog_itm_write(FAR struct syslog_channel_s *channel,
                         FAR const char *buffer, size_t buflen)
{
  return itm_send(g_syslog_itm_port, buffer, buflen);
}

int syslog_itm_init(void)
{
  int ret;

  g_syslog_itm_port = itm_alloc_port();
  if (g_syslog_itm_port == NULL)
    {
      return -ENODEV;
    }

  ret = syslog_channel(&g_itm_channel);
  if (ret < 0)
    {
      return ret;
    }

  return 0;
}
