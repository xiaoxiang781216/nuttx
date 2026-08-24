/****************************************************************************
 * libs/libc/fuse/lib_buffer.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to you under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
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

#include <string.h>
#include <unistd.h>
#include <sys/param.h>
#include <fuse_common.h>

#include <nuttx/lib/lib.h>

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static ssize_t fuse_buf_write(FAR const struct fuse_buf *dst,
                              size_t dst_off,
                              FAR const struct fuse_buf *src,
                              size_t src_off, size_t len)
{
  size_t copied = 0;
  ssize_t ret;

  while (len > 0)
    {
      if ((dst->flags & FUSE_BUF_FD_SEEK) != 0)
        {
          ret = pwrite(dst->fd, (FAR const char *)src->mem + src_off, len,
                       dst->pos + dst_off);
        }
      else
        {
          ret = write(dst->fd, (FAR const char *)src->mem + src_off, len);
        }

      if (ret <= 0)
        {
          if (copied == 0)
            {
              return ret < 0 ? -get_errno() : 0;
            }

          break;
        }

      copied += ret;
      if ((dst->flags & FUSE_BUF_FD_RETRY) == 0)
        {
          break;
        }

      src_off += ret;
      dst_off += ret;
      len -= ret;
    }

  return copied;
}

static ssize_t fuse_buf_read(FAR const struct fuse_buf *dst, size_t dst_off,
                             FAR const struct fuse_buf *src,
                             size_t src_off, size_t len)
{
  size_t copied = 0;
  ssize_t ret;

  while (len > 0)
    {
      if ((src->flags & FUSE_BUF_FD_SEEK) != 0)
        {
          ret = pread(src->fd, (FAR char *)dst->mem + dst_off, len,
                      src->pos + src_off);
        }
      else
        {
          ret = read(src->fd, (FAR char *)dst->mem + dst_off, len);
        }

      if (ret <= 0)
        {
          if (copied == 0)
            {
              return ret < 0 ? -get_errno() : 0;
            }

          break;
        }

      copied += ret;
      if ((src->flags & FUSE_BUF_FD_RETRY) == 0)
        {
          break;
        }

      dst_off += ret;
      src_off += ret;
      len -= ret;
    }

  return copied;
}

static ssize_t fuse_buf_fd_to_fd(FAR const struct fuse_buf *dst,
                                 size_t dst_off,
                                 FAR const struct fuse_buf *src,
                                 size_t src_off, size_t len)
{
  char buf[512];
  struct fuse_buf tmp =
  {
    .size = sizeof(buf),
    .flags = 0,
    .mem = buf,
  };

  size_t copied = 0;

  while (len > 0)
    {
      size_t this_len = MIN(tmp.size, len);
      size_t read_len;
      ssize_t ret;

      ret = fuse_buf_read(&tmp, 0, src, src_off, this_len);
      if (ret <= 0)
        {
          if (copied == 0)
            {
              return ret;
            }

          break;
        }

      read_len = ret;
      ret = fuse_buf_write(dst, dst_off, &tmp, 0, read_len);
      if (ret <= 0)
        {
          if (copied == 0)
            {
              return ret;
            }

          break;
        }

      copied += ret;

      if (ret < this_len)
        {
          break;
        }

      dst_off += ret;
      src_off += ret;
      len -= ret;
    }

  return copied;
}

static inline ssize_t
fuse_buf_copy_one(FAR const struct fuse_buf *dst,
                  size_t dst_off,
                  FAR const struct fuse_buf *src,
                  size_t src_off, size_t len,
                  enum fuse_buf_copy_flags flags)
{
  bool src_is_fd = (src->flags & FUSE_BUF_IS_FD) != 0;
  bool dst_is_fd = (dst->flags & FUSE_BUF_IS_FD) != 0;

  if (!src_is_fd && !dst_is_fd)
    {
      FAR char *dstmem = (FAR char *)dst->mem + dst_off;
      FAR char *srcmem = (FAR char *)src->mem + src_off;

      if (dstmem != srcmem)
        {
          if (dstmem + len <= srcmem || srcmem + len <= dstmem)
            {
              memcpy(dstmem, srcmem, len);
            }
          else
            {
              memmove(dstmem, srcmem, len);
            }
        }

      return len;
    }
  else if (!src_is_fd)
    {
      return fuse_buf_write(dst, dst_off, src, src_off, len);
    }
  else if (!dst_is_fd)
    {
      return fuse_buf_read(dst, dst_off, src, src_off, len);
    }
  else
    {
      return fuse_buf_fd_to_fd(dst, dst_off, src, src_off, len);
    }
}

static inline FAR const struct fuse_buf *
fuse_bufvec_current(FAR const struct fuse_bufvec *bufv)
{
  if (bufv->idx < bufv->count)
    {
      return &bufv->buf[bufv->idx];
    }

  return NULL;
}

static inline int
fuse_bufvec_advance(FAR struct fuse_bufvec *bufv, size_t len)
{
  FAR const struct fuse_buf *buf = fuse_bufvec_current(bufv);

  if (buf == NULL)
    {
      return 0;
    }

  bufv->off += len;
  if (bufv->off == buf->size)
    {
      bufv->idx++;
      if (bufv->idx == bufv->count)
        {
          return 0;
        }

      bufv->off = 0;
    }

  return 1;
}

static inline bool
fuse_bufvec_has_fd(FAR const struct fuse_bufvec *bufv)
{
  size_t i;

  for (i = 0; i < bufv->count; i++)
    {
      if (bufv->buf[i].flags & FUSE_BUF_IS_FD)
        {
          return true;
        }
    }

  return false;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void fuse_buf_free(FAR struct fuse_buf *buf)
{
  if (buf->mem != NULL)
    {
      lib_free(buf->mem);
      buf->mem = NULL;
    }
}

size_t fuse_buf_size(FAR const struct fuse_bufvec *bufv)
{
  size_t size = 0;
  size_t i;

  for (i = 0; i < bufv->count; i++)
    {
      if (bufv->buf[i].size >= SIZE_MAX - size)
        {
          return SIZE_MAX;
        }

      size += bufv->buf[i].size;
    }

  return size;
}

ssize_t fuse_buf_copy(FAR struct fuse_bufvec *dstv,
                      FAR struct fuse_bufvec *srcv,
                      enum fuse_buf_copy_flags flags)
{
  size_t copied = 0;

  if (dstv == srcv)
    {
      return fuse_buf_size(dstv);
    }

  for (; ; )
    {
      FAR const struct fuse_buf *src = fuse_bufvec_current(srcv);
      FAR const struct fuse_buf *dst = fuse_bufvec_current(dstv);
      size_t src_len;
      size_t dst_len;
      size_t len;
      ssize_t ret;

      if (src == NULL || dst == NULL)
        {
          break;
        }

      src_len = src->size - srcv->off;
      dst_len = dst->size - dstv->off;
      len = MIN(src_len, dst_len);

      ret = fuse_buf_copy_one(dst, dstv->off, src, srcv->off, len, flags);
      if (ret < 0)
        {
          if (copied == 0)
            {
              return ret;
            }

          break;
        }

      copied += ret;

      if (fuse_bufvec_advance(srcv, ret) == 0 ||
          fuse_bufvec_advance(dstv, ret) == 0)
        {
          break;
        }

      if (ret < len)
        {
          break;
        }
    }

  return copied;
}

ssize_t fuse_buf_to_iov(FAR struct fuse_bufvec *bufv,
                        enum fuse_buf_copy_flags flags,
                        FAR struct iovec *iov, FAR bool *free)
{
  size_t count = 0;

  *free = fuse_bufvec_has_fd(bufv);
  if (*free)
    {
      size_t len = fuse_buf_size(bufv);

      if (len != 0)
        {
          struct fuse_bufvec tmpv = FUSE_BUFVEC_INIT(len);
          ssize_t ret;

          tmpv.buf[0].mem = lib_malloc(len);
          if (tmpv.buf[0].mem == NULL)
            {
              return -ENOMEM;
            }

          ret = fuse_buf_copy(&tmpv, bufv, flags);
          if (ret != len)
            {
              lib_free(tmpv.buf[0].mem);
              return ret < 0 ? ret : -EIO;
            }

          iov[count].iov_base  = tmpv.buf[0].mem;
          iov[count++].iov_len = len;
        }
    }
  else
    {
      do
        {
          FAR const struct fuse_buf *buf = fuse_bufvec_current(bufv);

          if (buf == NULL)
            {
              break;
            }

          iov[count].iov_base = (FAR char *)buf->mem + bufv->off;
          iov[count].iov_len  = buf->size - bufv->off;
        }
      while (fuse_bufvec_advance(bufv, iov[count++].iov_len));
    }

  return count;
}
