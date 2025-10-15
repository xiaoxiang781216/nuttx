/****************************************************************************
 * drivers/can/vcan.c
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

#include <debug.h>
#include <errno.h>
#include <stdio.h>

#include <nuttx/kmalloc.h>

#include <nuttx/net/can.h>
#include <nuttx/net/netdev_lowerhalf.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct net_vcan_dev_s
{
  struct netdev_lowerhalf_s   dev;          /* Lower half device structure */
  bool                        loopback;     /* Loopback mode status */
                                            /* Packets which received
                                             * but not yet read by upper layer
                                             */
  FAR netpkt_t               *rx_pkt[CONFIG_CAN_VCAN_NPKTS];
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int vcan_transmit(FAR struct netdev_lowerhalf_s *dev,
                         FAR netpkt_t *pkt);
static FAR netpkt_t *vcan_receive(FAR struct netdev_lowerhalf_s *dev);
static int vcan_ifup(FAR struct netdev_lowerhalf_s *dev);
static int vcan_ifdown(FAR struct netdev_lowerhalf_s *dev);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct netdev_ops_s g_vcan_ops =
{
  .transmit = vcan_transmit,
  .receive  = vcan_receive,
  .ifup     = vcan_ifup,
  .ifdown   = vcan_ifdown,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: vcan_receive
 ****************************************************************************/

static FAR netpkt_t *vcan_receive(FAR struct netdev_lowerhalf_s *dev)
{
  FAR struct net_vcan_dev_s *priv = (FAR struct net_vcan_dev_s *)dev;
  FAR netpkt_t *pkt = NULL;
  int i;

  /* Get the first packet from the RX buffer */

  for (i = 0; i < CONFIG_CAN_VCAN_NPKTS; i++)
    {
      if (priv->rx_pkt[i] != NULL)
        {
          pkt = priv->rx_pkt[i];
          priv->rx_pkt[i] = NULL;
          break;
        }
    }

  if (pkt == NULL)
    {
      nwarn("Allocate RX MB buffer failed\n");
      return NULL;
    }

  atomic_fetch_sub(&dev->quota_ptr[NETPKT_RX], 1);
  atomic_fetch_add(&dev->quota_ptr[NETPKT_TX], 1);
  netdev_lower_txdone(dev);

  return pkt;
}

/****************************************************************************
 * Name: vcan_transmit
 ****************************************************************************/

static int vcan_transmit(FAR struct netdev_lowerhalf_s *dev,
                         FAR netpkt_t *pkt)
{
  FAR struct net_vcan_dev_s *priv = (FAR struct net_vcan_dev_s *)dev;
  int i;

  /* Put the packet to RX buffer */

  for (i = 0; i < CONFIG_CAN_VCAN_NPKTS; i++)
    {
      priv->rx_pkt[i] = pkt;
      break;
    }

  if (i >= CONFIG_CAN_VCAN_NPKTS)
    {
      nwarn("No free RX MB buffer\n");
      return -ENOMEM;
    }

  /* Notify upper layer that RX data is ready */

  netdev_lower_rxready(dev);

  return OK;
}

/****************************************************************************
 * Name: vcan_ifup
 ****************************************************************************/

static int vcan_ifup(FAR struct netdev_lowerhalf_s *dev)
{
  netdev_lower_carrier_on(dev);
  return OK;
}

/****************************************************************************
 * Name: vcan_ifdown
 ****************************************************************************/

static int vcan_ifdown(FAR struct netdev_lowerhalf_s *dev)
{
  FAR struct net_vcan_dev_s *priv = (FAR struct net_vcan_dev_s *)dev;
  int i;

  /* Free all RX packets in the buffer */

  for (i = 0; i < CONFIG_CAN_VCAN_NPKTS; i++)
    {
      if (priv->rx_pkt[i] != NULL)
        {
          netpkt_free(dev, priv->rx_pkt[i], NETPKT_TX);
          priv->rx_pkt[i] = NULL;
        }
    }

  netdev_lower_carrier_off(dev);
  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: vcan_initialize
 *
 * Description:
 *   Create a new virtual CAN device and register it.
 *
 * Returned Value:
 *   OK on success; Negated errno on failure.
 *
 ****************************************************************************/

int vcan_initialize(void)
{
  FAR struct net_vcan_dev_s *priv;
  int ret;
  int i;

  for (i = 0; i < CONFIG_CAN_VCAN_NDEVICES; i++)
    {
      /* Allocate memory for the virtual CAN device structure */

      priv = kmm_zalloc(sizeof(struct net_vcan_dev_s));
      if (!priv)
        {
          return -ENOMEM;
        }

      snprintf(priv->dev.netdev.d_ifname, IFNAMSIZ, "vcan%d", i);

      /* Assign device operations */

      priv->dev.ops = &g_vcan_ops;

#ifdef CONFIG_CAN_VCAN_RXDIRECT
      priv->dev.rxtype = NETDEV_RX_DIRECT;
#else
      priv->dev.rxtype = NETDEV_RX_THREAD;
      priv->dev.priority = CONFIG_CAN_VCAN_RXTHREAD_PRIORITY;
#endif

      priv->dev.quota[NETPKT_TX] = CONFIG_CAN_VCAN_NPKTS;
      priv->dev.quota[NETPKT_RX] = 1;

      /* register vcanX */

      ret = netdev_lower_register(&priv->dev, NET_LL_CAN);

      if (ret < 0)
        {
          kmm_free(priv);
          canerr("ERROR: Register CAN interfacefailed: %d\n", ret);
          return ret;
        }

      IFF_SET_LOOPBACK(priv->dev.netdev.d_flags);
    }

  return OK;
}
