/****************************************************************************
 * net/netforward/ieee1722acf.h
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

#ifndef __NET_NETCANFWD_IEEE1722ACF_H
#define __NET_NETCANFWD_IEEE1722ACF_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/net/netdev.h>
#include <nuttx/net/cangw.h>

#include <stdint.h>

/****************************************************************************
 * Public Type Definitions
 ****************************************************************************/

/* Non-Time-Synchronous Control Format Header */

begin_packed_struct struct ieee1722acf_ntscf_hdr_s
{
    uint8_t subtype:8;
    uint8_t ntscf_data_length_h:3;
    uint8_t reserved:1;
    uint8_t version:3;
    uint8_t stream_id_valid:1;
    uint8_t ntscf_data_length_l:8;
    uint8_t sequence_num:8;
    uint64_t stream_id;
} end_packed_struct;

/* Time-Synchronous Control Format header */

begin_packed_struct struct ieee1722acf_tscf_hdr_s
{
    uint8_t subtype:8;
    uint8_t time_valid:1;
    uint8_t reserved_0:2;
    uint8_t media_clock_restart:1;
    uint8_t version:3;
    uint8_t stream_id_valid:1;
    uint8_t sequence_num:8;
    uint8_t time_uncertain:1;
    uint8_t reserved_1:7;
    uint64_t stream_id;
    uint32_t avtp_timestamp;
    uint32_t reserved_2;
    uint16_t stream_data_length;
    uint16_t reserved_3;
} end_packed_struct;

/* ACF CAN Message Format header */

begin_packed_struct struct ieee1722acf_msg_hdr_s
{
    uint8_t acf_msg_lenth_h:1;
    uint8_t acf_msg_type:7;
    uint8_t acf_msg_lenth_l:8;
} end_packed_struct;

begin_packed_struct struct ieee1722acf_can_common_hdr_s
{
    uint8_t esi:1;
    uint8_t fdf:1;
    uint8_t brs:1;
    uint8_t eff:1;
    uint8_t rtr:1;
    uint8_t msg_time_valid:1;
    uint8_t padding:2;
    uint8_t can_bus_id:5;
    uint8_t reserved:3;
} end_packed_struct;

begin_packed_struct struct ieee1722acf_can_hdr_s
{
    struct ieee1722acf_msg_hdr_s msg_hdr;
    struct ieee1722acf_can_common_hdr_s can_hdr;
    uint64_t msg_timestamp;
    uint32_t canid;
} end_packed_struct;

begin_packed_struct struct ieee1722acf_canbrief_hdr_s
{
    struct ieee1722acf_msg_hdr_s msg_hdr;
    struct ieee1722acf_can_common_hdr_s can_hdr;
    uint32_t canid;
} end_packed_struct;

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* IEEE1722 Avtp ACF msg CAN/LIN type */

#define IEEE1722ACF_TYPE_CAN             0x1
#define IEEE1722ACF_TYPE_CAN_BRIEF       0x2
#define IEEE1722ACF_TYPE_LIN             0x3

#define IEEE1722ACF_MSG_LEN_ALIGN        4

/****************************************************************************
 * Public Types
 ****************************************************************************/

/****************************************************************************
 * Public Data
 ****************************************************************************/

#ifdef __cplusplus
#  define EXTERN extern "C"
extern "C"
{
#else
#  define EXTERN extern
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

int ieee1722acf_send(FAR struct net_driver_s *dev,
                     FAR struct cangw_rule_s *fwdrule,
                     FAR struct iob_s *can_iob);

#undef EXTERN
#ifdef __cplusplus
}
#endif

#endif /* __NET_CANFWD_IEEE1722ACF_H */
