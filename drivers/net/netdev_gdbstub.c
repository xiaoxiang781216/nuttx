/****************************************************************************
 * drivers/net/netdev_gdbstub.c
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

#include <nuttx/gdbstub.h>
#include <nuttx/net/netdev_gdbstub.h>
#include <nuttx/net/netpoll.h>

/****************************************************************************
 * Pre-processor definitions
 ****************************************************************************/

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct netdev_gdbstub_s
{
  FAR struct gdb_state_s *state;
  struct netpoll_s        np;

  /* Send data via netpoll */

  CODE ssize_t (*send)(FAR struct netpoll_s *np,
                       FAR const void *buf, size_t len);

  /* Receive data via netpoll */

  CODE ssize_t (*recv)(FAR struct netpoll_s *np,
                       FAR void *buf, size_t len);
};

/****************************************************************************
 * Private Functions prototypes
 ****************************************************************************/

static ssize_t netdev_gdbstub_recv(FAR void *priv, FAR void *buf,
                                   size_t len);
static ssize_t netdev_gdbstub_send(FAR void *priv, FAR const void *buf,
                                   size_t len);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct netdev_gdbstub_s g_netdev_gdbstub;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: netdev_gdbstub_recv
 *
 * Description:
 *   This is gdbstub receive char function.
 *
 ****************************************************************************/

static ssize_t netdev_gdbstub_recv(FAR void *priv, FAR void *buf, size_t len)
{
  FAR struct netdev_gdbstub_s *gdbstub = priv;

  return gdbstub->recv(&gdbstub->np, buf, len);
}

/****************************************************************************
 * Name: netdev_gdbstub_send
 *
 * Description:
 *   This is gdbstub send char function.
 *
 ****************************************************************************/

static ssize_t netdev_gdbstub_send(FAR void *priv, FAR const void *buf,
                                   size_t len)
{
  FAR struct netdev_gdbstub_s *gdbstub = priv;

  return gdbstub->send(&gdbstub->np, buf, len);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: netdev_gdbstub_init
 *
 * Description:
 *   Register a NETDEV gdbstub channel
 *
 * Input Parameters:
 *   dev - The lower half device driver structure to be registered.
 *
 * Returned Value:
 *   Zero on success. A negated errno value is returned on a failure.
 *
 ****************************************************************************/

int netdev_gdbstub_init(FAR struct netdev_lowerhalf_s *dev)
{
  FAR struct netdev_gdbstub_s *gdbstub = &g_netdev_gdbstub;
  struct gdb_state_init_s      init;
  size_t                       pkt_size = 0;
  int                          ret = -EINVAL;

  /* Initialize netpoll based on link-layer type */

#ifdef CONFIG_NET_CAN
  if (dev->netdev.d_lltype == NET_LL_CAN)
    {
      gdbstub->send = netpoll_isotp_send;
      gdbstub->recv = netpoll_isotp_recv;

      ret = netpoll_isotp_init(&gdbstub->np, dev,
                               CONFIG_GDBSTUB_NETDEV_PORT,
                               CONFIG_GDBSTUB_NETDEV_PORT + 1);

      /* ISO-TP reassembles multi-frame messages up to 4095 bytes.
       * Netpoll's per-frame maxlen (8 or 64) would truncate gdb packets.
       * 0 tells gdbstub to use CONFIG_LIBC_GDBSTUB_PKTSIZE.
       */
    }
  else
#endif
    {
#ifdef CONFIG_NET_UDP
      gdbstub->send = netpoll_udp_send;
      gdbstub->recv = netpoll_udp_recv;

      ret = netpoll_udp_init(&gdbstub->np, dev, CONFIG_GDBSTUB_NETDEV_PORT,
                             0, 0, NULL);

      /* UDP: one netpoll frame carries one gdb packet. */

      pkt_size = netpoll_get_maxlen(&gdbstub->np);
#endif
    }

  if (ret < 0)
    {
      return ret;
    }

  /* Initialize GDB handler */

  init.send          = netdev_gdbstub_send;
  init.recv          = netdev_gdbstub_recv;
  init.monitor       = NULL;
  init.packet        = true;
  init.pkt_size      = pkt_size;
#ifdef CONFIG_GDBSTUB_NETDEV_WAIT_CONNECT
  init.wait          = CONFIG_GDBSTUB_NETDEV_WAIT_CONNECT;
#else
  init.wait          = false;
#endif
  init.priv          = gdbstub;
  init.panic_timeout = CONFIG_GDBSTUB_NETDEV_PANIC_TIMEOUT;

  gdbstub->state = gdb_state_init(&init);
  if (gdbstub->state == NULL)
    {
      return -ENOMEM;
    }

  return OK;
}
