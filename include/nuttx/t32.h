/****************************************************************************
 * include/nuttx/t32.h
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

#ifndef __INCLUDE_NUTTX_T32_H
#define __INCLUDE_NUTTX_T32_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/streams.h>
#include <nuttx/syslog/syslog.h>

/****************************************************************************
 * Type Declarations
 ****************************************************************************/

#ifdef CONFIG_STREAM_FDX
struct lib_fdxoutstream_s
{
  struct lib_outstream_s common;
  FAR void *channel;
  spinlock_t lock;
};

struct lib_fdxinstream_s
{
  struct lib_instream_s common;
  FAR void *channel;
  spinlock_t lock;
};

#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef CONFIG_STREAM_FDX
/****************************************************************************
 * Name: lib_fdxoutstream_open
 ****************************************************************************/

void lib_fdxoutstream_open(FAR struct lib_fdxoutstream_s *stream,
                           FAR void *buf, size_t size);

/****************************************************************************
 * Name: lib_fdxoutstream_close
 ****************************************************************************/

void lib_fdxoutstream_close(FAR struct lib_fdxoutstream_s *stream);
#endif

#ifdef CONFIG_SYSLOG_FDX
int syslog_fdx_putc(FAR syslog_channel_t *channel, int ch);
ssize_t syslog_fdx_write(FAR syslog_channel_t *channel,
                         FAR const char *buffer, size_t buflen);
#endif

void serial_t32term_initialize(void);

#endif /* __INCLUDE_NUTTX_T32_H */
