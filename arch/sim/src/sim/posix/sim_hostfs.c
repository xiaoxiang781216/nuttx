/****************************************************************************
 * arch/sim/src/sim/posix/sim_hostfs.c
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

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/ioctl.h>

#include <dirent.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

#include "hostfs.h"
#include "sim_internal.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: host_stat_convert
 ****************************************************************************/

static void host_stat_convert(struct stat *hostbuf, struct nuttx_stat_s *buf)
{
  /* Map the return values */

  buf->st_mode = hostbuf->st_mode & 07777;

  if (S_ISDIR(hostbuf->st_mode))
    {
      buf->st_mode |= NUTTX_S_IFDIR;
    }
  else if (S_ISREG(hostbuf->st_mode))
    {
      buf->st_mode |= NUTTX_S_IFREG;
    }
  else if (S_ISCHR(hostbuf->st_mode))
    {
      buf->st_mode |= NUTTX_S_IFCHR;
    }
  else if (S_ISBLK(hostbuf->st_mode))
    {
      buf->st_mode |= NUTTX_S_IFBLK;
    }
  else if (S_ISLNK(hostbuf->st_mode))
    {
      buf->st_mode |= NUTTX_S_IFLNK;
    }
  else if (S_ISFIFO(hostbuf->st_mode))
    {
      buf->st_mode |= NUTTX_S_IFIFO;
    }
  else if (S_ISSOCK(hostbuf->st_mode))
    {
      buf->st_mode |= NUTTX_S_IFSOCK;
    }
  else if (S_TYPEISSEM(hostbuf))
    {
      buf->st_mode |= NUTTX_S_IFSEM;
    }
  else if (S_TYPEISMQ(hostbuf))
    {
      buf->st_mode |= NUTTX_S_IFMQ;
    }
  else if (S_TYPEISSHM(hostbuf))
    {
      buf->st_mode |= NUTTX_S_IFSHM;
    }

  buf->st_dev          = hostbuf->st_dev;
  buf->st_ino          = hostbuf->st_ino;
  buf->st_nlink        = hostbuf->st_nlink;
  buf->st_uid          = hostbuf->st_uid;
  buf->st_gid          = hostbuf->st_gid;
  buf->st_rdev         = hostbuf->st_rdev;
  buf->st_size         = hostbuf->st_size;
#if defined(__APPLE__)
  buf->st_atim.tv_sec  = hostbuf->st_atimespec.tv_sec;
  buf->st_atim.tv_nsec = hostbuf->st_atimespec.tv_nsec;
  buf->st_mtim.tv_sec  = hostbuf->st_mtimespec.tv_sec;
  buf->st_mtim.tv_nsec = hostbuf->st_mtimespec.tv_nsec;
  buf->st_ctim.tv_sec  = hostbuf->st_ctimespec.tv_sec;
  buf->st_ctim.tv_nsec = hostbuf->st_ctimespec.tv_nsec;
#else
  buf->st_atim.tv_sec  = hostbuf->st_atim.tv_sec;
  buf->st_atim.tv_nsec = hostbuf->st_atim.tv_nsec;
  buf->st_mtim.tv_sec  = hostbuf->st_mtim.tv_sec;
  buf->st_mtim.tv_nsec = hostbuf->st_mtim.tv_nsec;
  buf->st_ctim.tv_sec  = hostbuf->st_ctim.tv_sec;
  buf->st_ctim.tv_nsec = hostbuf->st_ctim.tv_nsec;
#endif
  buf->st_blksize      = hostbuf->st_blksize;
  buf->st_blocks       = hostbuf->st_blocks;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: host_open
 ****************************************************************************/

int host_open(const char *pathname, int flags, int mode)
{
  int mapflags = 0;

  /* Perform flag mapping */

  if ((flags & NUTTX_O_RDWR) == NUTTX_O_RDWR)
    {
      mapflags = O_RDWR;
    }
  else if (flags & NUTTX_O_RDONLY)
    {
      mapflags = O_RDONLY;
    }
  else if (flags & NUTTX_O_WRONLY)
    {
      mapflags = O_WRONLY;
    }

  if (flags & NUTTX_O_APPEND)
    {
      mapflags |= O_APPEND;
    }

  if (flags & NUTTX_O_CREAT)
    {
      mapflags |= O_CREAT;
    }

  if (flags & NUTTX_O_EXCL)
    {
      mapflags |= O_EXCL;
    }

  if (flags & NUTTX_O_TRUNC)
    {
      mapflags |= O_TRUNC;
    }

  if (flags & NUTTX_O_NONBLOCK)
    {
      mapflags |= O_NONBLOCK;
    }

  if (flags & NUTTX_O_SYNC)
    {
      mapflags |= O_SYNC;
    }

#ifdef O_DIRECT
  if (flags & NUTTX_O_DIRECT)
    {
      mapflags |= O_DIRECT;
    }
#endif

  if (flags & NUTTX_O_CLOEXEC)
    {
      mapflags |= O_CLOEXEC;
    }

  if (flags & NUTTX_O_DIRECTORY)
    {
      mapflags |= O_DIRECTORY;
    }

  int ret = host_uninterruptible(open, pathname, mapflags, mode);
  if (ret == -1)
    {
      ret = -errno;
    }

  return ret;
}

/****************************************************************************
 * Name: host_close
 ****************************************************************************/

int host_close(int fd)
{
  /* Just call the close routine */

  int ret = host_uninterruptible(close, fd);
  if (ret == -1)
    {
      ret = -errno;
    }

  return ret;
}

/****************************************************************************
 * Name: host_read
 ****************************************************************************/

nuttx_ssize_t host_read(int fd, void *buf, nuttx_size_t count)
{
  /* Just call the read routine */

  nuttx_ssize_t ret = host_uninterruptible(read, fd, buf, count);
  if (ret == -1)
    {
      ret = -errno;
    }

  return ret;
}

/****************************************************************************
 * Name: host_write
 ****************************************************************************/

nuttx_ssize_t host_write(int fd, const void *buf, nuttx_size_t count)
{
  /* Just call the write routine */

  nuttx_ssize_t ret = host_uninterruptible(write, fd, buf, count);
  if (ret == -1)
    {
      ret = -errno;
    }

  return ret;
}

/****************************************************************************
 * Name: host_lseek
 ****************************************************************************/

nuttx_off_t host_lseek(int fd, nuttx_off_t pos, nuttx_off_t offset,
                       int whence)
{
  /* Just call the lseek routine */

  nuttx_off_t ret = host_uninterruptible(lseek, fd, offset, whence);
  if (ret == (nuttx_off_t)-1)
    {
      ret = -errno;
    }

  return ret;
}

/****************************************************************************
 * Name: host_ioctl
 ****************************************************************************/

int host_ioctl(int fd, int request, unsigned long arg)
{
  /* Just call the ioctl routine */

  return host_uninterruptible(ioctl, fd, request, arg);
}

/****************************************************************************
 * Name: host_sync
 ****************************************************************************/

void host_sync(int fd)
{
  /* Just call the sync routine */

  host_uninterruptible_no_return(fsync, fd);
}

/****************************************************************************
 * Name: host_dup
 ****************************************************************************/

int host_dup(int fd)
{
  return host_uninterruptible(dup, fd);
}

/****************************************************************************
 * Name: host_fstat
 ****************************************************************************/

int host_fstat(int fd, struct nuttx_stat_s *buf)
{
  struct stat hostbuf;
  int ret;

  /* Call the host's stat routine */

  ret = host_uninterruptible(fstat, fd, &hostbuf);
  if (ret < 0)
    {
      ret = -errno;
    }

  /* Map the return values */

  host_stat_convert(&hostbuf, buf);
  return ret;
}

/****************************************************************************
 * Name: host_fchstat
 ****************************************************************************/

int host_fchstat(int fd, const struct nuttx_stat_s *buf, int flags)
{
  struct timespec times[2];
  int ret;

  if (flags & NUTTX_CH_STAT_MODE)
    {
      ret = host_uninterruptible(fchmod, fd, buf->st_mode);
      if (ret < 0)
        {
          return -errno;
        }
    }

  if (flags & (NUTTX_CH_STAT_UID | NUTTX_CH_STAT_GID))
    {
      ret = host_uninterruptible(fchown, fd, buf->st_uid, buf->st_gid);
      if (ret < 0)
        {
          return -errno;
        }
    }

  if (flags & (NUTTX_CH_STAT_ATIME | NUTTX_CH_STAT_MTIME))
    {
      if (flags & NUTTX_CH_STAT_ATIME)
        {
          times[0].tv_sec  = buf->st_atim.tv_sec;
          times[0].tv_nsec = buf->st_atim.tv_nsec;
        }
      else
        {
          times[0].tv_sec = 0;
          times[0].tv_nsec = UTIME_OMIT;
        }

      if (flags & NUTTX_CH_STAT_MTIME)
        {
          times[1].tv_sec  = buf->st_mtim.tv_sec;
          times[1].tv_nsec = buf->st_mtim.tv_nsec;
        }
      else
        {
          times[1].tv_sec = 0;
          times[1].tv_nsec = UTIME_OMIT;
        }

      ret = host_uninterruptible(futimens, fd, times);
      if (ret < 0)
        {
          return -errno;
        }
    }

  return 0;
}

/****************************************************************************
 * Name: host_truncate
 ****************************************************************************/

int host_ftruncate(int fd, nuttx_off_t length)
{
  int ret = host_uninterruptible(ftruncate, fd, length);
  if (ret < 0)
    {
      ret = -errno;
    }

  return ret;
}

/****************************************************************************
 * Name: host_opendir
 ****************************************************************************/

void *host_opendir(const char *name)
{
  /* Return the host DIR pointer */

  return (void *)host_uninterruptible(opendir, name);
}

/****************************************************************************
 * Name: host_readdir
 ****************************************************************************/

int host_readdir(void *dirp, struct nuttx_dirent_s *entry)
{
  struct dirent *ent;

  /* Call the host's readdir routine */

  ent = host_uninterruptible(readdir, dirp);
  if (ent != NULL)
    {
      /* Copy the entry name */

      strncpy(entry->d_name, ent->d_name, sizeof(entry->d_name) - 1);
      entry->d_name[sizeof(entry->d_name) - 1] = 0;

      /* Map the type */

      if (ent->d_type == DT_REG)
        {
          entry->d_type = NUTTX_DTYPE_FILE;
        }
      else if (ent->d_type == DT_FIFO)
        {
          entry->d_type = NUTTX_DTYPE_FIFO;
        }
      else if (ent->d_type == DT_CHR)
        {
          entry->d_type = NUTTX_DTYPE_CHR;
        }
      else if (ent->d_type == DT_BLK)
        {
          entry->d_type = NUTTX_DTYPE_BLK;
        }
      else if (ent->d_type == DT_DIR)
        {
          entry->d_type = NUTTX_DTYPE_DIRECTORY;
        }
      else if (ent->d_type == DT_LNK)
        {
          entry->d_type = NUTTX_DTYPE_LINK;
        }
      else if (ent->d_type == DT_SOCK)
        {
          entry->d_type = NUTTX_DTYPE_SOCK;
        }
      else
        {
          entry->d_type = NUTTX_DTYPE_UNKNOWN;
        }

      return 0;
    }

  return -ENOENT;
}

/****************************************************************************
 * Name: host_rewinddir
 ****************************************************************************/

void host_rewinddir(void *dirp)
{
  /* Just call the rewinddir routine */

  host_uninterruptible_no_return(rewinddir, dirp);
}

/****************************************************************************
 * Name: host_closedir
 ****************************************************************************/

int host_closedir(void *dirp)
{
  int ret = host_uninterruptible(closedir, dirp);
  if (ret < 0)
    {
      ret = -errno;
    }

  return ret;
}

/****************************************************************************
 * Name: host_statfs
 ****************************************************************************/

int host_statfs(const char *path, struct nuttx_statfs_s *buf)
{
  int            ret;
  struct statvfs hostbuf;

  /* Call the host's statfs routine */

  ret = host_uninterruptible(statvfs, path, &hostbuf);
  if (ret < 0)
    {
      ret = -errno;
    }

  /* Map the struct statfs value */

  buf->f_type    = 0; /* hostfs overwrites f_type anyway */
  buf->f_namelen = hostbuf.f_namemax;
  buf->f_bsize   = hostbuf.f_bsize;
  buf->f_blocks  = hostbuf.f_blocks;
  buf->f_bfree   = hostbuf.f_bfree;
  buf->f_bavail  = hostbuf.f_bavail;
  buf->f_files   = hostbuf.f_files;
  buf->f_ffree   = hostbuf.f_ffree;

  return ret;
}

/****************************************************************************
 * Name: host_unlink
 ****************************************************************************/

int host_unlink(const char *pathname)
{
  int ret = host_uninterruptible(unlink, pathname);
  if (ret < 0)
    {
      ret = -errno;
    }

  return ret;
}

/****************************************************************************
 * Name: host_mkdir
 ****************************************************************************/

int host_mkdir(const char *pathname, int mode)
{
  /* Just call the host's mkdir routine */

  int ret = host_uninterruptible(mkdir, pathname, mode);
  if (ret < 0)
    {
      ret = -errno;
    }

  return ret;
}

/****************************************************************************
 * Name: host_rmdir
 ****************************************************************************/

int host_rmdir(const char *pathname)
{
  int ret = host_uninterruptible(rmdir, pathname);
  if (ret < 0)
    {
      ret = -errno;
    }

  return ret;
}

/****************************************************************************
 * Name: host_rename
 ****************************************************************************/

int host_rename(const char *oldpath, const char *newpath)
{
  int ret = host_uninterruptible(rename, oldpath, newpath);
  if (ret < 0)
    {
      ret = -errno;
    }

  return ret;
}

/****************************************************************************
 * Name: host_stat
 ****************************************************************************/

int host_stat(const char *path, struct nuttx_stat_s *buf)
{
  struct stat hostbuf;
  int ret;

  /* Call the host's stat routine */

  ret = host_uninterruptible(stat, path, &hostbuf);
  if (ret < 0)
    {
      ret = -errno;
    }

  /* Map the return values */

  host_stat_convert(&hostbuf, buf);
  return ret;
}

/****************************************************************************
 * Name: host_chstat
 ****************************************************************************/

int host_chstat(const char *path, const struct nuttx_stat_s *buf, int flags)
{
  struct timespec times[2];
  int ret;

  if (flags & NUTTX_CH_STAT_MODE)
    {
      ret = host_uninterruptible(chmod, path, buf->st_mode);
      if (ret < 0)
        {
          return -errno;
        }
    }

  if (flags & (NUTTX_CH_STAT_UID | NUTTX_CH_STAT_GID))
    {
      ret = host_uninterruptible(chown, path, buf->st_uid, buf->st_gid);
      if (ret < 0)
        {
          return -errno;
        }
    }

  if (flags & (NUTTX_CH_STAT_ATIME | NUTTX_CH_STAT_MTIME))
    {
      if (flags & NUTTX_CH_STAT_ATIME)
        {
          times[0].tv_sec  = buf->st_atim.tv_sec;
          times[0].tv_nsec = buf->st_atim.tv_nsec;
        }
      else
        {
          times[0].tv_sec = 0;
          times[0].tv_nsec = UTIME_OMIT;
        }

      if (flags & NUTTX_CH_STAT_MTIME)
        {
          times[1].tv_sec  = buf->st_mtim.tv_sec;
          times[1].tv_nsec = buf->st_mtim.tv_nsec;
        }
      else
        {
          times[1].tv_sec = 0;
          times[1].tv_nsec = UTIME_OMIT;
        }

      ret = host_uninterruptible(utimensat, AT_FDCWD, path, times, 0);
      if (ret < 0)
        {
          return -errno;
        }
    }

  return 0;
}

/****************************************************************************
 * Name: host_link
 ****************************************************************************/

#ifdef CONFIG_FS_LINKS
int host_link(const char *path1, const char *path2)
{
  int ret = host_uninterruptible(link, path1, path2);
  if (ret < 0)
    {
      ret = -errno;
    }

  return ret;
}

/****************************************************************************
 * Name: host_symlink
 ****************************************************************************/

int host_symlink(const char *target, const char *linkpath)
{
  int ret = host_uninterruptible(symlink, target, linkpath);
  if (ret < 0)
    {
      ret = -errno;
    }

  return ret;
}

/****************************************************************************
 * Name: host_readlink
 ****************************************************************************/

nuttx_ssize_t host_readlink(const char *path, char *buf,
                            nuttx_size_t bufsize)
{
  ssize_t ret = host_uninterruptible(readlink, path, buf, bufsize);
  if (ret < 0)
    {
      ret = -errno;
    }

  return ret;
}

/****************************************************************************
 * Name: host_lstat
 ****************************************************************************/

int host_lstat(const char *path, struct nuttx_stat_s *buf)
{
  struct stat hostbuf;
  int ret;

  /* Call the host's lstat routine */

  ret = host_uninterruptible(lstat, path, &hostbuf);
  if (ret < 0)
    {
      ret = -errno;
    }

  /* Map the return values */

  host_stat_convert(&hostbuf, buf);
  return ret;
}
#endif /* CONFIG_FS_LINKS */
