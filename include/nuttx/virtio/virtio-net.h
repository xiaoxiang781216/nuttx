/****************************************************************************
 * include/nuttx/virtio/virtio-net.h
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

#ifndef __INCLUDE_NUTTX_VIRTIO_VIRTIO_NET_H
#define __INCLUDE_NUTTX_VIRTIO_VIRTIO_NET_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/compiler.h>
#include <nuttx/net/ethernet.h>

#include <stdint.h>
#include <sys/param.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Ethernet header size: wider when 802.1Q VLAN tagging is enabled */

#ifdef CONFIG_NET_VLAN
#  define VIRTIO_NET_ETH_HDRSIZE  ETH_8021Q_HDRLEN
#else
#  define VIRTIO_NET_ETH_HDRSIZE  ETH_HDRLEN
#endif

#define VIRTIO_NET_HDRSIZE        (sizeof(struct virtio_net_hdr_s))
#define VIRTIO_NET_BUFSIZE        (CONFIG_NET_ETH_PKTSIZE + CONFIG_NET_GUARDSIZE)

/* Virtio net feature bits */

#define VIRTIO_NET_F_MAC          5

#define VIRTIO_NET_MAX_PKT_SIZE   \
  ((CONFIG_NET_LL_GUARDSIZE - ETH_HDRLEN) + VIRTIO_NET_BUFSIZE)
#define VIRTIO_NET_MAX_NIOB       \
  ((VIRTIO_NET_MAX_PKT_SIZE + CONFIG_IOB_BUFSIZE_MAX - 1) / \
   CONFIG_IOB_BUFSIZE_MAX)

/****************************************************************************
 * Public Type Definitions
 ****************************************************************************/

/* Virtio net header structure
 * See: https://docs.oasis-open.org/virtio/virtio/v1.2/cs01/
 *      virtio-v1.2-cs01.html#x1-2230004
 */

begin_packed_struct struct virtio_net_hdr_s
{
  uint8_t  flags;
  uint8_t  gso_type;
  uint16_t hdr_len;
  uint16_t gso_size;
  uint16_t csum_start;
  uint16_t csum_offset;
} end_packed_struct;

/* Virtio net config layout
 * See: https://docs.oasis-open.org/virtio/virtio/v1.2/cs01/
 *      virtio-v1.2-cs01.html#x1-2230004
 */

begin_packed_struct struct virtio_net_config_s
{
  uint8_t  mac[IFHWADDRLEN];                 /* VIRTIO_NET_F_MAC */
  uint16_t status;                           /* VIRTIO_NET_F_STATUS */
  uint16_t max_virtqueue_pairs;              /* VIRTIO_NET_F_MQ */
  uint16_t mtu;                              /* VIRTIO_NET_F_MTU */
  uint32_t speed;                            /* VIRTIO_NET_F_SPEED_DUPLEX */
  uint8_t  duplex;
  uint8_t  rss_max_key_size;                 /* VIRTIO_NET_F_RSS */
  uint16_t rss_max_indirection_table_length;
  uint32_t supported_hash_types;
} end_packed_struct;

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/* Calculate the buffer number for each virtqueue, clamped by vring size.
 * rx_descs/tx_descs: num_descs of the RX/TX virtqueue respectively.
 */

static inline_function int virtio_net_calc_bufnum(int rx_descs, int tx_descs)
{
  int bufnum;

#if CONFIG_DRIVERS_VIRTIO_NET_BUFNUM > 0
  bufnum = CONFIG_DRIVERS_VIRTIO_NET_BUFNUM;
#else
  bufnum = CONFIG_IOB_NBUFFERS / VIRTIO_NET_MAX_NIOB / 4;
#endif
  bufnum = MIN(rx_descs / (VIRTIO_NET_MAX_NIOB + 1), bufnum);
  bufnum = MIN(tx_descs / (VIRTIO_NET_MAX_NIOB + 1), bufnum);
  return bufnum;
}

#ifdef __cplusplus
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

#undef EXTERN
#ifdef __cplusplus
}
#endif

#endif /* __INCLUDE_NUTTX_VIRTIO_VIRTIO_NET_H */
