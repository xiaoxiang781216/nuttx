/****************************************************************************
 * fs/vfs/fs_dir.c
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

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#include <nuttx/kmalloc.h>
#include <nuttx/fs/fs.h>
#include <nuttx/fs/ioctl.h>
#include <nuttx/lib/lib.h>
#include <nuttx/atomic.h>

#include "inode/inode.h"
#include "fs_heap.h"
#include "vfs.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* For the root pseudo-file system, we need retain only the 'next' inode
 * need for the next readdir() operation. We hold a reference on this
 * inode so we know that it will persist until closedir is called.
 */

struct fs_pseudodir_s
{
  struct fs_dirent_s dir;
  FAR struct inode *next;
};

/****************************************************************************
 * Private Functions Prototypes
 ****************************************************************************/

static int     dir_open(FAR struct file *filep);
static int     dir_close(FAR struct file *filep);
static ssize_t dir_read(FAR struct file *filep, FAR char *buffer,
                        size_t buflen);
static off_t   dir_seek(FAR struct file *filep, off_t offset, int whence);
static int     dir_ioctl(FAR struct file *filep, int cmd, unsigned long arg);

/****************************************************************************
 * Private Data
 ****************************************************************************/

const struct file_operations g_dir_fileops =
{
  dir_open,   /* open */
  dir_close,  /* close */
  dir_read,   /* read */
  NULL,       /* write */
  dir_seek,   /* seek */
  dir_ioctl,  /* ioctl */
};

#ifndef CONFIG_DISABLE_MOUNTPOINT
static DEFINE_PER_CPU_BMP(struct inode, g_dir_inode) =
{
  NULL,
  NULL,
  NULL,
  1,
  0,
  { &g_dir_fileops },
};
#define g_dir_inode this_cpu_var_bmp(g_dir_inode)
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: open_mountpoint
 *
 * Description:
 *   Handle the case where the inode to be opened is within a mountpoint.
 *
 * Input Parameters:
 *   inode -- the inode of the mountpoint to open
 *   relpath -- the relative path within the mountpoint to open
 *   dir -- the dirent structure to be initialized
 *   abspath -- the absolute path of the directory being opened
 *
 * Returned Value:
 *   On success, 0 is returned; Otherwise, a negative errno is returned.
 *
 ****************************************************************************/

#ifndef CONFIG_DISABLE_MOUNTPOINT
static int open_mountpoint(FAR struct inode *inode, FAR const char *relpath,
                           FAR struct fs_dirent_s **dir,
                           FAR const char *abspath)
{
  int ret;

  /* The inode itself as the 'root' of mounted volume. The actually
   * directory is at relpath into the mounted filesystem.
   *
   * Verify that the mountpoint inode supports the opendir() method.
   */

  if (inode->u.i_mops == NULL || inode->u.i_mops->opendir == NULL ||
      inode->u.i_mops->readdir == NULL)
    {
      return -ENOSYS;
    }

#ifdef CONFIG_FS_PATHCACHE
  if (INODE_IS_PATHCACHE(inode))
    {
      ret = pathcache_opendir(inode, relpath, dir, abspath);
    }
  else
#endif
    {
      ret = inode->u.i_mops->opendir(inode, relpath, dir);
    }

  if (ret >= 0)
    {
      (*dir)->fd_root = inode;
    }

  return ret;
}

/****************************************************************************
 * Name: close_mountpoint
 *
 * Description:
 *   Handle the case where the inode to be closed is within a mountpoint.
 *
 * Input Parameters:
 *   inode -- the inode of the mountpoint to close
 *   dir -- the dirent structure to be closed
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static inline void close_mountpoint(FAR struct inode *inode,
                                    FAR struct fs_dirent_s *dir)
{
  if (inode->u.i_mops != NULL && inode->u.i_mops->closedir != NULL)
    {
      inode->u.i_mops->closedir(inode, dir);
    }
}
#endif

/****************************************************************************
 * Name: open_pseudodir
 *
 * Description:
 *   Handle the case where the inode to be opened is within the top-level
 *   pseudo-file system.
 *
 * Input Parameters:
 *   inode -- the inode of the mountpoint to open
 *   dir -- the dirent structure to be initialized
 *
 * Returned Value:
 *   On success, 0 is returned; Otherwise, a negative errno is returned.
 *
 ****************************************************************************/

static int open_pseudodir(FAR struct inode *inode,
                          FAR struct fs_dirent_s **dir)
{
  FAR struct fs_pseudodir_s *pdir;

  pdir = fs_heap_zalloc(sizeof(*pdir));
  if (pdir == NULL)
    {
      return -ENOMEM;
    }

  *dir              = &pdir->dir;
  pdir->dir.fd_root = inode;          /* Save the inode where we start */

  /* Take the first child inode and a reference on it under the inode
   * tree lock.  On SMP, a concurrent inode_insert()/inode_unlink() on
   * this directory could otherwise change i_child between the read and
   * the addref, leaving pdir->next pointing at an inode we never
   * addref'd; a concurrent inode_remove() could then free it, causing
   * a use-after-free in the next readdir().
   */

  inode_lock();
  pdir->next = inode->i_child;        /* The next node for readdir */
  inode_addref(pdir->next);
  inode_unlock();

  return 0;
}

/****************************************************************************
 * Name: close_pseudodir
 *
 * Description:
 *   Handle the case where the inode to be closed is within the top-level
 *   pseudo-file system.
 *
 * Input Parameters:
 *   dir -- the dirent structure to be closed
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static inline void close_pseudodir(FAR struct fs_dirent_s *dir)
{
  FAR struct fs_pseudodir_s *pdir = (FAR struct fs_pseudodir_s *)dir;

  if (pdir->next != NULL)
    {
      inode_release(pdir->next);
    }

  fs_heap_free(pdir);
}

/****************************************************************************
 * Name: seek_pseudodir
 ****************************************************************************/

static off_t seek_pseudodir(FAR struct file *filep, off_t offset)
{
  FAR struct fs_pseudodir_s *pdir = filep->f_priv;
  FAR struct inode *curr;
  FAR struct inode *prev;
  off_t pos;

  /* Traverse the peer list starting at the 'root' of the
   * the list until we find the node at 'offset". If devices
   * are being registered and unregistered, then this can
   * be a very unpredictable operation.
   */

  inode_lock();

  /* Determine a starting point for the seek. If the seek is "forward" from
   * the current position, then we will start at the current position.
   * Otherwise, we will "rewind" to the root dir.  The starting inode is
   * read under the lock: fd_root->i_child has no reference of its own, so
   * on SMP a concurrent inode_remove() could free it before we acquire the
   * lock if it were read unlocked.
   */

  if (offset < filep->f_pos)
    {
      pos  = 0;
      curr = pdir->dir.fd_root->i_child;
    }
  else
    {
      pos  = filep->f_pos;
      curr = pdir->next;
    }

  for (; curr != NULL && pos != offset; pos++, curr = curr->i_peer)
    {
    }

  /* Now get the inode to visit next time that readdir() is called */

  prev = pdir->next;

  /* The next node to visit (might be null) */

  pdir->next = curr;
  if (curr != NULL)
    {
      /* Increment the reference count on this next node */

      atomic_add(&curr->i_crefs, 1);
    }

  inode_unlock();

  if (prev != NULL)
    {
      inode_release(prev);
    }

  return pos;
}

/****************************************************************************
 * Name: seek_mountptdir
 ****************************************************************************/

#ifndef CONFIG_DISABLE_MOUNTPOINT
static off_t seek_mountptdir(FAR struct file *filep, off_t offset)
{
  FAR struct fs_dirent_s *dir = filep->f_priv;
  FAR struct inode *inode = dir->fd_root;
  struct dirent entry;
  off_t pos;

  /* Determine a starting point for the seek. If the seek
   * is "forward" from the current position, then we will
   * start at the current position. Otherwise, we will
   * "rewind" to the root dir.
   */

  if (offset < filep->f_pos)
    {
      if (inode->u.i_mops->rewinddir != NULL)
        {
          inode->u.i_mops->rewinddir(inode, dir);
          pos = 0;
        }
      else
        {
          return -ENOTSUP;
        }
    }
  else
    {
      pos = filep->f_pos;
    }

  /* This is a brute force approach... we will just read
   * directory entries until we are at the desired position.
   */

  while (pos < offset)
    {
      int ret;

      ret = inode->u.i_mops->readdir(inode, dir, &entry);
      if (ret < 0)
        {
          return ret;
        }

      /* Increment the position on each successful read */

      pos++;
    }

  return pos;
}
#endif

/****************************************************************************
 * Name: read_pseudodir
 ****************************************************************************/

static int read_pseudodir(FAR struct fs_dirent_s *dir,
                          FAR struct dirent *entry)
{
  FAR struct fs_pseudodir_s *pdir = (FAR struct fs_pseudodir_s *)dir;
  FAR struct inode *next;
  FAR struct inode *prev;

  /* The pseudo inode tree may be modified concurrently by another CPU
   * (e.g. driver register/unregister). Hold the inode lock across the
   * entire read so that the 'next' pointer and every field dereferenced
   * below (i_name, u.i_ops, i_child and i_peer) stay valid, matching the
   * locking already used by seek_pseudodir().
   */

  inode_lock();

  next = pdir->next;

  /* Check if we are at the end of the list */

  if (next == NULL)
    {
      /* End of file and error conditions are not distinguishable with
       * readdir. Here we return -ENOENT to signal the end of the directory.
       */

      inode_unlock();
      return -ENOENT;
    }

  /* Copy the inode name into the dirent structure.
   * If it's a hardlink, we should get the name of hardlink itself,
   * not the target file name.
   * But other information like type should be based on the target file.
   * So we put this line before the INODE_IS_HARDLINK() check.
   */

  strlcpy(entry->d_name, next->i_name, sizeof(entry->d_name));

#ifdef CONFIG_FS_LINKS
  if (INODE_IS_HARDLINK(next))
    {
      DEBUGASSERT(next->i_private != NULL);
      next = next->i_private;
    }
#endif

  /* If the node has file operations, we will say that it is a file. */

  entry->d_type = DTYPE_UNKNOWN;
  if (next->u.i_ops != NULL)
    {
#ifndef CONFIG_DISABLE_MOUNTPOINT
      if (INODE_IS_BLOCK(next))
        {
          entry->d_type = DTYPE_BLK;
        }
      else if (INODE_IS_MTD(next))
        {
          entry->d_type = DTYPE_MTD;
        }
      else if (INODE_IS_MOUNTPT(next))
        {
          entry->d_type = DTYPE_DIRECTORY;
        }
      else
#endif
#ifdef CONFIG_FS_LINKS
      if (INODE_IS_SOFTLINK(next))
        {
          entry->d_type = DTYPE_LINK;
        }
      else
#endif
      if (INODE_IS_DRIVER(next))
        {
          entry->d_type = DTYPE_CHR;
        }
      else if (INODE_IS_NAMEDSEM(next))
        {
          entry->d_type = DTYPE_SEM;
        }
      else if (INODE_IS_MQUEUE(next))
        {
          entry->d_type = DTYPE_MQ;
        }
      else if (INODE_IS_SHM(next))
        {
          entry->d_type = DTYPE_SHM;
        }
      else if (INODE_IS_PIPE(next))
        {
          entry->d_type = DTYPE_FIFO;
        }
    }

  /* If the node has child node(s) or no operations, then we will say that
   * it is a directory rather than a special file. NOTE: that the node can
   * be both!
   */

  if (next->i_child != NULL ||
      next->u.i_ops == NULL)
    {
      entry->d_type = DTYPE_DIRECTORY;
    }

  entry->d_ino = next->i_ino;

  /* Now get the inode to visit next time that readdir() is called */

  prev       = pdir->next;
  pdir->next = prev->i_peer; /* The next node to visit */

  if (pdir->next != NULL)
    {
      /* Increment the reference count on this next node */

      atomic_add(&pdir->next->i_crefs, 1);
    }

  inode_unlock();

  if (prev != NULL)
    {
      inode_release(prev);
    }

  return 0;
}

/* Re-open the directory stream described by the template dirent 'tmpl'
 * (which belongs to the source file of a dup, e.g. /proc/<pid>/fd/N or
 * file_dup2()) as an independent stream owned by 'filep'.  The rebuilt
 * stream gets its own dirent allocation, its own copy of fd_path and its
 * own reference on fd_root, so closing either file never touches the
 * other.  The seek position of the new stream starts at 0, exactly like
 * a fresh open of the same path.
 *
 * This is the counterpart of the i_mops->dup() method that mountpoints
 * have:  the generic dup protocol hands the driver open file state in
 * f_priv and re-invokes ->open(), and the VFS directory stream backend
 * rebuilds its state from the template instead of sharing it.
 */

static int dir_reopen(FAR struct file *filep, FAR struct fs_dirent_s *tmpl)
{
#ifndef CONFIG_DISABLE_MOUNTPOINT
  FAR struct inode *root = tmpl->fd_root;
  struct inode_search_s desc;
#endif
  FAR struct fs_dirent_s *dir;
  FAR char *path;
  int ret;

  /* fd_path is the canonical absolute path of the directory (it carries
   * a trailing '/', see dir_allocate());  strdup it so each stream owns
   * its copy.
   */

  path = fs_heap_strdup(tmpl->fd_path);
  if (path == NULL)
    {
      return -ENOMEM;
    }

#ifndef CONFIG_DISABLE_MOUNTPOINT
  if (INODE_IS_MOUNTPT(root))
    {
      /* fd_root is the mountpoint inode, but the template carries no
       * relpath, and one cannot be derived from fd_path without a
       * search.  Resolve fd_path through the normal VFS lookup:  the
       * canonicalized buffer handed back by inode_find() yields the
       * same relpath a fresh open of the same path would produce
       * (symlinks along the way were already resolved when fd_path was
       * built).
       */

      SETUP_SEARCH(&desc, path, true);
      ret = inode_find(&desc);
      if (ret >= 0)
        {
          inode_release(desc.node);
        }

      if (ret < 0 || desc.node != root)
        {
          /* The path no longer resolves to the same mountpoint (it may
           * have been renamed, unmounted or shadowed by another mount);
           * the stream cannot be rebuilt faithfully.
           */

          RELEASE_SEARCH(&desc);
          fs_heap_free(path);
          return -ENOENT;
        }

      /* desc.node == root:  the inode_find() reference was dropped
       * above, so re-take it;  it is transferred to the rebuilt stream
       * below (dir_close() releases fd_root), exactly like the
       * inode_find() reference fs_open() holds when it calls
       * dir_allocate() for a fresh open.
       */

      inode_addref(root);

      ret = open_mountpoint(desc.node, desc.relpath, &dir, path);
      RELEASE_SEARCH(&desc);
      if (ret < 0)
        {
          inode_release(root);
          fs_heap_free(path);
          return ret;
        }
    }
  else
#endif
    {
      /* A pseudo directory:  fd_root is the pseudo tree node.  Reopen
       * the stream at its first entry, like a fresh opendir(), and
       * take the reference the stream will release on close.
       */

      inode_addref(tmpl->fd_root);
      ret = open_pseudodir(tmpl->fd_root, &dir);
      if (ret < 0)
        {
          inode_release(tmpl->fd_root);
          fs_heap_free(path);
          return ret;
        }
    }

  /* Both filep->f_inode and its reference were set up by the dup caller
   * (the source's f_inode, already addref'ed);  for mountpoint streams
   * that is the per-CPU g_dir_inode exactly like dir_allocate()
   * installs, so dir_close() symmetry is preserved.  For pseudo
   * directory streams the caller-passed inode is the source's, whose
   * u.i_ops already point at g_dir_fileops.
   */

  dir->fd_path = path;
  filep->f_priv = dir;
  filep->f_pos  = 0;
  return OK;
}

static int dir_open(FAR struct file *filep)
{
  FAR struct fs_dirent_s *dir = filep->f_priv;

  /* When the file structure was duplicated (e.g. via /proc/<pid>/fd/N,
   * file_dup2()), the dup path installs the source file's directory
   * stream in f_priv and re-invokes this open method.  A directory
   * stream cannot be shared:  its dirent owns a seek cursor and a
   * caller-owned fd_root/fd_path pair, so a second close would release
   * them twice.  Rebuild an independent stream from the template
   * instead (the mountpoint counterpart of i_mops->dup()).
   */

  if (dir != NULL && dir->fd_root != NULL && dir->fd_path != NULL)
    {
      return dir_reopen(filep, dir);
    }

  return dir_allocate(filep, dir ? dir->fd_path : "");
}

static int dir_close(FAR struct file *filep)
{
  FAR struct fs_dirent_s *dir = filep->f_priv;
  FAR struct inode *inode = dir->fd_root;
  FAR char *relpath = dir->fd_path;

  /* This is the 'root' inode of the directory. This means different
   * things with different filesystems.
   */

#ifndef CONFIG_DISABLE_MOUNTPOINT
  /* The way that we handle the close operation depends on what kind of
   * root inode we have open.
   */

  if (INODE_IS_MOUNTPT(inode))
    {
      close_mountpoint(inode, dir);
    }
  else
#endif
    {
      close_pseudodir(dir);
    }

  /* Release our references on the contained 'root' inode */

  inode_release(inode);
  fs_heap_free(relpath);
  return OK;
}

static ssize_t dir_read(FAR struct file *filep, FAR char *buffer,
                        size_t buflen)
{
  FAR struct fs_dirent_s *dir = filep->f_priv;
  FAR struct dirent *entry = (FAR struct dirent *)buffer;
#ifndef CONFIG_DISABLE_MOUNTPOINT
  FAR struct inode *inode = dir->fd_root;
#endif
  int ret;

  /* Verify that we were provided with a valid directory structure */

  if (buffer == NULL || buflen < sizeof(struct dirent))
    {
      return -EINVAL;
    }

  /* The way we handle the readdir depends on the type of inode
   * that we are dealing with.
   */

#ifndef CONFIG_DISABLE_MOUNTPOINT
  if (INODE_IS_MOUNTPT(inode))
    {
      ret = inode->u.i_mops->readdir(inode, dir, entry);
    }
  else
#endif
    {
      /* The node is part of the root pseudo file system */

      ret = read_pseudodir(dir, entry);
#ifdef CONFIG_PSEUDOFS_ATTRIBUTES
      nxclock_gettime(CLOCK_REALTIME, &dir->fd_root->i_atime);
#endif
    }

  /* ret < 0 is an error. Special case: ret = -ENOENT is end of file */

  if (ret < 0)
    {
      if (ret == -ENOENT)
        {
          ret = 0;
        }

      return ret;
    }

  entry->d_off = ++filep->f_pos;
  return sizeof(struct dirent);
}

static off_t dir_seek(FAR struct file *filep, off_t offset, int whence)
{
  off_t pos = 0;

  /* The way we handle the readdir depends on the type of inode
   * that we are dealing with.
   */

  if (whence == SEEK_SET)
    {
#ifndef CONFIG_DISABLE_MOUNTPOINT
      FAR struct fs_dirent_s *dir = filep->f_priv;

      if (INODE_IS_MOUNTPT(dir->fd_root))
        {
          pos = seek_mountptdir(filep, offset);
        }
      else
#endif
        {
          pos = seek_pseudodir(filep, offset);
        }
    }
  else if (whence == SEEK_CUR)
    {
      return filep->f_pos;
    }
  else
    {
      return -EINVAL;
    }

  if (pos >= 0)
    {
      filep->f_pos = pos;
    }

  return pos;
}

static int dir_ioctl(FAR struct file *filep, int cmd, unsigned long arg)
{
  FAR struct fs_dirent_s *dir = filep->f_priv;
  int ret = OK;

  if (cmd == FIOC_FILEPATH)
    {
      /* fd_path already carries the trailing '/' (see dir_allocate) */

      strlcpy((FAR char *)(uintptr_t)arg, dir->fd_path, PATH_MAX);
    }
  else if (cmd != BIOC_FLUSH)
    {
      ret = -ENOTTY;
    }

  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: dir_allocate
 *
 * Description:
 *   Allocate a directory instance and bind it to f_priv of filep.
 *
 ****************************************************************************/

int dir_allocate(FAR struct file *filep, FAR const char *relpath)
{
  FAR struct fs_dirent_s *dir;
  FAR struct inode *inode = filep->f_inode;
  FAR char *tempbuf;
  size_t len;
  int ret;

  tempbuf = lib_get_tempbuffer(PATH_MAX);
  if (tempbuf == NULL)
    {
      return -ENOMEM;
    }

  ret = inode_getpath(inode, tempbuf, PATH_MAX);
  if (ret < 0)
    {
      goto errout_with_tempbuf;
    }

  if (*relpath != '\0')
    {
      strlcat(tempbuf, relpath, PATH_MAX);
    }

  len = strlen(tempbuf);
  if (len == 0 || tempbuf[len - 1] != '/')
    {
      tempbuf[len++] = '/';
      tempbuf[len] = '\0';
    }

  /* Is this a node in the pseudo filesystem? Or a mountpoint? */

#ifndef CONFIG_DISABLE_MOUNTPOINT
  if (INODE_IS_MOUNTPT(inode))
    {
      /* Open the directory at the relative path */

      ret = open_mountpoint(inode, relpath, &dir, tempbuf);
      if (ret < 0)
        {
          goto errout_with_tempbuf;
        }

      filep->f_inode = &g_dir_inode;
    }
  else
#endif
    {
      ret = open_pseudodir(inode, &dir);
      if (ret < 0)
        {
          goto errout_with_tempbuf;
        }

      inode->u.i_ops = &g_dir_fileops;
    }

  dir->fd_path = fs_heap_strdup(tempbuf);
  if (dir->fd_path == NULL)
    {
      ret = -ENOMEM;
      goto errout_with_dir;
    }

  inode_addref(filep->f_inode);
  filep->f_priv = dir;
  lib_put_tempbuffer(tempbuf);
  return OK;

errout_with_dir:
#ifndef CONFIG_DISABLE_MOUNTPOINT
  if (INODE_IS_MOUNTPT(inode))
    {
      close_mountpoint(inode, dir);
    }
  else
#endif
    {
      close_pseudodir(dir);
    }

errout_with_tempbuf:
  lib_put_tempbuffer(tempbuf);
  return ret;
}
