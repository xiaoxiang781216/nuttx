/****************************************************************************
 * fs/yaffs/yaffs_vfs.c
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

#include <debug.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <nuttx/clock.h>
#include <nuttx/crc16.h>
#include <nuttx/fs/fs.h>
#include <nuttx/fs/ioctl.h>
#include <nuttx/kmalloc.h>
#include <nuttx/mtd/mtd.h>
#include <nuttx/mutex.h>

#include <sys/mount.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/statfs.h>

#include "inode/inode.h"
#include "fs_heap.h"

#include "yaffs_guts.h"
#include "direct/yaffsfs.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define mountpt_to_yaffs_dev(mountpt) \
            (((FAR struct yaffs_mountpt_s *)((mountpt)->i_private))->dev)
#define filep_to_yaffs_handle(filep) \
            (((FAR struct yaffs_file_s *)((filep)->f_priv))->handle)
#define dir_to_yaffs_dir(dir) \
            (((FAR struct yaffs_dir_s *)dir)->dir)
#define yaffs_dev_to_mtd(dev) \
            ((FAR struct mtd_dev_s *)((dev)->driver_context))

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* This structure represents the overall mountpoint state. An instance of
 * this structure is retained as inode private data on each mountpoint that
 * is mounted with a yaffs filesystem.
 */

struct yaffs_mountpt_s
{
  FAR struct inode      *drv;
  FAR struct mtd_dev_s  *mtd;
  FAR struct yaffs_dev  *dev;
  struct mtd_geometry_s geo;
};

struct yaffs_file_s
{
  char path[PATH_MAX + 1];
  int  handle;
};

struct yaffs_dir_s
{
  struct fs_dirent_s base;
  FAR yaffs_DIR      *dir;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int     yaffs_vfs_open(FAR struct file *filep,
                              FAR const char *relpath,
                              int oflags, mode_t mode);
static int     yaffs_vfs_close(FAR struct file *filep);
static ssize_t yaffs_vfs_read(FAR struct file *filep,
                              FAR char *buffer, size_t buflen);
static ssize_t yaffs_vfs_write(FAR struct file *filep,
                               FAR const char *buffer, size_t buflen);
static off_t   yaffs_vfs_seek(FAR struct file *filep, off_t offset,
                              int whence);
static int     yaffs_vfs_ioctl(FAR struct file *filep, int cmd,
                               unsigned long arg);

static int     yaffs_vfs_sync(FAR struct file *filep);
static int     yaffs_vfs_dup(FAR const struct file *oldp,
                             FAR struct file *newp);
static int     yaffs_vfs_fstat(FAR const struct file *filep,
                               FAR struct stat *buf);
static int     yaffs_vfs_fchstat(FAR const struct file *filep,
                                 FAR const struct stat *buf, int flags);
static int     yaffs_vfs_truncate(FAR struct file *filep,
                                  off_t length);

static int     yaffs_vfs_opendir(FAR struct inode *mountpt,
                                 FAR const char *relpath,
                                 FAR struct fs_dirent_s **dir);
static int     yaffs_vfs_closedir(FAR struct inode *mountpt,
                                  FAR struct fs_dirent_s *dir);
static int     yaffs_vfs_readdir(FAR struct inode *mountpt,
                                 FAR struct fs_dirent_s *dir,
                                 FAR struct dirent *entry);
static int     yaffs_vfs_rewinddir(FAR struct inode *mountpt,
                                   FAR struct fs_dirent_s *dir);

static int     yaffs_vfs_bind(FAR struct inode *driver,
                              FAR const void *data, FAR void **handle);
static int     yaffs_vfs_unbind(FAR void *handle, FAR struct inode **driver,
                                unsigned int flags);
static int     yaffs_vfs_statfs(FAR struct inode *mountpt,
                                FAR struct statfs *buf);

static int     yaffs_vfs_unlink(FAR struct inode *mountpt,
                                FAR const char *relpath);
static int     yaffs_vfs_mkdir(FAR struct inode *mountpt,
                               FAR const char *relpath, mode_t mode);
static int     yaffs_vfs_rmdir(FAR struct inode *mountpt,
                               FAR const char *relpath);
static int     yaffs_vfs_rename(FAR struct inode *mountpt,
                                FAR const char *oldrelpath,
                                FAR const char *newrelpath);
static int     yaffs_vfs_stat(FAR struct inode *mountpt,
                              FAR const char *relpath, FAR struct stat *buf);
static int     yaffs_vfs_chstat(FAR struct inode *mountpt,
                                FAR const char *relpath,
                                FAR const struct stat *buf, int flags);
static int     yaffs_vfs_syncfs(FAR struct inode *mountpt);
#ifdef CONFIG_FS_LINKS
static int     yaffs_vfs_link(FAR struct inode *mountpt,
                              FAR const char *relpath1,
                              FAR const char *relpath2);
static int     yaffs_vfs_symlink(FAR struct inode *mountpt,
                                 FAR const char *path1,
                                 FAR const char *relpath2);
static ssize_t yaffs_vfs_readlink(FAR struct inode *mountpt,
                                  FAR const char *relpath,
                                  FAR char *buf, size_t bufsize);
static int     yaffs_vfs_lstat(FAR struct inode *mountpt,
                               FAR const char *relpath,
                               FAR struct stat *buf);
#endif
/****************************************************************************
 * Public Data
 ****************************************************************************/

/* See fs_mount.c -- this structure is explicitly extern'ed there.
 * We use the old-fashioned kind of initializers so that this will compile
 * with any compiler.
 */

const struct mountpt_operations g_yaffs_operations =
{
  yaffs_vfs_open,          /* open */
  yaffs_vfs_close,         /* close */
  yaffs_vfs_read,          /* read */
  yaffs_vfs_write,         /* write */
  yaffs_vfs_seek,          /* seek */
  yaffs_vfs_ioctl,         /* ioctl */
  yaffs_vfs_truncate,      /* truncate */
  NULL,                    /* poll */
  NULL,                    /* readv */
  NULL,                    /* writev */
#ifdef CONFIG_FS_FILEMAP
  NULL,                    /* mmap */
#endif

  yaffs_vfs_sync,          /* sync */
  yaffs_vfs_dup,           /* dup */
  yaffs_vfs_fstat,         /* fstat */
  yaffs_vfs_fchstat,       /* fchstat */

  yaffs_vfs_opendir,       /* opendir */
  yaffs_vfs_closedir,      /* closedir */
  yaffs_vfs_readdir,       /* readdir */
  yaffs_vfs_rewinddir,     /* rewinddir */

  yaffs_vfs_bind,          /* bind */
  yaffs_vfs_unbind,        /* unbind */
  yaffs_vfs_statfs,        /* statfs */

  yaffs_vfs_unlink,        /* unlink */
  yaffs_vfs_mkdir,         /* mkdir */
  yaffs_vfs_rmdir,         /* rmdir */
  yaffs_vfs_rename,        /* rename */
  yaffs_vfs_stat,          /* stat */
  yaffs_vfs_chstat,        /* chstat */
  yaffs_vfs_syncfs         /* syncfs */
#ifdef CONFIG_FS_LINKS
  , yaffs_vfs_link,          /* link */
  yaffs_vfs_symlink,         /* symlink */
  yaffs_vfs_readlink,        /* readlink */
  yaffs_vfs_lstat            /* lstat */
#endif
};

/****************************************************************************
 * External Functions
 ****************************************************************************/

extern int yaffs_format_reldev(FAR struct yaffs_dev *dev, int unmount_flag,
                               int force_unmount_flag, int remount_flag);

extern void yaffs_remove_device(FAR struct yaffs_dev *dev);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: yaffs_vfs_open
 ****************************************************************************/

static int yaffs_vfs_open(FAR struct file *filep,
                          FAR const char *relpath, int oflags, mode_t mode)
{
  FAR struct yaffs_dev *dev;
  FAR struct yaffs_file_s *priv;
  int handle;
  off_t pos;
#ifdef CONFIG_FS_YAFFS_POSIX_TIMESTAMP
  struct yaffs_stat stat_buf;
  bool existed;
#endif

  dev = mountpt_to_yaffs_dev(filep->f_inode);

#ifdef CONFIG_FS_YAFFS_POSIX_TIMESTAMP
  /* Check if file exists before opening */

  existed = (yaffs_stat_reldev(dev, relpath, &stat_buf) == 0);
#endif

  /* Allocate memory for the open file */

  priv = fs_heap_zalloc(sizeof(*priv));
  if (priv == NULL)
    {
      return -ENOMEM;
    }

  handle = yaffs_open_reldev(dev, relpath, oflags, mode);
  if (handle < 0)
    {
      goto errout;
    }

  strlcpy(priv->path, relpath, sizeof(priv->path));

  /* In append mode, need to set the file pointer to end of the file */

  if (filep->f_oflags & O_APPEND)
    {
      pos = yaffs_lseek(handle, 0, SEEK_END);
      if (pos < 0)
        {
          goto errout_with_file;
        }

      filep->f_pos = pos;
    }

  priv->handle = handle;

  /* Attach the private date to the struct file instance */

  filep->f_priv = priv;

#ifdef CONFIG_FS_YAFFS_POSIX_TIMESTAMP
  /* A newly created or truncated file must have its mtime and ctime
   * updated. Route through fchstat now that f_priv is attached.
   */

  if ((oflags & O_TRUNC) || ((oflags & O_CREAT) && !existed))
    {
      struct stat st;
      struct timespec ts;

      nxclock_gettime(CLOCK_REALTIME, &ts);
      st.st_mtime = st.st_ctime = ts.tv_sec;
      yaffs_vfs_fchstat(filep, &st, CH_STAT_MTIME | CH_STAT_CTIME);
    }
#endif

  return 0;

errout_with_file:
  yaffs_close(handle);
errout:
  fs_heap_free(priv);

  return yaffsfs_GetLastError();
}

/****************************************************************************
 * Name: yaffs_vfs_close
 ****************************************************************************/

static int yaffs_vfs_close(FAR struct file *filep)
{
  FAR struct yaffs_file_s *priv;
  int ret;

  priv = filep->f_priv;
  ret = yaffs_close(priv->handle);
  if (ret >= 0)
    {
      fs_heap_free(priv);
      return 0;
    }

  return yaffsfs_GetLastError();
}

/****************************************************************************
 * Name: yaffs_vfs_settimes
 *
 * Description:
 *   Set the timestamps selected by 'flags' on 'obj' to the values in 'buf',
 *   mark the object dirty, and optionally flush it to flash.  This is the
 *   single entry point for the timestamp updates from read/write/readdir
 *   (flush == false) and from fchstat/utimes (flush == true).
 *
 *   The caller must hold the yaffs global lock (yaffsfs_Lock) and pass a
 *   resolved object, so the same helper serves the fd-based callers
 *   (yaffs_get_obj_from_fd) and the directory-based caller
 *   (yaffs_get_obj_from_dir).
 *
 *   A NULL buf means "use the current time for every selected field".
 *   Fields are set individually according to 'flags', so unlike
 *   yaffs_futime() there is no need to read back the untouched field first.
 *
 *   When flush is false the value is updated in memory only and the flash
 *   write is deferred to the next flush (close/sync/umount), mirroring the
 *   Linux lazytime behaviour.  This avoids the per-operation object-header
 *   rewrite that would otherwise allocate a new chunk and drive garbage
 *   collection on the log-structured yaffs backend -- prohibitively slow on
 *   a read-heavy workload (measured hundreds of seconds over a conformance
 *   run).  The value is still updated on every call, so in-session
 *   stat()/fstat() observe it immediately.
 *
 ****************************************************************************/

static int yaffs_vfs_settimes(FAR struct yaffs_obj *obj, int flags,
                              FAR const struct stat *buf, bool flush)
{
  struct timespec ts;
  time_t atime;
  time_t mtime;
  time_t ctime;

  if (obj->my_dev->read_only)
    {
      yaffsfs_SetError(-EROFS);
      return -1;
    }

  if (buf == NULL)
    {
      nxclock_gettime(CLOCK_REALTIME, &ts);
      atime = mtime = ctime = ts.tv_sec;
    }
  else
    {
      atime = buf->st_atime;
      mtime = buf->st_mtime;
      ctime = buf->st_ctime;
    }

  if (flags & CH_STAT_ATIME)
    {
      obj->yst_atime = atime;
    }

  if (flags & CH_STAT_MTIME)
    {
      obj->yst_mtime = mtime;
    }

  if (flags & CH_STAT_CTIME)
    {
      obj->yst_ctime = ctime;
    }

  obj->dirty = 1;
  if (flush)
    {
      return yaffs_flush_file(obj, 0, 0, 0) == YAFFS_OK ? 0 : -1;
    }

  return 0;
}

/****************************************************************************
 * Name: yaffs_vfs_read
 ****************************************************************************/

static ssize_t yaffs_vfs_read(FAR struct file *filep,
                              FAR char *buffer, size_t buflen)
{
  int handle;
  int totalread;

  handle = filep_to_yaffs_handle(filep);
  totalread = yaffs_read(handle, buffer, buflen);
  if (totalread >= 0)
    {
      filep->f_pos += totalread;

#ifdef CONFIG_FS_YAFFS_POSIX_TIMESTAMP
      /* Update access time after a successful read (unless O_NOATIME) */

      if (!(filep->f_oflags & O_NOATIME))
        {
          FAR struct yaffs_obj *obj;

          yaffsfs_Lock();
          obj = yaffs_get_obj_from_fd(handle);
          if (obj != NULL)
            {
              yaffs_vfs_settimes(obj, CH_STAT_ATIME, NULL, false);
            }

          yaffsfs_Unlock();
        }
#endif

      return totalread;
    }

  return yaffsfs_GetLastError();
}

/****************************************************************************
 * Name: yaffs_vfs_write
 ****************************************************************************/

static ssize_t yaffs_vfs_write(FAR struct file *filep,
                               const char *buffer, size_t buflen)
{
  int handle;
  int totalwritten;

  handle = filep_to_yaffs_handle(filep);
  totalwritten = yaffs_write(handle, buffer, buflen);
  if (totalwritten >= 0)
    {
#ifdef CONFIG_FS_YAFFS_POSIX_TIMESTAMP
      FAR struct yaffs_obj *obj;

      /* Update modification and change time after a successful write */

      yaffsfs_Lock();
      obj = yaffs_get_obj_from_fd(handle);
      if (obj != NULL)
        {
          yaffs_vfs_settimes(obj, CH_STAT_MTIME | CH_STAT_CTIME, NULL,
                             false);
        }

      yaffsfs_Unlock();
#endif

      filep->f_pos += totalwritten;
      return totalwritten;
    }

  return yaffsfs_GetLastError();
}

/****************************************************************************
 * Name: yaffs_vfs_seek
 ****************************************************************************/

static off_t yaffs_vfs_seek(FAR struct file *filep, off_t offset, int whence)
{
  int handle;
  off_t pos;

  handle = filep_to_yaffs_handle(filep);
  pos = yaffs_lseek(handle, offset, whence);
  if (pos >= 0)
    {
      filep->f_pos = pos;
      return pos;
    }

  return yaffsfs_GetLastError();
}

/****************************************************************************
 * Name: yaffs_vfs_ioctl
 ****************************************************************************/

static int yaffs_vfs_ioctl(FAR struct file *filep, int cmd,
                           unsigned long arg)
{
  FAR struct yaffs_mountpt_s *fs;
  FAR struct yaffs_file_s *priv;
  FAR struct inode *inode;
  FAR struct inode *drv;
  int ret;

  /* Recover our private data from the struct file instance */

  priv  = filep->f_priv;
  inode = filep->f_inode;
  fs    = inode->i_private;
  drv   = fs->drv;

  switch (cmd)
    {
      case FIOC_FILEPATH:
        {
          FAR char *path = (FAR char *)(uintptr_t)arg;
          ret = inode_getpath(inode, path, PATH_MAX);
          if (ret >= 0)
            {
              strcat(path, priv->path);
            }
        }
        break;

      default:
        {
          ret = MTD_IOCTL(drv->u.i_mtd, cmd, arg);
        }
    }

  return ret;
}

/****************************************************************************
 * Name: yaffs_vfs_sync
 *
 * Description:
 *  Synchronize the file state on disk to match internal, in-memory state.
 *
 ****************************************************************************/

static int yaffs_vfs_sync(FAR struct file *filep)
{
  int handle;
  int ret;

  handle = filep_to_yaffs_handle(filep);
  ret = yaffs_fsync(handle);

  return ret < 0 ? yaffsfs_GetLastError() : 0;
}

/****************************************************************************
 * Name: yaffs_vfs_dup
 *
 * Description: Duplicate open file data in the new file structure.
 *
 ****************************************************************************/

static int yaffs_vfs_dup(FAR const struct file *oldp, FAR struct file *newp)
{
  FAR struct yaffs_file_s *oldpriv;
  FAR struct yaffs_file_s *newpriv;
  int newhandle;

  /* Allocate memory for the new file */

  newpriv = fs_heap_zalloc(sizeof(*newpriv));
  if (newpriv == NULL)
    {
      return -ENOMEM;
    }

  oldpriv = oldp->f_priv;
  newhandle = yaffs_dup(oldpriv->handle);
  if (newhandle < 0)
    {
      goto errout;
    }

  newpriv->handle = newhandle;
  strlcpy(newpriv->path, oldpriv->path, sizeof(newpriv->path));

  /* Attach the private date to the new struct file instance */

  newp->f_priv = newpriv;
  return 0;

errout:
  fs_heap_free(newpriv);
  return yaffsfs_GetLastError();
}

/****************************************************************************
 * Name: yaffs_get_statbuf
 *
 * Description:
 *   Copy the YAFFS-specific stat structure to the standard stat structure.
 *
 ****************************************************************************/

static void yaffs_get_statbuf(FAR struct stat *st,
                              FAR struct yaffs_stat *yst)
{
  memset(st, 0, sizeof(*st));
  st->st_dev   = yst->st_dev;
  st->st_ino   = yst->st_ino;
  st->st_mode  = yst->st_mode;
  st->st_nlink = yst->st_nlink;
  st->st_uid   = yst->st_uid;
  st->st_gid   = yst->st_gid;
  st->st_rdev  = yst->st_rdev;
  st->st_size  = yst->st_size;
  st->st_atime = yst->yst_atime;
  st->st_mtime = yst->yst_mtime;
  st->st_ctime = yst->yst_ctime;
}

/****************************************************************************
 * Name: yaffs_vfs_fstat
 *
 * Description:
 *   Obtain information about an open file associated with the file
 *   descriptor 'fd', and will write it to the area pointed to by 'buf'.
 *
 ****************************************************************************/

static int yaffs_vfs_fstat(FAR const struct file *filep,
                           FAR struct stat *buf)
{
  struct yaffs_stat buffer;
  int handle;
  int ret;

  handle = filep_to_yaffs_handle(filep);
  ret = yaffs_fstat(handle, &buffer);
  if (ret < 0)
    {
      goto errout;
    }

  yaffs_get_statbuf(buf, &buffer);

  return 0;

errout:
  return yaffsfs_GetLastError();
}

/****************************************************************************
 * Name: yaffs_vfs_fchstat
 ****************************************************************************/

static int yaffs_vfs_fchstat(FAR const struct file *filep,
                             FAR const struct stat *buf, int flags)
{
  int handle;
  int ret = 0;

  handle = filep_to_yaffs_handle(filep);

  if ((flags & CH_STAT_GID) || (flags & CH_STAT_UID))
    {
      return -EPERM;
    }

  if (flags & CH_STAT_MODE)
    {
      ret = yaffs_fchmod(handle, buf->st_mode);
      if (ret < 0)
        {
          goto out;
        }
    }

  /* Set the requested timestamps from buf through the shared helper (no
   * read-back needed since fields are set individually). The VFS layer
   * (file_fchstat) already sets CH_STAT_CTIME whenever metadata changes and
   * fills st_ctime with the current time, so we simply honor the flags here.
   * Flush so the explicit change is persisted immediately.
   */

  if (flags & (CH_STAT_ATIME | CH_STAT_MTIME | CH_STAT_CTIME))
    {
      FAR struct yaffs_obj *obj;

      yaffsfs_Lock();
      obj = yaffs_get_obj_from_fd(handle);
      if (obj == NULL)
        {
          yaffsfs_SetError(-EBADF);
          ret = -1;
        }
      else
        {
          ret = yaffs_vfs_settimes(obj, flags, buf, true);
        }

      yaffsfs_Unlock();
      if (ret < 0)
        {
          goto out;
        }
    }

out:
  return ret < 0 ? yaffsfs_GetLastError() : 0;
}

/****************************************************************************
 * Name: yaffs_vfs_truncate
 *
 * Description:
 *   Set the length of the open, regular file associated with the file
 *   structure 'filep' to 'length'.
 *
 ****************************************************************************/

static int yaffs_vfs_truncate(FAR struct file *filep, off_t length)
{
  int handle;
  int ret;
#ifdef CONFIG_FS_YAFFS_POSIX_TIMESTAMP
  struct stat st;
  struct timespec ts;
#endif

  handle = filep_to_yaffs_handle(filep);
  ret = yaffs_ftruncate(handle, length);
  if (ret < 0)
    {
      return yaffsfs_GetLastError();
    }

#ifdef CONFIG_FS_YAFFS_POSIX_TIMESTAMP
  nxclock_gettime(CLOCK_REALTIME, &ts);
  st.st_mtime = st.st_ctime = ts.tv_sec;
  yaffs_vfs_fchstat(filep, &st, CH_STAT_MTIME | CH_STAT_CTIME);
#endif

  return 0;
}

/****************************************************************************
 * Name: yaffs_vfs_opendir
 *
 * Description: Open a directory for read access
 *
 ****************************************************************************/

static int yaffs_vfs_opendir(FAR struct inode *mountpt,
                             FAR const char *relpath,
                             FAR struct fs_dirent_s **dir)
{
  FAR struct yaffs_dev *dev;
  FAR struct yaffs_dir_s *ydir;
  FAR yaffs_DIR *dirp;

  dev = mountpt_to_yaffs_dev(mountpt);

  /* Allocate memory for the open directory */

  ydir = fs_heap_zalloc(sizeof(*ydir));
  if (ydir == NULL)
    {
      return -ENOMEM;
    }

  dirp = yaffs_opendir_reldev(dev, relpath);
  if (dirp == NULL)
    {
      goto errout;
    }

  ydir->dir = dirp;
  *dir = &ydir->base;

  return 0;

errout:
  fs_heap_free(ydir);
  return yaffsfs_GetLastError();
}

/****************************************************************************
 * Name: yaffs_vfs_closedir
 *
 * Description: Close a directory
 *
 ****************************************************************************/

static int yaffs_vfs_closedir(FAR struct inode *mountpt,
                              FAR struct fs_dirent_s *dir)
{
  FAR struct yaffs_dir_s *ydir;
  int ret;

  ydir = (FAR struct yaffs_dir_s *)dir;
  ret = yaffs_closedir(ydir->dir);
  if (ret >= 0)
    {
      fs_heap_free(ydir);
      return 0;
    }

  return yaffsfs_GetLastError();
}

/****************************************************************************
 * Name: yaffs_vfs_readdir
 *
 * Description: Read the next directory entry
 *
 ****************************************************************************/

static int yaffs_vfs_readdir(FAR struct inode *mountpt,
                             FAR struct fs_dirent_s *dir,
                             FAR struct dirent *entry)
{
  FAR yaffs_DIR *dirp;
  FAR struct yaffs_dirent *dirent;
  int ret = 0;

  dirp = dir_to_yaffs_dir(dir);
  dirent = yaffs_readdir(dirp);
  if (dirent == NULL)
    {
      ret = yaffsfs_GetLastError();
      if (ret == 0)
        {
          ret = -ENOENT;
        }

      goto out;
    }

  entry->d_ino = dirent->d_ino;
  entry->d_type = dirent->d_type;
  strlcpy(entry->d_name, dirent->d_name, sizeof(entry->d_name));

#ifdef CONFIG_FS_YAFFS_POSIX_TIMESTAMP
  /* Update the directory access time after a successful readdir. The
   * yaffs_DIR handle already holds the directory object, so resolve it and
   * route through the shared helper. The update is in memory only; the
   * dirty object is flushed on close/sync (lazytime), avoiding a flash
   * object-header rewrite on every directory entry read.
   */

    {
      FAR struct yaffs_obj *obj;

      yaffsfs_Lock();
      obj = yaffs_get_obj_from_dir(dirp);
      if (obj != NULL)
        {
          yaffs_vfs_settimes(obj, CH_STAT_ATIME, NULL, false);
        }

      yaffsfs_Unlock();
    }
#endif

out:
  return ret;
}

/****************************************************************************
 * Name: yaffs_vfs_rewinddir
 *
 * Description: Reset directory read to the first entry
 *
 ****************************************************************************/

static int yaffs_vfs_rewinddir(FAR struct inode *mountpt,
                               FAR struct fs_dirent_s *dir)
{
  yaffs_rewinddir(dir_to_yaffs_dir(dir));
  return 0;
}

/****************************************************************************
 * Name: yaffs_vfs_statfs
 *
 * Description: Return filesystem statistics
 *
 ****************************************************************************/

static int yaffs_vfs_statfs(FAR struct inode *mountpt,
                            FAR struct statfs *buf)
{
  FAR struct yaffs_mountpt_s *fs;
  FAR struct yaffs_param *param;
  off_t quota_blocks;
  off_t blocks;
  off_t size;

  fs = (FAR struct yaffs_mountpt_s *)(mountpt->i_private);
  size = yaffs_freespace_reldev(fs->dev);
  if (size < 0)
    {
      goto errout;
    }

  /* Return something for the file system description */

  param = &fs->dev->param;

  buf->f_type    = YAFFS_SUPER_MAGIC;
  buf->f_namelen = YAFFS_MAX_NAME_LENGTH;
  buf->f_bsize   = fs->geo.erasesize;

  /* Physical usable total, in erase blocks, less what the core holds
   * back for garbage collection.
   */

  blocks = (off_t)fs->geo.neraseblocks - param->n_reserved_blocks;

  /* Logical soft quota: report the capped total when one is configured.
   * df free (f_bfree) is derived from size, which yaffs_get_n_free_chunks
   * has already clamped to the same budget. The total stays bounded by the
   * physical usable size, so a misconfigured quota (>= partition size)
   * cannot make df advertise more space than the flash can hold.
   */

  if (param->quota_bytes > 0)
    {
      quota_blocks = param->quota_bytes / fs->geo.erasesize;
      blocks = MIN(quota_blocks, blocks);
    }

  buf->f_blocks  = blocks;
  buf->f_bfree   = size / fs->geo.erasesize;
  buf->f_bavail  = buf->f_bfree;

  return OK;

errout:
  return yaffsfs_GetLastError();
}

/****************************************************************************
 * Name: yaffs_vfs_unlink
 *
 * Description: Remove a file
 *
 ****************************************************************************/

static int yaffs_vfs_unlink(FAR struct inode *mountpt,
                            FAR const char *relpath)
{
  FAR struct yaffs_dev *dev;
  int ret;

  dev = mountpt_to_yaffs_dev(mountpt);
  ret = yaffs_unlink_reldev(dev, relpath);

  return ret < 0 ? yaffsfs_GetLastError() : 0;
}

/****************************************************************************
 * Name: yaffs_vfs_mkdir
 *
 * Description: Create a directory
 *
 ****************************************************************************/

static int yaffs_vfs_mkdir(FAR struct inode *mountpt,
                           FAR const char *relpath, mode_t mode)
{
  FAR struct yaffs_dev *dev;
  int ret;

  dev = mountpt_to_yaffs_dev(mountpt);
  ret = yaffs_mkdir_reldev(dev, relpath, mode);

  return ret < 0 ? yaffsfs_GetLastError() : 0;
}

/****************************************************************************
 * Name: yaffs_vfs_rmdir
 *
 * Description: Remove a directory
 *
 ****************************************************************************/

static int yaffs_vfs_rmdir(FAR struct inode *mountpt,
                           FAR const char *relpath)
{
  FAR struct yaffs_dev *dev;
  int ret;

  dev = mountpt_to_yaffs_dev(mountpt);
  ret = yaffs_rmdir_reldev(dev, relpath);

  return ret < 0 ? yaffsfs_GetLastError() : 0;
}

/****************************************************************************
 * Name: yaffs_vfs_rename
 *
 * Description: Rename a file or directory
 *
 ****************************************************************************/

static int yaffs_vfs_rename(FAR struct inode *mountpt,
                            FAR const char *oldrelpath,
                            FAR const char *newrelpath)
{
  FAR struct yaffs_dev *dev;
  int ret;

  dev = mountpt_to_yaffs_dev(mountpt);
  ret = yaffs_rename_reldev(dev, oldrelpath, newrelpath);

  return ret < 0 ? yaffsfs_GetLastError() : 0;
}

/****************************************************************************
 * Name: yaffs_vfs_stat
 *
 * Description: Return information about a file or directory
 *
 ****************************************************************************/

static int yaffs_vfs_stat(FAR struct inode *mountpt, FAR const char *relpath,
                          FAR struct stat *buf)
{
  FAR struct yaffs_dev *dev;
  struct yaffs_stat buffer;
  int ret;

  dev = mountpt_to_yaffs_dev(mountpt);
  ret = yaffs_stat_reldev(dev, relpath, &buffer);
  if (ret < 0)
    {
      goto errout;
    }

  yaffs_get_statbuf(buf, &buffer);

  return 0;

errout:
  return yaffsfs_GetLastError();
}

/****************************************************************************
 * Name: yaffs_vfs_chstat
 ****************************************************************************/

static int yaffs_vfs_chstat(FAR struct inode *mountpt,
                            FAR const char *relpath,
                            FAR const struct stat *buf, int flags)
{
  FAR struct yaffs_dev *dev;
  struct yaffs_stat buffer;
  struct yaffs_utimbuf timbuf;
  int ret = 0;

  dev = mountpt_to_yaffs_dev(mountpt);

  if ((flags & CH_STAT_GID) || (flags & CH_STAT_UID))
    {
      return -EPERM;
    }

  if (flags & CH_STAT_MODE)
    {
      ret = yaffs_chmod_reldev(dev, relpath, buf->st_mode);
      if (ret < 0)
        {
          goto out;
        }
    }

  if (flags & (CH_STAT_ATIME | CH_STAT_MTIME))
    {
      if (!(flags & CH_STAT_ATIME) != !(flags & CH_STAT_MTIME))
        {
          ret = yaffs_stat_reldev(dev, relpath, &buffer);
          if (ret < 0)
            {
              goto out;
            }

          if (flags & CH_STAT_ATIME)
            {
              timbuf.actime = buf->st_atime;
              timbuf.modtime = buffer.yst_mtime;
            }
          else
            {
              timbuf.actime = buffer.yst_atime;
              timbuf.modtime = buf->st_mtime;
            }
        }
      else
        {
          timbuf.actime = buf->st_atime;
          timbuf.modtime = buf->st_mtime;
        }

      ret = yaffs_utime_reldev(dev, relpath, &timbuf);
    }

out:
  return ret < 0 ? yaffsfs_GetLastError() : 0;
}

/****************************************************************************
 * Name: yaffs_vfs_syncfs
 ****************************************************************************/

static int yaffs_vfs_syncfs(FAR struct inode *mountpt)
{
  FAR struct yaffs_dev *dev;
  int ret;

  dev = mountpt_to_yaffs_dev(mountpt);
  ret = yaffs_sync_reldev(dev);

  return ret < 0 ? yaffsfs_GetLastError() : 0;
}

#ifdef CONFIG_FS_LINKS
/****************************************************************************
 * Name: yaffs_vfs_link
 ****************************************************************************/

static int yaffs_vfs_link(FAR struct inode *mountpt,
                          FAR const char *relpath1,
                          FAR const char *relpath2)
{
  FAR struct yaffs_dev *dev;
  int ret;

  dev = mountpt_to_yaffs_dev(mountpt);
  ret = yaffs_link_reldev(dev, relpath1, relpath2);
  return ret < 0 ? yaffsfs_GetLastError() : 0;
}

/****************************************************************************
 * Name: yaffs_vfs_symlink
 ****************************************************************************/

static int yaffs_vfs_symlink(FAR struct inode *mountpt,
                             FAR const char *path1,
                             FAR const char *relpath2)
{
  FAR struct yaffs_dev *dev;
  int ret;

  dev = mountpt_to_yaffs_dev(mountpt);
  ret = yaffs_symlink_reldev(dev, path1, relpath2);
  return ret < 0 ? yaffsfs_GetLastError() : 0;
}

/****************************************************************************
 * Name: yaffs_vfs_readlink
 ****************************************************************************/

static ssize_t yaffs_vfs_readlink(FAR struct inode *mountpt,
                                  FAR const char *relpath,
                                  FAR char *buf, size_t bufsize)
{
  FAR struct yaffs_dev *dev;
  ssize_t ret;

  dev = mountpt_to_yaffs_dev(mountpt);
  ret = yaffs_readlink_reldev(dev, relpath, buf, bufsize);

  /* yaffs_readlink_reldev returns 0 on success, but POSIX readlink()
   * should return the number of bytes placed in buf. Calculate the
   * actual length of the string that was copied.
   */

  return ret < 0 ? yaffsfs_GetLastError() : strlen(buf);
}

/****************************************************************************
 * Name: yaffs_vfs_lstat
 ****************************************************************************/

static int yaffs_vfs_lstat(FAR struct inode *mountpt,
                           FAR const char *relpath, FAR struct stat *buf)
{
  FAR struct yaffs_dev *dev;
  struct yaffs_stat buffer;
  int ret;

  dev = mountpt_to_yaffs_dev(mountpt);
  ret = yaffs_lstat_reldev(dev, relpath, &buffer);
  if (ret < 0)
    {
      return yaffsfs_GetLastError();
    }

  yaffs_get_statbuf(buf, &buffer);

  return 0;
}
#endif

/****************************************************************************
 * Name: yaffs_initialise_fn
 ****************************************************************************/

static int yaffs_initialise_fn(FAR struct yaffs_dev *dev)
{
  return YAFFS_OK;
}

/****************************************************************************
 * Name: yaffs_deinitialise_fn
 ****************************************************************************/

static int yaffs_deinitialise_fn(FAR struct yaffs_dev *dev)
{
  return YAFFS_OK;
}

/****************************************************************************
 * Name: yaffs_check
 ****************************************************************************/

#ifdef CONFIG_FS_YAFFS_DEBUG
static void yaffs_check(FAR struct mtd_dev_s *mtd, int nand_chunk,
                        int nchunk, FAR const u8 *buffer, u32 per_chunk_size)
{
  uint16_t crc16_check;
  uint16_t crc16_ori;
  int ret;

  crc16_ori = crc16(buffer, per_chunk_size * nchunk);
  ret = MTD_BREAD(mtd, nand_chunk, nchunk, (FAR u8 *)buffer);
  if (ret < 0)
    {
      ferr("[%s] read block failed!\n", __func__);
      lib_dumpbuffer("buffer:", buffer, per_chunk_size * nchunk);
      DEBUGASSERT(0);
    }
  else
    {
      crc16_check = crc16(buffer, per_chunk_size * nchunk);
      if (crc16_ori != crc16_check)
        {
          ferr("CRC16 check failed!\n");
          lib_dumpbuffer("check:", buffer, per_chunk_size * nchunk);
          DEBUGASSERT(0);
        }
    }
}
#endif

/****************************************************************************
 * Name: yaffs_read_chunk_fn
 ****************************************************************************/

static int yaffs_read_chunk_fn(FAR struct yaffs_dev *dev, int nand_chunk,
                               FAR u8 *data, int data_len,
                               FAR u8 *oob, int oob_len,
                               FAR enum yaffs_ecc_result *ecc_result)
{
  FAR struct mtd_dev_s *mtd;
  int nchunk;
  ssize_t retval;
  enum yaffs_ecc_result ecc_status;
  int ret = YAFFS_FAIL;

  mtd = yaffs_dev_to_mtd(dev);
  nchunk = data_len / dev->param.total_bytes_per_chunk;

  if (data && data_len > 0)
    {
      retval = MTD_BREAD(mtd, nand_chunk, nchunk, data);
      if (retval == nchunk)
        {
          ecc_status = YAFFS_ECC_RESULT_NO_ERROR;
          ret = YAFFS_OK;
#ifdef CONFIG_FS_YAFFS_DEBUG
          yaffs_check(mtd, nand_chunk, nchunk, data,
                      dev->param.total_bytes_per_chunk);
#endif
        }
      else
        {
          if (retval == -EUCLEAN)
            {
              ecc_status = YAFFS_ECC_RESULT_FIXED;
              ret = YAFFS_OK;
            }
          else if (retval == -EBADMSG)
            {
              ecc_status = YAFFS_ECC_RESULT_UNFIXED;
            }
          else
            {
              ecc_status = YAFFS_ECC_RESULT_UNKNOWN;
            }
        }
    }
  else
    {
      ecc_status = YAFFS_ECC_RESULT_UNKNOWN;
    }

  if (ecc_result)
    {
      *ecc_result = ecc_status;
    }

  if (oob && oob_len > 0)
    {
      ferr("### read oob? inband tag should not read oob!\n");
    }

  return ret;
}

/****************************************************************************
 * Name: yaffs_write_chunk_fn
 ****************************************************************************/

static int yaffs_write_chunk_fn(FAR struct yaffs_dev *dev, int nand_chunk,
                                FAR const u8 *data, int data_len,
                                FAR const u8 *oob, int oob_len)
{
  FAR struct mtd_dev_s *mtd;
  int nchunk;
  ssize_t retval;
  int ret = YAFFS_FAIL;

  mtd = yaffs_dev_to_mtd(dev);
  nchunk = data_len / dev->param.total_bytes_per_chunk;

  if (data && data_len > 0)
    {
      retval = MTD_BWRITE(mtd, nand_chunk, nchunk, data);
      if (retval == nchunk)
        {
          ret = YAFFS_OK;
        }
    }

  if (oob && oob_len > 0)
    {
      ferr("### write oob? inband tag should not read oob!\n");
    }

#ifdef CONFIG_FS_YAFFS_DEBUG
  yaffs_check(mtd, nand_chunk, nchunk, data,
              dev->param.total_bytes_per_chunk);
#endif
  return ret;
}

/****************************************************************************
 * Name: yaffs_erase_block_fn
 ****************************************************************************/

static int yaffs_erase_block_fn(FAR struct yaffs_dev *dev, int block_no)
{
  FAR struct mtd_dev_s *mtd = yaffs_dev_to_mtd(dev);
  return MTD_ERASE(mtd, block_no, 1) < 0 ? YAFFS_FAIL : YAFFS_OK;
}

/****************************************************************************
 * Name: yaffs_mark_block_bad_fn
 ****************************************************************************/

static int yaffs_mark_block_bad_fn(FAR struct yaffs_dev *dev, int block_no)
{
  FAR struct mtd_dev_s *mtd = yaffs_dev_to_mtd(dev);
  return MTD_MARKBAD(mtd, block_no) == 0 ? YAFFS_OK : YAFFS_FAIL;
}

/****************************************************************************
 * Name: yaffs_check_block_bad_fn
 ****************************************************************************/

static int yaffs_check_block_bad_fn(FAR struct yaffs_dev *dev, int block_no)
{
  FAR struct mtd_dev_s *mtd = yaffs_dev_to_mtd(dev);
  return MTD_ISBAD(mtd, block_no) == 0 ? YAFFS_OK : YAFFS_FAIL;
}

/****************************************************************************
 * Name: yaffs_vfs_bind
 *
 * Description: This implements a portion of the mount operation. This
 *  function allocates and initializes the mountpoint private data and
 *  binds the driver inode to the filesystem private data. The final
 *  binding of the private data (containing the driver) to the
 *  mountpoint is performed by mount().
 *
 ****************************************************************************/

static int yaffs_vfs_bind(FAR struct inode *driver, FAR const void *data,
                          FAR void **handle)
{
  FAR struct yaffs_mountpt_s *fs;
  FAR struct yaffs_dev *dev;
  FAR struct yaffs_param *p;
  FAR struct yaffs_driver *d;
  FAR struct mtd_dev_s *mtd;
  int ret;

  DEBUGASSERT(INODE_IS_MTD(driver) && driver->u.i_mtd != NULL);
  mtd = driver->u.i_mtd;

  /* Create an instance of the mountpt state structure */

  fs = fs_heap_zalloc(sizeof(*fs));
  if (!fs)
    {
      return -ENOMEM;
    }

  /* Initialize the allocated mountpt state structure. The filesystem is
   * responsible for one reference on the driver inode and does not
   * have to addref() here (but does have to release in unbind().
   */

  fs->drv = driver; /* Save the driver reference */
  fs->mtd = mtd;

  ret = MTD_IOCTL(mtd, MTDIOC_GEOMETRY, (unsigned long)&fs->geo);
  if (ret < 0)
    {
      goto errout_with_fs;
    }

  /* Initialize yaffs_dev structure */

  dev = fs_heap_zalloc(sizeof(*dev));
  if (!dev)
    {
      ret = -ENOMEM;
      goto errout_with_fs;
    }

  dev->driver_context = mtd;

  p = &dev->param;
  p->name = fs_heap_strdup(mtd->name);
  if (p->name == NULL)
    {
      ret = -ENOMEM;
      goto errout_with_dev;
    }

  p->is_yaffs2 = 1;
  p->use_nand_ecc = 1;
  p->inband_tags = 1;
  p->n_reserved_blocks = CONFIG_FS_YAFFS_RESERVED_BLOCKS;
  p->quota_bytes = (loff_t)CONFIG_FS_YAFFS_QUOTA_MB << 20;
  p->n_caches = CONFIG_FS_YAFFS_CHUNK_CACHE;
  p->refresh_period = CONFIG_FS_YAFFS_REFRESH_PERIOD;
#ifdef CONFIG_FS_YAFFS_CACHE_BYPASS_ALIGNED
  p->cache_bypass_aligned = 1;
#endif
#ifdef CONFIG_FS_YAFFS_SKIP_READ_CHECKPOINT
  p->skip_checkpt_rd = 1;
#endif
#ifdef CONFIG_FS_YAFFS_SKIP_WRITE_CHECKPOINT
  p->skip_checkpt_wr = 1;
#endif
#ifdef CONFIG_FS_YAFFS_EMPTY_LOST_FOUND
  p->empty_lost_n_found = 1;
#endif
  p->hide_lost_n_found = 1;
  p->start_block = 0;
  p->end_block = fs->geo.neraseblocks - 1;
  p->total_bytes_per_chunk = fs->geo.blocksize;
  p->chunks_per_block = fs->geo.erasesize / fs->geo.blocksize;

  d = &dev->drv;
  d->drv_initialise_fn = yaffs_initialise_fn;
  d->drv_deinitialise_fn = yaffs_deinitialise_fn;
  d->drv_read_chunk_fn = yaffs_read_chunk_fn;
  d->drv_write_chunk_fn = yaffs_write_chunk_fn;
  d->drv_erase_fn = yaffs_erase_block_fn;
  d->drv_check_bad_fn = yaffs_check_block_bad_fn;
  d->drv_mark_bad_fn = yaffs_mark_block_bad_fn;

  fs->dev = dev;

  yaffs_add_device(dev);

  /* Then get information about the yaffs filesystem on the devices
   * managed by this driver.
   */

  /* Force format the device if -o forceformat */

  if (data && strstr(data, "forceformat"))
    {
      ret = yaffs_format_reldev(dev, 1, 1, 0);
      if (ret < 0)
        {
          goto errout_with_name;
        }
    }

  ret = yaffs_mount3_reldev(dev,
                           (data && strstr(data, "ro")),
                           (data && strstr(data, "skip_chkpt")));
  if (ret < 0)
    {
      /* Auto format the device if -o autoformat */

      if (ret != -EFAULT || !data || !strstr(data, "autoformat"))
        {
          goto errout_with_name;
        }

      ret = yaffs_format_reldev(dev, 1, 1, 1);
      if (ret < 0)
        {
          goto errout_with_name;
        }
    }

  *handle = fs;
  return OK;

errout_with_name:
  fs_heap_free((FAR void *)(p->name));
  yaffs_remove_device(dev);
errout_with_dev:
  fs_heap_free(dev);
errout_with_fs:
  fs_heap_free(fs);
  return ret;
}

/****************************************************************************
 * Name: yaffs_vfs_unbind
 *
 * Description: This implements the filesystem portion of the umount
 *  operation.
 *
 ****************************************************************************/

static int yaffs_vfs_unbind(FAR void *handle, FAR struct inode **driver,
                            unsigned int flags)
{
  FAR struct yaffs_mountpt_s *fs;
  FAR struct yaffs_dev *dev;
  FAR struct inode *drv;
  int ret;

  fs = handle;
  dev = fs->dev;
  drv = fs->drv;

  /* Unmount */

  ret = yaffs_unmount2_reldev(dev, flags & MNT_FORCE);
  if (ret >= 0)
    {
      /* Remove and release dev */

      yaffs_remove_device(dev);
      fs_heap_free((FAR void *)(dev->param.name));
      fs_heap_free(dev);

      /* We hold a reference to the driver but should not but
       * mucking with inodes in this context. So, we will just return
       * our contained reference to the driver inode and let the
       * umount logic dispose of it.
       */

      if (driver)
        {
          *driver = drv;
        }

      /* Release the mountpoint private data */

      fs_heap_free(fs);
    }

  return ret;
}
