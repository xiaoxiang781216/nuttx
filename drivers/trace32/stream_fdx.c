/****************************************************************************
 * drivers/trace32/stream_fdx.c
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

#include <nuttx/nuttx.h>
#include <nuttx/streams.h>
#include <nuttx/spinlock.h>
#include <nuttx/trace32/fdx.h>

#include "t32fdx/t32fdx.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct fdx_channel_s
{
  T32_FDX_BUFFER header;
  T32_FDX_DATATYPE data[0];
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: fdxstream_putc
 ****************************************************************************/

static void fdxstream_putc(FAR struct lib_outstream_s *self, int ch)
{
  FAR struct lib_fdxoutstream_s *stream =
                                 (FAR struct lib_fdxoutstream_s *)self;
  char c = ch;
  irqstate_t flags = spin_lock_irqsave(&stream->lock);
  T32_Fdx_Send(stream->channel, &c, 1);
  spin_unlock_irqrestore(&stream->lock, flags);
}

/****************************************************************************
 * Name: fdxstream_puts
 ****************************************************************************/

static ssize_t fdxstream_puts(FAR struct lib_outstream_s *self,
                              FAR const void *buf, size_t len)
{
  FAR struct lib_fdxoutstream_s *stream =
                                 (FAR struct lib_fdxoutstream_s *)self;
  irqstate_t flags = spin_lock_irqsave(&stream->lock);
  int ret = T32_Fdx_Send(stream->channel, (FAR void *)buf, len);
  spin_unlock_irqrestore(&stream->lock, flags);
  return ret;
}

/****************************************************************************
 * Name: fdxstream_getc
 ****************************************************************************/

static int fdxstream_getc(FAR struct lib_instream_s *self)
{
  FAR struct lib_fdxinstream_s *stream =
                                (FAR struct lib_fdxinstream_s *)self;
  char ch = -1;

  stream->common.nget += T32_Fdx_Receive(stream->channel, &ch, 1);
  return ch;
}

/****************************************************************************
 * Name: fdxstream_gets
 ****************************************************************************/

static ssize_t fdxstream_gets(FAR struct lib_instream_s *self,
                              FAR void * buffer, size_t size)
{
  FAR struct lib_fdxinstream_s *stream =
                                (FAR struct lib_fdxinstream_s *)self;
  ssize_t ret;

  ret = T32_Fdx_Receive(stream->channel, buffer, size);
  stream->common.nget += ret;
  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: lib_fdxoutstream_open
 *
 * Description:
 *   Initializes a stream for use with the configured FDX interface.
 *
 * Input Parameters:
 *   stream - User allocated, uninitialized instance of struct
 *            lib_fdxoutstream_s to be initialized.
 *   channel - SEGGER FDX channel number
 *   bufsize - Size of the FDX buffer
 *
 * Returned Value:
 *   None (User allocated instance initialized).
 *
 ****************************************************************************/

void lib_fdxoutstream_open(FAR struct lib_fdxoutstream_s *stream,
                           FAR void *buf, size_t size)
{
  FAR struct fdx_channel_s *channel = (FAR struct fdx_channel_s *)buf;
  stream->common.putc = fdxstream_putc;
  stream->common.puts = fdxstream_puts;
  stream->common.flush = lib_noflush;
  stream->common.nput = 0;
  stream->channel = buf;
  T32_Fdx_InitChannel(*channel);
  channel->header.size = size - sizeof(T32_FDX_BUFFER);
}

/****************************************************************************
 * Name: lib_fdxoutstream_close
 ****************************************************************************/

void lib_fdxoutstream_close(FAR struct lib_fdxoutstream_s *stream)
{
  FAR struct fdx_channel_s *channel =
                            (FAR struct fdx_channel_s *)stream->channel;
  T32_Fdx_DisableChannel(*channel);
}

/****************************************************************************
 * Name: lib_fdxinstream_open
 ****************************************************************************/

void lib_fdxinstream_open(FAR struct lib_fdxinstream_s *stream,
                          FAR void *buf, size_t size)
{
  FAR struct fdx_channel_s *channel = (FAR struct fdx_channel_s *)buf;
  stream->common.getc = fdxstream_getc;
  stream->common.gets = fdxstream_gets;
  stream->common.nget = 0;
  stream->channel = buf;
  T32_Fdx_InitChannel(*channel);
  channel->header.size = size - sizeof(T32_FDX_BUFFER);
}

/****************************************************************************
 * Name: lib_fdxinstream_close
 ****************************************************************************/

void lib_fdxinstream_close(FAR struct lib_fdxoutstream_s *stream)
{
  FAR struct fdx_channel_s *channel =
                            (FAR struct fdx_channel_s *)stream->channel;
  T32_Fdx_DisableChannel(*channel);
}
