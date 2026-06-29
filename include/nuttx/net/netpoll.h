/****************************************************************************
 * include/nuttx/net/netpoll.h
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

#ifndef __INCLUDE_NUTTX_NET_NETPOLL_H
#define __INCLUDE_NUTTX_NET_NETPOLL_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/net/netdev_lowerhalf.h>

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct netpoll_s
{
  /* Netdev lower-half */

  FAR struct netdev_lowerhalf_s *lower;

  size_t    maxlen;                  /* Max payload bytes per packet/frame */
  size_t    hdrlen;                  /* Total header length */
  uint16_t  srcport;                 /* Source port number */
  uint16_t  destport;                /* Destination port number */
  in_addr_t destipaddr;              /* Destination IP address */
  uint8_t   destmac[ETHER_ADDR_LEN]; /* Ethernet destination address */
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef __cplusplus
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

/****************************************************************************
 * Name: netpoll_get_maxlen
 *
 * Description:
 *   Get maximum data length for netpoll
 *
 * Input Parameters:
 *   np - pointer to netpoll data
 *
 * Returned Value:
 *   Returns maxumum data length.
 *
 ****************************************************************************/

#define netpoll_get_maxlen(np) ((np)->maxlen)

/****************************************************************************
 * Name: netpoll_pkt_alloc
 *
 * Description:
 *   Allocate a TX netpkt via the lower half's TX accounting path and
 *   reserve np->hdrlen bytes at the front for the header.  Blocks
 *   (draining pending TX) until a TX quota slot is available.
 *
 * Input Parameters:
 *   np - pointer to netpoll data
 *
 * Returned Value:
 *   Pointer to the allocated netpkt.
 *
 ****************************************************************************/

FAR netpkt_t *netpoll_pkt_alloc(FAR struct netpoll_s *np);

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
                                      FAR netpkt_queue_t *pktq);

/****************************************************************************
 * Name: netpoll_queue_copyin
 *
 * Description:
 *   Append 'len' bytes of data to the packet queue. The queue grows as
 *   needed, splitting at np->maxlen boundaries (one queue entry per wire
 *   packet/frame).
 *
 * Input Parameters:
 *   np   - pointer to netpoll data
 *   pktq - packet queue to append into
 *   src  - The source buffer
 *   len  - How many bytes to copy
 *
 * Returned Value:
 *   0:Success; negated errno on failure
 *
 ****************************************************************************/

int netpoll_queue_copyin(FAR struct netpoll_s *np, FAR netpkt_queue_t *pktq,
                         FAR const void *src, size_t len);

/****************************************************************************
 * Name: netpoll_udp_init
 *
 * Description:
 *   Initialize netpoll with UDP.
 *
 * Input Parameters:
 *   np         - pointer to netpoll data
 *   lower      - pointer to netdev lower
 *   srcport    - source port number
 *   destipaddr - destination IP address
 *   destport   - destination port number
 *   destmac    - MAC address octets in string format: "ff:ff:ff:ff:ff:ff"
 *                Default:  "ff:ff:ff:ff:ff:ff"
 *
 * Returned Value:
 *   Returns OK on success
 *
 ****************************************************************************/

int netpoll_udp_init(FAR struct netpoll_s *np,
                     FAR struct netdev_lowerhalf_s *lower,
                     uint16_t srcport, in_addr_t destipaddr,
                     uint16_t destport, FAR const char *destmac);

/****************************************************************************
 * Name: netpoll_udp_send
 *
 * Description:
 *   Send UDP packet with netpoll.
 *
 * Input Parameters:
 *   np  - pointer to netpoll data
 *   buf - data to send
 *   len - length of data to send
 *
 * Returned Value:
 *   On success or partial transmit, returns the cumulative payload byte
 *   count actually handed to the lower-half transmit() (i.e. the number
 *   of bytes from 'buf' that were successfully queued for TX).  On a
 *   transmit() failure midway through the queue, the already-transmitted
 *   prefix is reported.  A negative errno is only returned when no
 *   payload was transmitted at all.
 *
 ****************************************************************************/

ssize_t netpoll_udp_send(FAR struct netpoll_s *np,
                         FAR const void *buf, size_t len);

/****************************************************************************
 * Name: netpoll_udp_recv
 *
 * Description:
 *   Receive UDP packet with netpoll (blocking).
 *
 * Input Parameters:
 *   np  - pointer to netpoll data
 *   buf - receive buffer
 *   len - length of receive buffer
 *
 * Returned Value:
 *   On success, returns the number of characters received.
 *
 ****************************************************************************/

ssize_t netpoll_udp_recv(FAR struct netpoll_s *np, FAR void *buf,
                         size_t len);

/****************************************************************************
 * Name: netpoll_udp_queue_send
 *
 * Description:
 *   Send queued packets as UDP packets with netpoll.
 *
 * Input Parameters:
 *   np  - pointer to netpoll data
 *   pktq - packet queue to send
 *
 * Returned Value:
 *   On success or partial transmit, returns the cumulative payload byte
 *   count (per packet: io_pktlen - np->hdrlen) of packets actually handed
 *   to the lower-half transmit().  On a transmit() failure midway
 *   through the queue, the already-transmitted prefix is reported and
 *   the remaining tail is freed.  A negative errno is only returned
 *   when no packet was transmitted at all (e.g. -ENODEV when the device
 *   is not running).  The caller never has to free pktq; ownership is
 *   consumed on every path.
 *
 * Assumption:
 *   Packets in queue are ready to send. All packet headers are complete,
 *   only UDP payload length can change.
 *
 ****************************************************************************/

ssize_t netpoll_udp_queue_send(FAR struct netpoll_s *np,
                               FAR netpkt_queue_t *pktq);

/****************************************************************************
 * Name: netpoll_can_init
 *
 * Description:
 *   Initialize netpoll with CAN.
 *
 * Input Parameters:
 *   np      - pointer to netpoll data
 *   lower   - pointer to netdev lower
 *   srcport - source port number
 *
 * Returned Value:
 *   Returns OK on success
 *
 ****************************************************************************/

int netpoll_can_init(FAR struct netpoll_s *np,
                     FAR struct netdev_lowerhalf_s *lower,
                     uint16_t srcport);

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
 *   On success or partial transmit, returns the cumulative payload byte
 *   count (per packet: io_pktlen - np->hdrlen) of packets actually handed
 *   to the lower-half transmit().  On a transmit() failure midway
 *   through the queue, the already-transmitted prefix is reported and
 *   the remaining tail is freed.  A negative errno is only returned
 *   when no packet was transmitted at all (e.g. -ENODEV when the device
 *   is not running).  The caller never has to free pktq; ownership is
 *   consumed on every path.
 *
 ****************************************************************************/

ssize_t netpoll_can_queue_send(FAR struct netpoll_s *np,
                               FAR netpkt_queue_t *pktq);

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
 *   On success or partial transmit, returns the cumulative payload byte
 *   count actually handed to the lower-half transmit().  On a transmit()
 *   failure midway through the queue, the already-transmitted prefix is
 *   reported.  A negative errno is only returned when no payload was
 *   transmitted at all.
 *
 ****************************************************************************/

ssize_t netpoll_can_send(FAR struct netpoll_s *np, FAR const void *buf,
                         size_t len);

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
                       uint16_t txid, uint16_t rxid);

/****************************************************************************
 * Name: netpoll_isotp_send
 *
 * Description:
 *   Send a buffer via ISO-TP over CAN netpoll.
 *
 * Input Parameters:
 *   np  - pointer to netpoll data
 *   buf - data to send
 *   len - length of data to send
 *
 * Returned Value:
 *   On success, returns 'len' (number of bytes accepted).  On failure
 *   before any frame is transmitted, a negative errno is returned.  No
 *   partial-success reporting is provided: ISO-TP sequences either
 *   complete or are abandoned.
 *
 ****************************************************************************/

ssize_t netpoll_isotp_send(FAR struct netpoll_s *np, FAR const void *buf,
                           size_t len);

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
                           size_t len);

#undef EXTERN
#ifdef __cplusplus
}
#endif

#endif /* __INCLUDE_NUTTX_NET_NETPOLL_H */
