/****************************************************************************
 * net/netdev/netdev_iob.c
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

#include <nuttx/net/netdev.h>

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: netdev_iob_next
 *
 * Description:
 *   Per-chain continuation allocator installed on driver-supplied IOBs.
 *   Adapts the device d_alloc callback to the iob_s io_alloc signature
 *   (FAR struct iob_s *(*)(FAR void *arg, uint16_t size, bool throttled,
 *   unsigned int timeout)) so that the generic IOB data operations
 *   (iob_copyin, iob_clone_partial, iob_update_pktlen) can grow a
 *   driver-owned chain from the same allocator without any knowledge of the
 *   network device.  Returns the raw driver IOB; the generic helper resets
 *   io_offset/io_len/io_pktlen and propagates the allocator.
 *
 ****************************************************************************/

#if CONFIG_IOB_EXTRA_HEADS > 0
static FAR struct iob_s *netdev_iob_next(FAR void *arg, uint16_t size,
                                         bool throttled,
                                         unsigned int timeout)
{
  FAR struct net_driver_s *dev = arg;

  /* Forward the caller's allocation policy to the driver allocator so a
   * continuation buffer can block or draw from the throttled reserve just
   * like the head buffer.
   */

  return dev->d_alloc(dev, size, throttled, timeout);
}
#endif

/****************************************************************************
 * Name: netdev_iob_timedalloc
 *
 * Description:
 *   Allocate and initialize a single IOB for a given NIC, waiting up to the
 *   caller-specified timeout.  This is essentially the allocation core of
 *   netdev_iob_prepare(), but it returns the prepared IOB to the caller
 *   WITHOUT assigning dev->d_iob or dev->d_buf.
 *
 *   The function dispatches the allocation to the proper source:
 *
 *     - When the device supplies its own allocator (d_alloc, e.g. a
 *       DMA-capable / zero-copy NIC) the head IOB is obtained from the
 *       driver and a per-chain continuation allocator is installed so that
 *       every buffer of the chain originates from the same source.  This
 *       path only exists when CONFIG_IOB_EXTRA_HEADS > 0.
 *     - Otherwise the IOB is drawn from the global pool, retrying against
 *       the throttled reserve when requested.
 *
 *   The returned IOB has CONFIG_NET_LL_GUARDSIZE reserved at its head and is
 *   owned by the device.
 *
 * Input Parameters:
 *   dev       - The device requesting the buffer.
 *   size      - Requested total buffer capacity in bytes, including the
 *               L2 guard.
 *   throttled - Draw from the throttled reserve when true (global pool).
 *   timeout   - Allocation timeout in milliseconds (0 = no wait,
 *               UINT_MAX = wait forever), matching conn_dev_iobtimedalloc.
 *
 * Assumptions:
 *   The caller has locked the network.
 *
 * Returned Value:
 *   A prepared FAR struct iob_s * on success, or NULL on allocation failure.
 *
 ****************************************************************************/

FAR struct iob_s *netdev_iob_timedalloc(FAR struct net_driver_s *dev,
                                        uint16_t size, bool throttled,
                                        unsigned int timeout)
{
  FAR struct iob_s *iob = NULL;

#if CONFIG_IOB_EXTRA_HEADS > 0
  if (dev->d_alloc != NULL)
    {
      /* Driver supplies the IOB (e.g. DMA-capable / zero-copy).  Call it
       * exactly once and never fall back to the global allocator: a NULL
       * return is a hard failure handled below.
       */

      iob = dev->d_alloc(dev, size, throttled, timeout);
      if (iob != NULL)
        {
          /* Install the per-chain continuation allocator so that any later
           * growth of this chain by the generic IOB data operations (copyin,
           * clone, update_pktlen) draws from the same driver source as the
           * head buffer.
           */

          iob->io_alloc = netdev_iob_next;
          iob->io_arg   = dev;
        }
    }
#endif

  if (iob == NULL)
    {
      iob = iob_timedalloc_with_size(size, false, timeout);
      if (iob == NULL && throttled)
        {
          iob = iob_timedalloc_with_size(size, true, timeout);
        }

      if (iob == NULL)
        {
          nwarn("WARNING: IOB alloc failed for dev %s!\n", dev->d_ifname);
          return NULL;
        }

      /* The head came from the global pool, so leave io_alloc NULL (already
       * cleared by iob_alloc_internal) and the chain continues to extend
       * from the global pool.
       */
    }

  /* Update l2 guard size and assign ownership to the device */

  iob_reserve(iob, CONFIG_NET_LL_GUARDSIZE);
  IOB_SET_OWNER(iob, "%s", dev->d_ifname);

  return iob;
}

/****************************************************************************
 * Name: netdev_iob_prepare
 *
 * Description:
 *   Ensure dev->d_iob is ready for a new packet, obtaining the buffer
 *   through netdev_iob_timedalloc() so a driver-supplied d_alloc (e.g.
 *   DMA-capable / zero-copy memory) is honored when present.
 *
 *   A new buffer is allocated and installed (replacing any previous one)
 *   when dev->d_iob is NULL, or when the device provides a d_alloc
 *   callback but the current head did not originate from it (so the head
 *   always ends up coming from the driver).  When the current head already
 *   originates from d_alloc it is reused as-is.  The installed IOB has
 *   CONFIG_NET_LL_GUARDSIZE reserved at its head and dev->d_buf is set to
 *   the L2 start:
 *  ----------------------------------------------------------------
 *  |                     iob entry                                |
 *  ---------------------------------------------------------------|
 *  |<--- CONFIG_NET_LL_GUARDSIZE -->|<--- io_len/io_pktlen(0) --->|
 *  ---------------------------------------------------------------|
 *
 * Assumptions:
 *   The caller has locked the network.
 *
 * Returned Value:
 *   OK on success, or a negative errno value on allocation failure.
 *
 ****************************************************************************/

int netdev_iob_prepare(FAR struct net_driver_s *dev, bool throttled,
                       unsigned int timeout)
{
  /* Prepare iob buffer.
   *
   * A new buffer must be installed when there is none, or when the device
   * supplies its own allocator (d_alloc) but the current head did NOT
   * come from it (io_alloc == NULL, i.e. a leftover global-pool buffer such
   * as one installed by netdev_iob_pool_prepare on the RX path).  A DMA /
   * zero-copy device can only transmit from its own buffers, so such a head
   * has to be replaced.
   *
   * When the current head already originates from d_alloc (io_alloc != NULL)
   * it is reused as-is, avoiding a needless free/realloc of a perfectly good
   * driver buffer.
   */

  if (dev->d_iob == NULL
#if CONFIG_IOB_EXTRA_HEADS > 0
      || (dev->d_alloc != NULL && dev->d_iob->io_alloc == NULL)
#endif
     )
    {
      FAR struct iob_s *iob = netdev_iob_timedalloc(dev, CONFIG_IOB_BUFSIZE,
                                                    throttled, timeout);
      if (iob == NULL)
        {
          nwarn("WARNING: IOB Prepare failed for dev %s!\n", dev->d_ifname);
          return -ENOMEM;
        }

      netdev_iob_replace(dev, iob);
    }
  else
    {
      /* Reusing an existing buffer: re-establish the L2 guard headroom.
       *
       * A reused head may have lost its headroom (io_offset == 0) after an
       * in-place consume such as iob_trimhead() on the RX/recv path (which
       * resets io_offset to 0 when the whole chain is trimmed away).  A
       * freshly allocated buffer already carries CONFIG_NET_LL_GUARDSIZE of
       * headroom, so this only matters on the reuse path.  Without restoring
       * it, prepending the L2 header (NETLLBUF = IOB_DATA - NET_LL_HDRLEN)
       * would underflow past io_data into the iob struct header and corrupt
       * io_flink, later crashing iob_add_queue() while walking the chain.
       */

      iob_reserve(dev->d_iob, CONFIG_NET_LL_GUARDSIZE);
    }

  /* Set the device buffer to l2 */

  dev->d_buf = NETLLBUF;

  return OK;
}

/****************************************************************************
 * Name: netdev_iob_prepare_with_size
 *
 * Description:
 *   Pre-alloc the iob for the data to be sent with specified size.
 *
 * Assumptions:
 *   The caller has locked the network.
 *
 ****************************************************************************/

void netdev_iob_prepare_with_size(FAR struct net_driver_s *dev,
                                  uint16_t size)
{
  FAR struct iob_s *iob;
  size += CONFIG_NET_LL_GUARDSIZE;

  if (dev->d_iob && size <= IOB_BUFSIZE(dev->d_iob))
    {
      return;
    }

  /* alloc new iob for jumbo frame */

  iob = netdev_iob_alloc(dev, size, true);
  if (iob == NULL)
    {
      nerr("ERROR: Failed to allocate an I/O buffer.");
      return;
    }

  netdev_iob_replace(dev, iob);
}

/****************************************************************************
 * Name: netdev_iob_replace
 *
 * Description:
 *   Replace buffer resources for a given NIC, used by net stack for L3/L4
 * and set d_buf to l2 (for legacy drivers using d_buf).
 *
 * Assumptions:
 *   The caller has locked the network and new iob is prepared with
 *   l2 gruard size as offset.
 *
 ****************************************************************************/

void netdev_iob_replace(FAR struct net_driver_s *dev, FAR struct iob_s *iob)
{
  /* Release previous buffer */

  netdev_iob_release(dev);

  /* Set new buffer */

  dev->d_iob = iob;
  dev->d_buf = NETLLBUF;
  dev->d_len = iob->io_pktlen;
  IOB_SET_OWNER(iob, "%s", dev->d_ifname);
}

/****************************************************************************
 * Name: netdev_iob_replace_l2
 *
 * Description:
 *   Replace buffer resources for a given NIC, used by L2 (net drivers) and
 * set d_len to l2, keep d_buf as NULL.
 *
 * Assumptions:
 *   The caller has locked the network and new iob is prepared with
 *   l2 gruard size as offset.
 *
 ****************************************************************************/

void netdev_iob_replace_l2(FAR struct net_driver_s *dev,
                           FAR struct iob_s *iob)
{
  /* Release previous buffer */

  netdev_iob_release(dev);

  /* Set new buffer */

  dev->d_iob = iob;
  dev->d_len = iob->io_pktlen + NET_LL_HDRLEN(dev);
  IOB_SET_OWNER(iob, "%s", dev->d_ifname);
}

/****************************************************************************
 * Name: netdev_iob_clear
 *
 * Description:
 *   Clean up buffer resources for a given NIC
 *
 * Assumptions:
 *   The caller has locked the network and dev->d_iob has been
 *   released or taken away.
 *
 ****************************************************************************/

void netdev_iob_clear(FAR struct net_driver_s *dev)
{
  /* Clear the device buffer */

  dev->d_iob = NULL;
  dev->d_buf = NULL;
  dev->d_len = 0;
}

/****************************************************************************
 * Name: netdev_iob_release
 *
 * Description:
 *   Release buffer resources for a given NIC
 *
 * Assumptions:
 *   The caller has locked the network.
 *
 ****************************************************************************/

void netdev_iob_release(FAR struct net_driver_s *dev)
{
  /* Release device buffer */

  if (dev->d_iob != NULL)
    {
      iob_free_chain(dev->d_iob);
      dev->d_iob = NULL;
    }

  dev->d_buf = NULL;
}

/****************************************************************************
 * Name: netdev_iob_clone
 *
 * Description:
 *   Backup the current iob buffer for a given NIC by cloning it.
 *
 * Assumptions:
 *   The caller has locked the network.
 *
 ****************************************************************************/

FAR struct iob_s *netdev_iob_clone(FAR struct net_driver_s *dev,
                                   bool throttled)
{
  FAR struct iob_s *iob;
  int ret;

  iob = iob_tryalloc_with_size(IOB_BUFSIZE(dev->d_iob), throttled);
  if (iob == NULL)
    {
      nwarn("WARNING: IOB alloc failed for dev %s!\n", dev->d_ifname);
      return NULL;
    }

  iob_reserve(iob, CONFIG_NET_LL_GUARDSIZE);
  ret = iob_clone_partial(dev->d_iob, dev->d_iob->io_pktlen, 0,
                          iob, 0, throttled, false);
  if (ret < 0)
    {
      iob_free_chain(iob);
      nwarn("WARNING: IOB clone failed for dev %s, ret=%d!\n",
            dev->d_ifname, ret);
      return NULL;
    }

  return iob;
}
