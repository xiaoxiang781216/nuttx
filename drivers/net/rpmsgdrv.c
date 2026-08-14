/****************************************************************************
 * drivers/net/rpmsgdrv.c
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
#include <errno.h>
#include <netpacket/packet.h>
#include <poll.h>
#include <stdio.h>
#include <sys/socket.h>

#include <nuttx/kmalloc.h>
#include <nuttx/net/netdev.h>
#include <nuttx/semaphore.h>
#include <nuttx/spinlock.h>
#include <nuttx/wqueue.h>

#include <nuttx/can.h>
#include <nuttx/net/can.h>
#include <nuttx/net/dns.h>
#include <nuttx/net/ioctl.h>
#include <nuttx/net/ip.h>
#include <nuttx/net/netdev_lowerhalf.h>
#include <nuttx/net/net.h>
#include <nuttx/net/pkt.h>
#include <nuttx/net/rpmsg.h>
#include <nuttx/net/rpmsgdrv.h>
#include <nuttx/rpmsg/rpmsg.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define NET_RPMSG_DRV_BUFSIZE (CONFIG_NET_ETH_PKTSIZE + CONFIG_NET_GUARDSIZE)
#define NET_RPMSG_DRV_MAX_PKT_SIZE \
    ((CONFIG_NET_LL_GUARDSIZE - ETH_HDRLEN) + NET_RPMSG_DRV_BUFSIZE)
#define NET_RPMSG_DRV_MAX_NIOB \
    ((NET_RPMSG_DRV_MAX_PKT_SIZE + CONFIG_IOB_BUFSIZE - 1) / \
     CONFIG_IOB_BUFSIZE)

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct net_rpmsg_cookie_s
{
  FAR struct net_rpmsg_header_s *header;
  sem_t                         sem;
};

/* net_rpmsg_base_s is the rpmsg link state shared by the netdev role and
 * the bridge role
 */

struct net_rpmsg_base_s
{
  char                  cpuname[RPMSG_NAME_SIZE];
  char                  devname[IFNAMSIZ];
  struct work_s         txwork;  /* Deferred TX re-arm work */
  rpmsg_ept_cb          eptcb;   /* Role endpoint receive callback */
  sem_t                 wait;    /* Wait sem, used for preventing any
                                  * opreation until the connection
                                  * between two cpu established.
                                  */
  struct rpmsg_endpoint ept;
};

/* net_rpmsg_bridge_s is the bridge role on top of the link: no netdev
 * is registered; the endpoint is relayed to a socket on the local
 * interface, which follows the remote state.
 */

struct net_rpmsg_bridge_s
{
  struct net_rpmsg_base_s  base;    /* The rpmsg link */

  FAR struct net_driver_s *ifdev;   /* The local bridged interface */
  struct socket            ifsock;  /* Socket on the local interface */
  struct pollfd            pfd;
};

/* net_rpmsg_drv_s is the netdev role on top of the link */

struct net_rpmsg_drv_s
{
  struct net_rpmsg_base_s base;    /* The rpmsg link */

  netpkt_queue_t          rxqueue; /* RX packet queue */
  spinlock_t              lock;    /* Spinlock for protecting rxqueue */
  FAR void               *priv;    /* Private data for upper layer */
  net_rpmsg_drv_cb_t      cb;      /* IFUP/DOWN Callback function */

  /* This holds the information visible to the NuttX network */

  struct netdev_lowerhalf_s dev; /* Interface understood by the network */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/* RPMSG related functions */

/* Request handler functions */

static int net_rpmsg_drv_default_handler(FAR struct rpmsg_endpoint *ept,
                                         FAR void *data, size_t len,
                                         uint32_t src, FAR void *priv);
static int net_rpmsg_drv_ifup_handler(FAR struct rpmsg_endpoint *ept,
                                      FAR void *data, size_t len,
                                      uint32_t src, FAR void *priv);
static int net_rpmsg_drv_ifdown_handler(FAR struct rpmsg_endpoint *ept,
                                        FAR void *data, size_t len,
                                        uint32_t src, FAR void *priv);
static int net_rpmsg_drv_transfer_handler(FAR struct rpmsg_endpoint *ept,
                                          FAR void *data, size_t len,
                                          uint32_t src, FAR void *priv);

/* Response handler functions */

static int net_rpmsg_drv_default_response(FAR struct rpmsg_endpoint *ept,
                                          FAR void *data, size_t len,
                                          uint32_t src, FAR void *priv);

/* RPMSG device related functions */

static void net_rpmsg_device_created(FAR struct rpmsg_device *rdev,
                                     FAR void *priv);
static void net_rpmsg_device_destroy(FAR struct rpmsg_device *rdev,
                                     FAR void *priv);
static int  net_rpmsg_drv_ept_cb(FAR struct rpmsg_endpoint *ept, void *data,
                                 size_t len, uint32_t src, FAR void *priv);

static int  net_rpmsg_drv_send_recv(struct netdev_lowerhalf_s *dev,
                                    void *header_, uint32_t command,
                                    int len);

/* NuttX callback functions */

static int  net_rpmsg_drv_ifup(FAR struct netdev_lowerhalf_s *dev);
static int  net_rpmsg_drv_ifdown(FAR struct netdev_lowerhalf_s *dev);

static void net_rpmsg_drv_deferred_tx(FAR void *arg);
static int  net_rpmsg_drv_transmit(FAR struct netdev_lowerhalf_s *dev,
                                   FAR netpkt_t *pkt);
static FAR netpkt_t *
net_rpmsg_drv_receive(FAR struct netdev_lowerhalf_s *dev);

#ifdef CONFIG_NET_MCASTGROUP
static int  net_rpmsg_drv_addmac(FAR struct netdev_lowerhalf_s *dev,
                                 FAR const uint8_t *mac);
#ifdef CONFIG_NET_IGMP
static int  net_rpmsg_drv_rmmac(FAR struct netdev_lowerhalf_s *dev,
                                FAR const uint8_t *mac);
#endif
#endif
#ifdef CONFIG_NETDEV_IOCTL
static int  net_rpmsg_drv_ioctl(FAR struct netdev_lowerhalf_s *dev, int cmd,
                                unsigned long arg);
#endif

/* Bridge role functions */

static int net_rpmsg_bridge_ifup_handler(FAR struct rpmsg_endpoint *ept,
                                         FAR void *data, size_t len,
                                         uint32_t src, FAR void *priv);
static int net_rpmsg_bridge_ifdown_handler(FAR struct rpmsg_endpoint *ept,
                                           FAR void *data, size_t len,
                                           uint32_t src, FAR void *priv);
static int net_rpmsg_bridge_transfer_handler(FAR struct rpmsg_endpoint *ept,
                                             FAR void *data, size_t len,
                                             uint32_t src, FAR void *priv);
static int net_rpmsg_bridge_mcast_handler(FAR struct rpmsg_endpoint *ept,
                                          FAR void *data, size_t len,
                                          uint32_t src, FAR void *priv);
static int net_rpmsg_bridge_devioctl_handler(FAR struct rpmsg_endpoint *ept,
                                             FAR void *data, size_t len,
                                             uint32_t src, FAR void *priv);
static int net_rpmsg_bridge_ept_cb(FAR struct rpmsg_endpoint *ept,
                                   FAR void *data, size_t len,
                                   uint32_t src, FAR void *priv);
static void net_rpmsg_bridge_work(FAR void *arg);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const rpmsg_ept_cb g_net_rpmsg_drv_handler[] =
{
  [NET_RPMSG_IFUP]      = net_rpmsg_drv_ifup_handler,
  [NET_RPMSG_IFDOWN]    = net_rpmsg_drv_ifdown_handler,
  [NET_RPMSG_ADDMCAST]  = net_rpmsg_drv_default_handler,
  [NET_RPMSG_RMMCAST]   = net_rpmsg_drv_default_handler,
  [NET_RPMSG_DEVIOCTL]  = net_rpmsg_drv_default_handler,
  [NET_RPMSG_TRANSFER]  = net_rpmsg_drv_transfer_handler,
};

static const rpmsg_ept_cb g_net_rpmsg_drv_response[] =
{
  [NET_RPMSG_IFUP]      = net_rpmsg_drv_default_response,
  [NET_RPMSG_IFDOWN]    = net_rpmsg_drv_default_response,
  [NET_RPMSG_ADDMCAST]  = net_rpmsg_drv_default_response,
  [NET_RPMSG_RMMCAST]   = net_rpmsg_drv_default_response,
  [NET_RPMSG_DEVIOCTL]  = net_rpmsg_drv_default_response,
  [NET_RPMSG_TRANSFER]  = net_rpmsg_drv_default_response,
};

static const rpmsg_ept_cb g_net_rpmsg_bridge_handler[] =
{
  [NET_RPMSG_IFUP]      = net_rpmsg_bridge_ifup_handler,
  [NET_RPMSG_IFDOWN]    = net_rpmsg_bridge_ifdown_handler,
  [NET_RPMSG_ADDMCAST]  = net_rpmsg_bridge_mcast_handler,
  [NET_RPMSG_RMMCAST]   = net_rpmsg_bridge_mcast_handler,
  [NET_RPMSG_DEVIOCTL]  = net_rpmsg_bridge_devioctl_handler,
  [NET_RPMSG_TRANSFER]  = net_rpmsg_bridge_transfer_handler,
};

static const struct netdev_ops_s g_net_rpmsg_drv_ops =
{
  .ifup     = net_rpmsg_drv_ifup,
  .ifdown   = net_rpmsg_drv_ifdown,
  .transmit = net_rpmsg_drv_transmit,
  .receive  = net_rpmsg_drv_receive,
#ifdef CONFIG_NET_MCASTGROUP
  .addmac   = net_rpmsg_drv_addmac,
  .rmmac    = net_rpmsg_drv_rmmac,
#endif
#ifdef CONFIG_NETDEV_IOCTL
  .ioctl    = net_rpmsg_drv_ioctl,
#endif
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: net_rpmsg_drv_transmit
 *
 * Description:
 *   Start hardware transmission.  Acquires an rpmsg TX buffer without
 *   blocking and sends the packet.  When the buffer is temporarily
 *   unavailable (or the send fails transiently), returns -EBUSY so the
 *   upper half keeps the packet at the head of its txq and pauses TX; a
 *   deferred worker then re-arms TX via netdev_lower_txdone().
 *
 * Parameters:
 *   dev - Reference to the NuttX driver state structure
 *   pkt - The packet to be sent
 *
 * Returned Value:
 *   OK on success; -EBUSY to defer; a negated errno on permanent failure
 *
 * Assumptions:
 *   The network is locked.
 *
 ****************************************************************************/

static int net_rpmsg_drv_transmit(FAR struct netdev_lowerhalf_s *dev,
                                  FAR netpkt_t *pkt)
{
  FAR struct net_rpmsg_drv_s *drv =
    container_of(dev, struct net_rpmsg_drv_s, dev);
  FAR struct net_rpmsg_base_s *base = &drv->base;
  FAR struct net_rpmsg_transfer_s *transfer;
  uint32_t buflen;
#ifdef CONFIG_NET_PRMSG_DRV_ZERO_COPY_TRANSFER
  unsigned int datalen = sizeof(FAR netpkt_t *);
#else
  unsigned int datalen = netpkt_getdatalen(dev, pkt);
#endif
  int ret;

  /* Try to acquire an rpmsg TX buffer without blocking.  The quota tracking
   * that used to throttle the upper half to a single in-flight packet has
   * been removed, so we must not cache the packet in the driver.  Instead,
   * when no buffer is available we return -EBUSY: the upper half then keeps
   * the packet at the head of its txq and pauses TX (txdone=true), and a
   * deferred worker re-arms TX via netdev_lower_txdone() after a short
   * delay.
   */

  transfer = rpmsg_get_tx_payload_buffer(&base->ept, &buflen, false);
  if (transfer == NULL)
    {
      goto defer;
    }

  if (buflen < sizeof(*transfer) + datalen)
    {
      nerr("ERROR: Buffer is too small for xmit\n");
      rpmsg_release_tx_buffer(&base->ept, transfer);
      return -EMSGSIZE;
    }

  transfer->header.command = NET_RPMSG_TRANSFER;
  transfer->header.result  = 0;
  transfer->header.cookie  = 0;
  transfer->length         = datalen;

#ifdef CONFIG_NET_PRMSG_DRV_ZERO_COPY_TRANSFER
  *((FAR netpkt_t **)(transfer + 1)) = pkt;
#else
  netpkt_copyout(dev, (FAR uint8_t *)(transfer + 1), pkt, datalen, 0);
#endif

  ret = rpmsg_send_nocopy(&base->ept, transfer, sizeof(*transfer) + datalen);
  if (ret < 0)
    {
      nerr("ERROR: Failed to send packet\n");
      rpmsg_release_tx_buffer(&base->ept, transfer);
      goto defer;
    }

#ifndef CONFIG_NET_PRMSG_DRV_ZERO_COPY_TRANSFER
  netpkt_free(dev, pkt, NETPKT_TX);
#endif
  return OK;

defer:
  /* Schedule a worker to wait for a TX buffer to become available and then
   * re-arm the upper-half TX path.  The packet itself stays in the
   * upper-half txq (because we return -EBUSY), so the worker only needs to
   * wake the TX path once a buffer is free; the upper half re-delivers the
   * packet.
   */

  work_queue(LPWORK, &base->txwork, net_rpmsg_drv_deferred_tx, drv, 0);
  return -EBUSY;
}

/****************************************************************************
 * Name: net_rpmsg_drv_deferred_tx
 *
 * Description:
 *   Worker function for deferred TX retry.  Blocks until an rpmsg TX buffer
 *   becomes available, releases it immediately (it is only used as an
 *   availability probe), and re-arms the upper-half TX path via
 *   netdev_lower_txdone() so it re-delivers the packet that was paused in
 *   its txq when transmit() returned -EBUSY.
 *
 ****************************************************************************/

static void net_rpmsg_drv_deferred_tx(FAR void *arg)
{
  FAR struct net_rpmsg_drv_s *drv = arg;
  FAR struct net_rpmsg_base_s *base = &drv->base;
  FAR struct net_rpmsg_transfer_s *transfer;
  uint32_t buflen;

  /* Block until a TX buffer is available, then release it and re-arm TX. */

  transfer = rpmsg_get_tx_payload_buffer(&base->ept, &buflen, true);
  if (transfer == NULL)
    {
      /* The blocking get failed (e.g. transient error / endpoint not ready).
       * Re-schedule with a delay so TX can recover; otherwise the upper-half
       * txq would stay paused forever.  On teardown this work is cancelled
       * by net_rpmsg_device_destroy().
       */

      work_queue(LPWORK, &base->txwork, net_rpmsg_drv_deferred_tx, drv,
                 TICK_PER_SEC);
      return;
    }

  rpmsg_release_tx_buffer(&base->ept, transfer);
  netdev_lower_txdone(&drv->dev);
}

static FAR netpkt_t *
net_rpmsg_drv_receive(FAR struct netdev_lowerhalf_s *dev)
{
  FAR struct net_rpmsg_drv_s *drv =
    container_of(dev, struct net_rpmsg_drv_s, dev);
  FAR netpkt_t *pkt;
  irqstate_t flags;

  flags = spin_lock_irqsave(&drv->lock);
  pkt = netpkt_remove_queue(&drv->rxqueue);
  spin_unlock_irqrestore(&drv->lock, flags);

  return pkt;
}

/* RPMSG related functions */

static void rpmsg_send_response(FAR struct rpmsg_endpoint *ept,
                                FAR struct net_rpmsg_header_s *header,
                                size_t len, int result)
{
  header->command |= NET_RPMSG_RESPONSE;
  header->result   = result;
  rpmsg_send(ept, header, len);
}

static int net_rpmsg_drv_default_handler(FAR struct rpmsg_endpoint *ept,
                                         FAR void *data, size_t len,
                                         uint32_t src, FAR void *priv)
{
  FAR struct net_rpmsg_header_s *header = data;

  if (header->cookie)
    {
      rpmsg_send_response(ept, header, sizeof(*header), -EOPNOTSUPP);
    }

  return 0;
}

static int net_rpmsg_drv_ifup_handler(FAR struct rpmsg_endpoint *ept,
                                      FAR void *data, size_t len,
                                      uint32_t src, FAR void *priv)
{
  FAR struct net_rpmsg_drv_s *drv = priv;
  FAR struct net_rpmsg_header_s *header = data;

  netdev_lower_carrier_on(&drv->dev);
  if (drv->cb != NULL)
    {
      drv->cb(&drv->dev, NET_RPMSG_EVENT_CARRIER_ON);
    }

  rpmsg_send_response(ept, header, sizeof(*header), 0);

  return 0;
}

static int net_rpmsg_drv_ifdown_handler(FAR struct rpmsg_endpoint *ept,
                                        FAR void *data, size_t len,
                                        uint32_t src, FAR void *priv)
{
  FAR struct net_rpmsg_drv_s *drv = priv;
  FAR struct net_rpmsg_header_s *header = data;

  netdev_lower_carrier_off(&drv->dev);
  if (drv->cb != NULL)
    {
      drv->cb(&drv->dev, NET_RPMSG_EVENT_CARRIER_OFF);
    }

  rpmsg_send_response(ept, header, sizeof(*header), 0);

  return 0;
}

/****************************************************************************
 * Name: net_rpmsg_drv_transfer_handler
 *
 * Description:
 *   An message was received indicating the availability of a new RX packet
 *
 * Parameters:
 *   ept - Reference to the endpoint which receive the message
 *
 * Returned Value:
 *   OK on success
 *
 * Assumptions:
 *   The network is locked.
 *
 ****************************************************************************/

static int net_rpmsg_drv_transfer_handler(FAR struct rpmsg_endpoint *ept,
                                          FAR void *data, size_t len,
                                          uint32_t src, FAR void *priv)
{
  FAR struct net_rpmsg_transfer_s *transfer = data;
  FAR struct net_rpmsg_drv_s *drv = priv;
  FAR struct netdev_lowerhalf_s *dev = &drv->dev;
  FAR netpkt_t *pkt;
  irqstate_t flags;

  if (transfer->length > len - sizeof(*transfer))
    {
      nerr("ERROR: net_rpmsg got invalid transfer length!");
      goto drop;
    }

#ifdef CONFIG_NET_PRMSG_DRV_ZERO_COPY_TRANSFER
  /* Zero-Copy transfer mode: the data contains the pkt address directly */

  pkt = *((FAR netpkt_t **)(transfer + 1));

  DEBUGASSERT(pkt != NULL);

#else

  /* Normal copy mode: allocate new packet and copy data */

  pkt = netpkt_alloc(dev, NETPKT_RX);
  if (pkt == NULL)
    {
      nerr("ERROR: Failed to allocate buffer!\n");
      goto drop;
    }

  if (netpkt_copyin(dev, pkt, (FAR uint8_t *)(transfer + 1),
                    transfer->length, 0) < 0)
    {
      nerr("ERROR: Failed to copyin data!\n");
      goto free;
    }
#endif

  flags = spin_lock_irqsave(&drv->lock);
  netpkt_add_queue(pkt, &drv->rxqueue);
  spin_unlock_irqrestore(&drv->lock, flags);

  netdev_lower_rxready(dev);
  return 0;

#ifndef CONFIG_NET_PRMSG_DRV_ZERO_COPY_TRANSFER
free:
  netpkt_free(dev, pkt, NETPKT_RX);
#endif

drop:
  NETDEV_RXDROPPED(&dev->netdev);
  return 0;
}

/****************************************************************************
 * Name: net_rpmsg_drv_default_response
 *
 * Description:
 *   This function is used to handle the response from the RPMSG device.
 *   It is used to copy the response to the cookie and post the semaphore.
 *
 ****************************************************************************/

static int net_rpmsg_drv_default_response(FAR struct rpmsg_endpoint *ept,
                                          FAR void *data, size_t len,
                                          uint32_t src, FAR void *priv)
{
  FAR struct net_rpmsg_header_s *header = data;
  FAR struct net_rpmsg_cookie_s *cookie =
    (struct net_rpmsg_cookie_s *)(uintptr_t)header->cookie;

  memcpy(cookie->header, header, len);
  nxsem_post(&cookie->sem);
  return 0;
}

/****************************************************************************
 * Name: net_rpmsg_drv_ept_release
 ****************************************************************************/

static void net_rpmsg_drv_ept_release(FAR struct rpmsg_endpoint *ept)
{
  FAR struct net_rpmsg_drv_s *drv = ept->priv;

  netdev_lower_carrier_off(&drv->dev);
  rpmsg_wait(&drv->base.ept, &drv->base.wait);
}

/****************************************************************************
 * Name: net_rpmsg_ns_bound
 *
 * Description:
 *   Rpmsg device end point service bound callback function , called when
 *   remote end point address is received.
 *
 * Parameters:
 *   ept  - The rpmsg-device end point
 *
 * Returned Values:
 *   None
 *
 ****************************************************************************/

static void net_rpmsg_ns_bound(FAR struct rpmsg_endpoint *ept)
{
  FAR struct net_rpmsg_base_s *base = ept->priv;

  rpmsg_post(&base->ept, &base->wait);
}

/****************************************************************************
 * Name: net_rpmsg_create_ept
 ****************************************************************************/

static void net_rpmsg_create_ept(FAR struct net_rpmsg_base_s *base,
                                 FAR struct rpmsg_device *rdev)
{
  char eptname[RPMSG_NAME_SIZE];

  base->ept.priv = base;
  snprintf(eptname, sizeof(eptname),
           NET_RPMSG_EPT_PREFIX "%s", base->devname);

  rpmsg_create_ept(&base->ept, rdev, eptname,
                   RPMSG_ADDR_ANY, RPMSG_ADDR_ANY, base->eptcb, NULL);
}

/****************************************************************************
 * Name: net_rpmsg_device_created
 ****************************************************************************/

static void net_rpmsg_device_created(FAR struct rpmsg_device *rdev,
                                     FAR void *priv)
{
  FAR struct net_rpmsg_base_s *base = priv;

  if (!strcmp(base->cpuname, rpmsg_get_cpuname(rdev)))
    {
      net_rpmsg_create_ept(base, rdev);
    }
}

/****************************************************************************
 * Name: net_rpmsg_device_destroy
 ****************************************************************************/

static void net_rpmsg_device_destroy(FAR struct rpmsg_device *rdev,
                                     FAR void *priv)
{
  FAR struct net_rpmsg_base_s *base = priv;

  if (!strcmp(base->cpuname, rpmsg_get_cpuname(rdev)))
    {
      /* Cancel any pending deferred TX re-arm work.  The packet itself (if
       * any) is owned by the upper-half txq and released by the network
       * stack, so there is nothing to free here.
       */

      work_cancel_sync(LPWORK, &base->txwork);

      rpmsg_destroy_ept(&base->ept);
    }
}

static int net_rpmsg_drv_ept_cb(FAR struct rpmsg_endpoint *ept, void *data,
                                size_t len, uint32_t src, FAR void *priv)
{
  FAR struct net_rpmsg_header_s *header = data;
  uint32_t cmd = NET_RPMSG_GET_COMMAND(header->command);

  if (cmd < nitems(g_net_rpmsg_drv_handler))
    {
      rpmsg_ept_cb cb;

      cb = NET_RPMSG_IS_RESPONSE(header->command) ?
           g_net_rpmsg_drv_response[cmd] : g_net_rpmsg_drv_handler[cmd];
      if (cb != NULL)
        {
          return cb(ept, data, len, src, priv);
        }
    }

  return -EINVAL;
}

static int net_rpmsg_drv_send_recv(FAR struct netdev_lowerhalf_s *dev,
                                   FAR void *header_, uint32_t command,
                                   int len)
{
  FAR struct net_rpmsg_base_s *base =
    &container_of(dev, struct net_rpmsg_drv_s, dev)->base;
  FAR struct net_rpmsg_header_s *header = header_;
  FAR struct net_rpmsg_cookie_s cookie;
  int sval = 0;
  int ret;

  nxsem_get_value(&base->wait, &sval);
  if (sval <= 0)
    {
      rpmsg_wait(&base->ept, &base->wait);
      rpmsg_post(&base->ept, &base->wait);
    }

  nxsem_init(&cookie.sem, 0, 0);

  cookie.header   = header;
  header->command = command;
  header->result  = -ENXIO;
  header->cookie  = (uintptr_t)&cookie;

  ret = rpmsg_send(&base->ept, header, len);
  if (ret < 0)
    {
      goto out;
    }

  net_sem_timedwait2(&cookie.sem, false, UINT_MAX, &dev->netdev.d_lock,
                     NULL);
  ret = cookie.header->result;

out:
  nxsem_destroy(&cookie.sem);
  return ret;
}

/****************************************************************************
 * Name: net_rpmsg_drv_ifup
 *
 * Description:
 *   NuttX Callback: Bring up the link interface when an IP address is
 *   provided
 *
 * Parameters:
 *   dev - Reference to the NuttX driver state structure
 *
 * Returned Value:
 *   None
 *
 * Assumptions:
 *   The network is locked.
 *
 ****************************************************************************/

static int net_rpmsg_drv_ifup(FAR struct netdev_lowerhalf_s *dev)
{
  FAR struct net_rpmsg_drv_s *drv =
    container_of(dev, struct net_rpmsg_drv_s, dev);
  struct net_rpmsg_ifup_s msg =
  {
  };

  int ret;

#ifdef CONFIG_NET_IPv4
  ninfo("Bringing up: %u.%u.%u.%u\n",
        ip4_addr1(dev->netdev.d_ipaddr), ip4_addr2(dev->netdev.d_ipaddr),
        ip4_addr3(dev->netdev.d_ipaddr), ip4_addr4(dev->netdev.d_ipaddr));
#endif
#ifdef CONFIG_NET_IPv6
  ninfo("Bringing up: %04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x\n",
        dev->netdev.d_ipv6addr[0], dev->netdev.d_ipv6addr[1],
        dev->netdev.d_ipv6addr[2], dev->netdev.d_ipv6addr[3],
        dev->netdev.d_ipv6addr[4], dev->netdev.d_ipv6addr[5],
        dev->netdev.d_ipv6addr[6], dev->netdev.d_ipv6addr[7]);
#endif

  netdev_lock(&dev->netdev);

  /* Prepare the message */

  msg.lnkaddr.length = MIN(netdev_lladdrsize(&dev->netdev),
                          sizeof(msg.lnkaddr.addr));
  if (msg.lnkaddr.length > 0)
    {
      memcpy(msg.lnkaddr.addr, &dev->netdev.d_mac, msg.lnkaddr.length);
    }

#ifdef CONFIG_NET_IPv4
  net_ipv4addr_copy(msg.ipaddr, dev->netdev.d_ipaddr);
  net_ipv4addr_copy(msg.draddr, dev->netdev.d_draddr);
  net_ipv4addr_copy(msg.netmask, dev->netdev.d_netmask);
#endif

#ifdef CONFIG_NET_IPv6
  net_ipv6addr_copy(msg.ipv6addr, dev->netdev.d_ipv6addr);
  net_ipv6addr_copy(msg.ipv6draddr, dev->netdev.d_ipv6draddr);
  net_ipv6addr_copy(msg.ipv6netmask, dev->netdev.d_ipv6netmask);
#endif

  /* Send the message */

  ret = net_rpmsg_drv_send_recv(dev, &msg, NET_RPMSG_IFUP, sizeof(msg));
  if (ret < 0)
    {
      netdev_unlock(&dev->netdev);
      return ret;
    }

  /* Update net_driver_t field */

  if (msg.lnkaddr.length > 0)
    {
      memcpy(&dev->netdev.d_mac, msg.lnkaddr.addr,
             MIN(msg.lnkaddr.length, sizeof(msg.lnkaddr.addr)));
    }

#ifdef CONFIG_NET_IPv4
  net_ipv4addr_copy(dev->netdev.d_ipaddr, msg.ipaddr);
  net_ipv4addr_copy(dev->netdev.d_draddr, msg.draddr);
  net_ipv4addr_copy(dev->netdev.d_netmask, msg.netmask);
#endif

#ifdef CONFIG_NET_IPv6
  net_ipv6addr_copy(dev->netdev.d_ipv6addr, msg.ipv6addr);
  net_ipv6addr_copy(dev->netdev.d_ipv6draddr, msg.ipv6draddr);
  net_ipv6addr_copy(dev->netdev.d_ipv6netmask, msg.ipv6netmask);
#endif

  netdev_unlock(&dev->netdev);

#ifdef CONFIG_NETDB_DNSCLIENT
#  ifdef CONFIG_NET_IPv4
  if (!net_ipv4addr_cmp(msg.dnsaddr, INADDR_ANY))
    {
      struct sockaddr_in dnsaddr =
      {
      };

      dnsaddr.sin_family = AF_INET;
      dnsaddr.sin_port   = HTONS(DNS_DEFAULT_PORT);
      memcpy(&dnsaddr.sin_addr, &msg.dnsaddr, sizeof(msg.dnsaddr));

      dns_add_nameserver((FAR const struct sockaddr *)&dnsaddr,
                         sizeof(dnsaddr));
    }
#  endif

#  ifdef CONFIG_NET_IPv6
  if (!net_ipv6addr_cmp(msg.ipv6dnsaddr, &in6addr_any))
    {
      struct sockaddr_in6 dnsaddr =
      {
      };

      dnsaddr.sin6_family = AF_INET6;
      dnsaddr.sin6_port   = HTONS(DNS_DEFAULT_PORT);
      memcpy(&dnsaddr.sin6_addr, msg.ipv6dnsaddr, sizeof(msg.ipv6dnsaddr));

      dns_add_nameserver((FAR const struct sockaddr *)&dnsaddr,
                         sizeof(dnsaddr));
    }
#  endif
#endif

  if (drv->cb != NULL)
    {
      drv->cb(dev, NET_RPMSG_EVENT_IF_UP);
    }

  return OK;
}

/****************************************************************************
 * Name: net_rpmsg_drv_ifdown
 *
 * Description:
 *   NuttX Callback: Stop the interface.
 *
 * Parameters:
 *   dev - Reference to the NuttX driver state structure
 *
 * Returned Value:
 *   None
 *
 * Assumptions:
 *   The network is locked.
 *
 ****************************************************************************/

static int net_rpmsg_drv_ifdown(FAR struct netdev_lowerhalf_s *dev)
{
  FAR struct net_rpmsg_drv_s *drv =
    container_of(dev, struct net_rpmsg_drv_s, dev);
  struct net_rpmsg_ifdown_s msg =
  {
  };

  int ret;

  ret = net_rpmsg_drv_send_recv(dev, &msg, NET_RPMSG_IFDOWN, sizeof(msg));
  if (ret < 0)
    {
      return ret;
    }

  if (drv->cb != NULL)
    {
      drv->cb(dev, NET_RPMSG_EVENT_IF_DOWN);
    }

  return ret;
}

/****************************************************************************
 * Name: net_rpmsg_drv_addmac
 *
 * Description:
 *   NuttX Callback: Add the specified MAC address to the hardware multicast
 *   address filtering
 *
 * Parameters:
 *   dev  - Reference to the NuttX driver state structure
 *   mac  - The MAC address to be added
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure.
 *
 ****************************************************************************/

#ifdef CONFIG_NET_MCASTGROUP
static int net_rpmsg_drv_addmac(FAR struct netdev_lowerhalf_s *dev,
                                FAR const uint8_t *mac)
{
  struct net_rpmsg_mcast_s msg;

  /* Add the MAC address to the hardware multicast routing table */

  msg.lnkaddr.length = MIN(netdev_lladdrsize(&dev->netdev),
                          sizeof(msg.lnkaddr.addr));
  if (msg.lnkaddr.length > 0)
    {
      memcpy(msg.lnkaddr.addr, mac, msg.lnkaddr.length);
    }

  return net_rpmsg_drv_send_recv(dev, &msg, NET_RPMSG_ADDMCAST, sizeof(msg));
}

/****************************************************************************
 * Name: net_rpmsg_drv_rmmac
 *
 * Description:
 *   NuttX Callback: Remove the specified MAC address from the hardware
 *   multicast address filtering
 *
 * Parameters:
 *   dev  - Reference to the NuttX driver state structure
 *   mac  - The MAC address to be removed
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure.
 *
 ****************************************************************************/

static int net_rpmsg_drv_rmmac(FAR struct netdev_lowerhalf_s *dev,
                               FAR const uint8_t *mac)
{
  struct net_rpmsg_mcast_s msg;

  /* Remove the MAC address from the hardware multicast routing table */

  msg.lnkaddr.length = MIN(netdev_lladdrsize(&dev->netdev),
                          sizeof(msg.lnkaddr.addr));
  if (msg.lnkaddr.length > 0)
    {
      memcpy(msg.lnkaddr.addr, mac, msg.lnkaddr.length);
    }

  return net_rpmsg_drv_send_recv(dev, &msg, NET_RPMSG_RMMCAST, sizeof(msg));
}
#endif

/****************************************************************************
 * Name: net_rpmsg_drv_ioctl
 *
 * Description:
 *   Handle network IOCTL commands directed to this device.
 *
 * Parameters:
 *   dev - Reference to the NuttX driver state structure
 *   cmd - The IOCTL command
 *   arg - The argument for the IOCTL command
 *
 * Returned Value:
 *   OK on success; Negated errno on failure.
 *
 * Assumptions:
 *   The network is locked.
 *
 ****************************************************************************/

#ifdef CONFIG_NETDEV_IOCTL
static int net_rpmsg_drv_ioctl(FAR struct netdev_lowerhalf_s *dev, int cmd,
                               unsigned long arg)
{
  ssize_t len;
  int ret;

  len = net_ioctl_arglen(PF_RPMSG, cmd);
  if (len >= 0)
    {
      FAR struct net_rpmsg_ioctl_s *msg;
      char buf[sizeof(*msg) + len];

      msg = (FAR struct net_rpmsg_ioctl_s *)buf;

      msg->code   = cmd;
      msg->length = len;
      memcpy(msg->arg, (FAR void *)arg, len);

      ret = net_rpmsg_drv_send_recv(dev, msg,
              NET_RPMSG_DEVIOCTL, sizeof(*msg) + len);
      if (ret >= 0)
        {
          memcpy((FAR void *)arg, msg->arg, len);
        }
    }
  else
    {
      ret = len;
    }

  return ret;
}
#endif

/****************************************************************************
 * Name: net_rpmsg_bridge_ifup_handler
 ****************************************************************************/

static int net_rpmsg_bridge_ifup_handler(FAR struct rpmsg_endpoint *ept,
                                         FAR void *data, size_t len,
                                         uint32_t src, FAR void *priv)
{
  FAR struct net_rpmsg_bridge_s *br =
    container_of(priv, struct net_rpmsg_bridge_s, base);
  FAR struct net_rpmsg_header_s *header = data;
  int ret;

  ret = psock_poll(&br->ifsock, &br->pfd, true);
  if (ret >= 0)
    {
      ret = netdev_ifup(br->ifdev);
      if (ret < 0)
        {
          psock_poll(&br->ifsock, &br->pfd, false);
        }
    }

  rpmsg_send_response(ept, header, sizeof(*header), ret);

  return 0;
}

/****************************************************************************
 * Name: net_rpmsg_bridge_ifdown_handler
 ****************************************************************************/

static int net_rpmsg_bridge_ifdown_handler(FAR struct rpmsg_endpoint *ept,
                                           FAR void *data, size_t len,
                                           uint32_t src, FAR void *priv)
{
  FAR struct net_rpmsg_bridge_s *br =
    container_of(priv, struct net_rpmsg_bridge_s, base);
  FAR struct net_rpmsg_header_s *header = data;
  int ret;

  psock_poll(&br->ifsock, &br->pfd, false);
  ret = netdev_ifdown(br->ifdev);
  rpmsg_send_response(ept, header, sizeof(*header), ret);

  return 0;
}

/****************************************************************************
 * Name: net_rpmsg_bridge_mcast_handler
 *
 * Description:
 *   Apply a multicast filter change to the local interface.
 *
 ****************************************************************************/

static int net_rpmsg_bridge_mcast_handler(FAR struct rpmsg_endpoint *ept,
                                          FAR void *data, size_t len,
                                          uint32_t src, FAR void *priv)
{
  FAR struct net_rpmsg_mcast_s *msg = data;
  int ret = -ENOSYS;

#ifdef CONFIG_NET_MCASTGROUP
  FAR struct net_rpmsg_bridge_s *br =
    container_of(priv, struct net_rpmsg_bridge_s, base);

  if (NET_RPMSG_GET_COMMAND(msg->header.command) == NET_RPMSG_ADDMCAST)
    {
      if (br->ifdev->d_addmac != NULL)
        {
          ret = br->ifdev->d_addmac(br->ifdev, msg->lnkaddr.addr);
        }
    }
  else if (br->ifdev->d_rmmac != NULL)
    {
      ret = br->ifdev->d_rmmac(br->ifdev, msg->lnkaddr.addr);
    }
#endif

  rpmsg_send_response(ept, &msg->header, sizeof(*msg), ret);
  return 0;
}

/****************************************************************************
 * Name: net_rpmsg_bridge_devioctl_handler
 *
 * Description:
 *   Forward a device ioctl to the local interface.
 *
 ****************************************************************************/

static int net_rpmsg_bridge_devioctl_handler(FAR struct rpmsg_endpoint *ept,
                                             FAR void *data, size_t len,
                                             uint32_t src, FAR void *priv)
{
  FAR struct net_rpmsg_ioctl_s *msg = data;
  int ret = -ENOSYS;
#ifdef CONFIG_NETDEV_IOCTL
  FAR struct net_rpmsg_bridge_s *br =
    container_of(priv, struct net_rpmsg_bridge_s, base);

  if (br->ifdev->d_ioctl != NULL)
    {
      ret = br->ifdev->d_ioctl(br->ifdev, msg->code,
                               (unsigned long)(uintptr_t)msg->arg);
    }
#endif

  rpmsg_send_response(ept, &msg->header, sizeof(*msg) + msg->length, ret);
  return 0;
}

/****************************************************************************
 * Name: net_rpmsg_bridge_transfer_handler
 *
 * Description:
 *   Put the received frame on the local interface.
 *
 ****************************************************************************/

static int net_rpmsg_bridge_transfer_handler(FAR struct rpmsg_endpoint *ept,
                                             FAR void *data, size_t len,
                                             uint32_t src, FAR void *priv)
{
  FAR struct net_rpmsg_bridge_s *br =
    container_of(priv, struct net_rpmsg_bridge_s, base);
  FAR struct net_rpmsg_transfer_s *transfer = data;

  if (transfer->length > len - sizeof(*transfer))
    {
      nerr("ERROR: net_rpmsg got invalid transfer length!");
      return 0;
    }

  psock_sendto(&br->ifsock, transfer + 1, transfer->length, 0, NULL, 0);

  return 0;
}

/****************************************************************************
 * Name: net_rpmsg_bridge_ept_cb
 ****************************************************************************/

static int net_rpmsg_bridge_ept_cb(FAR struct rpmsg_endpoint *ept,
                                   FAR void *data, size_t len,
                                   uint32_t src, FAR void *priv)
{
  FAR struct net_rpmsg_header_s *header = data;
  uint32_t cmd = NET_RPMSG_GET_COMMAND(header->command);

  /* The bridge issues no commands, so no responses arrive */

  if (cmd < nitems(g_net_rpmsg_bridge_handler) &&
      g_net_rpmsg_bridge_handler[cmd] != NULL &&
      !NET_RPMSG_IS_RESPONSE(header->command))
    {
      return g_net_rpmsg_bridge_handler[cmd](ept, data, len, src, priv);
    }

  return -EINVAL;
}

/****************************************************************************
 * Name: net_rpmsg_bridge_ept_release
 ****************************************************************************/

static void net_rpmsg_bridge_ept_release(FAR struct rpmsg_endpoint *ept)
{
  FAR struct net_rpmsg_bridge_s *br =
    container_of(ept->priv, struct net_rpmsg_bridge_s, base);

  /* The remote went away without an IFDOWN */

  psock_poll(&br->ifsock, &br->pfd, false);
  netdev_ifdown(br->ifdev);
  rpmsg_wait(&br->base.ept, &br->base.wait);
}

/****************************************************************************
 * Name: net_rpmsg_bridge_poll_cb
 ****************************************************************************/

static void net_rpmsg_bridge_poll_cb(FAR struct pollfd *pfds)
{
  FAR struct net_rpmsg_bridge_s *br = pfds->arg;

  work_queue(LPWORK, &br->base.txwork, net_rpmsg_bridge_work, br, 0);
}

/****************************************************************************
 * Name: net_rpmsg_bridge_sock
 *
 * Description:
 *   Raw socket bound to the local interface, the link layer specific part
 *   of the bridge.  recv_own loops the frames written to the interface
 *   back to the remote side.
 *
 ****************************************************************************/

static int net_rpmsg_bridge_sock(FAR struct socket *sock,
                                 FAR struct net_driver_s *dev)
{
  int ret = -ENOSYS;

  switch (dev->d_lltype)
    {
#ifdef CONFIG_NET_CAN
      case NET_LL_CAN:
        {
          struct sockaddr_can addr;
          int on = 1;

          ret = psock_socket(PF_CAN, SOCK_RAW, CAN_RAW, sock);
          if (ret < 0)
            {
              return ret;
            }

          psock_setsockopt(sock, SOL_CAN_RAW, CAN_RAW_RECV_OWN_MSGS,
                           &on, sizeof(on));
          psock_setsockopt(sock, SOL_CAN_RAW, CAN_RAW_FD_FRAMES,
                           &on, sizeof(on));

          memset(&addr, 0, sizeof(addr));
          addr.can_family  = AF_CAN;
          addr.can_ifindex = dev->d_ifindex;
          ret = psock_bind(sock, (FAR struct sockaddr *)&addr,
                           sizeof(addr));
          if (ret < 0)
            {
              psock_close(sock);
            }

          return ret;
        }
#endif

#ifdef CONFIG_NET_PKT
      case NET_LL_ETHERNET:
        {
          struct sockaddr_ll addr;

          ret = psock_socket(PF_PACKET, SOCK_RAW, 0, sock);
          if (ret < 0)
            {
              return ret;
            }

          memset(&addr, 0, sizeof(addr));
          addr.sll_family  = AF_PACKET;
          addr.sll_ifindex = dev->d_ifindex;
          ret = psock_bind(sock, (FAR struct sockaddr *)&addr,
                           sizeof(addr));
          if (ret < 0)
            {
              psock_close(sock);
            }

          return ret;
        }
#endif

      default:
        return ret;
    }
}

/****************************************************************************
 * Name: net_rpmsg_bridge_work
 *
 * Description:
 *   Relay the local socket into rpmsg TX buffers.  Queued on the shared
 *   txwork item by the socket poll callback, which is registered only
 *   while the remote interface is up, so the endpoint is valid here.
 *
 ****************************************************************************/

static void net_rpmsg_bridge_work(FAR void *arg)
{
  FAR struct net_rpmsg_bridge_s *br = arg;
  FAR struct net_rpmsg_base_s *base = &br->base;
  FAR struct net_rpmsg_transfer_s *msg;
  uint32_t space;
  ssize_t len;

  for (; ; )
    {
      /* Read the frame straight into the rpmsg buffer.  No buffer is
       * backpressure: nothing signals a buffer return, so retry after
       * a short delay.
       */

      msg = rpmsg_get_tx_payload_buffer(&base->ept, &space, false);
      if (msg == NULL)
        {
          work_queue(LPWORK, &base->txwork, net_rpmsg_bridge_work, br,
                     MSEC2TICK(10));
          return;
        }

      len = psock_recvfrom(&br->ifsock, msg + 1, space - sizeof(*msg),
                           MSG_DONTWAIT, NULL, NULL);
      if (len <= 0)
        {
          rpmsg_release_tx_buffer(&base->ept, msg);
          return;
        }

      msg->header.command = NET_RPMSG_TRANSFER;
      msg->header.result  = 0;
      msg->header.cookie  = 0;
      msg->length         = len;
      rpmsg_send_nocopy(&base->ept, msg, sizeof(*msg) + len);
    }
}

/****************************************************************************
 * Name: net_rpmsg_drv_alloc
 ****************************************************************************/

static FAR struct net_rpmsg_drv_s *
net_rpmsg_drv_alloc(FAR const char *devname, enum net_lltype_e lltype)
{
  FAR struct net_rpmsg_drv_s *drv = kmm_zalloc(sizeof(*drv));
  FAR struct netdev_lowerhalf_s *netdev;
  FAR struct net_rpmsg_base_s *base;
  int ret;

  if (!drv)
    {
      return NULL;
    }

  base = &drv->base;
  netdev = &drv->dev;

  netdev->ops = &g_net_rpmsg_drv_ops;

  base->ept.priv = base;
  base->ept.release_cb = net_rpmsg_drv_ept_release;
  base->ept.ns_bound_cb = net_rpmsg_ns_bound;
  base->eptcb = net_rpmsg_drv_ept_cb;

  nxsem_init(&base->wait, 0, 0);
  spin_lock_init(&drv->lock);

  /* Init a random MAC address, the caller can override it. */

#if defined(CONFIG_NET_ETHERNET) || defined(CONFIG_NET_6LOWPAN) || \
    defined(CONFIG_NET_BLUETOOTH) || defined(CONFIG_NET_IEEE802154) || \
    defined(CONFIG_NET_TUN)
  if (lltype != NET_LL_CAN)
    {
      arc4random_buf(&netdev->netdev.d_mac.ether.ether_addr_octet,
                     sizeof(netdev->netdev.d_mac.ether.ether_addr_octet));
    }
#endif

  strlcpy(base->devname, devname, IFNAMSIZ);
  strlcpy(netdev->netdev.d_ifname, devname, IFNAMSIZ);

  ret = netdev_lower_register(netdev, lltype);
  if (ret < 0)
    {
      nerr("ERROR: netdev_lower_register failed: %d\n", ret);
      nxsem_destroy(&base->wait);
      kmm_free(drv);
      return NULL;
    }

  return drv;
}

#ifdef CONFIG_NET_RPMSG_DRV_SERVER
/****************************************************************************
 * Name: net_rpmsg_drv_ns_match
 ****************************************************************************/

static bool net_rpmsg_drv_ns_match(FAR struct rpmsg_device *rdev,
                                   FAR void *priv, FAR const char *name,
                                   uint32_t dest)
{
  return !strncmp(name, NET_RPMSG_EPT_PREFIX, strlen(NET_RPMSG_EPT_PREFIX));
}

/****************************************************************************
 * Name: net_rpmsg_drv_ns_bind
 ****************************************************************************/

static void net_rpmsg_drv_ns_bind(FAR struct rpmsg_device *rdev,
                                  FAR void *priv_, FAR const char *name,
                                  uint32_t dest)
{
  FAR struct net_rpmsg_base_s *base;
  FAR struct net_driver_s *dev;
  const char *devname = name + strlen(NET_RPMSG_EPT_PREFIX);

  dev = netdev_findbyname(devname);
  if (dev)
    {
      base = &container_of(dev, struct net_rpmsg_drv_s,
                           dev.netdev)->base;
      base->ept.priv = base;
      base->ept.release_cb = net_rpmsg_drv_ept_release;
      base->ept.ns_bound_cb = net_rpmsg_ns_bound;
    }
  else
    {
      FAR struct net_rpmsg_drv_s *drv;

      if (strstr(devname, "can") || strstr(devname, "lin"))
        {
          drv = net_rpmsg_drv_alloc(devname, NET_LL_CAN);
        }
      else
        {
          drv = net_rpmsg_drv_alloc(devname, NET_LL_ETHERNET);
        }

      if (!drv)
        {
          return;
        }

      base = &drv->base;
    }

  rpmsg_create_ept(&base->ept, rdev, name, RPMSG_ADDR_ANY, dest,
                   net_rpmsg_drv_ept_cb, rpmsg_destroy_ept);
  rpmsg_post(&base->ept, &base->wait);
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: net_rpmsg_drv_init
 *
 * Description:
 *   Allocate a new network device instance for the RPMSG network and
 *   register it with the network device manager.  This is the client side of
 *   the RPMSG driver.  The RPMSG driver is the server side of the driver.
 *
 * Parameters:
 *   cpuname    - Remote CPU name
 *   devname    - Local and remote network device name
 *   lltype     - Link layer type
 *
 * Returned Value:
 *   A pointer to the allocated network device instance.  NULL is returned on
 *   failure.
 *
 ****************************************************************************/

FAR struct netdev_lowerhalf_s *
net_rpmsg_drv_init(FAR const char *cpuname, FAR const char *devname,
                   enum net_lltype_e lltype)
{
  FAR struct net_rpmsg_drv_s *drv;
  FAR struct netdev_lowerhalf_s *dev;
  int ret;

  /* Allocate the interface structure */

  if (!devname || !cpuname ||
      !(drv = net_rpmsg_drv_alloc(devname, lltype)))
    {
      return NULL;
    }

  strlcpy(drv->base.cpuname, cpuname, RPMSG_NAME_SIZE);

  dev = &drv->dev;

  /* Register the device with the openamp */

  ret = rpmsg_register_callback(&drv->base,
                                net_rpmsg_device_created,
                                net_rpmsg_device_destroy,
                                NULL,
                                NULL);

  if (ret < 0)
    {
      netdev_lower_unregister(dev);
      nxsem_destroy(&drv->base.wait);
      kmm_free(drv);
      return NULL;
    }

  return dev;
}

/****************************************************************************
 * Name: net_rpmsg_drv_priv
 ****************************************************************************/

FAR void *net_rpmsg_drv_priv(FAR struct netdev_lowerhalf_s *dev)
{
  FAR struct net_rpmsg_drv_s *drv =
    container_of(dev, struct net_rpmsg_drv_s, dev);

  return drv->priv;
}

/****************************************************************************
 * Name: net_rpmsg_drv_set_callback
 ****************************************************************************/

void net_rpmsg_drv_set_callback(FAR struct netdev_lowerhalf_s *dev,
                                net_rpmsg_drv_cb_t cb, FAR void *priv)
{
  FAR struct net_rpmsg_drv_s *drv =
    container_of(dev, struct net_rpmsg_drv_s, dev);

  drv->cb = cb;
  drv->priv = priv;
}

/****************************************************************************
 * Name: net_rpmsg_bridge_init
 *
 * Description:
 *   Bridge the remote interface devname to the local interface ifname.
 *
 ****************************************************************************/

int net_rpmsg_bridge_init(FAR const char *cpuname,
                          FAR const char *devname,
                          FAR const char *ifname)
{
  FAR struct net_rpmsg_bridge_s *br;
  FAR struct net_rpmsg_base_s *base;
  FAR struct net_driver_s *ifdev;
  int ret;

  if (cpuname == NULL || devname == NULL || ifname == NULL)
    {
      return -EINVAL;
    }

  ifdev = netdev_findbyname(ifname);
  if (ifdev == NULL)
    {
      return -ENODEV;
    }

  /* The bridge shares the endpoint machinery with the netdev role but
   * registers no netdev: the local interface is the only one.
   */

  br = kmm_zalloc(sizeof(*br));
  if (br == NULL)
    {
      return -ENOMEM;
    }

  base = &br->base;
  br->ifdev             = ifdev;
  br->pfd.events        = POLLIN;
  br->pfd.arg           = br;
  br->pfd.cb            = net_rpmsg_bridge_poll_cb;
  base->ept.priv        = base;
  base->ept.release_cb  = net_rpmsg_bridge_ept_release;
  base->ept.ns_bound_cb = net_rpmsg_ns_bound;
  base->eptcb           = net_rpmsg_bridge_ept_cb;
  nxsem_init(&base->wait, 0, 0);
  strlcpy(base->cpuname, cpuname, RPMSG_NAME_SIZE);
  strlcpy(base->devname, devname, IFNAMSIZ);

  ret = net_rpmsg_bridge_sock(&br->ifsock, ifdev);
  if (ret < 0)
    {
      goto err;
    }

  ret = rpmsg_register_callback(base,
                                net_rpmsg_device_created,
                                net_rpmsg_device_destroy,
                                NULL,
                                NULL);
  if (ret < 0)
    {
      psock_close(&br->ifsock);
      goto err;
    }

  return OK;

err:
  nxsem_destroy(&base->wait);
  kmm_free(br);
  return ret;
}

#ifdef CONFIG_NET_RPMSG_DRV_SERVER
/****************************************************************************
 * Name: net_rpmsg_drv_server_init
 ****************************************************************************/

int net_rpmsg_drv_server_init(void)
{
  return rpmsg_register_callback(NULL,
                                 NULL,
                                 NULL,
                                 net_rpmsg_drv_ns_match,
                                 net_rpmsg_drv_ns_bind);
}
#endif
