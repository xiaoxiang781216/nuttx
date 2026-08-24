/****************************************************************************
 * fs/lwext4/lwext4_vfs.c
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

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statfs.h>

#include <inttypes.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <debug.h>

#include <nuttx/kmalloc.h>
#include <nuttx/lib/lib.h>
#include <nuttx/fs/fs.h>
#include <nuttx/fs/ioctl.h>
#include <nuttx/atomic.h>
#include <nuttx/mutex.h>
#include <nuttx/nuttx.h>
#include "inode/inode.h"
#include "fs_heap.h"

/* lwext4 headers */

#include "ext4.h"
#include "ext4_mkfs.h"
#include "ext4_blockdev.h"
#include "ext4_inode.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define LWEXT4_MOUNT_PREFIX "/nx"

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* Per-mount point private data */

struct lwext4_mountpt_s
{
  struct ext4_blockdev bdev;           /* lwext4 block device */
  struct ext4_blockdev_iface bdiface;  /* Block device interface */
  struct ext4_fs fs;                   /* Filesystem structure */
  FAR struct inode *drv;               /* NuttX block device inode */
  rmutex_t lock;                       /* Per-mount-point access lock */
  struct ext4_lock oslock;             /* Per-mount lwext4 lock callbacks */
  char mount_name[32];                 /* Unique mount point name */
  bool readonly;                       /* Read-only mount flag */
  uint8_t ph_bbuf[];                   /* Physical block buffer */
};

/* File object private data */

struct lwext4_file_s
{
  ext4_file file;                 /* lwext4 file handle */
  atomic_t refs;                  /* Shared open-file references from dup() */
  atomic_t dirty;                 /* Written since open; update mtime on close */
  char path[];                    /* Full file path (flexible) */
};

/* Directory object private data */

struct lwext4_dir_s
{
  struct fs_dirent_s base;        /* VFS directory entry base */
  ext4_dir dir;                   /* lwext4 directory handle */
  char path[];                    /* Directory path (flexible) */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/* File operations */

static int     lwext4_open(FAR struct file *filep,
                           FAR const char *relpath,
                           int oflags, mode_t mode);
static int     lwext4_close(FAR struct file *filep);
static ssize_t lwext4_read(FAR struct file *filep, FAR char *buffer,
                           size_t buflen);
static ssize_t lwext4_write(FAR struct file *filep,
                            FAR const char *buffer,
                            size_t buflen);
static off_t   lwext4_seek(FAR struct file *filep, off_t offset,
                           int whence);
static int     lwext4_ioctl(FAR struct file *filep, int cmd,
                            unsigned long arg);
static int     lwext4_sync(FAR struct file *filep);
static int     lwext4_dup(FAR const struct file *oldp,
                          FAR struct file *newp);
static int     lwext4_fstat(FAR const struct file *filep,
                            FAR struct stat *buf);
static int     lwext4_fchstat(FAR const struct file *filep,
                              FAR const struct stat *buf,
                              int flags);
static int     lwext4_truncate(FAR struct file *filep, off_t length);

/* Directory operations */

static int     lwext4_opendir(FAR struct inode *mountpt,
                              FAR const char *relpath,
                              FAR struct fs_dirent_s **dir);
static int     lwext4_closedir(FAR struct inode *mountpt,
                               FAR struct fs_dirent_s *dir);
static int     lwext4_readdir(FAR struct inode *mountpt,
                              FAR struct fs_dirent_s *dir,
                              FAR struct dirent *entry);
static int     lwext4_rewinddir(FAR struct inode *mountpt,
                                FAR struct fs_dirent_s *dir);

/* Mount point operations */

static int     lwext4_bind(FAR struct inode *driver,
                           FAR const void *data,
                           FAR void **handle);
static int     lwext4_unbind(FAR void *handle,
                             FAR struct inode **driver,
                             unsigned int flags);
static int     lwext4_statfs(FAR struct inode *mountpt,
                             FAR struct statfs *buf);

/* File system operations */

static int     lwext4_unlink(FAR struct inode *mountpt,
                             FAR const char *relpath);
static int     lwext4_mkdir(FAR struct inode *mountpt,
                            FAR const char *relpath,
                            mode_t mode);
static int     lwext4_rmdir(FAR struct inode *mountpt,
                            FAR const char *relpath);
static int     lwext4_rename(FAR struct inode *mountpt,
                             FAR const char *oldrelpath,
                             FAR const char *newrelpath);
static int     lwext4_stat(FAR struct inode *mountpt,
                           FAR const char *relpath,
                           FAR struct stat *buf);
static int     lwext4_chstat(FAR struct inode *mountpt,
                             FAR const char *relpath,
                             FAR const struct stat *buf,
                             int flags);
static int     lwext4_syncfs(FAR struct inode *mountpt);

/* Helper functions */

static bool    lwext4_option_present(FAR const char *options,
                                     FAR const char *option);
static int     lwext4_parse_options(FAR const void *data,
                                    FAR bool *readonly,
                                    FAR bool *autoformat,
                                    FAR bool *forceformat);
static int     lwext4_format(FAR struct lwext4_mountpt_s *fs);
static void    lwext4_fill_stat(FAR struct lwext4_mountpt_s *fs,
                                FAR struct stat *buf,
                                uint32_t ino,
                                FAR struct ext4_inode *inode);
#ifdef CONFIG_FS_LINKS
static int     lwext4_link(FAR struct inode *mountpt,
                           FAR const char *relpath1,
                           FAR const char *relpath2);
static int     lwext4_symlink(FAR struct inode *mountpt,
                              FAR const char *target,
                              FAR const char *relpath);
static ssize_t lwext4_readlink(FAR struct inode *mountpt,
                               FAR const char *relpath,
                               FAR char *buf,
                               size_t bufsize);
static int     lwext4_lstat(FAR struct inode *mountpt,
                            FAR const char *relpath,
                            FAR struct stat *buf);
#endif

/****************************************************************************
 * Public Data
 ****************************************************************************/

const struct mountpt_operations g_lwext4_operations =
{
  .open      = lwext4_open,
  .close     = lwext4_close,
  .read      = lwext4_read,
  .write     = lwext4_write,
  .seek      = lwext4_seek,
  .ioctl     = lwext4_ioctl,
  .truncate  = lwext4_truncate,

  .sync      = lwext4_sync,
  .dup       = lwext4_dup,
  .fstat     = lwext4_fstat,
  .fchstat   = lwext4_fchstat,

  .opendir   = lwext4_opendir,
  .closedir  = lwext4_closedir,
  .readdir   = lwext4_readdir,
  .rewinddir = lwext4_rewinddir,

  .bind      = lwext4_bind,
  .unbind    = lwext4_unbind,
  .statfs    = lwext4_statfs,

  .unlink    = lwext4_unlink,
  .mkdir     = lwext4_mkdir,
  .rmdir     = lwext4_rmdir,
  .rename    = lwext4_rename,
  .stat      = lwext4_stat,
  .chstat    = lwext4_chstat,
  .syncfs    = lwext4_syncfs,

#ifdef CONFIG_FS_LINKS
  .link      = lwext4_link,
  .symlink   = lwext4_symlink,
  .readlink  = lwext4_readlink,
  .lstat     = lwext4_lstat,
#endif
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: lwext4_lock / lwext4_unlock
 *
 * Description:
 *   Per-mount-point lock callbacks installed into lwext4 via
 *   ext4_mount_setup_locks().  The library passes back the registered
 *   ext4_lock; container_of() recovers the enclosing mount point, so each
 *   mount serializes on its own lock and different mount points never block
 *   each other.
 *
 ****************************************************************************/

static void lwext4_lock(FAR const struct ext4_lock *lock)
{
  FAR struct lwext4_mountpt_s *fs =
    container_of(lock, struct lwext4_mountpt_s, oslock);
  nxrmutex_lock(&fs->lock);
}

static void lwext4_unlock(FAR const struct ext4_lock *lock)
{
  FAR struct lwext4_mountpt_s *fs =
    container_of(lock, struct lwext4_mountpt_s, oslock);
  nxrmutex_unlock(&fs->lock);
}

/****************************************************************************
 * Name: lwext4_option_present
 ****************************************************************************/

static bool lwext4_option_present(FAR const char *options,
                                  FAR const char *option)
{
  FAR const char *token;
  FAR const char *end;
  size_t optionlen;

  DEBUGASSERT(option != NULL);

  if (options == NULL)
    {
      return false;
    }

  optionlen = strlen(option);
  token = options;

  while (*token != '\0')
    {
      while (*token == ',' || *token == ' ' || *token == '\t')
        {
          token++;
        }

      if (*token == '\0')
        {
          break;
        }

      end = token;
      while (*end != '\0' && *end != ',')
        {
          end++;
        }

      while (end > token && (*(end - 1) == ' ' || *(end - 1) == '\t'))
        {
          end--;
        }

      if ((end - token) == optionlen &&
          strncmp(token, option, optionlen) == 0)
        {
          return true;
        }

      token = end;
    }

  return false;
}

/****************************************************************************
 * Name: lwext4_parse_options
 ****************************************************************************/

static int lwext4_parse_options(FAR const void *data,
                                FAR bool *readonly,
                                FAR bool *autoformat,
                                FAR bool *forceformat)
{
  FAR const char *options = data;

  DEBUGASSERT(readonly != NULL && autoformat != NULL && forceformat != NULL);

  *readonly = false;
  *autoformat = false;
  *forceformat = false;

  if (options == NULL || options[0] == '\0')
    {
      return OK;
    }

  *readonly = lwext4_option_present(options, "ro");
  *autoformat = lwext4_option_present(options, "autoformat");
  *forceformat = lwext4_option_present(options, "forceformat");

  if (*readonly && (*autoformat || *forceformat))
    {
      ferr("Read-only mount cannot be combined with format options: %s\n",
           options);
      return -EINVAL;
    }

  return OK;
}

/****************************************************************************
 * Name: ext4_user_malloc/calloc/realloc/free
 *
 * Description:
 *   Allocation hooks used by the lwext4 library when it is built with
 *   CONFIG_USE_USER_MALLOC=1.  They route the library's allocations onto
 *   the NuttX file-system heap instead of the global libc allocator.
 *
 ****************************************************************************/

FAR void *ext4_user_malloc(size_t size)
{
  return fs_heap_malloc(size);
}

FAR void *ext4_user_calloc(size_t nmemb, size_t size)
{
  if (nmemb != 0 && size > SIZE_MAX / nmemb)
    {
      return NULL;
    }

  return fs_heap_zalloc(nmemb * size);
}

FAR void *ext4_user_realloc(FAR void *ptr, size_t size)
{
  return fs_heap_realloc(ptr, size);
}

void ext4_user_free(FAR void *ptr)
{
  fs_heap_free(ptr);
}

/****************************************************************************
 * Name: lwext4_format
 ****************************************************************************/

static int lwext4_format(FAR struct lwext4_mountpt_s *fs)
{
  struct ext4_mkfs_info info;
  int ret;

  DEBUGASSERT(fs != NULL);

  memset(&fs->fs, 0, sizeof(fs->fs));
  memset(&info, 0, sizeof(info));

#ifdef CONFIG_LWEXT4_ENABLE_JOURNAL
  info.journal = true;
#endif

  info.label = "nuttx";

  ret = -ext4_mkfs(&fs->fs, &fs->bdev, &info, F_SET_EXT4);
  if (ret < 0)
    {
      ferr("Failed to format filesystem: %d\n", ret);
    }

  return ret;
}

/****************************************************************************
 * Name: lwext4_build_path
 *
 * Description:
 *   Build full path for lwext4 from mount point and relative path
 *
 ****************************************************************************/

static void lwext4_build_path(FAR struct lwext4_mountpt_s *fs,
                              FAR const char *relpath,
                              FAR char *fullpath,
                              size_t size)
{
  if (relpath == NULL || relpath[0] == '\0')
    {
      strlcpy(fullpath, fs->mount_name, size);
      return;
    }

  /* Skip leading '/' from relpath since mount_name already ends with '/' */

  while (relpath[0] == '/')
    {
      relpath++;
    }

  snprintf(fullpath, size, "%s%s", fs->mount_name, relpath);
}

/****************************************************************************
 * Block Device Interface Implementation
 ****************************************************************************/

/****************************************************************************
 * Name: lwext4_bdev_open
 ****************************************************************************/

static int lwext4_bdev_open(FAR struct ext4_blockdev *bdev)
{
  finfo("Block device opened\n");
  return EOK;
}

/****************************************************************************
 * Name: lwext4_bdev_bread
 *
 * Description:
 *   Read sectors from block device
 *
 ****************************************************************************/

static int lwext4_bdev_bread(FAR struct ext4_blockdev *bdev,
                             FAR void *buf,
                             uint64_t blk_id,
                             uint32_t blk_cnt)
{
  FAR struct lwext4_mountpt_s *fs;
  FAR struct inode *drv;
  ssize_t ret;

  if (bdev == NULL || bdev->bdif == NULL)
    {
      return EIO;
    }

  fs = bdev->bdif->p_user;
  drv = fs != NULL ? fs->drv : NULL;

  DEBUGASSERT(drv != NULL);
  DEBUGASSERT(drv->u.i_bops != NULL);
  DEBUGASSERT(drv->u.i_bops->read != NULL);

  if (drv == NULL || drv->u.i_bops == NULL || drv->u.i_bops->read == NULL)
    {
      return EIO;
    }

  ret = drv->u.i_bops->read(drv, buf, blk_id, blk_cnt);
  if (ret < 0)
    {
      ferr("Block read failed: sector=%" PRIu64 " count=%" PRIu32
           " ret=%zd\n", blk_id, blk_cnt, ret);
      return -ret;
    }

  if (ret != blk_cnt)
    {
      ferr("Block read incomplete: expected=%" PRIu32 " got=%zd\n",
           blk_cnt, ret);
      return EIO;
    }

  return EOK;
}

/****************************************************************************
 * Name: lwext4_bdev_bwrite
 *
 * Description:
 *   Write sectors to block device
 *
 ****************************************************************************/

static int lwext4_bdev_bwrite(FAR struct ext4_blockdev *bdev,
                              FAR const void *buf,
                              uint64_t blk_id,
                              uint32_t blk_cnt)
{
  FAR struct lwext4_mountpt_s *fs;
  FAR struct inode *drv;
  ssize_t ret;

  if (bdev == NULL || bdev->bdif == NULL)
    {
      return EIO;
    }

  fs = bdev->bdif->p_user;
  drv = fs != NULL ? fs->drv : NULL;

  DEBUGASSERT(drv != NULL);
  DEBUGASSERT(drv->u.i_bops != NULL);
  DEBUGASSERT(drv->u.i_bops->write != NULL);

  if (drv == NULL || drv->u.i_bops == NULL || drv->u.i_bops->write == NULL)
    {
      return EIO;
    }

  ret = drv->u.i_bops->write(drv, buf, blk_id, blk_cnt);
  if (ret < 0)
    {
      ferr("Block write failed: sector=%llu count=%" PRIu32 " ret=%zd\n",
           blk_id, blk_cnt, ret);
      return -ret;
    }

  if (ret != blk_cnt)
    {
      ferr("Block write incomplete: expected=%" PRIu32 " got=%zd\n",
           blk_cnt, ret);
      return EIO;
    }

  return EOK;
}

/****************************************************************************
 * Name: lwext4_bdev_close
 ****************************************************************************/

static int lwext4_bdev_close(FAR struct ext4_blockdev *bdev)
{
  finfo("Block device closed\n");
  return EOK;
}

/****************************************************************************
 * File Operations Implementation
 ****************************************************************************/

/****************************************************************************
 * Name: lwext4_open
 ****************************************************************************/

static int lwext4_open(FAR struct file *filep, FAR const char *relpath,
                       int oflags, mode_t mode)
{
  FAR struct lwext4_mountpt_s *fs;
  FAR struct lwext4_file_s *priv;
  struct ext4_inode inode;
  FAR char *tmppath;
  uint32_t ino;
  int ret;
  int i;

  finfo("Opening: %s flags=%04x mode=%04x\n", relpath, oflags, mode);

  if (filep == NULL || filep->f_inode == NULL)
    {
      return -EINVAL;
    }

  fs = filep->f_inode->i_private;
  DEBUGASSERT(fs != NULL);
  if (fs == NULL)
    {
      return -EINVAL;
    }

  /* Build full path in temporary buffer first */

  tmppath = lib_get_tempbuffer(PATH_MAX);
  if (tmppath == NULL)
    {
      return -ENOMEM;
    }

  lwext4_build_path(fs, relpath, tmppath, PATH_MAX);

  /* Resolve symbolic links.  lwext4 does not follow links on open(), so a
   * symlink would be opened as a raw link inode and fail.  Follow the chain
   * here (up to SYMLOOP_MAX hops) so that open() reaches the target,
   * matching POSIX path resolution.  O_NOFOLLOW rejects a symlink final
   * component with ELOOP; a non-existent component stops resolution so that
   * O_CREAT can still create it.
   */

  for (i = 0; i < SYMLOOP_MAX; i++)
    {
      FAR char *target;
      size_t rcnt;

      ret = -ext4_raw_inode_fill(tmppath, &ino, &inode);
      if (ret < 0)
        {
          /* Path does not exist yet: let ext4_fopen2() handle it
           * (O_CREAT creates it, otherwise it returns the error).
           */

          break;
        }

      if (!S_ISLNK(ext4_inode_get_mode(&fs->fs.sb, &inode)))
        {
          break;  /* Reached a non-symlink target */
        }

      if ((oflags & O_NOFOLLOW) != 0 && i == 0)
        {
          lib_put_tempbuffer(tmppath);
          return -ELOOP;
        }

      target = lib_get_tempbuffer(PATH_MAX);
      if (target == NULL)
        {
          lib_put_tempbuffer(tmppath);
          return -ENOMEM;
        }

      ret = -ext4_readlink(tmppath, target, PATH_MAX - 1, &rcnt);
      if (ret < 0)
        {
          lib_put_tempbuffer(target);
          lib_put_tempbuffer(tmppath);
          return ret;
        }

      target[rcnt] = '\0';

      if (target[0] != '/')
        {
          /* Relative to the directory containing the symlink */

          FAR char *slash = strrchr(tmppath, '/');
          if (slash != NULL)
            {
              *(slash + 1) = '\0';
              strlcat(tmppath, target, PATH_MAX);
            }
        }
      else
        {
          /* Absolute target: relative to this mount point */

          snprintf(tmppath, PATH_MAX, "%s%s", fs->mount_name, target + 1);
        }

      lib_put_tempbuffer(target);
    }

  if (i >= SYMLOOP_MAX)
    {
      lib_put_tempbuffer(tmppath);
      return -ELOOP;
    }

  /* Allocate file private data with actual path length */

  priv = fs_heap_zalloc(sizeof(struct lwext4_file_s) + strlen(tmppath) + 1);
  if (priv == NULL)
    {
      lib_put_tempbuffer(tmppath);
      return -ENOMEM;
    }

  strcpy(priv->path, tmppath);
  lib_put_tempbuffer(tmppath);

  /* Pass NuttX oflags directly to ext4_fopen2. The lwext4 library is
   * compiled with NuttX headers, so it uses the same O_CREAT, O_RDWR,
   * etc. values as NuttX. No flag conversion is needed.
   */

  ret = -ext4_fopen2(&priv->file, priv->path, oflags);
  if (ret < 0)
    {
      ferr("ext4_fopen failed: %d\n", ret);
      fs_heap_free(priv);
      return ret;
    }

  atomic_set(&priv->refs, 1);
  filep->f_priv = priv;
  return OK;
}

/****************************************************************************
 * Name: lwext4_close
 ****************************************************************************/

static int lwext4_close(FAR struct file *filep)
{
  FAR struct lwext4_file_s *priv = filep->f_priv;
  time_t now;
  int refs;
  int ret;

  finfo("Closing file\n");

  DEBUGASSERT(priv != NULL);

  refs = atomic_sub(&priv->refs, 1);
  DEBUGASSERT(refs > 0);

  if (refs > 1)
    {
      filep->f_priv = NULL;
      return OK;
    }

  /* Update mtime/ctime once at close if file was written */

  if (atomic_read(&priv->dirty) != 0)
    {
      now = time(NULL);
      if (now >= 0)
        {
          ext4_mtime_set(priv->path, now);
          ext4_ctime_set(priv->path, now);
        }
    }

  ret = -ext4_fclose(&priv->file);
  filep->f_priv = NULL;
  fs_heap_free(priv);
  return ret;
}

/****************************************************************************
 * Name: lwext4_read
 ****************************************************************************/

static ssize_t lwext4_read(FAR struct file *filep, FAR char *buffer,
                          size_t buflen)
{
  FAR struct lwext4_file_s *priv = filep->f_priv;
  size_t bytes_read;
  int ret;

  DEBUGASSERT(priv != NULL);

  ret = -ext4_fread(&priv->file, buffer, buflen, &bytes_read);
  if (ret < 0)
    {
      ferr("ext4_fread failed: %d\n", ret);
      return ret;
    }

  filep->f_pos = ext4_ftell(&priv->file);
  return bytes_read;
}

/****************************************************************************
 * Name: lwext4_write
 ****************************************************************************/

static ssize_t lwext4_write(FAR struct file *filep,
                           FAR const char *buffer,
                           size_t buflen)
{
  FAR struct lwext4_file_s *priv = filep->f_priv;
  size_t bytes_written;
  int ret;

  DEBUGASSERT(priv != NULL);

  ret = -ext4_fwrite(&priv->file, buffer, buflen, &bytes_written);
  if (ret < 0)
    {
      ferr("ext4_fwrite failed: %d\n", ret);
      return ret;
    }

  if (bytes_written > 0)
    {
      atomic_set(&priv->dirty, 1);
    }

  filep->f_pos = ext4_ftell(&priv->file);
  return bytes_written;
}

/****************************************************************************
 * Name: lwext4_seek
 ****************************************************************************/

static off_t lwext4_seek(FAR struct file *filep, off_t offset, int whence)
{
  FAR struct lwext4_file_s *priv;
  int ret;

  if (filep == NULL)
    {
      return -EINVAL;
    }

  priv = filep->f_priv;
  DEBUGASSERT(priv != NULL);

  if (priv == NULL)
    {
      return -EINVAL;
    }

  /* lwext4 computes SEEK_END as "fpos = fsize - offset"; negate the offset
   * so it matches the POSIX "fpos = fsize + offset" convention.  Seeking
   * past EOF is not supported and is rejected by ext4_fseek with EINVAL.
   */

  if (whence == SEEK_END)
    {
      offset = -offset;
    }

  ret = -ext4_fseek(&priv->file, offset, whence);
  if (ret < 0)
    {
      ferr("ext4_fseek failed: %d\n", ret);
      return ret;
    }

  filep->f_pos = ext4_ftell(&priv->file);
  return filep->f_pos;
}

/****************************************************************************
 * Name: lwext4_ioctl
 ****************************************************************************/

static int lwext4_ioctl(FAR struct file *filep, int cmd, unsigned long arg)
{
  FAR struct lwext4_mountpt_s *fs;
  FAR struct lwext4_file_s *priv;
  FAR struct inode *inode;

  priv  = filep->f_priv;
  inode = filep->f_inode;
  fs    = inode->i_private;

  switch (cmd)
    {
      case FIOC_FILEPATH:
        {
          FAR char *path = (FAR char *)(uintptr_t)arg;
          int ret;

          ret = inode_getpath(inode, path, PATH_MAX);
          if (ret >= 0)
            {
              size_t len = strlen(path);
              if (path[len - 1] != '/')
                {
                  path[len++] = '/';
                }

              /* Extract relative path from lwext4 internal path
               * by stripping the mount_name prefix (e.g., "/nx0/")
               */

              strlcpy(path + len,
                      priv->path + strlen(fs->mount_name),
                      PATH_MAX - len);
            }

          return ret;
        }

      default:
        break;
    }

  return -ENOTTY;
}

/****************************************************************************
 * Name: lwext4_sync
 ****************************************************************************/

static int lwext4_sync(FAR struct file *filep)
{
  FAR struct lwext4_file_s *priv = filep->f_priv;
  int ret;

  DEBUGASSERT(priv != NULL);

  finfo("Syncing file\n");

  /* lwext4 has no per-file flush; this flushes the whole block cache */

  ret = -ext4_cache_flush(priv->path);
  return ret;
}

/****************************************************************************
 * Name: lwext4_dup
 ****************************************************************************/

static int lwext4_dup(FAR const struct file *oldp, FAR struct file *newp)
{
  FAR struct lwext4_file_s *oldpriv = oldp->f_priv;

  finfo("Duplicating file descriptor\n");

  DEBUGASSERT(oldpriv != NULL);

  atomic_add(&oldpriv->refs, 1);
  newp->f_priv = oldpriv;

  return OK;
}

/****************************************************************************
 * Name: lwext4_fstat
 ****************************************************************************/

static int lwext4_fstat(FAR const struct file *filep, FAR struct stat *buf)
{
  FAR struct lwext4_mountpt_s *fs = filep->f_inode->i_private;
  FAR struct lwext4_file_s *priv = filep->f_priv;
  struct ext4_inode inode;
  uint32_t ino;
  int ret;

  DEBUGASSERT(priv != NULL);

  memset(buf, 0, sizeof(struct stat));
  buf->st_blksize = fs->bdev.bdif->ph_bsize;

  ret = -ext4_raw_inode_fill(priv->path, &ino, &inode);
  if (ret < 0)
    {
      return ret;
    }

  lwext4_fill_stat(fs, buf, ino, &inode);
  return OK;
}

/****************************************************************************
 * Name: lwext4_fchstat
 ****************************************************************************/

static int lwext4_fchstat(FAR const struct file *filep,
                         FAR const struct stat *buf, int flags)
{
  FAR struct lwext4_file_s *priv = filep->f_priv;
  int ret;

  finfo("fchstat: flags=%04x\n", flags);

  DEBUGASSERT(priv != NULL);

  if (flags & CH_STAT_MODE)
    {
      ret = -ext4_mode_set(priv->path, buf->st_mode & ~S_IFMT);
      if (ret < 0)
        {
          return ret;
        }
    }

  if (flags & (CH_STAT_UID | CH_STAT_GID))
    {
      uint32_t uid;
      uint32_t gid;

      ret = -ext4_owner_get(priv->path, &uid, &gid);
      if (ret < 0)
        {
          return ret;
        }

      if (flags & CH_STAT_UID)
        {
          uid = buf->st_uid;
        }

      if (flags & CH_STAT_GID)
        {
          gid = buf->st_gid;
        }

      ret = -ext4_owner_set(priv->path, uid, gid);
      if (ret < 0)
        {
          return ret;
        }
    }

  if (flags & CH_STAT_ATIME)
    {
      ret = -ext4_atime_set(priv->path, buf->st_atime);
      if (ret < 0)
        {
          return ret;
        }
    }

  if (flags & CH_STAT_MTIME)
    {
      ret = -ext4_mtime_set(priv->path, buf->st_mtime);
      if (ret < 0)
        {
          return ret;
        }
    }

  if (flags & CH_STAT_CTIME)
    {
      ret = -ext4_ctime_set(priv->path, buf->st_ctime);
      if (ret < 0)
        {
          return ret;
        }
    }

  return OK;
}

/****************************************************************************
 * Name: lwext4_truncate
 ****************************************************************************/

static int lwext4_truncate(FAR struct file *filep, off_t length)
{
  FAR struct lwext4_file_s *priv = filep->f_priv;
  int ret;

  DEBUGASSERT(priv != NULL);

  ret = -ext4_ftruncate(&priv->file, length);
  return ret;
}

/****************************************************************************
 * Directory Operations Implementation
 ****************************************************************************/

/****************************************************************************
 * Name: lwext4_opendir
 ****************************************************************************/

static int lwext4_opendir(FAR struct inode *mountpt,
                          FAR const char *relpath,
                          FAR struct fs_dirent_s **dir)
{
  FAR struct lwext4_mountpt_s *fs;
  FAR struct lwext4_dir_s *dpriv;
  int ret;

  finfo("Opening directory: %s\n", relpath);

  if (mountpt == NULL || dir == NULL)
    {
      return -EINVAL;
    }

  fs = mountpt->i_private;
  DEBUGASSERT(fs != NULL);
  if (fs == NULL)
    {
      return -EINVAL;
    }

  FAR char *tmppath = lib_get_tempbuffer(PATH_MAX);
  if (tmppath == NULL)
    {
      return -ENOMEM;
    }

  lwext4_build_path(fs, relpath, tmppath, PATH_MAX);

  dpriv = fs_heap_zalloc(sizeof(struct lwext4_dir_s) + strlen(tmppath) + 1);
  if (dpriv == NULL)
    {
      lib_put_tempbuffer(tmppath);
      return -ENOMEM;
    }

  strcpy(dpriv->path, tmppath);
  lib_put_tempbuffer(tmppath);

  ret = -ext4_dir_open(&dpriv->dir, dpriv->path);
  if (ret < 0)
    {
      ferr("ext4_dir_open failed: %d\n", ret);
      fs_heap_free(dpriv);
      return ret;
    }

  *dir = &dpriv->base;
  return OK;
}

/****************************************************************************
 * Name: lwext4_closedir
 ****************************************************************************/

static int lwext4_closedir(FAR struct inode *mountpt,
                          FAR struct fs_dirent_s *dir)
{
  FAR struct lwext4_dir_s *dpriv = (FAR struct lwext4_dir_s *)dir;
  int ret;

  finfo("Closing directory\n");

  DEBUGASSERT(dpriv != NULL);

  ret = -ext4_dir_close(&dpriv->dir);
  fs_heap_free(dpriv);

  return ret;
}

/****************************************************************************
 * Name: lwext4_readdir
 ****************************************************************************/

static int lwext4_readdir(FAR struct inode *mountpt,
                         FAR struct fs_dirent_s *dir,
                         FAR struct dirent *entry)
{
  FAR struct lwext4_dir_s *dpriv = (FAR struct lwext4_dir_s *)dir;
  FAR const ext4_direntry *dentry;

  DEBUGASSERT(dpriv != NULL);

  dentry = ext4_dir_entry_next(&dpriv->dir);
  if (dentry == NULL)
    {
      return -ENOENT;  /* End of directory */
    }

  /* Copy directory entry */

  memset(entry, 0, sizeof(struct dirent));
  strlcpy(entry->d_name, (FAR const char *)dentry->name,
          sizeof(entry->d_name));

  /* Set file type */

  switch (dentry->inode_type)
    {
      case EXT4_DE_DIR:
        entry->d_type = DT_DIR;
        break;
      case EXT4_DE_REG_FILE:
        entry->d_type = DT_REG;
        break;
      case EXT4_DE_SYMLINK:
        entry->d_type = DT_LNK;
        break;
      default:
        entry->d_type = DT_UNKNOWN;
        break;
    }

  return OK;
}

/****************************************************************************
 * Name: lwext4_rewinddir
 ****************************************************************************/

static int lwext4_rewinddir(FAR struct inode *mountpt,
                           FAR struct fs_dirent_s *dir)
{
  FAR struct lwext4_dir_s *dpriv = (FAR struct lwext4_dir_s *)dir;
  int ret;

  DEBUGASSERT(dpriv != NULL);

  ret = -ext4_dir_close(&dpriv->dir);
  if (ret < 0)
    {
      return ret;
    }

  ret = -ext4_dir_open(&dpriv->dir, dpriv->path);
  return ret;
}

/****************************************************************************
 * Mount Point Operations Implementation
 ****************************************************************************/

/****************************************************************************
 * Name: lwext4_bind
 *
 * Description:
 *   Bind a block device to the lwext4 filesystem
 *
 ****************************************************************************/

static int lwext4_bind(FAR struct inode *driver, FAR const void *data,
                       FAR void **handle)
{
  FAR struct lwext4_mountpt_s *fs;
  int cleanupret;
  struct geometry geo;
  bool autoformat;
  bool forceformat;
  int ret;

  finfo("Binding lwext4 filesystem\n");

  if (driver == NULL || handle == NULL)
    {
      return -EINVAL;
    }

  if (driver->u.i_bops == NULL || driver->u.i_bops->geometry == NULL)
    {
      return -ENODEV;
    }

  /* Open block device */

  if (driver->u.i_bops->open != NULL)
    {
      ret = driver->u.i_bops->open(driver);
      if (ret < 0)
        {
          ferr("Failed to open block device: %d\n", ret);
          return ret;
        }
    }

  /* Get device geometry */

  ret = driver->u.i_bops->geometry(driver, &geo);
  if (ret < 0)
    {
      ferr("Failed to get geometry: %d\n", ret);
      goto errout_with_open;
    }

  finfo("Block device: sector_size=%u nsectors=%llu\n",
        geo.geo_sectorsize, geo.geo_nsectors);

  /* Allocate mount point structure with physical block buffer */

  fs = fs_heap_zalloc(sizeof(struct lwext4_mountpt_s) + geo.geo_sectorsize);
  if (fs == NULL)
    {
      ret = -ENOMEM;
      goto errout_with_open;
    }

  /* Save block device driver */

  fs->drv = driver;

  /* Initialize the per-mount-point recursive lock and the lwext4 lock
   * callbacks that resolve to it (container_of on the embedded oslock
   * recovers this mount point).
   */

  nxrmutex_init(&fs->lock);
  fs->oslock.lock   = lwext4_lock;
  fs->oslock.unlock = lwext4_unlock;

  /* Initialize block device interface */

  fs->bdiface.open = lwext4_bdev_open;
  fs->bdiface.bread = lwext4_bdev_bread;
  fs->bdiface.bwrite = lwext4_bdev_bwrite;
  fs->bdiface.close = lwext4_bdev_close;
  fs->bdiface.ph_bsize = geo.geo_sectorsize;
  fs->bdiface.ph_bcnt = geo.geo_nsectors;
  fs->bdiface.ph_bbuf = fs->ph_bbuf;
  fs->bdiface.p_user = fs;

  fs->bdev.bdif = &fs->bdiface;
  fs->bdev.part_size = geo.geo_nsectors * geo.geo_sectorsize;

  ret = lwext4_parse_options(data, &fs->readonly, &autoformat, &forceformat);
  if (ret < 0)
    {
      goto errout_with_fs;
    }

  /* Generate unique mount name (must end with '/' for lwext4) */

  snprintf(fs->mount_name, sizeof(fs->mount_name),
           LWEXT4_MOUNT_PREFIX "%p/", fs);

  /* Register block device with lwext4 */

  ret = -ext4_device_register(&fs->bdev, fs->mount_name);
  if (ret < 0)
    {
      ferr("Failed to register device: %d\n", ret);
      goto errout_with_fs;
    }

  if (forceformat)
    {
      finfo("Formatting filesystem before mount\n");
      ret = lwext4_format(fs);
      if (ret < 0)
        {
          goto errout_with_device;
        }
    }

  /* Mount the filesystem */

  ret = -ext4_mount(fs->mount_name, fs->mount_name, fs->readonly);
  if (ret < 0)
    {
      if (!autoformat)
        {
          ferr("Failed to mount filesystem: %d\n", ret);
          goto errout_with_device;
        }

      fwarn("Mount failed (%d), formatting filesystem and retrying\n", ret);

      ret = lwext4_format(fs);
      if (ret < 0)
        {
          goto errout_with_device;
        }

      ret = -ext4_mount(fs->mount_name, fs->mount_name, fs->readonly);
      if (ret < 0)
        {
          ferr("Failed to mount filesystem after format: %d\n", ret);
          goto errout_with_device;
        }
    }

#ifdef CONFIG_LWEXT4_ENABLE_JOURNAL
  /* Enable journaling if configured */

  if (!fs->readonly)
    {
      ret = -ext4_recover(fs->mount_name);
      if (ret < 0)
        {
          ferr("Journal recovery failed: %d\n", ret);
          goto errout_with_mount;
        }

      ret = -ext4_journal_start(fs->mount_name);
      if (ret < 0)
        {
          ferr("Failed to start journal: %d\n", ret);
          goto errout_with_mount;
        }
    }
#endif

  /* Install the per-mount-point lock callbacks now that the mount point
   * exists in the library table.
   */

  ret = -ext4_mount_setup_locks(fs->mount_name, &fs->oslock);
  if (ret < 0)
    {
      fwarn("Failed to setup mount locks: %d (continuing)\n", ret);
    }

  if (!fs->readonly)
    {
      ret = -ext4_cache_write_back(fs->mount_name, 1);
      if (ret < 0)
        {
          fwarn("Failed to enable write-back cache: %d\n", ret);
        }
    }

  finfo("Filesystem mounted successfully as %s\n", fs->mount_name);

  *handle = fs;
  return OK;

errout_with_mount:
  cleanupret = -ext4_umount(fs->mount_name);
  if (cleanupret < 0)
    {
      fwarn("Failed to unmount after mount error: %d\n", cleanupret);
    }

errout_with_device:
  ext4_device_unregister(fs->mount_name);
errout_with_fs:
  nxrmutex_destroy(&fs->lock);
  fs_heap_free(fs);
errout_with_open:
  if (driver->u.i_bops->close != NULL)
    {
      driver->u.i_bops->close(driver);
    }

  return ret;
}

/****************************************************************************
 * Name: lwext4_unbind
 *
 * Description:
 *   Unbind the filesystem from the block device
 *
 ****************************************************************************/

static int lwext4_unbind(FAR void *handle, FAR struct inode **driver,
                        unsigned int flags)
{
  FAR struct lwext4_mountpt_s *fs = handle;
  int ret;

  finfo("Unbinding filesystem\n");

  DEBUGASSERT(fs != NULL);

#ifdef CONFIG_LWEXT4_ENABLE_JOURNAL
  /* Stop journal */

  if (!fs->readonly)
    {
      ext4_journal_stop(fs->mount_name);
    }
#endif

  /* Flush cache and unmount */

  ret = -ext4_cache_flush(fs->mount_name);
  if (ret < 0)
    {
      ferr("Failed to flush cache during unbind: %d\n", ret);
      return ret;
    }

  ret = -ext4_umount(fs->mount_name);
  if (ret < 0)
    {
      ferr("Failed to unmount filesystem: %d\n", ret);
      return ret;
    }

  /* Unregister device */

  ext4_device_unregister(fs->mount_name);

  /* Close block device */

  *driver = fs->drv;
  if ((*driver)->u.i_bops->close != NULL)
    {
      (*driver)->u.i_bops->close(*driver);
    }

  /* Clean up */

  nxrmutex_destroy(&fs->lock);
  fs_heap_free(fs);

  return ret;
}

/****************************************************************************
 * Name: lwext4_statfs
 ****************************************************************************/

static int lwext4_statfs(FAR struct inode *mountpt, FAR struct statfs *buf)
{
  FAR struct lwext4_mountpt_s *fs;
  struct ext4_mount_stats stats;
  int ret;

  if (mountpt == NULL || buf == NULL)
    {
      return -EINVAL;
    }

  fs = mountpt->i_private;

  DEBUGASSERT(fs != NULL);
  if (fs == NULL)
    {
      return -EINVAL;
    }

  ret = -ext4_mount_point_stats(fs->mount_name, &stats);
  if (ret < 0)
    {
      return ret;
    }

  /* Clamp free_blocks_count to blocks_count. The ext4_mkfs had a bug
   * where the last block group's free count was based on blocks_per_group
   * instead of the actual blocks in that group, inflating free_blocks.
   */

  if (stats.free_blocks_count > stats.blocks_count)
    {
      stats.free_blocks_count = stats.blocks_count;
    }

  memset(buf, 0, sizeof(struct statfs));
  buf->f_type = EXT2_SUPER_MAGIC;
  buf->f_namelen = 255;
  buf->f_bsize = stats.block_size;
  buf->f_blocks = stats.blocks_count;
  buf->f_bfree = stats.free_blocks_count;
  buf->f_bavail = stats.free_blocks_count;
  buf->f_files = stats.inodes_count;
  buf->f_ffree = stats.free_inodes_count;

  return OK;
}

/****************************************************************************
 * Filesystem Operations Implementation
 ****************************************************************************/

/****************************************************************************
 * Name: lwext4_unlink
 ****************************************************************************/

static int lwext4_unlink(FAR struct inode *mountpt,
                        FAR const char *relpath)
{
  FAR struct lwext4_mountpt_s *fs = mountpt->i_private;
  FAR char *path;
  int ret;

  finfo("Unlinking: %s\n", relpath);

  path = lib_get_tempbuffer(PATH_MAX);
  if (path == NULL)
    {
      return -ENOMEM;
    }

  lwext4_build_path(fs, relpath, path, PATH_MAX);

  ret = -ext4_fremove(path);
  lib_put_tempbuffer(path);
  return ret;
}

/****************************************************************************
 * Name: lwext4_mkdir
 ****************************************************************************/

static int lwext4_mkdir(FAR struct inode *mountpt,
                       FAR const char *relpath, mode_t mode)
{
  FAR struct lwext4_mountpt_s *fs = mountpt->i_private;
  FAR char *path;
  int ret;

  finfo("Creating directory: %s mode=%04x\n", relpath, mode);

  path = lib_get_tempbuffer(PATH_MAX);
  if (path == NULL)
    {
      return -ENOMEM;
    }

  lwext4_build_path(fs, relpath, path, PATH_MAX);

  ret = -ext4_dir_mk(path);
  lib_put_tempbuffer(path);
  return ret;
}

/****************************************************************************
 * Name: lwext4_rmdir
 ****************************************************************************/

static int lwext4_rmdir(FAR struct inode *mountpt,
                       FAR const char *relpath)
{
  FAR struct lwext4_mountpt_s *fs = mountpt->i_private;
  FAR char *path;
  int ret;

  finfo("Removing directory: %s\n", relpath);

  path = lib_get_tempbuffer(PATH_MAX);
  if (path == NULL)
    {
      return -ENOMEM;
    }

  lwext4_build_path(fs, relpath, path, PATH_MAX);

  ret = -ext4_dir_rm(path);
  lib_put_tempbuffer(path);
  return ret;
}

/****************************************************************************
 * Name: lwext4_rename
 ****************************************************************************/

static int lwext4_rename(FAR struct inode *mountpt,
                        FAR const char *oldrelpath,
                        FAR const char *newrelpath)
{
  FAR struct lwext4_mountpt_s *fs = mountpt->i_private;
  FAR char *oldpath;
  FAR char *newpath;
  int ret;

  finfo("Renaming: %s -> %s\n", oldrelpath, newrelpath);

  oldpath = lib_get_tempbuffer(PATH_MAX);
  if (oldpath == NULL)
    {
      return -ENOMEM;
    }

  newpath = lib_get_tempbuffer(PATH_MAX);
  if (newpath == NULL)
    {
      lib_put_tempbuffer(oldpath);
      return -ENOMEM;
    }

  lwext4_build_path(fs, oldrelpath, oldpath, PATH_MAX);
  lwext4_build_path(fs, newrelpath, newpath, PATH_MAX);

  ret = -ext4_frename(oldpath, newpath);
  lib_put_tempbuffer(newpath);
  lib_put_tempbuffer(oldpath);
  return ret;
}

/****************************************************************************
 * Name: lwext4_fill_stat
 *
 * Description:
 *   Fill stat buffer from a raw ext4 inode.
 *
 ****************************************************************************/

static void lwext4_fill_stat(FAR struct lwext4_mountpt_s *fs,
                             FAR struct stat *buf,
                             uint32_t ino,
                             FAR struct ext4_inode *inode)
{
  buf->st_mode   = ext4_inode_get_mode(&fs->fs.sb, inode);
  buf->st_ino    = ino;
  buf->st_nlink  = ext4_inode_get_links_cnt(inode);
  buf->st_size   = ext4_inode_get_size(&fs->fs.sb, inode);
  buf->st_uid    = ext4_inode_get_uid(inode);
  buf->st_gid    = ext4_inode_get_gid(inode);
  buf->st_atime  = ext4_inode_get_access_time(inode);
  buf->st_mtime  = ext4_inode_get_modif_time(inode);
  buf->st_ctime  = ext4_inode_get_change_inode_time(inode);
  buf->st_blocks = ext4_inode_get_blocks_count(&fs->fs.sb, inode);
}

/****************************************************************************
 * Name: lwext4_stat
 *
 * Description:
 *   Return information about a file, following symbolic links.
 *
 ****************************************************************************/

static int lwext4_stat(FAR struct inode *mountpt,
                      FAR const char *relpath,
                      FAR struct stat *buf)
{
  FAR struct lwext4_mountpt_s *fs = mountpt->i_private;
  struct ext4_inode inode;
  FAR char *path;
  FAR char *target;
  uint32_t ino;
  size_t rcnt;
  int ret;
  int i;

  finfo("Stat: %s\n", relpath ? relpath : "(null)");

  memset(buf, 0, sizeof(struct stat));
  buf->st_blksize = fs->bdev.bdif->ph_bsize;

  /* Root directory: relpath is NULL or empty */

  if (relpath == NULL || relpath[0] == '\0')
    {
      buf->st_mode = S_IFDIR | 0755;
      buf->st_nlink = 2;
      return OK;
    }

  path = lib_get_tempbuffer(PATH_MAX);
  if (path == NULL)
    {
      return -ENOMEM;
    }

  target = lib_get_tempbuffer(PATH_MAX);
  if (target == NULL)
    {
      lib_put_tempbuffer(path);
      return -ENOMEM;
    }

  lwext4_build_path(fs, relpath, path, PATH_MAX);

  /* Follow symbolic links up to SYMLOOP_MAX times */

  for (i = 0; i < SYMLOOP_MAX; i++)
    {
      ret = -ext4_raw_inode_fill(path, &ino, &inode);
      if (ret < 0)
        {
          goto errout;
        }

      if (!S_ISLNK(ext4_inode_get_mode(&fs->fs.sb, &inode)))
        {
          /* Not a symlink, fill stat and return */

          lwext4_fill_stat(fs, buf, ino, &inode);
          ret = OK;
          goto errout;
        }

      /* Read symlink target */

      ret = -ext4_readlink(path, target, PATH_MAX - 1, &rcnt);
      if (ret < 0)
        {
          goto errout;
        }

      target[rcnt] = '\0';

      /* Resolve target path */

      if (target[0] != '/')
        {
          /* Relative: resolve against the directory containing
           * the symlink.
           */

          FAR char *slash = strrchr(path, '/');
          if (slash != NULL)
            {
              *(slash + 1) = '\0';
              strlcat(path, target, PATH_MAX);
            }
        }
      else
        {
          /* Absolute: treat as relative to this mount point */

          snprintf(path, PATH_MAX, "%s%s",
                   fs->mount_name, target + 1);
        }
    }

  /* The loop exhausted SYMLOOP_MAX hops without reaching a
   * non-symlink target: report too many levels of symbolic links.
   */

  ret = -ELOOP;

errout:
  lib_put_tempbuffer(target);
  lib_put_tempbuffer(path);
  return ret;
}

/****************************************************************************
 * Name: lwext4_chstat
 ****************************************************************************/

static int lwext4_chstat(FAR struct inode *mountpt,
                        FAR const char *relpath,
                        FAR const struct stat *buf,
                        int flags)
{
  FAR struct lwext4_mountpt_s *fs = mountpt->i_private;
  FAR char *path;
  int ret = 0;

  finfo("chstat: %s flags=%04x\n", relpath, flags);

  path = lib_get_tempbuffer(PATH_MAX);
  if (path == NULL)
    {
      return -ENOMEM;
    }

  lwext4_build_path(fs, relpath, path, PATH_MAX);

  if (flags & CH_STAT_MODE)
    {
      ret = -ext4_mode_set(path, buf->st_mode & ~S_IFMT);
      if (ret < 0)
        {
          goto errout;
        }
    }

  if (flags & (CH_STAT_UID | CH_STAT_GID))
    {
      uint32_t uid;
      uint32_t gid;

      ret = -ext4_owner_get(path, &uid, &gid);
      if (ret < 0)
        {
          goto errout;
        }

      if (flags & CH_STAT_UID)
        {
          uid = buf->st_uid;
        }

      if (flags & CH_STAT_GID)
        {
          gid = buf->st_gid;
        }

      ret = -ext4_owner_set(path, uid, gid);
      if (ret < 0)
        {
          goto errout;
        }
    }

  if (flags & CH_STAT_ATIME)
    {
      ret = -ext4_atime_set(path, buf->st_atime);
      if (ret < 0)
        {
          goto errout;
        }
    }

  if (flags & CH_STAT_MTIME)
    {
      ret = -ext4_mtime_set(path, buf->st_mtime);
      if (ret < 0)
        {
          goto errout;
        }
    }

  if (flags & CH_STAT_CTIME)
    {
      ret = -ext4_ctime_set(path, buf->st_ctime);
      if (ret < 0)
        {
          goto errout;
        }
    }

errout:
  lib_put_tempbuffer(path);
  return ret;
}

/****************************************************************************
 * Name: lwext4_syncfs
 ****************************************************************************/

static int lwext4_syncfs(FAR struct inode *mountpt)
{
  FAR struct lwext4_mountpt_s *fs;
  int ret;

  DEBUGASSERT(mountpt && mountpt->i_private);
  fs = mountpt->i_private;

  ret = -ext4_cache_flush(fs->mount_name);
  return ret;
}

#ifdef CONFIG_FS_LINKS

/****************************************************************************
 * Name: lwext4_link
 ****************************************************************************/

static int lwext4_link(FAR struct inode *mountpt,
                       FAR const char *relpath1,
                       FAR const char *relpath2)
{
  FAR struct lwext4_mountpt_s *fs;
  FAR char *path1;
  FAR char *path2;
  int ret;

  DEBUGASSERT(mountpt && mountpt->i_private);
  fs = mountpt->i_private;

  path1 = lib_get_tempbuffer(PATH_MAX);
  if (path1 == NULL)
    {
      return -ENOMEM;
    }

  path2 = lib_get_tempbuffer(PATH_MAX);
  if (path2 == NULL)
    {
      lib_put_tempbuffer(path1);
      return -ENOMEM;
    }

  lwext4_build_path(fs, relpath1, path1, PATH_MAX);
  lwext4_build_path(fs, relpath2, path2, PATH_MAX);

  finfo("link: %s -> %s\n", path1, path2);

  ret = -ext4_flink(path1, path2);
  lib_put_tempbuffer(path2);
  lib_put_tempbuffer(path1);
  return ret;
}

/****************************************************************************
 * Name: lwext4_symlink
 ****************************************************************************/

static int lwext4_symlink(FAR struct inode *mountpt,
                          FAR const char *target,
                          FAR const char *relpath)
{
  FAR struct lwext4_mountpt_s *fs;
  FAR char *path;
  int ret;

  DEBUGASSERT(mountpt && mountpt->i_private);
  fs = mountpt->i_private;

  path = lib_get_tempbuffer(PATH_MAX);
  if (path == NULL)
    {
      return -ENOMEM;
    }

  lwext4_build_path(fs, relpath, path, PATH_MAX);

  finfo("symlink: %s -> %s\n", path, target);

  ret = -ext4_fsymlink(target, path);
  lib_put_tempbuffer(path);
  return ret;
}

/****************************************************************************
 * Name: lwext4_readlink
 ****************************************************************************/

static ssize_t lwext4_readlink(FAR struct inode *mountpt,
                               FAR const char *relpath,
                               FAR char *buf, size_t bufsize)
{
  FAR struct lwext4_mountpt_s *fs;
  FAR char *path;
  size_t rcnt;
  int ret;

  DEBUGASSERT(mountpt && mountpt->i_private);
  fs = mountpt->i_private;

  path = lib_get_tempbuffer(PATH_MAX);
  if (path == NULL)
    {
      return -ENOMEM;
    }

  lwext4_build_path(fs, relpath, path, PATH_MAX);

  finfo("readlink: %s\n", path);

  ret = -ext4_readlink(path, buf, bufsize, &rcnt);
  lib_put_tempbuffer(path);
  if (ret < 0)
    {
      return ret;
    }

  return rcnt;
}

/****************************************************************************
 * Name: lwext4_lstat
 *
 * Description:
 *   Return information about the file/directory at 'relpath' without
 *   following symbolic links.  lwext4 does not auto-follow symlinks
 *   in its path walk, so ext4_raw_inode_fill returns the symlink
 *   inode itself.
 *
 ****************************************************************************/

static int lwext4_lstat(FAR struct inode *mountpt,
                        FAR const char *relpath,
                        FAR struct stat *buf)
{
  FAR struct lwext4_mountpt_s *fs = mountpt->i_private;
  struct ext4_inode inode;
  FAR char *path;
  uint32_t ino;
  int ret;

  finfo("Lstat: %s\n", relpath ? relpath : "(null)");

  memset(buf, 0, sizeof(struct stat));
  buf->st_blksize = fs->bdev.bdif->ph_bsize;

  /* Root directory: relpath is NULL or empty */

  if (relpath == NULL || relpath[0] == '\0')
    {
      buf->st_mode = S_IFDIR | 0755;
      buf->st_nlink = 2;
      return OK;
    }

  path = lib_get_tempbuffer(PATH_MAX);
  if (path == NULL)
    {
      return -ENOMEM;
    }

  lwext4_build_path(fs, relpath, path, PATH_MAX);

  ret = -ext4_raw_inode_fill(path, &ino, &inode);
  lib_put_tempbuffer(path);
  if (ret < 0)
    {
      return ret;
    }

  lwext4_fill_stat(fs, buf, ino, &inode);
  return OK;
}

#endif /* CONFIG_FS_LINKS */
