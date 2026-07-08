/****************************************************************************
 * libs/libc/stream/lib_blksistream.c
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

#include <string.h>
#include <unistd.h>
#include <nuttx/fs/fs.h>
#include <nuttx/streams.h>

#include "libc.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: blksistream_refill
 *
 * Description:
 *   Fill the cache buffer with the specified sector. If the sector is
 *   already cached, this is a no-op, avoiding redundant reads when the
 *   caller reads small buffers continuously.
 *
 ****************************************************************************/

static ssize_t blksistream_refill(FAR struct lib_blksistream_s *stream,
                                  off_t sector)
{
  ssize_t ret;

  if (stream->sector == sector)
    {
      return 0;
    }

  ret = stream->inode->u.i_bops->read(stream->inode, stream->cache,
                                      sector, 1);
  if (ret >= 0)
    {
      stream->sector = sector;
    }

  return ret;
}

/****************************************************************************
 * Name: blksistream_gets
 ****************************************************************************/

static ssize_t blksistream_gets(FAR struct lib_sistream_s *self,
                                FAR void *buf, size_t len)
{
  FAR struct lib_blksistream_s *stream =
    (FAR struct lib_blksistream_s *)self;
  size_t sectorsize = stream->geo.geo_sectorsize;
  off_t devsize = stream->geo.geo_nsectors * sectorsize;
  FAR unsigned char *ptr = buf;
  size_t remain = len;
  ssize_t ret;

  while (remain > 0)
    {
      off_t sector = self->nget / sectorsize;
      off_t offset = self->nget % sectorsize;

      if (self->nget >= devsize)
        {
          break;
        }

      if (offset > 0)
        {
          size_t copyout = offset + remain > sectorsize ?
                           sectorsize - offset : remain;

          ret = blksistream_refill(stream, sector);
          if (ret < 0)
            {
              return (len - remain) > 0 ? len - remain : ret;
            }

          memcpy(ptr, stream->cache + offset, copyout);
          ptr        += copyout;
          self->nget += copyout;
          remain     -= copyout;
        }
      else if (remain < sectorsize)
        {
          ret = blksistream_refill(stream, sector);
          if (ret < 0)
            {
              return (len - remain) > 0 ? len - remain : ret;
            }

          memcpy(ptr, stream->cache, remain);
          self->nget += remain;
          remain      = 0;
        }
      else
        {
          size_t nsector = remain / sectorsize;
          size_t copyout = nsector * sectorsize;

          ret = stream->inode->u.i_bops->read(stream->inode, ptr,
                                              sector, nsector);
          if (ret < 0)
            {
              return (len - remain) > 0 ? len - remain : ret;
            }

          ptr        += copyout;
          self->nget += copyout;
          remain     -= copyout;
        }
    }

  return len - remain;
}

/****************************************************************************
 * Name: blksistream_getc
 ****************************************************************************/

static int blksistream_getc(FAR struct lib_sistream_s *self)
{
  unsigned char ch;
  return blksistream_gets(self, &ch, 1) == 1 ? ch : EOF;
}

/****************************************************************************
 * Name: blksistream_seek
 ****************************************************************************/

static off_t blksistream_seek(FAR struct lib_sistream_s *self,
                              off_t offset, int whence)
{
  FAR struct lib_blksistream_s *stream =
    (FAR struct lib_blksistream_s *)self;
  off_t devsize = stream->geo.geo_nsectors *
                  stream->geo.geo_sectorsize;

  switch (whence)
    {
      case SEEK_SET:
        break;
      case SEEK_CUR:
        offset += self->nget;
        break;
      case SEEK_END:
        offset += devsize;
        break;
      default:
        return -EINVAL;
    }

  if (offset < 0 || offset > devsize)
    {
      return -EINVAL;
    }

  self->nget = offset;
  return offset;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: lib_blksistream_open
 ****************************************************************************/

int lib_blksistream_open(FAR struct lib_blksistream_s *stream,
                         FAR const char *name)
{
  FAR struct inode *inode;
  int ret;

  if (stream == NULL || name == NULL)
    {
      return -EINVAL;
    }

  ret = open_blockdriver(name, 0, &inode);
  if (ret < 0)
    {
      return ret;
    }

  memset(stream, 0, sizeof(*stream));

  if (inode->u.i_bops->geometry == NULL ||
      inode->u.i_bops->read == NULL ||
      inode->u.i_bops->geometry(inode, &stream->geo) < 0 ||
      stream->geo.geo_sectorsize <= 0 ||
      stream->geo.geo_nsectors <= 0)
    {
      close_blockdriver(inode);
      return -EINVAL;
    }

  stream->cache = lib_malloc(stream->geo.geo_sectorsize);
  if (stream->cache == NULL)
    {
      close_blockdriver(inode);
      return -ENOMEM;
    }

  stream->inode        = inode;
  stream->sector       = -1;
  stream->common.getc  = blksistream_getc;
  stream->common.gets  = blksistream_gets;
  stream->common.seek  = blksistream_seek;

  return OK;
}

/****************************************************************************
 * Name: lib_blksistream_close
 ****************************************************************************/

void lib_blksistream_close(FAR struct lib_blksistream_s *stream)
{
  if (stream != NULL)
    {
      if (stream->cache != NULL)
        {
          lib_free(stream->cache);
          stream->cache = NULL;
        }

      if (stream->inode != NULL)
        {
          close_blockdriver(stream->inode);
          stream->inode = NULL;
        }
    }
}
