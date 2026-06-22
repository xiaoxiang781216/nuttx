/****************************************************************************
 * include/nuttx/virtio/virtio-fs.h
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

#ifndef __INCLUDE_NUTTX_VIRTIO_VIRTIO_FS_H
#define __INCLUDE_NUTTX_VIRTIO_VIRTIO_FS_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>

#include <nuttx/compiler.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define VIRTIO_FS_F_NOTIFICATION 0

/****************************************************************************
 * Public Types
 ****************************************************************************/

begin_packed_struct struct virtio_fs_config_s
{
  char     tag[36];
  uint32_t num_request_queues;
  uint32_t notify_buf_size;
} end_packed_struct;

#endif /* __INCLUDE_NUTTX_VIRTIO_VIRTIO_FS_H */
