/****************************************************************************
 * libs/libc/stream/lib_wrapsistream.c
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

#include <nuttx/streams.h>

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static ssize_t wrapsistream_gets(FAR struct lib_sistream_s *self,
                                 FAR void *buf, size_t len)
{
  FAR struct lib_wrapsistream_s *stream =
    (FAR struct lib_wrapsistream_s *)self;
  ssize_t total = 0;
  ssize_t ret;

  while (len > 0)
    {
      size_t chunk = len;

      if (self->nget + chunk > stream->length)
        {
          chunk = stream->length - self->nget;
        }

      ret = stream->backend->gets(stream->backend, buf, chunk);
      if (ret <= 0)
        {
          return total > 0 ? total : ret;
        }

      buf = (FAR char *)buf + ret;
      len -= ret;
      total += ret;
      self->nget += ret;

      /* Wrap: seek backend to beginning */

      if (self->nget >= stream->length)
        {
          self->nget = 0;
          ret = stream->backend->seek(stream->backend, 0, SEEK_SET);
          if (ret < 0)
            {
              return total > 0 ? total : ret;
            }
        }
    }

  return total;
}

static int wrapsistream_getc(FAR struct lib_sistream_s *self)
{
  unsigned char ch;
  return wrapsistream_gets(self, &ch, 1) == 1 ? ch : EOF;
}

static off_t wrapsistream_seek(FAR struct lib_sistream_s *self,
                               off_t offset, int whence)
{
  FAR struct lib_wrapsistream_s *stream =
    (FAR struct lib_wrapsistream_s *)self;
  off_t ret;

  switch (whence)
    {
      case SEEK_SET:
        break;
      case SEEK_CUR:
        offset += self->nget;
        break;
      case SEEK_END:
        offset += stream->length;
        break;
      default:
        return -EINVAL;
    }

  offset %= stream->length;

  if (offset < 0)
    {
      offset += stream->length;
    }

  ret = stream->backend->seek(stream->backend, offset, SEEK_SET);
  if (ret < 0)
    {
      return ret;
    }

  self->nget = offset;
  return offset;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void lib_wrapsistream_init(FAR struct lib_wrapsistream_s *stream,
                           FAR struct lib_sistream_s *backend)
{
  off_t length;

  length = backend->seek(backend, 0, SEEK_END);
  backend->seek(backend, 0, SEEK_SET);

  stream->common.getc  = wrapsistream_getc;
  stream->common.gets  = wrapsistream_gets;
  stream->common.seek  = wrapsistream_seek;
  stream->common.nget  = 0;
  stream->backend      = backend;
  stream->length       = length;
}
