/****************************************************************************
 * drivers/trace32/hostfs_t32term.c
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
#include <nuttx/lib/lib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>

#include "t32term/t32term.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define POSIX_TO_T32_FLAGS(pf)                                               \
    (                                                                        \
      (((pf) & O_RDWR)   ? T32_TERM_O_RDWR  :                                \
       ((pf) & O_WRONLY) ? T32_TERM_O_WRONLY : T32_TERM_O_RDONLY)            \
    | (((pf) & O_CREAT) && ((pf) & O_TRUNC)  ? T32_TERM_O_CREATE_TRUNC  : 0) \
    | (((pf) & O_CREAT) && ((pf) & O_APPEND) ? T32_TERM_O_CREATE_APPEND : 0) \
    | (((pf) & O_BINARY) ? T32_TERM_O_BINARY : T32_TERM_O_ASCII)             \
    )

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static char *path_to_t32(const char *path, char *t32name)
{
  size_t i;

  for (i = 0; path[i] != '\0'; i++)
    {
      if (path[i] == '/')
        {
          t32name[i] = '_';
        }
      else
        {
          t32name[i] = path[i];
        }
    }

  t32name[i] = '\0';
  return t32name;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int host_open(FAR const char *pathname, int flags, int mode)
{
  FAR char *buf = lib_get_tempbuffer(PATH_MAX);
  int handler;

  if (buf == NULL)
    {
      return -ENOMEM;
    }

  handler = T32_Term_OpenFile(path_to_t32(pathname, buf),
                              POSIX_TO_T32_FLAGS(flags));
  lib_put_tempbuffer(buf);
  return handler;
}

int host_close(int fd)
{
  return T32_Term_CloseFile(fd);
}

ssize_t host_read(int fd, FAR void *buf, size_t count)
{
  return T32_Term_ReadFile(fd, buf, count);
}

ssize_t host_write(int fd, FAR const void *buf, size_t count)
{
  return T32_Term_WriteFile(fd, buf, count);
}

off_t host_lseek(int fd, off_t pos, off_t offset, int whence)
{
  switch (whence)
    {
      case SEEK_CUR:
        pos += offset;
      case SEEK_SET:
        pos = offset;
        break;
      case SEEK_END:
        pos = T32_Term_TellSizeFile(fd) + offset;
      default:
        break;
    }

  return T32_Term_SeekFile(fd, pos);
}

int host_ioctl(int fd, int request, unsigned long arg)
{
  return -ENOSYS;
}

void host_sync(int fd)
{
}

int host_dup(int fd)
{
  return -ENOSYS;
}

int host_fstat(int fd, struct stat *buf)
{
  return -ENOSYS;
}

int host_fchstat(int fd, FAR const struct stat *buf, int flags)
{
  return -ENOSYS;
}

int host_ftruncate(int fd, off_t length)
{
  return -ENOSYS;
}

void *host_opendir(FAR const char *name)
{
  return NULL;
}

int host_readdir(FAR void *dirp, FAR struct dirent *entry)
{
  return -ENOSYS;
}

void host_rewinddir(FAR void *dirp)
{
}

int host_closedir(FAR void *dirp)
{
  return -ENOSYS;
}

int host_statfs(FAR const char *path, FAR struct statfs *buf)
{
  return -ENOSYS;
}

int host_unlink(FAR const char *pathname)
{
  return -ENOSYS;
}

int host_mkdir(FAR const char *pathname, int mode)
{
  return -ENOSYS;
}

int host_rmdir(FAR const char *pathname)
{
  FAR char *buf = lib_get_tempbuffer(PATH_MAX);
  int ret;
  if (buf == NULL)
    {
      return -ENOMEM;
    }

  T32_Term_Remove(path_to_t32(pathname, buf));
  lib_put_tempbuffer(buf);
  return ret;
}

int host_rename(FAR const char *oldpath, FAR const char *newpath)
{
  FAR char *buf1;
  FAR char *buf2;
  int ret;

  buf1 = lib_get_tempbuffer(PATH_MAX);
  if (buf1 == NULL)
    {
      return -ENOMEM;
    }

  buf2 = lib_get_tempbuffer(PATH_MAX);
  if (buf2 == NULL)
    {
      lib_put_tempbuffer(buf1);
      return -ENOMEM;
    }

  ret = T32_Term_Rename(path_to_t32(oldpath, buf1),
                        path_to_t32(newpath, buf2));

  lib_put_tempbuffer(buf1);
  lib_put_tempbuffer(buf2);
  return ret;
}

int host_stat(FAR const char *path, FAR struct stat *buf)
{
  return -ENOSYS;
}

int host_chstat(FAR const char *path, FAR const struct stat *buf, int flags)
{
  return -ENOSYS;
}
