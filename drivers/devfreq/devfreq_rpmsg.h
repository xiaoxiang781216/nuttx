/****************************************************************************
 * drivers/devfreq/devfreq_rpmsg.h
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

#ifndef __DRIVERS_DEVFREQ_DEVFREQ_RPMSG_H
#define __DRIVERS_DEVFREQ_DEVFREQ_RPMSG_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define DEVFREQ_RPMSG_EPT_PREFIX      "rpmsg-devfreq-"
#define DEVFREQ_RPMSG_EPT_PREFIX_LEN  (sizeof(DEVFREQ_RPMSG_EPT_PREFIX) - 1)

#define DEVFREQ_RPMSG_TARGET          0
#define DEVFREQ_RPMSG_GET_FREQ        1
#define DEVFREQ_RPMSG_SUSPEND         2
#define DEVFREQ_RPMSG_RESUME          3
#define DEVFREQ_RPMSG_GET_TABLE       4

/****************************************************************************
 * Public Types
 ****************************************************************************/

begin_packed_struct struct devfreq_rpmsg_header_s
{
  uint32_t command;
  int32_t result;
  uint64_t cookie;
  uint32_t response;
} end_packed_struct;

begin_packed_struct struct devfreq_rpmsg_target_s
{
  struct devfreq_rpmsg_header_s header;
  uint32_t freq;
} end_packed_struct;

begin_packed_struct struct devfreq_rpmsg_table_s
{
  struct devfreq_rpmsg_header_s header;
  uint32_t freq_table[1];
} end_packed_struct;

#endif /* __DRIVERS_DEVFREQ_DEVFREQ_RPMSG_H */
