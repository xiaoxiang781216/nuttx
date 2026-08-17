/****************************************************************************
 * libs/libc/stream/lib_mtdsistream.c
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
#include <nuttx/mtd/mtd.h>
#include <nuttx/streams.h>

#include "libc.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: mtdsistream_reads
 *
 * Description:
 *   Read using the byte-oriented read callback (no cache needed).
 *
 ****************************************************************************/

static ssize_t mtdsistream_reads(FAR struct lib_sistream_s *self,
                                 FAR void *buf, size_t len)
{
  FAR struct lib_mtdsistream_s *stream =
    (FAR struct lib_mtdsistream_s *)self;
  FAR struct mtd_dev_s *mtd = stream->mtd;
  ssize_t ret;

  ret = mtd->read(mtd, self->nget, len, buf);
  if (ret > 0)
    {
      self->nget += ret;
    }

  return ret;
}

/****************************************************************************
 * Name: mtdsistream_refill
 *
 * Description:
 *   Read one block into the cache. Skip if already cached.
 *
 ****************************************************************************/

static ssize_t mtdsistream_refill(FAR struct lib_mtdsistream_s *stream,
                                  off_t block)
{
  FAR struct mtd_dev_s *mtd = stream->mtd;
  ssize_t ret;

  if (stream->eblock == block)
    {
      return 0;
    }

  ret = MTD_BREAD(mtd, block, 1, stream->cache);
  if (ret >= 0)
    {
      stream->eblock = block;
    }

  return ret;
}

/****************************************************************************
 * Name: mtdsistream_gets
 ****************************************************************************/

static ssize_t mtdsistream_gets(FAR struct lib_sistream_s *self,
                                FAR void *buf, size_t len)
{
  FAR struct lib_mtdsistream_s *stream =
    (FAR struct lib_mtdsistream_s *)self;
  FAR struct mtd_dev_s *mtd = stream->mtd;
  size_t blocksize = stream->geo.blocksize;
  off_t devsize = stream->geo.neraseblocks * stream->geo.erasesize;
  FAR unsigned char *ptr = buf;
  ssize_t ret;

  while (len > 0)
    {
      off_t block = self->nget / blocksize;
      off_t offset = self->nget % blocksize;

      if (self->nget >= devsize)
        {
          break;
        }

      if (offset > 0)
        {
          size_t copyout = offset + len > blocksize ?
                           blocksize - offset : len;

          ret = mtdsistream_refill(stream, block);
          if (ret < 0)
            {
              return ptr - (FAR unsigned char *)buf > 0 ?
                     ptr - (FAR unsigned char *)buf : ret;
            }

          memcpy(ptr, stream->cache + offset, copyout);
          ptr        += copyout;
          self->nget += copyout;
          len        -= copyout;
        }
      else if (len < blocksize)
        {
          ret = mtdsistream_refill(stream, block);
          if (ret < 0)
            {
              return ptr - (FAR unsigned char *)buf > 0 ?
                     ptr - (FAR unsigned char *)buf : ret;
            }

          memcpy(ptr, stream->cache, len);
          self->nget += len;
          len         = 0;
        }
      else
        {
          size_t nblock = len / blocksize;
          size_t copyout = nblock * blocksize;

          ret = MTD_BREAD(mtd, block, nblock, ptr);
          if (ret < 0)
            {
              return ptr - (FAR unsigned char *)buf > 0 ?
                     ptr - (FAR unsigned char *)buf : ret;
            }

          ptr        += copyout;
          self->nget += copyout;
          len        -= copyout;
        }
    }

  return ptr - (FAR unsigned char *)buf;
}

/****************************************************************************
 * Name: mtdsistream_getc
 ****************************************************************************/

static int mtdsistream_getc(FAR struct lib_sistream_s *self)
{
  unsigned char ch = 0;
  return mtdsistream_gets(self, &ch, 1) == 1 ? ch : EOF;
}

/****************************************************************************
 * Name: mtdsistream_seek
 ****************************************************************************/

static off_t mtdsistream_seek(FAR struct lib_sistream_s *self,
                              off_t offset, int whence)
{
  FAR struct lib_mtdsistream_s *stream =
    (FAR struct lib_mtdsistream_s *)self;
  off_t devsize = stream->geo.neraseblocks * stream->geo.erasesize;

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
 * Name: lib_mtdsistream_open
 ****************************************************************************/

int lib_mtdsistream_open(FAR struct lib_mtdsistream_s *stream,
                         FAR const char *name)
{
  int ret;

  if (stream == NULL || name == NULL)
    {
      return -EINVAL;
    }

  memset(stream, 0, sizeof(*stream));

  ret = find_mtddriver(name, &stream->inode);
  if (ret < 0)
    {
      return ret;
    }

  /* Add bad block management on top of the device.  Returns the device
   * unchanged when it reports no bad blocks, so NOR flash is unaffected.
   * The geometry is queried afterwards on purpose: the wrapper reports the
   * number of good blocks, which is what the size checks below have to use.
   */

  stream->mtd = mtd_bbm_initialize(stream->inode->u.i_mtd);
  if (stream->mtd == NULL)
    {
      ret = -ENOMEM;
      goto err;
    }

  if (stream->mtd->ioctl == NULL ||
      (stream->mtd->read == NULL && stream->mtd->bread == NULL) ||
      stream->mtd->ioctl(stream->mtd, MTDIOC_GEOMETRY,
                         (unsigned long)&stream->geo) < 0 ||
      stream->geo.blocksize <= 0 ||
      stream->geo.erasesize <= 0 ||
      stream->geo.neraseblocks <= 0)
    {
      ret = -EINVAL;
      goto err;
    }

  /* If byte-oriented read is available, use it directly without cache */

  if (stream->mtd->read != NULL)
    {
      stream->common.gets = mtdsistream_reads;
    }
  else
    {
      stream->cache = lib_malloc(stream->geo.blocksize);
      if (stream->cache == NULL)
        {
          ret = -ENOMEM;
          goto err;
        }

      stream->common.gets = mtdsistream_gets;
    }

  stream->eblock       = -1;
  stream->common.getc  = mtdsistream_getc;
  stream->common.seek  = mtdsistream_seek;

  return OK;

err:
  mtd_bbm_uninitialize(stream->mtd);
  close_mtddriver(stream->inode);
  stream->mtd   = NULL;
  stream->inode = NULL;
  return ret;
}

/****************************************************************************
 * Name: lib_mtdsistream_close
 ****************************************************************************/

void lib_mtdsistream_close(FAR struct lib_mtdsistream_s *stream)
{
  if (stream != NULL)
    {
      if (stream->cache != NULL)
        {
          lib_free(stream->cache);
          stream->cache = NULL;
        }

      mtd_bbm_uninitialize(stream->mtd);
      stream->mtd = NULL;

      if (stream->inode != NULL)
        {
          close_mtddriver(stream->inode);
          stream->inode = NULL;
        }
    }
}
