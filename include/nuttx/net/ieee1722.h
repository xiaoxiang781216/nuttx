/****************************************************************************
 * include/nuttx/net/ieee1722.h
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

#ifndef __INCLUDE_NUTTX_NET_IEEE1722_H
#define __INCLUDE_NUTTX_NET_IEEE1722_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <netinet/if_ether.h>
#include <nuttx/queue.h>
#include <nuttx/wqueue.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* IEEE1722 Avtp sub type */

#define IEEE1722_SUBTYPE_61883_IIDC             0x0
#define IEEE1722_SUBTYPE_MMA_STREAM             0x1
#define IEEE1722_SUBTYPE_AAF                    0x2
#define IEEE1722_SUBTYPE_CVF                    0x3
#define IEEE1722_SUBTYPE_CRF                    0x4
#define IEEE1722_SUBTYPE_TSCF                   0x5
#define IEEE1722_SUBTYPE_SVF                    0x6
#define IEEE1722_SUBTYPE_RVF                    0x7
#define IEEE1722_SUBTYPE_AEF_CONTINUOUS         0x6E
#define IEEE1722_SUBTYPE_VSF_STREAM             0x6F
#define IEEE1722_SUBTYPE_EF_STREAM              0x7F
#define IEEE1722_SUBTYPE_NTSCF                  0x82
#define IEEE1722_SUBTYPE_ESCF                   0xEC
#define IEEE1722_SUBTYPE_EECF                   0xED
#define IEEE1722_SUBTYPE_AEF_DISCRETE           0xEE
#define IEEE1722_SUBTYPE_ADP                    0xFA
#define IEEE1722_SUBTYPE_AECP                   0xFB
#define IEEE1722_SUBTYPE_ACMP                   0xFC
#define IEEE1722_SUBTYPE_MAAP                   0xFE
#define IEEE1722_SUBTYPE_EF_CONTROL             0xFF

/****************************************************************************
 * Public Type Definitions
 ****************************************************************************/

/* Structure representing an IEEE 1722 ACF (AVTP Control Format) stream
 * configuration and runtime state.
 */

struct ieee1722acf_stream_s
{
  /* List management - used to maintain a single-linked list of
   * eth1722 acf streams
   */

  sq_entry_t node;                /* List node for stream management */

  /* Stream configuration - set during stream initialization */

  uint16_t index;                 /* Stream index for fwd processing */
  uint16_t cycle_us;              /* Tx Cycle time of acf pkt in units of us,
                                   * set to 0 means not cyclical sending.
                                   */
  uint8_t msgcnt_limit;           /* Max CAN message counter limit per ACF packet
                                   * (0 or 1 means single message per packet),
                                   * Only take effect when tx_cycle == 0
                                   */
  uint8_t stream_type;            /* Stream type: TSCF or NTSCF */
  uint8_t dest_mac[ETH_ALEN];     /* Destination MAC address */
  uint16_t stream_id;             /* IEEE 1722 stream identifier from user setting,
                                   * will be added into stream packet header
                                   */

  /* Runtime state - dynamically updated during packet processing */

  uint8_t msg_cnt;                /* Available CAN message counter in the ACF packet */
  uint8_t stream_seq;             /* Stream packet sequence counter */
  FAR struct iob_s *iob;          /* I/O buffer holding the ACF stream packet */
  struct work_s cycle_work;       /* For stream cyclical send handling */
  FAR void *priv;                 /* Private data pointer */
};

#endif /* __INCLUDE_NUTTX_NET_IEEE1722_H */
