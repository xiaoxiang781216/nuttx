/****************************************************************************
 * libs/libc/stream/lib_wrapsostream.c
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

static ssize_t wrapsostream_puts(FAR struct lib_sostream_s *self,
                                 FAR const void *buf, size_t len)
{
  FAR struct lib_wrapsostream_s *stream =
    (FAR struct lib_wrapsostream_s *)self;
  ssize_t total = 0;
  ssize_t ret;

  while (len > 0)
    {
      size_t chunk = len;

      if (self->nput + chunk > stream->length)
        {
          chunk = stream->length - self->nput;
        }

      ret = stream->backend->puts(stream->backend, buf, chunk);
      if (ret <= 0)
        {
          return total > 0 ? total : ret;
        }

      buf = (FAR const char *)buf + ret;
      len -= ret;
      total += ret;
      self->nput += ret;

      /* Wrap: seek backend to beginning */

      if (self->nput >= stream->length)
        {
          self->nput = 0;
          ret = stream->backend->seek(stream->backend, 0, SEEK_SET);
          if (ret < 0)
            {
              return total > 0 ? total : ret;
            }
        }
    }

  return total;
}

static void wrapsostream_putc(FAR struct lib_sostream_s *self, int ch)
{
  char tmp = ch;
  wrapsostream_puts(self, &tmp, 1);
}

static off_t wrapsostream_seek(FAR struct lib_sostream_s *self,
                               off_t offset, int whence)
{
  FAR struct lib_wrapsostream_s *stream =
    (FAR struct lib_wrapsostream_s *)self;
  off_t ret;

  switch (whence)
    {
      case SEEK_SET:
        break;
      case SEEK_CUR:
        offset += self->nput;
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

  self->nput = offset;
  return offset;
}

static int wrapsostream_flush(FAR struct lib_sostream_s *self)
{
  FAR struct lib_wrapsostream_s *stream =
    (FAR struct lib_wrapsostream_s *)self;

  return stream->backend->flush(stream->backend);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void lib_wrapsostream_init(FAR struct lib_wrapsostream_s *stream,
                           FAR struct lib_sostream_s *backend)
{
  off_t length;

  length = backend->seek(backend, 0, SEEK_END);
  backend->seek(backend, 0, SEEK_SET);

  stream->common.putc  = wrapsostream_putc;
  stream->common.puts  = wrapsostream_puts;
  stream->common.seek  = wrapsostream_seek;
  stream->common.flush = wrapsostream_flush;
  stream->common.nput  = 0;
  stream->backend      = backend;
  stream->length       = length;
}
