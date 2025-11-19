/****************************************************************************
 * drivers/syslog/syslog_netdev.c
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

#include <nuttx/net/netpoll.h>
#include <nuttx/spinlock.h>
#include <nuttx/syslog/syslog_netdev.h>
#include <nuttx/wqueue.h>

/****************************************************************************
 * Pre-processor definitions
 ****************************************************************************/

#define SYSLOG_NETDEV_WORK_DELAY MSEC2TICK(CONFIG_SYSLOG_NETDEV_WORK_DELAY)

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct syslog_netdev_s
{
  spinlock_t       lock;    /* Lock */
  FAR netpkt_t    *pkt;     /* Allocated net packet */
  size_t           next;    /* The next index (where data is added) */
  struct work_s    work;    /* Used for deferred callback work */
  struct netpoll_s np;      /* Netpoll */
};

/****************************************************************************
 * Private Functions Prototypes
 ****************************************************************************/

static bool syslog_netdev_transfer(FAR struct syslog_netdev_s *priv);
static void syslog_netdev_work(FAR void *arg);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct syslog_netdev_s g_syslog_netdev =
{
  .lock = SP_UNLOCKED
};

/****************************************************************************
 * Private Types
 ****************************************************************************/

/****************************************************************************
 * Name: syslog_netdev_transfer
 ****************************************************************************/

static bool syslog_netdev_transfer(FAR struct syslog_netdev_s *priv)
{
  irqstate_t flags;
  int        ret;

  flags = spin_lock_irqsave(&priv->lock);

  /* Len can be zero if flush with no data in buffer */

  if (priv->next == 0)
    {
      spin_unlock_irqrestore(&priv->lock, flags);
      return true;
    }

  /* Send IOB */

  ret = netpoll_udp_pkt_send(&priv->np, priv->pkt);
  if (ret < 0)
    {
      spin_unlock_irqrestore(&priv->lock, flags);
      return false;
    }

  /* Get IOB - blocking */

  priv->pkt = netpoll_udp_pkt_alloc(&priv->np);
  priv->next = 0;

  spin_unlock_irqrestore(&priv->lock, flags);

  return true;
}

/****************************************************************************
 * Name: syslog_netdev_work
 ****************************************************************************/

static void syslog_netdev_work(FAR void *arg)
{
  FAR struct syslog_netdev_s *priv = arg;

  if (!syslog_netdev_transfer(priv))
    {
      work_queue(HPWORK, &priv->work, syslog_netdev_work, priv,
                 SYSLOG_NETDEV_WORK_DELAY);
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: syslog_netdev_putc
 ****************************************************************************/

int syslog_netdev_putc(FAR syslog_channel_t *channel, int ch)
{
  char tmp = ch;

  syslog_netdev_write(channel, &tmp, 1);
  return ch;
}

/****************************************************************************
 * Name: syslog_netdev_flush
 ****************************************************************************/

int syslog_netdev_flush(FAR syslog_channel_t *channel)
{
  FAR struct syslog_netdev_s *priv = &g_syslog_netdev;

  syslog_netdev_transfer(priv);

  return OK;
}

/****************************************************************************
 * Name: syslog_netdev_write
 ****************************************************************************/

ssize_t syslog_netdev_write(FAR syslog_channel_t *channel,
                            FAR const char *buffer, size_t buflen)
{
  FAR struct syslog_netdev_s *priv   = &g_syslog_netdev;
  clock_t                     delay  = SYSLOG_NETDEV_WORK_DELAY;
  size_t                      maxlen = netpoll_get_maxlen(&priv->np);
  size_t                      space;
  irqstate_t                  flags;

  flags = spin_lock_irqsave(&priv->lock);

  /* Ignore data if device is not ready */

  if (!priv->pkt || buflen <= 0)
    {
      spin_unlock_irqrestore(&priv->lock, flags);
      return 0;
    }

try:
  if (priv->next + buflen > maxlen)
    {
      /* Unlock before entering syslog_netdev_transfer,
       * it is already locked internally
       */

      spin_unlock_irqrestore(&priv->lock, flags);
      syslog_netdev_transfer(priv);
      flags = spin_lock_irqsave(&priv->lock);
      goto try;
    }

  /* Copy data to packet */

  netpoll_pkt_copy(&priv->np, priv->pkt, buffer, buflen, priv->next);
  priv->next += buflen;

  /* Start work immediately when data more then 75% */

  space = maxlen - priv->next;
  if (space < maxlen / 4)
    {
      delay = 0;
    }

  spin_unlock_irqrestore(&priv->lock, flags);

  if (work_available(&priv->work))
    {
      work_queue(HPWORK, &priv->work, syslog_netdev_work, priv, delay);
    }

  return buflen;
}

/****************************************************************************
 * Name: syslog_netdev_init
 *
 * Description:
 *   Register a NETDEV syslog channel
 *
 * Input Parameters:
 *   dev - The lower half device driver structure to be registered.
 *
 * Returned Value:
 *   Zero on success. A negated errno value is returned on a failure.
 *
 ****************************************************************************/

int syslog_netdev_init(FAR struct netdev_lowerhalf_s *dev)
{
  FAR struct syslog_netdev_s *priv = &g_syslog_netdev;
  irqstate_t                  flags;
  int                         ret;

  flags = spin_lock_irqsave(&priv->lock);

  /* Initialize netpoll */

  ret = netpoll_udp_init(&priv->np, dev,
                         CONFIG_SYSLOG_NETDEV_PORT,
                         CONFIG_SYSLOG_NETDEV_IPADDR,
                         CONFIG_SYSLOG_NETDEV_PORT,
                         CONFIG_SYSLOG_NETDEV_MAC);
  if (ret < 0)
    {
      goto errout;
    }

  /* Allocate packet */

  priv->pkt = netpoll_udp_pkt_alloc(&priv->np);

errout:
  spin_unlock_irqrestore(&priv->lock, flags);
  return ret;
}
