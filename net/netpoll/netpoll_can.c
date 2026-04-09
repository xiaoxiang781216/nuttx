/****************************************************************************
 * net/netpoll/netpoll_can.c
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

#include <stddef.h>
#include <string.h>

#include <nuttx/net/netpoll.h>
#include <nuttx/net/can.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* `struct can_frame` and `struct canfd_frame` share the same 8-byte header
 * layout before data[]. netpoll reserves this many bytes at the front of
 * each packet via np->hdrlen so the header can be written in place
 * without touching the IOB guard space.
 */

#define CAN_HDR_SIZE offsetof(struct can_frame, data)

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int netpoll_can_pkt_send(FAR struct netpoll_s *np, FAR netpkt_t *pkt,
                                canid_t can_id)
{
  FAR struct netdev_lowerhalf_s *lower = np->lower;
#ifdef CONFIG_NET_CAN_CANFD
  FAR struct canfd_frame        *frame;
  uint8_t                        wirelen;
#else
  FAR struct can_frame          *frame;
#endif
  uint8_t                        dlc;
  int                            ret;

  /* netpoll_queue_copyin reserves CAN_HDR_SIZE bytes at the front of each
   * queue entry (np->hdrlen == CAN_HDR_SIZE). The payload then sits at
   * io_data[io_offset + CAN_HDR_SIZE], so the total content length is
   * hdrlen + payload and dlc is simply io_pktlen - CAN_HDR_SIZE.
   */

  dlc = pkt->io_pktlen - CAN_HDR_SIZE;

#ifdef CONFIG_NET_CAN_CANFD
  frame = (FAR struct canfd_frame *)(pkt->io_data + pkt->io_offset);
  frame->can_id = can_id;
  frame->len    = dlc;
  frame->flags  = CANFD_FDF;
  frame->__res0 = 0;
  frame->__res1 = 0;

  wirelen = can_dlc2bytes(can_bytes2dlc(dlc));
  if (wirelen > dlc)
    {
      memset(&frame->data[dlc], 0, wirelen - dlc);
    }

  pkt->io_len = sizeof(struct canfd_frame);
#else
  frame = (FAR struct can_frame *)(pkt->io_data + pkt->io_offset);
  frame->can_id  = can_id;
  frame->can_dlc = dlc;
  frame->__pad   = 0;
  frame->__res0  = 0;
  frame->__res1  = 0;

  pkt->io_len = sizeof(struct can_frame);
#endif

  NETDEV_TXPACKETS(&lower->netdev);
  for (; ; )
    {
      ret = lower->ops->transmit(lower, pkt);
      if (ret >= 0)
        {
          NETDEV_TXDONE(&lower->netdev);
          break;
        }

      lower->ops->txpoll(lower);
    }

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: netpoll_can_init
 *
 * Description:
 *   Initialize netpoll with CAN.
 *
 * Input Parameters:
 *   np      - pointer to netpoll data
 *   lower   - pointer to netdev lower
 *   srcport - CAN ID used as source identifier
 *
 * Returned Value:
 *   Returns OK on success
 *
 ****************************************************************************/

int netpoll_can_init(FAR struct netpoll_s *np,
                     FAR struct netdev_lowerhalf_s *lower,
                     uint16_t srcport)
{
  if (lower->ops->txpoll == NULL)
    {
      return -EINVAL;
    }

  memset(np, 0, sizeof(struct netpoll_s));

  /* Parameters */

  np->srcport = srcport;

  /* Reserve CAN_HDR_SIZE bytes at the front of each queue entry so the
   * frame header can be written in place at io_data[io_offset].  maxlen
   * is the per-frame payload capacity (8 for classic CAN, 64 for CAN-FD);
   * NETDEV_PKTSIZE equals sizeof(can_frame) or sizeof(canfd_frame).
   */

  np->hdrlen = CAN_HDR_SIZE;
  np->maxlen = NETDEV_PKTSIZE(&lower->netdev) - CAN_HDR_SIZE;

  /* Set lower last */

  np->lower = lower;

  return OK;
}

/****************************************************************************
 * Name: netpoll_can_queue_send
 *
 * Description:
 *   Send all queued packets as CAN packets with netpoll.
 *
 *   Each queue entry holds a reserved CAN_HDR_SIZE-byte header slot
 *   followed by the payload. This function fills the header in place at
 *   io_data[io_offset] and hands the packet to the lower half.
 *
 *   CAN FD or classic CAN frames are emitted depending on whether
 *   CONFIG_NET_CAN_CANFD is enabled at compile time.
 *
 * Input Parameters:
 *   np  - pointer to netpoll data
 *   pktq - packet queue to send
 *
 * Returned Value:
 *   OK on success; negative errno on failure.
 *
 ****************************************************************************/

int netpoll_can_queue_send(FAR struct netpoll_s *np,
                           FAR netpkt_queue_t *pktq)
{
  FAR struct netdev_lowerhalf_s *lower = np->lower;
  FAR netpkt_t                  *pkt;
  canid_t                        can_id;
  int                            ret;

  /* Don't send data if device is not ready */

  if (!IFF_IS_RUNNING(lower->netdev.d_flags))
    {
      iob_free_queue(pktq);
      return -ENODEV;
    }

  /* Fill CAN ID field */

  can_id = np->srcport;
#ifdef CONFIG_NET_CAN_EXTID
  can_id |= CAN_EFF_FLAG;
#endif

  while ((pkt = iob_remove_queue(pktq)) != NULL)
    {
      ret = netpoll_can_pkt_send(np, pkt, can_id);
      if (ret < 0)
        {
          netpkt_free(lower, pkt, NETPKT_TX);
          iob_free_queue(pktq);
          return ret;
        }
    }

  return OK;
}

/****************************************************************************
 * Name: netpoll_can_send
 *
 * Description:
 *   Send CAN packet with netpoll.
 *
 * Input Parameters:
 *   np  - pointer to netpoll data
 *   buf - data to send
 *   len - length of data to send
 *
 * Returned Value:
 *   OK on success; Negated errno on failure.
 *
 ****************************************************************************/

int netpoll_can_send(FAR struct netpoll_s *np, FAR const void *buf,
                     size_t len)
{
  netpkt_queue_t pktq;
  int            ret;

  /* Snapshot source buffer into queued packets first, then transmit.
   * This avoids source-buffer mutation while TX is in progress.
   */

  IOB_QINIT(&pktq);

  ret = netpoll_queue_copyin(np, &pktq, buf, len);
  if (ret < 0)
    {
      iob_free_queue(&pktq);
      return ret;
    }

  ret = netpoll_can_queue_send(np, &pktq);
  if (ret < 0)
    {
      return ret;
    }

  return OK;
}
