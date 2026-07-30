/****************************************************************************
 * libs/libc/stream/lib_netpolloutstream.c
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
#include <sys/param.h>

#include <nuttx/net/net.h>
#include <nuttx/nuttx.h>
#include <nuttx/kmalloc.h>
#include <nuttx/net/netpoll.h>
#include <nuttx/streams.h>
#ifdef CONFIG_NET_CAN
#  include <nuttx/can/isotp.h>
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: netpolloutstream_bind
 ****************************************************************************/

static int netpolloutstream_bind(FAR struct lib_netpolloutstream_s *stream)
{
  FAR struct net_driver_s *netdev;
  FAR struct netdev_lowerhalf_s *dev;

  if (stream->np == NULL)
    {
      return -ENOMEM;
    }

  netdev = netdev_findbyname(stream->intf);
  if (netdev == NULL)
    {
      return -ENODEV;
    }

  dev = container_of(netdev, struct netdev_lowerhalf_s, netdev);

#ifdef CONFIG_NET_CAN
  if (netdev->d_lltype == NET_LL_CAN)
    {
      return netpoll_isotp_init(stream->np, dev, stream->port,
                                stream->port + 1);
    }
  else
#endif
    {
#ifdef CONFIG_NET_UDP
      return netpoll_udp_init(stream->np, dev, stream->port, stream->ipaddr,
                              stream->port, stream->mac);
#endif
    }

  return -EINVAL;
}

/****************************************************************************
 * Name: netpolloutstream_maxsend
 ****************************************************************************/

static size_t
netpolloutstream_maxsend(FAR const struct lib_netpolloutstream_s *stream)
{
#ifdef CONFIG_NET_CAN
  if (stream->np->lower->netdev.d_lltype == NET_LL_CAN)
    {
      return ISOTP_MAX_MSG_LEN;
    }
#endif

  return netpoll_get_maxlen(stream->np);
}

/****************************************************************************
 * Name: netpolloutstream_send
 ****************************************************************************/

static ssize_t
netpolloutstream_send(FAR struct lib_netpolloutstream_s *stream,
                      FAR const void *buf, size_t len)
{
  FAR struct netpoll_s *np = stream->np;
  size_t chunk;
  int ret = -ENODEV;

  if (np == NULL || np->lower == NULL)
    {
      return -ENODEV;
    }

  chunk = netpolloutstream_maxsend(stream);
  if (chunk == 0)
    {
      return -EMSGSIZE;
    }

  chunk = MIN(len, chunk);

#ifdef CONFIG_NET_CAN
  if (np->lower->netdev.d_lltype == NET_LL_CAN)
    {
      ret = netpoll_isotp_send(np, buf, chunk);
    }
  else
#endif
    {
#ifdef CONFIG_NET_UDP
      ret = netpoll_udp_send(np, buf, chunk);
#endif
    }

  if (ret < 0)
    {
      return ret;
    }

  if (np->lower->ops->poll != NULL)
    {
      np->lower->ops->poll(np->lower, NETPKT_TX);
    }

  return chunk;
}

/****************************************************************************
 * Name: netpolloutstream_flush
 ****************************************************************************/

static int netpolloutstream_flush(FAR struct lib_outstream_s *self)
{
  UNUSED(self);
  return OK;
}

/****************************************************************************
 * Name: netpolloutstream_puts
 ****************************************************************************/

static ssize_t netpolloutstream_puts(FAR struct lib_outstream_s *self,
                                     FAR const void *buf, size_t len)
{
  FAR struct lib_netpolloutstream_s *stream =
    (FAR struct lib_netpolloutstream_s *)self;
  FAR const uint8_t *src = buf;
  size_t total = len;
  ssize_t ret;

  /* Defer the netdev bind to the first puts().  lib_netpolloutstream_init()
   * may run at boot before the netpoll interface (e.g. eth0.65 VLAN) has
   * been created, so binding there would fail.  By the time the first
   * puts() happens on the crash path, the interface is up.
   */

  if (stream->np != NULL && stream->np->lower == NULL)
    {
      ret = netpolloutstream_bind(stream);
      if (ret < 0)
        {
          return ret;
        }
    }

  while (len > 0)
    {
      ret = netpolloutstream_send(stream, src, len);
      if (ret < 0)
        {
          return ret;
        }

      self->nput += ret;
      src        += ret;
      len        -= ret;
    }

  return total;
}

/****************************************************************************
 * Name: netpolloutstream_putc
 ****************************************************************************/

static void netpolloutstream_putc(FAR struct lib_outstream_s *self, int ch)
{
  char tmp = ch;

  netpolloutstream_puts(self, &tmp, 1);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: lib_netpolloutstream_init
 ****************************************************************************/

int lib_netpolloutstream_init(FAR struct lib_netpolloutstream_s *stream,
                              FAR const char *intf, uint16_t port,
                              in_addr_t ipaddr, FAR const char *mac)
{
  stream->common.nput  = 0;
  stream->common.putc  = netpolloutstream_putc;
  stream->common.puts  = netpolloutstream_puts;
  stream->common.flush = netpolloutstream_flush;
  stream->common.none  = NULL;
  stream->np           = NULL;

  /* Keep the bind parameters for the deferred bind in the first puts().
   * coredump_initialize() runs early at boot, before the netpoll interface
   * (e.g. the eth0.65 VLAN) is created, so binding here would fail.  By the
   * time the first puts() happens (on the crash path), the interface exists.
   */

  stream->intf   = intf;
  stream->port   = port;
  stream->ipaddr = ipaddr;
  stream->mac    = mac;

  /* Allocate netpoll state instead of embedding it in streams.h.  Embedding
   * would require streams.h to include nuttx/net/netpoll.h, which pulls in
   * netdev lower-half headers through a generic libc header.
   */

  stream->np = kmm_zalloc(sizeof(*stream->np));
  if (stream->np == NULL)
    {
      return -ENOMEM;
    }

  return OK;
}
