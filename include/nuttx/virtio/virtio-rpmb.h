/****************************************************************************
 * include/nuttx/virtio/virtio-rpmb.h
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
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ****************************************************************************/

#ifndef __INCLUDE_NUTTX_VIRTIO_VIRTIO_RPMB_H
#define __INCLUDE_NUTTX_VIRTIO_VIRTIO_RPMB_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <limits.h>
#include <stdint.h>

#include <nuttx/compiler.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* RPMB Request Types */

#define VIRTIO_RPMB_REQ_PROGRAM_KEY           0x0001
#define VIRTIO_RPMB_REQ_GET_WRITE_COUNTER     0x0002
#define VIRTIO_RPMB_REQ_DATA_WRITE            0x0003
#define VIRTIO_RPMB_REQ_DATA_READ             0x0004
#define VIRTIO_RPMB_REQ_RESULT_READ           0x0005

/* RPMB Response Types */

#define VIRTIO_RPMB_RESP_PROGRAM_KEY          0x0100
#define VIRTIO_RPMB_RESP_GET_COUNTER          0x0200
#define VIRTIO_RPMB_RESP_DATA_WRITE           0x0300
#define VIRTIO_RPMB_RESP_DATA_READ            0x0400

/* RPMB Operation Results */

#define VIRTIO_RPMB_RES_OK                    0x0000
#define VIRTIO_RPMB_RES_GENERAL_FAILURE       0x0001
#define VIRTIO_RPMB_RES_AUTH_FAILURE          0x0002
#define VIRTIO_RPMB_RES_COUNT_FAILURE         0x0003
#define VIRTIO_RPMB_RES_ADDR_FAILURE          0x0004
#define VIRTIO_RPMB_RES_WRITE_FAILURE         0x0005
#define VIRTIO_RPMB_RES_READ_FAILURE          0x0006
#define VIRTIO_RPMB_RES_NO_AUTH_KEY           0x0007
#define VIRTIO_RPMB_RES_WRITE_COUNTER_EXPIRED 0x0080

#define VIRTIO_RPMB_PATH_SIZE                 32

/* RPMB EXT_CSD (eMMC standard command 8) layout */

#define VIRTIO_RPMB_EXT_CSD_SIZE              512
#define VIRTIO_RPMB_EXT_CSD_RPMB_SIZE         168
#define VIRTIO_RPMB_EXT_CSD_REL_WR_SEC_C      222

/****************************************************************************
 * Public Type Definitions
 ****************************************************************************/

/* Device configuration layout (virtio spec 5.12.4) */

begin_packed_struct struct virtio_rpmb_config_s
{
  uint8_t capacity;
  uint8_t max_wr_cnt;
  uint8_t max_rd_cnt;
} end_packed_struct;

/* Vhost RPMB config: extends virtio_rpmb_config_s with the AP-side
 * device path. Written by the TEE side into the virtio config space,
 * read by the vhost-rpmb probe to open the backend block device.
 * The AP updates capacity/max_wr_cnt from EXT_CSD after probing the
 * eMMC, so the TEE can read accurate values via config space.
 */

begin_packed_struct struct vhost_rpmb_config_s
{
  struct virtio_rpmb_config_s config;
  char                        path[VIRTIO_RPMB_PATH_SIZE];
} end_packed_struct;

/* RPMB frame (virtio spec 5.12.6.1, 512 bytes) */

begin_packed_struct struct virtio_rpmb_frame_s
{
  uint8_t  stuff[196];
  uint8_t  key_mac[32];
  uint8_t  data[256];
  uint8_t  nonce[16];
  uint32_t write_counter;
  uint16_t address;
  uint16_t block_count;
  uint16_t result;
  uint16_t req_resp;
} end_packed_struct;

#endif /* __INCLUDE_NUTTX_VIRTIO_VIRTIO_RPMB_H */
