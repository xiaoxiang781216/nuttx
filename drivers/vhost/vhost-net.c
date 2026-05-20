/****************************************************************************
 * drivers/vhost/vhost-net.c
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

#include <debug.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include <nuttx/compiler.h>
#include <nuttx/kmalloc.h>
#include <nuttx/mm/iob.h>
#include <nuttx/net/ip.h>
#include <nuttx/net/netdev_lowerhalf.h>
#include <nuttx/percpu.h>
#include <nuttx/spinlock.h>
#include <nuttx/vhost/vhost.h>
#include <nuttx/virtio/virtio-net.h>
#include <nuttx/virtio/virtio.h>

#include "vhost-net.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define VHOST_NET_TX      0
#define VHOST_NET_RX      1
#define VHOST_NET_NUM     2

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct vhost_net_priv_s
{
  struct netdev_lowerhalf_s lower;
  spinlock_t                lock[VHOST_NET_NUM];
  FAR struct vhost_device  *hdev;
  int                       bufnum;
};

/* RX metadata stored inline in the VIRTIO_NET_HDRSIZE bytes preceding the
 * packet data.  It is overlaid on a (possibly unaligned) vring buffer, so
 * the packed attribute makes the compiler emit safe member accesses.
 */

begin_packed_struct struct vhost_net_llhdr_s
{
  FAR struct vhost_net_priv_s *priv;   /* Owning device */
  uint16_t                     idx;    /* vring head index */
} end_packed_struct;

static_assert(VIRTIO_NET_HDRSIZE >= sizeof(struct vhost_net_llhdr_s),
              "VIRTIO_NET_HDRSIZE too small for inline RX metadata");

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int vhost_net_ifup(FAR struct netdev_lowerhalf_s *dev);
static int vhost_net_ifdown(FAR struct netdev_lowerhalf_s *dev);
static int vhost_net_send(FAR struct netdev_lowerhalf_s *dev,
                          FAR netpkt_t *pkt);
static FAR netpkt_t *vhost_net_recv(FAR struct netdev_lowerhalf_s *dev);
#ifdef CONFIG_NET_MCASTGROUP
static int vhost_net_addmac(FAR struct netdev_lowerhalf_s *dev,
                            FAR const uint8_t *mac);
static int vhost_net_rmmac(FAR struct netdev_lowerhalf_s *dev,
                           FAR const uint8_t *mac);
#endif
#ifdef CONFIG_NETDEV_IOCTL
static int vhost_net_ioctl(FAR struct netdev_lowerhalf_s *dev,
                           int cmd, unsigned long arg);
#endif
#ifdef CONFIG_NET_NETPOLL
static void vhost_net_poll(FAR struct netdev_lowerhalf_s *dev,
                           enum netpkt_type_e type);
#endif

static int  vhost_net_probe(FAR struct vhost_device *hdev);
static void vhost_net_remove(FAR struct vhost_device *hdev);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static DEFINE_PER_CPU_BMP(struct vhost_driver, g_vhost_net_driver) =
{
  VIRTIO_ID_NETWORK,
  vhost_net_probe,
  vhost_net_remove,
};
#define g_vhost_net_driver this_cpu_var_bmp(g_vhost_net_driver)

static const struct netdev_ops_s g_vhost_net_ops =
{
  vhost_net_ifup,
  vhost_net_ifdown,
  vhost_net_send,
  vhost_net_recv,
#ifdef CONFIG_NET_MCASTGROUP
  vhost_net_addmac,
  vhost_net_rmmac,
#endif
#ifdef CONFIG_NETDEV_IOCTL
  vhost_net_ioctl,
#endif
#ifdef CONFIG_NET_NETPOLL
  vhost_net_poll,
#endif
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: vhost_net_rxfree
 ****************************************************************************/

static void vhost_net_rxfree(FAR void *data)
{
  FAR struct vhost_net_llhdr_s *hdr = (FAR struct vhost_net_llhdr_s *)
    ((FAR uint8_t *)data - VIRTIO_NET_HDRSIZE);
  FAR struct vhost_net_priv_s *priv = hdr->priv;
  FAR struct virtqueue *vq;
  irqstate_t flags;

  vq = priv->hdev->vrings_info[VHOST_NET_RX].vq;
  flags = spin_lock_irqsave(&priv->lock[VHOST_NET_RX]);
  virtqueue_add_consumed_buffer(vq, hdr->idx, 0);
  virtqueue_kick(vq);
  spin_unlock_irqrestore(&priv->lock[VHOST_NET_RX], flags);
}

/****************************************************************************
 * Name: vhost_net_alloc_rxpkt
 ****************************************************************************/

static FAR netpkt_t *
vhost_net_alloc_rxpkt(FAR struct vhost_net_priv_s *priv,
                      FAR struct virtqueue_buf *vb, size_t vbcnt,
                      uint16_t idx)
{
  FAR struct vhost_net_llhdr_s *hdr = vb[0].buf;
  FAR uint8_t *base;
  FAR netpkt_t *pkt;
  uint16_t len;

  DEBUGASSERT(vbcnt == 1);
  DEBUGASSERT(vb[0].len >= VIRTIO_NET_HDRSIZE);

  len  = vb[0].len - VIRTIO_NET_HDRSIZE;
  base = (FAR uint8_t *)vb[0].buf + VIRTIO_NET_HDRSIZE;

  /* Stash priv + idx inline in the virtio-net header region that sits
   * right before "base".  The free callback recovers them from there.
   */

  hdr->priv = priv;
  hdr->idx  = idx;

  pkt = iob_alloc_with_data(base, len, vhost_net_rxfree);
  if (pkt != NULL)
    {
      /* The network stack treats IOB_DATA(pkt) (i.e. io_data + io_offset)
       * as the start of the L3 header and expects the L2 (Ethernet) header
       * to sit in the NET_LL_HDRLEN bytes right before it.  "base" points
       * at the Ethernet header, so we must reserve NET_LL_HDRLEN bytes to
       * push io_offset past the L2 header:
       *
       *   io_data(base)        io_offset = NET_LL_HDRLEN
       *     |<-- L2 header -->|<-------- L3 payload -------->|
       *     ^ NETLLBUF         ^ IOB_DATA / IPBUF(0)
       *
       * io_data must stay equal to "base" because the iob free callback
       * receives io_data and reads the vhost_net_llhdr_s that precedes
       * it.  netpkt_setdatalen() then subtracts NET_LL_HDRLEN from "len"
       * to store the L3 payload length in io_pktlen, so the upper half
       * computes d_len correctly.
       */

      iob_reserve(pkt, NET_LL_HDRLEN(&priv->lower.netdev));
      netpkt_setdatalen(&priv->lower, pkt, len);
    }

  return pkt;
}

/****************************************************************************
 * Name: vhost_net_pkt2vb
 *
 * Description:
 *   Copy data from a netpkt into vring buffers (TX path).
 *   Writes a zeroed virtio-net header at the start of the first buffer.
 *
 ****************************************************************************/

static uint32_t vhost_net_pkt2vb(FAR struct netdev_lowerhalf_s *dev,
                                 FAR netpkt_t *pkt, uint32_t pktlen,
                                 FAR struct virtqueue_buf *vb,
                                 size_t vbcnt)
{
  FAR uint8_t *data;
  uint32_t total = 0;
  uint32_t copy;
  uint32_t len;
  size_t i;

  DEBUGASSERT(vb[0].len >= VIRTIO_NET_HDRSIZE);
  memset(vb[0].buf, 0, VIRTIO_NET_HDRSIZE);

  for (i = 0; i < vbcnt && total < pktlen; i++)
    {
      if (i == 0)
        {
          data = (FAR uint8_t *)vb[0].buf + VIRTIO_NET_HDRSIZE;
          len  = vb[0].len - VIRTIO_NET_HDRSIZE;
        }
      else
        {
          data = vb[i].buf;
          len  = vb[i].len;
        }

      copy = pktlen - total;
      if (copy > len)
        {
          copy = len;
        }

      DEBUGVERIFY(netpkt_copyout(dev, data, pkt, copy, total));
      total += copy;
    }

  return total;
}

/****************************************************************************
 * Name: vhost_net_poll
 ****************************************************************************/

#ifdef CONFIG_NET_NETPOLL
static void vhost_net_poll(FAR struct netdev_lowerhalf_s *dev,
                           enum netpkt_type_e type)
{
  FAR struct vhost_net_priv_s *priv = (FAR struct vhost_net_priv_s *)dev;
  FAR struct virtqueue *vq;

  /* RX is self-contained: vhost_net_recv() pulls buffers off the RX
   * virtqueue directly and does not depend on the RX ISR, so nothing to
   * do for the RX direction here.
   */

  if (type == NETPKT_RX)
    {
      return;
    }

  vq = priv->hdev->vrings_info[VHOST_NET_TX].vq;

  while (virtqueue_navail(vq) == 0);
}
#endif

/****************************************************************************
 * Name: vhost_net_ifup
 ****************************************************************************/

static int vhost_net_ifup(FAR struct netdev_lowerhalf_s *dev)
{
  FAR struct vhost_net_priv_s *priv = (FAR struct vhost_net_priv_s *)dev;

#ifdef CONFIG_NET_IPv4
  vhostinfo("Bringing up: %u.%u.%u.%u\n",
            ip4_addr1(dev->netdev.d_ipaddr),
            ip4_addr2(dev->netdev.d_ipaddr),
            ip4_addr3(dev->netdev.d_ipaddr),
            ip4_addr4(dev->netdev.d_ipaddr));
#endif
#ifdef CONFIG_NET_IPv6
  vhostinfo("Bringing up: %04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x\n",
            dev->netdev.d_ipv6addr[0], dev->netdev.d_ipv6addr[1],
            dev->netdev.d_ipv6addr[2], dev->netdev.d_ipv6addr[3],
            dev->netdev.d_ipv6addr[4], dev->netdev.d_ipv6addr[5],
            dev->netdev.d_ipv6addr[6], dev->netdev.d_ipv6addr[7]);
#endif

  virtqueue_enable_cb_lock(priv->hdev->vrings_info[VHOST_NET_RX].vq,
                           &priv->lock[VHOST_NET_RX]);

  netdev_lower_carrier_on(dev);
  return OK;
}

/****************************************************************************
 * Name: vhost_net_ifdown
 ****************************************************************************/

static int vhost_net_ifdown(FAR struct netdev_lowerhalf_s *dev)
{
  FAR struct vhost_net_priv_s *priv = (FAR struct vhost_net_priv_s *)dev;
  int i;

  for (i = 0; i < VHOST_NET_NUM; i++)
    {
      virtqueue_disable_cb_lock(priv->hdev->vrings_info[i].vq,
                                &priv->lock[i]);
    }

  netdev_lower_carrier_off(dev);
  return OK;
}

/****************************************************************************
 * Name: vhost_net_send
 *
 * Description:
 *   Copy netpkt data into a vring buffer and notify the virtio side.
 *   The netpkt is queued in txpend and freed later when the virtio
 *   side refills the avail ring.
 *
 ****************************************************************************/

static int vhost_net_send(FAR struct netdev_lowerhalf_s *dev,
                          FAR netpkt_t *pkt)
{
  FAR struct vhost_net_priv_s *priv = (FAR struct vhost_net_priv_s *)dev;
  FAR struct virtqueue *vq = priv->hdev->vrings_info[VHOST_NET_TX].vq;
  FAR spinlock_t *lock = &priv->lock[VHOST_NET_TX];
  struct virtqueue_buf vb[VIRTIO_NET_MAX_NIOB + 1];
  irqstate_t flags;
  uint32_t pktlen;
  uint32_t ret;
  size_t vbcnt;
  ssize_t idx;

  pktlen = netpkt_getdatalen(dev, pkt);
  if (pktlen == 0)
    {
      vhosterr("Invalid zero-length packet\n");
      return -EINVAL;
    }

  flags = spin_lock_irqsave(lock);
  idx = vhost_get_vq_buffers(vq, vb, VIRTIO_NET_MAX_NIOB + 1, &vbcnt);
  if (idx < 0)
    {
      virtqueue_enable_cb(vq);
      idx = vhost_get_vq_buffers(vq, vb, VIRTIO_NET_MAX_NIOB + 1,
                                 &vbcnt);
      if (idx < 0)
        {
          spin_unlock_irqrestore(lock, flags);
          return -EBUSY;
        }

      virtqueue_disable_cb(vq);
    }

  spin_unlock_irqrestore(lock, flags);

  ret = vhost_net_pkt2vb(dev, pkt, pktlen, vb, vbcnt);

  flags = spin_lock_irqsave(lock);
  virtqueue_add_consumed_buffer(vq, idx, VIRTIO_NET_HDRSIZE + ret);
  virtqueue_kick(vq);
  spin_unlock_irqrestore(lock, flags);

  netpkt_free(dev, pkt, NETPKT_TX);

  /* Backpressure is handled at the top of this function: when the TX vring
   * has no available buffer, vhost_get_vq_buffers() fails, we arm the TX
   * callback and return -EBUSY so the upper half pauses until the peer
   * refills the ring.
   */

  return OK;
}

/****************************************************************************
 * Name: vhost_net_recv
 *
 * Description:
 *   Receive a packet from the virtio side using zero-copy. Constructs
 *   an IOB chain directly from vring buffers. The vring buffer is
 *   returned to the peer only after all IOBs in the chain are freed.
 *
 ****************************************************************************/

static FAR netpkt_t *vhost_net_recv(FAR struct netdev_lowerhalf_s *dev)
{
  FAR struct vhost_net_priv_s *priv = (FAR struct vhost_net_priv_s *)dev;
  FAR struct virtqueue *vq = priv->hdev->vrings_info[VHOST_NET_RX].vq;
  FAR spinlock_t *lock = &priv->lock[VHOST_NET_RX];
  struct virtqueue_buf vb[VIRTIO_NET_MAX_NIOB + 1];
  irqstate_t flags;
  size_t vbcnt;
  ssize_t idx;

  /* Zero-copy RX does not consume IOBs from the pool: the payload lives in
   * the shared vring buffer and only a wrapping IOB structure is allocated.
   * The RX quota (which tracks pool IOB usage) therefore does not apply
   * here, and the real backpressure is whether the vring still has buffers
   * filled by the peer (handled by vhost_get_vq_buffers below).
   */

  flags = spin_lock_irqsave(lock);
  idx = vhost_get_vq_buffers(vq, vb, VIRTIO_NET_MAX_NIOB + 1, &vbcnt);
  if (idx < 0)
    {
      virtqueue_enable_cb(vq);
      idx = vhost_get_vq_buffers(vq, vb, VIRTIO_NET_MAX_NIOB + 1,
                                 &vbcnt);
      if (idx < 0)
        {
          spin_unlock_irqrestore(lock, flags);
          return NULL;
        }

      virtqueue_disable_cb(vq);
    }

  spin_unlock_irqrestore(lock, flags);

  return vhost_net_alloc_rxpkt(priv, vb, vbcnt, idx);
}

#ifdef CONFIG_NET_MCASTGROUP

/****************************************************************************
 * Name: vhost_net_addmac
 ****************************************************************************/

static int vhost_net_addmac(FAR struct netdev_lowerhalf_s *dev,
                            FAR const uint8_t *mac)
{
  return -ENOSYS;
}

/****************************************************************************
 * Name: vhost_net_rmmac
 ****************************************************************************/

static int vhost_net_rmmac(FAR struct netdev_lowerhalf_s *dev,
                           FAR const uint8_t *mac)
{
  return -ENOSYS;
}
#endif

#ifdef CONFIG_NETDEV_IOCTL

/****************************************************************************
 * Name: vhost_net_ioctl
 ****************************************************************************/

static int vhost_net_ioctl(FAR struct netdev_lowerhalf_s *dev,
                           int cmd, unsigned long arg)
{
  return -ENOTTY;
}
#endif

/****************************************************************************
 * Name: vhost_net_set_macaddr
 ****************************************************************************/

static void vhost_net_set_macaddr(FAR struct vhost_net_priv_s *priv)
{
  FAR struct vhost_device *hdev = priv->hdev;
  FAR uint8_t *mac = priv->lower.netdev.d_mac.ether.ether_addr_octet;

  if (vhost_has_feature(hdev, VIRTIO_NET_F_MAC))
    {
      vhost_read_config(hdev, offsetof(struct virtio_net_config_s, mac),
                        mac, IFHWADDRLEN);
    }
  else
    {
      arc4random_buf(mac, IFHWADDRLEN);
      mac[0] &= 0xfe;
      mac[0] |= 0x02;
    }
}

/****************************************************************************
 * Name: vhost_net_rxready
 ****************************************************************************/

static void vhost_net_rxready(FAR struct virtqueue *vq)
{
  FAR struct vhost_net_priv_s *priv = vq->vq_dev->priv;

  virtqueue_disable_cb_lock(vq, &priv->lock[VHOST_NET_RX]);
  netdev_lower_rxready(&priv->lower);
}

/****************************************************************************
 * Name: vhost_net_txdone
 ****************************************************************************/

static void vhost_net_txdone(FAR struct virtqueue *vq)
{
  FAR struct vhost_net_priv_s *priv = vq->vq_dev->priv;

  virtqueue_disable_cb_lock(vq, &priv->lock[VHOST_NET_TX]);
  netdev_lower_txdone(&priv->lower);
}

/****************************************************************************
 * Name: vhost_net_init
 ****************************************************************************/

static int vhost_net_init(FAR struct vhost_net_priv_s *priv,
                          FAR struct vhost_device *hdev)
{
  FAR const char *vqnames[VHOST_NET_NUM];
  vq_callback callbacks[VHOST_NET_NUM];
  uint64_t features;
  int ret;

  spin_lock_init(&priv->lock[VHOST_NET_RX]);
  spin_lock_init(&priv->lock[VHOST_NET_TX]);
  priv->hdev = hdev;
  hdev->priv = priv;

  vhost_get_features(hdev, &features);

  /* Current implementation requires ANY_LAYOUT feature to placed
   * the header and data in the same IO buffer.
   */

  DEBUGASSERT(vhost_has_feature(hdev, VIRTIO_F_ANY_LAYOUT));

  vhost_net_set_macaddr(priv);

  vqnames[VHOST_NET_RX] = "vhost_net_rx";
  vqnames[VHOST_NET_TX] = "vhost_net_tx";
  callbacks[VHOST_NET_RX] = vhost_net_rxready;
  callbacks[VHOST_NET_TX] = vhost_net_txdone;

  ret = vhost_create_virtqueues(hdev, 0, VHOST_NET_NUM,
                                vqnames, callbacks, NULL);
  if (ret < 0)
    {
      vhosterr("Failed to create virtqueues: %d\n", ret);
      return ret;
    }

  priv->bufnum = virtio_net_calc_bufnum(
      hdev->vrings_info[VHOST_NET_RX].info.num_descs,
      hdev->vrings_info[VHOST_NET_TX].info.num_descs);

  return OK;
}

/****************************************************************************
 * Name: vhost_net_probe
 ****************************************************************************/

static int vhost_net_probe(FAR struct vhost_device *hdev)
{
  FAR struct vhost_net_priv_s *priv;
  int ret;

  priv = kmm_zalloc(sizeof(*priv));
  if (priv == NULL)
    {
      vhosterr("Failed to allocate vhost-net private data\n");
      return -ENOMEM;
    }

  ret = vhost_net_init(priv, hdev);
  if (ret < 0)
    {
      vhosterr("vhost_net_init failed, ret=%d\n", ret);
      goto err_with_priv;
    }

  priv->lower.ops = &g_vhost_net_ops;
  ret = netdev_lower_register(&priv->lower, NET_LL_ETHERNET);
  if (ret < 0)
    {
      vhosterr("Failed to register net device\n");
      goto err_with_vq;
    }

  vhostinfo("probe: bufnum=%d, %s registered\n",
            priv->bufnum, priv->lower.netdev.d_ifname);
  return OK;

err_with_vq:
  vhost_delete_virtqueues(hdev);
err_with_priv:
  kmm_free(priv);
  return ret;
}

/****************************************************************************
 * Name: vhost_net_remove
 ****************************************************************************/

static void vhost_net_remove(FAR struct vhost_device *hdev)
{
  FAR struct vhost_net_priv_s *priv = hdev->priv;

  netdev_lower_unregister(&priv->lower);
  vhost_delete_virtqueues(hdev);
  kmm_free(priv);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: vhost_register_net_driver
 ****************************************************************************/

int vhost_register_net_driver(void)
{
  return vhost_register_driver(&g_vhost_net_driver);
}
