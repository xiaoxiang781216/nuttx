/****************************************************************************
 * net/netpoll/netpoll_pkt.c
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

#include <nuttx/mm/iob.h>
#include <nuttx/net/netpoll.h>

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: netpoll_pkt_stamp_tail
 *
 * Description:
 *   Walk 'pkt' to its chain tail, mark it with IOB_CHAIN_END and refresh
 *   pktq->qh_tail.  iob_trycopyin clears CHAIN_END whenever it extends a
 *   chain, so we must re-stamp whenever a queue entry is finalized
 *   (before moving on to the next entry, and at function exit).
 *
 ****************************************************************************/

static void netpoll_pkt_stamp_tail(FAR netpkt_queue_t *pktq,
                                   FAR netpkt_t *pkt)
{
  while (IOB_LINK_GET(pkt) != NULL)
    {
      pkt = IOB_LINK_GET(pkt);
    }

  IOB_FLAG_SET(pkt, NULL, IOB_CHAIN_END);
  pktq->qh_tail = pkt;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: netpoll_pkt_alloc
 *
 * Description:
 *   Allocate a packet for a new queue entry via the lower half's TX
 *   accounting path.  Each queue entry consumes one TX quota slot and
 *   returns it on netpkt_free, keeping alloc/free balanced even when a
 *   single pkt_copy spans many frames.  The lower half is expected to
 *   size its TX quota (CONFIG_..._TX_QUOTA) to cover the largest
 *   anticipated netpoll write.
 *
 *   Blocks (draining pending TX) until a quota slot is available; the
 *   returned pkt has np->hdrlen bytes already reserved at the front.
 *
 ****************************************************************************/

FAR netpkt_t *netpoll_pkt_alloc(FAR struct netpoll_s *np)
{
  FAR struct netdev_lowerhalf_s *lower = np->lower;
  FAR netpkt_t                  *pkt;

  for (; ; )
    {
      pkt = netpkt_alloc(lower, NETPKT_TX);
      if (pkt)
        {
          netpkt_setdatalen(lower, pkt, np->hdrlen);
          return pkt;
        }

      /* Drain pending TX so a quota slot comes back */

      lower->ops->txpoll(lower);
    }
}

/****************************************************************************
 * Name: netpoll_queue_get_payload_size
 *
 * Description:
 *   Return the total payload byte count (Sum of io_pktlen - np->hdrlen)
 *   of a netpoll packet queue, matching what netpoll_*_queue_send()
 *   reports as sent.
 *
 * Input Parameters:
 *   np   - pointer to netpoll data
 *   pktq - packet queue to measure
 *
 * Returned Value:
 *   The total payload byte count of the queue.
 *
 ****************************************************************************/

size_t netpoll_queue_get_payload_size(FAR struct netpoll_s *np,
                                      FAR netpkt_queue_t *pktq)
{
  return iob_get_queue_size(pktq) -
         iob_get_queue_entry_count(pktq) * np->hdrlen;
}

/****************************************************************************
 * Name: netpoll_queue_copyin
 *
 * Description:
 *   Append 'len' bytes to the packet queue, splitting across queue entries
 *   at np->maxlen boundaries.  One queue entry represents one wire packet
 *   or frame; within a queue entry netpkt_copyin (iob_trycopyin) auto-
 *   chains IOBs when IOB_BUFSIZE < maxlen.
 *
 ****************************************************************************/

int netpoll_queue_copyin(FAR struct netpoll_s *np, FAR netpkt_queue_t *pktq,
                         FAR const void *src, size_t len)
{
  FAR struct netdev_lowerhalf_s *lower = np->lower;
  FAR netpkt_t                  *pkt;
  size_t                         maxlen = np->maxlen;
  size_t                         rsvlen;
  size_t                         used;
  size_t                         chunk;
  int                            ret;

  /* rsvlen matches what netpkt_setdatalen reserves: hdrlen minus the
   * llhdrlen that lives in guard space.  Subtract from io_pktlen to
   * recover the content bytes already copied into the tail packet.
   */

  rsvlen = np->hdrlen - NET_LL_HDRLEN(&lower->netdev);

  /* qh_tail points at the head IOB of the last chain between iob_add_queue
   * and netpoll_pkt_stamp_tail.
   */

  pkt  = iob_peek_tail(pktq);
  used = pkt ? pkt->io_pktlen - rsvlen : maxlen;

  while (len > 0)
    {
      if (used >= maxlen)
        {
          /* Close the previous entry (iob_trycopyin may have extended it
           * and cleared CHAIN_END) before starting a new one.
           */

          if (pkt != NULL)
            {
              netpoll_pkt_stamp_tail(pktq, pkt);
            }

          pkt = netpoll_pkt_alloc(np);
          if (pkt == NULL)
            {
              return -ENOMEM;
            }

          iob_add_queue(pkt, pktq);
          used = 0;
        }

      chunk = MIN(len, maxlen - used);

      ret = netpkt_copyin(lower, pkt, src, chunk, np->hdrlen + used);
      if (ret < 0)
        {
          return ret;
        }

      src   = (FAR const uint8_t *)src + chunk;
      len  -= chunk;
      used += chunk;
    }

  return OK;
}
