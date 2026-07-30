/****************************************************************************
 * net/netpoll/netpoll_isotp.c
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

#include <nuttx/clock.h>
#include <nuttx/net/can.h>
#include <nuttx/net/netpoll.h>
#include <nuttx/can/isotp.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define NETPOLL_ISOTP_RX_STMIN_MS 1

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: netpoll_isotp_pkt_send
 *
 * Description:
 *   Transmit a TX netpkt previously allocated via isotp_alloc_frame().
 *   On both success and failure, ownership of pkt is released.
 *
 ****************************************************************************/

static int netpoll_isotp_pkt_send(FAR struct netpoll_s *np,
                                  FAR netpkt_t *pkt, uint8_t dlen)
{
  netpkt_queue_t pktq;

  netpkt_setdatalen(np->lower, pkt, np->hdrlen + dlen);

  IOB_QINIT(&pktq);
  iob_add_queue(pkt, &pktq);

  return netpoll_can_queue_send(np, &pktq);
}

/****************************************************************************
 * Name: netpoll_isotp_pkt_recv
 *
 * Description:
 *   Block until a CAN frame addressed to np->destport arrives, or the
 *   deadline expires. Returns the receiving netpkt or NULL on timed out.
 *
 ****************************************************************************/

static FAR netpkt_t *netpoll_isotp_pkt_recv(FAR struct netpoll_s *np,
                                            clock_t deadline)
{
  FAR struct netdev_lowerhalf_s *lower = np->lower;
  FAR struct can_frame          *f;
  FAR netpkt_t                  *pkt;
  size_t                         hdrlen;

  DEBUGASSERT(lower->ops->receive != NULL);

#ifdef CONFIG_NET_CAN_CANFD
  hdrlen = sizeof(struct canfd_frame);
#else
  hdrlen = sizeof(struct can_frame);
#endif

  for (; ; )
    {
      /* Advance the RX path in the caller's context before every attempt:
       * with interrupts disabled the RX ISR cannot run, so drivers that
       * stage frames from an ISR (or throttle receive() between hardware
       * interrupts) must be given a chance to make progress first.
       */

      lower->ops->poll(lower, NETPKT_RX);

      pkt = lower->ops->receive(lower);
      if (pkt == NULL)
        {
          if (deadline != 0 &&
              (sclock_t)(clock_systime_ticks() - deadline) >= 0)
            {
              return NULL;
            }

          continue;
        }

      NETDEV_RXPACKETS(&lower->netdev);

      f = (FAR struct can_frame *)netpkt_getdata(lower, pkt);
      if (netpkt_getdatalen(lower, pkt) >= hdrlen &&
          (f->can_id & CAN_EFF_MASK) == np->destport)
        {
          return pkt;
        }

      NETDEV_RXDROPPED(&lower->netdev);
      netpkt_free(lower, pkt, NETPKT_RX);
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: netpoll_isotp_init
 *
 * Description:
 *   Initialize netpoll with CAN ISO-TP.
 *
 * Input Parameters:
 *   np    - pointer to netpoll data
 *   lower - pointer to netdev lower
 *   txid  - CAN ID used for transmission (srcport).
 *   rxid  - CAN ID used for reception (destport).
 *
 * Returned Value:
 *   Returns OK on success
 *
 * Description:
 *   Initialize netpoll for ISO-TP over CAN.
 *
 ****************************************************************************/

int netpoll_isotp_init(FAR struct netpoll_s *np,
                       FAR struct netdev_lowerhalf_s *lower,
                       uint16_t txid, uint16_t rxid)
{
  int ret;

  ret = netpoll_can_init(np, lower, txid);
  if (ret < 0)
    {
      return ret;
    }

  np->destport = rxid;
  return OK;
}

/****************************************************************************
 * Name: netpoll_isotp_send
 *
 * Description:
 *   Send a buffer via ISO-TP over CAN netpoll.
 *
 *   There is no interface for sending ISO-TP packetsdirectly from
 *   netpakcet, it makes no sense for ISO-TP protocol which require
 *   flow-control handling and block size.
 *
 * Input Parameters:
 *   np  - pointer to netpoll data
 *   buf - data to send
 *   len - length of data to send
 *
 * Returned Value:
 *   On success, returns 'len' (number of bytes accepted).  On failure
 *   before any frame is transmitted, a negative errno is returned.
 *
 ****************************************************************************/

ssize_t netpoll_isotp_send(FAR struct netpoll_s *np, FAR const void *buf,
                           size_t len)
{
  FAR struct netdev_lowerhalf_s *lower = np->lower;
  struct isotp_encode_s          enc;
  FAR struct can_frame          *f;
  FAR netpkt_t                  *pkt;
  FAR uint8_t                   *payload;
  uint8_t                        pci_type;
  uint8_t                        fs;
  uint8_t                        dlen;
  clock_t                        deadline;
  int                            ret;

  ret = isotp_encode_init(&enc, buf, len);
  if (ret < 0)
    {
      return ret;
    }

  for (; ; )
    {
      pkt     = netpoll_pkt_alloc(np);
      payload = netpkt_getdata(lower, pkt) + np->hdrlen;
      dlen    = np->maxlen;
      ret     = isotp_encode_frame(&enc, payload, &dlen);
      if (ret < 0)
        {
          netpkt_free(lower, pkt, NETPKT_TX);
          return ret;
        }

      pci_type = ISOTP_PCI_TYPE(payload[0]);

      ret = netpoll_isotp_pkt_send(np, pkt, dlen);
      if (ret < 0)
        {
          return ret;
        }

      /* After FF, wait for FC before sending CFs */

      if (pci_type == ISOTP_PCI_TYPE_FF)
        {
          deadline = clock_systime_ticks() +
            MSEC2TICK(ISOTP_NCR_TIMEOUT_MS);

          for (; ; )
            {
              pkt = netpoll_isotp_pkt_recv(np, deadline);
              if (pkt == NULL)
                {
                  return -ETIMEDOUT;
                }

              f = (FAR struct can_frame *)netpkt_getdata(lower, pkt);

              if (f->can_dlc >= ISOTP_FC_LEN &&
                  ISOTP_PCI_TYPE(f->data[0]) == ISOTP_PCI_TYPE_FC)
                {
                  fs = f->data[0] & ISOTP_FC_FS_MASK;
                  netpkt_free(lower, pkt, NETPKT_RX);

                  if (fs != ISOTP_FC_CTS)
                    {
                      NETDEV_RXERRORS(&lower->netdev);
                      return -EPROTO;
                    }

                  break;
                }

              NETDEV_RXDROPPED(&lower->netdev);
              netpkt_free(lower, pkt, NETPKT_RX);
            }
        }

      if (enc.done >= enc.len)
        {
          break;
        }
    }

  return len;
}

/****************************************************************************
 * Name: netpoll_isotp_recv
 *
 * Description:
 *   Receive a message via ISO-TP over CAN netpoll.
 *
 *   This function doesn't use the NET stack so it can be used in
 *   an interrupt context.
 *
 * Input Parameters:
 *   np  - pointer to netpoll data
 *   buf - receive buffer
 *   len - length of receive buffer
 *
 * Returned Value:
 *   Returns the number of characters received.
 *
 ****************************************************************************/

ssize_t netpoll_isotp_recv(FAR struct netpoll_s *np, FAR void *buf,
                           size_t len)
{
  FAR struct netdev_lowerhalf_s *lower = np->lower;
  struct isotp_decode_s          dec;
  FAR netpkt_t                  *pkt;
  FAR uint8_t                   *payload;
  FAR struct can_frame          *f;
  uint8_t                        pci_type;
  clock_t                        deadline;
  int                            ret;

restart:
  isotp_decode_init(&dec, buf, len);
  deadline = 0;

  for (; ; )
    {
      pkt = netpoll_isotp_pkt_recv(np, deadline);
      if (pkt == NULL)
        {
          /* No CF arrived within N_Cr — discard partial message
           * and wait for a new FF from the beginning.
           */

          goto restart;
        }

      /* Decode the frame straight from the IOB, then release it. */

      f = (FAR struct can_frame *)netpkt_getdata(lower, pkt);
      pci_type = ISOTP_PCI_TYPE(f->data[0]);
      ret = isotp_decode_frame(&dec, f->data, f->can_dlc);
      netpkt_free(lower, pkt, NETPKT_RX);

      if (ret < 0)
        {
          /* Framing error — restart */

          NETDEV_RXERRORS(&lower->netdev);
          goto restart;
        }

      if (ret == 0)
        {
          break;
        }

      if (pci_type == ISOTP_PCI_TYPE_FF)
        {
          /* Send CTS flow control */

          pkt     = netpoll_pkt_alloc(np);
          payload = netpkt_getdata(lower, pkt) + np->hdrlen;
          isotp_build_fc(payload, ISOTP_FC_CTS, 0,
                         NETPOLL_ISOTP_RX_STMIN_MS);
          ret = netpoll_isotp_pkt_send(np, pkt, ISOTP_FC_LEN);
          if (ret < 0)
            {
              return ret;
            }
        }

      /* Arm N_Cr deadline */

      deadline = clock_systime_ticks() + MSEC2TICK(ISOTP_NCR_TIMEOUT_MS);
    }

  return dec.done;
}
