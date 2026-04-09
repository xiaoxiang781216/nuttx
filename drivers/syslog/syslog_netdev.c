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

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct syslog_netdev_s
{
  spinlock_t       lock;    /* Lock */
  netpkt_queue_t   pktq;    /* Allocated net packet queue */
  struct netpoll_s np;      /* Netpoll */
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct syslog_netdev_s g_syslog_netdev =
{
  .lock = SP_UNLOCKED
};

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
  irqstate_t                  flags;

  flags = spin_lock_irqsave(&priv->lock);

  if (!IOB_QEMPTY(&priv->pktq))
    {
      netpoll_udp_queue_send(&priv->np, &priv->pktq);
    }

  spin_unlock_irqrestore(&priv->lock, flags);

  return OK;
}

/****************************************************************************
 * Name: syslog_netdev_write
 ****************************************************************************/

ssize_t syslog_netdev_write(FAR syslog_channel_t *channel,
                            FAR const char *buffer, size_t buflen)
{
  FAR struct syslog_netdev_s *priv   = &g_syslog_netdev;
  size_t                      maxlen = netpoll_get_maxlen(&priv->np);
  irqstate_t                  flags;
  int                         ret;

  flags = spin_lock_irqsave(&priv->lock);

  /* Ignore data if device is not ready */

  if (priv->np.lower == NULL || buflen == 0)
    {
      spin_unlock_irqrestore(&priv->lock, flags);
      return buflen;
    }

  /* Copy the whole buffer; netpoll_queue_copyin grows the queue as needed,
   * splitting at maxlen boundaries (one queue entry per wire frame/packet).
   */

  ret = netpoll_queue_copyin(&priv->np, &priv->pktq, buffer, buflen);
  if (ret < 0)
    {
      /* On copy failure (e.g. IOB exhaustion), discard the batch so the
       * next write starts from a clean queue state.
       */

      iob_free_queue(&priv->pktq);
      spin_unlock_irqrestore(&priv->lock, flags);
      return ret;
    }

  /* Flush immediately on queue overflow or >75% packet fill */

  if (iob_get_queue_size(&priv->pktq) > 3 * maxlen / 4)
    {
      netpoll_udp_queue_send(&priv->np, &priv->pktq);
    }

  spin_unlock_irqrestore(&priv->lock, flags);
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

errout:
  spin_unlock_irqrestore(&priv->lock, flags);
  return ret;
}
