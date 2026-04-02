/****************************************************************************
 * net/netforward/ieee1722acf_send.c
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

#include <string.h>
#include <assert.h>
#include <debug.h>
#include <errno.h>
#include <fcntl.h>

#include <nuttx/net/cangw.h>
#include <nuttx/net/ieee1722.h>
#include <nuttx/net/netstats.h>

#include "netdev/netdev.h"
#include "devif/devif.h"
#include "utils/utils.h"
#include "netforward/ieee1722acf.h"
#include "netforward/netforward.h"

#if defined(CONFIG_NET_TIMESTAMP) && defined(CONFIG_PTP_CLOCK)
#include <nuttx/timers/ptp_clock.h>
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MAX_1722ACF_STREAMS 128

/****************************************************************************
 * Private Data
 ****************************************************************************/

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: ieee1722acf_stream_gettime
 *
 * Description:
 *   This function is used to get the PTP time for acf stream packet
 *   timestamping.
 *
 * Input Parameters:
 *   acf_stream - The acf stream structure
 *   tv         - The location to return the PTP time
 *
 * Returned Value:
 *   Zero is returned on success; A negated errno value is returned on any
 *   failure.
 *
 ****************************************************************************/

#ifdef CONFIG_NET_TIMESTAMP
static inline_function int
ieee1722acf_stream_gettime(FAR struct ieee1722acf_stream_s *acf_stream,
                           FAR struct timespec *tv)
{
  int ret = -EINVAL;

#ifdef CONFIG_PTP_CLOCK
  if (acf_stream->ptp_filep.f_inode != NULL)
    {
      /* Get PTP time */

      ret = file_ioctl(&acf_stream->ptp_filep, PTP_CLOCK_GETTIME, tv);
    }
#else
  ret = clock_gettime(CLOCK_REALTIME, tv);
#endif

  return ret;
}
#endif

/****************************************************************************
 * Name: ieee1722acf_dev_send
 *
 * Description:
 *   This function is called from ieee1722acf_send when it is
 *   necessary to send the acf packet to the target device.  In this case,
 *   the forwarding operation must be performed asynchronously when the TX
 *   poll is received from the forwarding device.
 *
 * Input Parameters:
 *   dev        - The device on which the packet was received.
 *   stream     - The acf stream which contains the packet
 *                and the target forwarding dev.
 *
 * Returned Value:
 *   Zero is returned if the packet was successfully forward;  A negated
 *   errno value is returned if the packet is not forwardable. In that
 *   latter case, the caller should drop the packet.
 *
 ****************************************************************************/

static int ieee1722acf_dev_send(FAR struct net_driver_s *dev,
                                FAR struct ieee1722acf_stream_s *stream,
                                FAR struct net_driver_s *fwddev)
{
  FAR struct forward_s *fwd;
  int ret;

  /* If the interface isn't "Running", we can't forward. */

  if (!IFF_IS_RUNNING(fwddev->d_flags))
    {
      nwarn("WARNING: device is DOWN\n");
      ret = -EHOSTUNREACH;
      goto errout;
    }

  if (IFF_IS_NODST_FORWARD(fwddev->d_flags))
    {
      nwarn("WARNING: CAN2ETH1722 forwarding disabled"
            " on destination device %s\n", fwddev->d_ifname);
      ret = -EOPNOTSUPP;
      goto errout;
    }

  /* Get a pre-allocated forwarding structure,  This structure will be
   * completely zeroed when we receive it.
   */

  fwd = netfwd_alloc();
  if (fwd == NULL)
    {
      nwarn("WARNING: Failed to allocate forwarding structure\n");
      ret = -ENOMEM;
      goto errout;
    }

  /* Initialize the easy stuff in the forwarding structure */

  fwd->f_dev = fwddev;     /* Forwarding device */
  fwd->f_domain = PF_CAN;  /* CAN domain */

  /* Relay the device buffer */

  fwd->f_iob = stream->iob;  /* IOB chain to be forwarded */

  stream->iob = NULL;        /* Free acf stream buffer */
  stream->stream_seq++;      /* Increase sequence number */

  /* Then set up to forward the packet according to the protocol. */

  ret = netfwd_forward(dev, fwd);
  if (ret >= 0)
    {
#ifdef CONFIG_NET_STATISTICS
      g_netstats.cangw.acf_sent++;
#endif
      return OK;
    }

  if (fwd->f_iob != NULL)
    {
      iob_free_chain(fwd->f_iob);
    }

  if (fwd != NULL)
    {
      netfwd_free(fwd);
    }

errout:
#ifdef CONFIG_NET_STATISTICS
  g_netstats.cangw.acf_drop++;
#endif
  return ret;
}

/****************************************************************************
 * Name: ieee1722acf_update_header
 *
 * Description:
 *   This function will set the acf can msg common header infos
 *   based on the source can msg.
 *
 * Input Parameters:
 *   src_msg    - source can message
 *   dest_hdr   - pointer of target acf can msg header
 *   dest_canif - pointer to save acf can message id
 *   msg_len_bytes - acf msg lenth in bytes, include common hdr & data
 *
 ****************************************************************************/

static inline_function uint8_t
ieee1722acf_update_header(FAR void *src_msg,
                          FAR struct ieee1722acf_msg_hdr_s *msg_hdr,
                          FAR struct ieee1722acf_can_common_hdr_s *can_hdr,
                          FAR canid_t *canid,
                          uint8_t msg_type)
{
#ifdef CONFIG_NET_CAN_CANFD
  FAR struct canfd_frame *msg = (struct canfd_frame *)src_msg;
  uint8_t data_len = msg->len;
#else
  FAR struct can_frame *msg = (struct can_frame *)src_msg;
  uint8_t data_len = msg->can_dlc;
#endif
  uint16_t aligned_len;

  can_hdr->esi = (msg->flags & CANFD_ESI) ? 1 : 0;
#ifdef CONFIG_NET_CAN_CANFD
  can_hdr->brs = (msg->flags & CANFD_BRS) ? 1 : 0;
  can_hdr->fdf = (msg->flags & CANFD_FDF) ? 1 : 0;
#endif

  if (msg->can_id & CAN_EFF_FLAG)
    {
      can_hdr->eff = 1;
      *canid = msg->can_id & CAN_EFF_MASK;
    }
  else
    {
      can_hdr->eff = 0;
      *canid = msg->can_id & CAN_SFF_MASK;
    }

  switch (msg_type)
  {
    case IEEE1722ACF_TYPE_CAN:
      data_len += sizeof(struct ieee1722acf_can_hdr_s);
      break;

    case IEEE1722ACF_TYPE_CAN_BRIEF:
    default:
      data_len += sizeof(struct ieee1722acf_canbrief_hdr_s);
      break;
  }

  aligned_len = div_round_up(data_len, IEEE1722ACF_MSG_LEN_ALIGN);

  can_hdr->padding = aligned_len * IEEE1722ACF_MSG_LEN_ALIGN - data_len;

  msg_hdr->acf_msg_lenth_h = aligned_len >> 8;
  msg_hdr->acf_msg_lenth_l = aligned_len;

  return data_len + can_hdr->padding;
}

/****************************************************************************
 * Name: ieee1722acf_update_packet_length
 *
 * Description:
 *   This function is to update the stream pkt length info
 *   in acf packet header.
 *
 * Input Parameters:
 *   stream - The acf stream which contains the packet.
 *
 ****************************************************************************/

static uint16_t
ieee1722acf_update_packet_length(FAR struct ieee1722acf_stream_s *stream)
{
  uint16_t payload_len;
  switch (stream->stream_type)
  {
    case IEEE1722_SUBTYPE_TSCF:
      {
        FAR struct ieee1722acf_tscf_hdr_s *packt_hdr =
        (FAR struct ieee1722acf_tscf_hdr_s *)IOB_DATA(stream->iob);
        payload_len = stream->iob->io_pktlen - sizeof(*packt_hdr);

#ifdef CONFIG_NET_TIMESTAMP
        struct timespec tv;
        if (ieee1722acf_stream_gettime(stream, &tv) == OK)
          {
            packt_hdr->time_valid = 1;
            packt_hdr->avtp_timestamp = HTONQ(tv.tv_sec * 1000000000ull +
                                              tv.tv_nsec);
          }
#endif

        packt_hdr->stream_data_length = HTONS(payload_len);
      }
    break;

    case IEEE1722_SUBTYPE_NTSCF:
    default:
      {
        FAR struct ieee1722acf_ntscf_hdr_s *packt_hdr =
        (FAR struct ieee1722acf_ntscf_hdr_s *)IOB_DATA(stream->iob);
        payload_len = stream->iob->io_pktlen - sizeof(*packt_hdr);
        packt_hdr->ntscf_data_length_h = payload_len >> 8;
        packt_hdr->ntscf_data_length_l = payload_len;
      }
    break;
  }

  return payload_len;
}

/****************************************************************************
 * Name: ieee1722acf_alloc_packet
 *
 * Description:
 *   This function is used to initialize a new pkt for acf stream.
 *   New iob alloc and acf stream packet header info update.
 *
 * Input Parameters:
 *   stream - The acf stream which contains the packet.
 *
 ****************************************************************************/

static FAR struct iob_s *
ieee1722acf_alloc_packet(FAR struct ieee1722acf_stream_s *stream,
                         FAR struct net_driver_s *dev)
{
  FAR struct eth_hdr_s *ethhdr;
  FAR struct iob_s *iob;
  int ret;

  iob = iob_tryalloc(true);
  if (iob == NULL)
    {
      nerr("ERROR: Failed to alloc iob buffer\n");
      return NULL;
    }

  iob_reserve(iob, CONFIG_NET_LL_GUARDSIZE);
  iob_update_pktlen(iob, 0, true);

  /* Set AVTP eth type */

  ethhdr = (FAR struct eth_hdr_s *)(IOB_DATA(iob) - NET_LL_HDRLEN(dev));
  ethhdr->type = HTONS(ETHTYPE_AVBTP);

  /* Set source mac */

  memcpy(ethhdr->src, dev->d_mac.ether.ether_addr_octet,
         sizeof(ethhdr->src));

  /* Set destination mac */

  memcpy(ethhdr->dest, stream->dest_mac, sizeof(ethhdr->src));

  /* Set stream header */

  switch (stream->stream_type)
  {
    case IEEE1722_SUBTYPE_TSCF:
      {
        struct ieee1722acf_tscf_hdr_s packt_hdr;

        memset(&packt_hdr, 0, sizeof(packt_hdr));
        packt_hdr.subtype = IEEE1722_SUBTYPE_TSCF;
        packt_hdr.stream_id_valid = 1;
        packt_hdr.sequence_num = stream->stream_seq;
        packt_hdr.stream_id = HTONQ(
            ((uint64_t)dev->d_mac.ether.ether_addr_octet[0] << 56) +
            ((uint64_t)dev->d_mac.ether.ether_addr_octet[1] << 48) +
            ((uint64_t)dev->d_mac.ether.ether_addr_octet[2] << 40) +
            ((uint64_t)dev->d_mac.ether.ether_addr_octet[3] << 32) +
            ((uint64_t)dev->d_mac.ether.ether_addr_octet[4] << 24) +
            ((uint64_t)dev->d_mac.ether.ether_addr_octet[5] << 16) +
            ((uint64_t)(stream->stream_id)));

        /* timestamp */

        ret = iob_trycopyin(iob, (FAR const uint8_t *)&packt_hdr,
                            sizeof(packt_hdr), 0, true);
        if (ret != sizeof(packt_hdr))
          {
            nerr("ERROR: Failed to copy data into iob buffer\n");
            goto errout;
          }
      }
    break;

    case IEEE1722_SUBTYPE_NTSCF:
    default:
      {
        struct ieee1722acf_ntscf_hdr_s packt_hdr;

        memset(&packt_hdr, 0, sizeof(packt_hdr));
        packt_hdr.subtype = IEEE1722_SUBTYPE_NTSCF;
        packt_hdr.stream_id_valid = 1;
        packt_hdr.sequence_num = stream->stream_seq;
        packt_hdr.stream_id = HTONQ(
            ((uint64_t)dev->d_mac.ether.ether_addr_octet[0] << 56) +
            ((uint64_t)dev->d_mac.ether.ether_addr_octet[1] << 48) +
            ((uint64_t)dev->d_mac.ether.ether_addr_octet[2] << 40) +
            ((uint64_t)dev->d_mac.ether.ether_addr_octet[3] << 32) +
            ((uint64_t)dev->d_mac.ether.ether_addr_octet[4] << 24) +
            ((uint64_t)dev->d_mac.ether.ether_addr_octet[5] << 16) +
            ((uint64_t)(stream->stream_id)));

        ret = iob_trycopyin(iob, (FAR const uint8_t *)&packt_hdr,
                            sizeof(packt_hdr), 0, true);
        if (ret != sizeof(packt_hdr))
          {
            nerr("ERROR: Failed to copy data into iob buffer\n");
            goto errout;
          }
      }
    break;
  }

  return iob;

errout:
  iob_free_chain(iob);
  return NULL;
}

/****************************************************************************
 * Name: ieee1722acf_stream_cycle_work
 *
 * Description:
 *   This function is used to trigger a new cyclical sending for acf stream.
 *
 * Input Parameters:
 *   arg - The acf stream which contains the packet.
 *
 ****************************************************************************/

static void ieee1722acf_stream_cycle_work(FAR void *arg)
{
  FAR struct ieee1722acf_stream_s *stream =
      (FAR struct ieee1722acf_stream_s *)arg;
  FAR struct net_driver_s *dev;
  int ret;

  DEBUGASSERT(stream != NULL && stream->priv != NULL);

  dev = (FAR struct net_driver_s *)stream->priv;

  netdev_lock(dev);

  if (IFF_IS_RUNNING(dev->d_flags) && stream->iob != NULL)
    {
      /* Update stream packet len */

      if (ieee1722acf_update_packet_length(stream) > 0)
        {
          ret = ieee1722acf_dev_send(dev, stream, dev);
          if (ret < 0)
            {
              nwarn("WARNING: stream send failed: %d\n", ret);
            }
        }

      work_queue_next(HPWORK, &stream->cycle_work,
                      ieee1722acf_stream_cycle_work,
                      stream, USEC2TICK(stream->cycle_us));
    }

  netdev_unlock(dev);
}

/****************************************************************************
 * Name: ieee1722acf_stream_flush
 *
 * Description:
 *   Flush the current acf stream packet: update packet length, send it
 *   out, bump sequence number, and cancel the cycle work if active.
 *
 ****************************************************************************/

static inline_function void
ieee1722acf_stream_flush(FAR struct net_driver_s *dev,
                         FAR struct ieee1722acf_stream_s *stream,
                         FAR struct net_driver_s *fwddev)
{
  unsigned int blcount;
  int ret;

  ieee1722acf_update_packet_length(stream);

  ret = ieee1722acf_dev_send(dev, stream, fwddev);
  if (ret < 0)
    {
      nwarn("WARNING: stream send failed: %d\n", ret);
    }

  if (stream->cycle_us != 0)
    {
      netdev_breaklock(fwddev, &blcount);
      work_cancel_sync(HPWORK, &stream->cycle_work);
      netdev_restorelock(fwddev, blcount);
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: del_1722acf_stream
 *
 * Description:
 *   Delete a acf stream structure from the list.
 *
 * Assumptions: Input steam pointer should not be null
 *              which is assured by caller.
 *
 ****************************************************************************/

void del_1722acf_stream(FAR struct net_driver_s *dev,
                        FAR struct ieee1722acf_stream_s *streams,
                        size_t num)
{
  int i;

  DEBUGASSERT(dev != NULL && streams != NULL);

  netdev_lock(dev);

  for (i = 0; i < num; i++)
    {
      sq_rem(&streams[i].node, &dev->d_1722acf_streams);
    }

  netdev_unlock(dev);

  for (i = 0; i < num; i++)
    {
      work_cancel_sync(HPWORK, &streams[i].cycle_work);
      iob_free_chain(streams[i].iob);
      streams[i].iob = NULL;
      streams[i].priv = NULL;

#if defined(CONFIG_NET_TIMESTAMP) && defined(CONFIG_PTP_CLOCK)

      /* Close PTP clock device */

      file_close(&streams[i].ptp_filep);
#endif
    }
}

/****************************************************************************
 * Name: add_1722acf_stream
 *
 * Description:
 *   Add a acf stream structure to the list.
 *
 * Assumptions: Input steam pointer should not be null
 *              which is assured by caller.
 *
 ****************************************************************************/

void add_1722acf_stream(FAR struct net_driver_s *dev,
                        FAR struct ieee1722acf_stream_s *streams,
                        size_t num)
{
  int i;
  DEBUGASSERT(dev != NULL && streams != NULL);

  if (num == 0 || num > MAX_1722ACF_STREAMS)
    {
      return;
    }

  netdev_lock(dev);

  for (i = 0; i < num; i++)
    {
#if defined(CONFIG_NET_TIMESTAMP) && defined(CONFIG_PTP_CLOCK)

      /* Open PTP clock device */

      char path[16];

      snprintf(path, sizeof(path), "/dev/ptp%d", streams[i].ptp_id);
      int ret = file_open(&streams[i].ptp_filep, path,
                          O_RDONLY | O_CLOEXEC);
      if (ret < 0)
        {
          nerr("ERROR: Failed to open PTP clock device %s: %d\n", path, ret);
        }
#endif

      streams[i].priv = dev;
      sq_addlast(&streams[i].node, &dev->d_1722acf_streams);
    }

  netdev_unlock(dev);
}

/****************************************************************************
 * Name: ieee1722acf_send
 *
 * Description:
 *   This function is called from can_forward when it is necessary to
 *   forward a packet from the current device to different device.  In this
 *   case, the forwarding operation must be performed asynchronously when
 *   the TX poll is received from the forwarding device.
 *
 * Input Parameters:
 *   dev      - The device on which the packet was received.
 *   fwdrule  - The forward target rule.
 *   can_iob  - The packet need to be forward.
 *
 * Returned Value:
 *   Zero is returned if the packet was successfully forward;  A negated
 *   errno value is returned if the packet is not forwardable.  In that
 *   latter case, the caller (can_input()) should drop the packet.
 *
 ****************************************************************************/

int ieee1722acf_send(FAR struct net_driver_s *dev,
                     FAR struct cangw_rule_s *fwdrule,
                     FAR struct iob_s *can_iob)
{
#ifdef CONFIG_NET_CAN_CANFD
  FAR struct canfd_frame *msg = (FAR struct canfd_frame *)IOB_DATA(can_iob);
  uint8_t data_len = msg->len;
#else
  FAR struct can_frame *msg = (FAR struct can_frame *)IOB_DATA(can_iob);
  uint8_t data_len = msg->can_dlc;
#endif
  FAR struct ieee1722acf_stream_s *stream;
  FAR struct iob_s *iob;
  FAR sq_entry_t *entry;
  uint16_t offset;
  uint8_t hdr_len;
  uint8_t msg_len;
  int ret = OK;

  DEBUGASSERT(fwdrule->dest_dev != NULL);

  netdev_lock(fwdrule->dest_dev);
  sq_for_every(&fwdrule->dest_dev->d_1722acf_streams, entry)
    {
      stream = (FAR struct ieee1722acf_stream_s *)entry;

      if (stream->index == fwdrule->gw.cegw.stream_index)
        {
          /* Check if the stream iob enough space for new can msg */

          if (stream->iob != NULL && stream->iob->io_pktlen + data_len +
              sizeof(struct ieee1722acf_can_hdr_s) +
              fwdrule->dest_dev->d_llhdrlen > CONFIG_NET_IEEE1722_PKTSIZE)
            {
              ieee1722acf_stream_flush(dev, stream, fwdrule->dest_dev);
            }

          /* Prepare Stream packet */

          if (stream->iob == NULL)
            {
              stream->msg_cnt = stream->msgcnt_limit;
              iob = ieee1722acf_alloc_packet(stream, fwdrule->dest_dev);
              if (iob == NULL)
                {
#ifdef CONFIG_NET_STATISTICS
                  g_netstats.cangw.drop++;
#endif
                  nerr("ERROR: Failed to prepare stream iob buffer\n");
                  ret = -ENOMEM;
                  goto errout;
                }

              stream->iob = iob;

              IOB_SET_OWNER(iob, "ieee1722acf send");
              if (stream->cycle_us != 0 &&
                  work_available(&stream->cycle_work))
                {
                  work_queue(HPWORK, &stream->cycle_work,
                             ieee1722acf_stream_cycle_work,
                             stream, USEC2TICK(stream->cycle_us));
                }
            }
          else
            {
              iob = stream->iob;
            }

          offset = iob->io_pktlen;

          /* Insert CAN msg to acf stream payload */

          /* Copy can hdr infos */

          switch (fwdrule->gw.cegw.acf_msg_type)
          {
            case IEEE1722ACF_TYPE_CAN:
              {
                struct ieee1722acf_can_hdr_s dest_hdr;
                canid_t can_id;

                memset(&dest_hdr, 0, sizeof(dest_hdr));
                msg_len = ieee1722acf_update_header(msg,
                                                    &dest_hdr.msg_hdr,
                                                    &dest_hdr.can_hdr,
                                                    &can_id,
                                                    IEEE1722ACF_TYPE_CAN);

#ifdef CONFIG_NET_TIMESTAMP

                /* If the source CAN IOB already carries a valid receive
                 * timestamp (e.g. from hardware RX timestamping via
                 * routing), reuse it directly.  Otherwise fall back to
                 * reading the stream's PTP / system clock.
                 */

#ifdef CONFIG_NET_IEEE1722ACF_CAN_RX_TIMESTAMP
                if (can_iob->io_time.tv_sec != 0 ||
                    can_iob->io_time.tv_nsec != 0)
                  {
                    dest_hdr.can_hdr.msg_time_valid = 1;
                    dest_hdr.msg_timestamp =
                        HTONQ(1000000000ull * can_iob->io_time.tv_sec +
                              can_iob->io_time.tv_nsec);
                  }
                else
#endif
                  {
                    struct timespec tv;
                    if (ieee1722acf_stream_gettime(stream, &tv) == OK)
                      {
                        dest_hdr.can_hdr.msg_time_valid = 1;
                        dest_hdr.msg_timestamp =
                            HTONQ(1000000000ull * tv.tv_sec + tv.tv_nsec);
                      }
                  }
#endif

                dest_hdr.canid = HTONL(can_id);
                dest_hdr.msg_hdr.acf_msg_type = IEEE1722ACF_TYPE_CAN;
                dest_hdr.can_hdr.can_bus_id = fwdrule->gw.cegw.can_bus_id;

                ret = iob_update_pktlen(iob, offset + msg_len, true);
                if (ret < offset + msg_len)
                  {
                    ret = -ENOMEM;
                    goto errout_trim_iob;
                  }

                hdr_len = iob_trycopyin(iob, (FAR const uint8_t *)&dest_hdr,
                                        sizeof(dest_hdr), offset, true);
                if (hdr_len != sizeof(dest_hdr))
                  {
                    ret = -ENOMEM;
                    goto errout_trim_iob;
                  }
              }
            break;

            case IEEE1722ACF_TYPE_CAN_BRIEF:
            default:
              {
                struct ieee1722acf_canbrief_hdr_s dest_hdr;
                canid_t can_id;

                memset(&dest_hdr, 0, sizeof(dest_hdr));
                msg_len = ieee1722acf_update_header(msg,
                                                    &dest_hdr.msg_hdr,
                                                    &dest_hdr.can_hdr,
                                                    &can_id,
                                                IEEE1722ACF_TYPE_CAN_BRIEF);

                dest_hdr.canid = HTONL(can_id);
                dest_hdr.msg_hdr.acf_msg_type = IEEE1722ACF_TYPE_CAN_BRIEF;
                dest_hdr.can_hdr.can_bus_id = fwdrule->gw.cegw.can_bus_id;

                ret = iob_update_pktlen(iob, offset + msg_len, true);
                if (ret < offset + msg_len)
                  {
                    ret = -ENOMEM;
                    goto errout_trim_iob;
                  }

                hdr_len = iob_trycopyin(iob, (FAR const uint8_t *)&dest_hdr,
                                        sizeof(dest_hdr), offset, true);

                if (hdr_len != sizeof(dest_hdr))
                  {
                    ret = -ENOMEM;
                    goto errout_trim_iob;
                  }
              }
            break;
          }

          /* Copy can msg data */

          ret = iob_trycopyin(iob, (FAR const uint8_t *)&msg->data,
                              data_len, offset + hdr_len, true);
          if (ret != data_len)
            {
              ret = -ENOMEM;
              goto errout_trim_iob;
            }

          /* Check CAN msg lenth */

          if (iob->io_pktlen > offset + msg_len)
            {
              ret = -EOVERFLOW;
              goto errout_trim_iob;
            }

#ifdef CONFIG_NET_STATISTICS
          g_netstats.cangw.forward++;
#endif

          if (stream->msg_cnt == 0)
            {
              break;
            }

          stream->msg_cnt--;

          /* Check if the sending of stream need to be trigger */

          if (stream->msg_cnt <= 0)
            {
              ieee1722acf_stream_flush(dev, stream, fwdrule->dest_dev);
            }

          break;
        }
    }

errout_trim_iob:
  if (ret < 0)
    {
      iob_trimtail(iob, iob->io_pktlen - offset);
#ifdef CONFIG_NET_STATISTICS
      g_netstats.cangw.drop++;
#endif
      nerr("ERROR: Failed to copy data into iob buffer\n");
    }

errout:
  netdev_unlock(fwdrule->dest_dev);
  return ret;
}
