/****************************************************************************
 * drivers/vhost/vhost-fs.c
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
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ****************************************************************************/

/* vhost-fs: serve a VirtIO-Fs device with the libc FUSE engine.
 *
 * The FUSE protocol carried over the request virtqueue is terminated by
 * the libc/fuse engine (CONFIG_LIBC_FS_FUSE) through a fuse_custom_io
 * transport bridging the engine session to the virtqueue:
 *
 *   - read()        gathers one request chain (in header plus arguments)
 *                   into the engine receive buffer,
 *   - writev()      scatters one reply (out header plus payload) into
 *                   the device-writable descriptors of the same chain
 *                   and returns the chain to the guest,
 *   - request_done() returns a chain that completes without a reply
 *                   (FUSE_FORGET) to the guest.
 *
 * The driver registers a fuse_operations table implemented on the local
 * VFS, rooted at the directory the device tag names; the engine high
 * level bridge owns request decoding, the nodeid-to-path table and reply
 * formatting.  Filesystem callbacks report errors by returning -errno,
 * the per-request context (private data) is reachable through
 * fuse_get_context().
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <debug.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/uio.h>

#include <nuttx/fs/fs.h>
#include <nuttx/fuse.h>
#include <nuttx/kmalloc.h>
#include <nuttx/kthread.h>
#include <nuttx/percpu.h>
#include <nuttx/semaphore.h>
#include <nuttx/vhost/vhost.h>
#include <nuttx/virtio/virtio-fs.h>

#include <fuse.h>

#include "vhost-fs.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define VHOST_FS_NUM_VQS      2
#define VHOST_FS_HIPRIO_VQ    0
#define VHOST_FS_REQUEST_VQ   1

/* Maximum descriptors per request chain: the FUSE protocol queues the
 * request (in header + arguments) device-readable and the reply buffer
 * device-writable, usually as two or three descriptors.
 */

#define VHOST_FS_MAX_DESCS    8

/* Entry/attribute cache timeout advertised to the guest, in seconds */

#define VHOST_FS_TIMEOUT      1.0

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* Per-request state shared between the custom_io read() and writev() */

struct vhost_fs_req_s
{
  struct virtqueue_buf vb[VHOST_FS_MAX_DESCS];
  size_t               vbcnt;
  uint16_t             head;
  struct fuse_in_header hdr;   /* Request header, saved for writev */
  bool                 busy;   /* A request was read, reply pending */
};

struct vhost_fs_priv_s
{
  FAR struct vhost_device  *hdev;
  sem_t                     semrx;
  pid_t                     pid;
  FAR struct fuse          *f;
  char                      root[PATH_MAX];
  struct vhost_fs_req_s     req;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int  vhost_fs_probe(FAR struct vhost_device *hdev);
static void vhost_fs_remove(FAR struct vhost_device *hdev);
static void vhost_fs_req_complete(FAR struct vhost_fs_priv_s *priv,
                                  uint32_t written);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static DEFINE_PER_CPU_BMP(struct vhost_driver, g_vhost_fs_driver) =
{
  VIRTIO_ID_FS,      /* device id */
  vhost_fs_probe,    /* probe */
  vhost_fs_remove,   /* remove */
};
#define g_vhost_fs_driver this_cpu_var_bmp(g_vhost_fs_driver)

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* Filesystem callbacks: the engine high level bridge calls them with full
 * paths ("/dir/file") already resolved from its nodeid-to-path table and
 * reports their -errno returns to the guest.  The exported root is
 * reachable through fuse_get_context()->private_data.
 */

static FAR struct vhost_fs_priv_s *vhost_fs_priv(void)
{
  return fuse_get_context()->private_data;
}

/* Join the exported root and an engine-issued path ("/dir/file") */

static int vhost_fs_rooted(FAR char *buf, size_t buflen,
                           FAR const char *path)
{
  FAR struct vhost_fs_priv_s *priv = vhost_fs_priv();
  size_t rlen = strlen(priv->root);
  size_t plen = strlen(path);

  if (rlen + plen + 1 > buflen)
    {
      return -ENAMETOOLONG;
    }

  memcpy(buf, priv->root, rlen);
  memcpy(buf + rlen, path, plen + 1);
  return 0;
}

static int vhost_fs_getattr(FAR const char *path, FAR struct stat *st,
                            FAR struct fuse_file_info *fi)
{
  char full[PATH_MAX];
  int ret;

  ret = vhost_fs_rooted(full, sizeof(full), path);
  if (ret < 0)
    {
      return ret;
    }

  if (nx_stat(full, st, 1) < 0)
    {
      return -get_errno();
    }

  return 0;
}

static int vhost_fs_chmod(FAR const char *path, mode_t mode,
                          FAR struct fuse_file_info *fi)
{
  char full[PATH_MAX];
  int ret;

  ret = vhost_fs_rooted(full, sizeof(full), path);
  if (ret < 0)
    {
      return ret;
    }

  if (chmod(full, mode) < 0)
    {
      return -get_errno();
    }

  return 0;
}

static int vhost_fs_chown(FAR const char *path, uid_t uid, gid_t gid,
                          FAR struct fuse_file_info *fi)
{
  char full[PATH_MAX];
  int ret;

  ret = vhost_fs_rooted(full, sizeof(full), path);
  if (ret < 0)
    {
      return ret;
    }

  if (chown(full, uid, gid) < 0)
    {
      return -get_errno();
    }

  return 0;
}

static int vhost_fs_truncate(FAR const char *path, off_t size,
                             FAR struct fuse_file_info *fi)
{
  char full[PATH_MAX];
  int ret;

  ret = vhost_fs_rooted(full, sizeof(full), path);
  if (ret < 0)
    {
      return ret;
    }

  if (fi != NULL)
    {
      FAR struct file *filep = (FAR struct file *)(uintptr_t)fi->fh;

      if (file_truncate(filep, size) < 0)
        {
          return -get_errno();
        }
    }
  else if (truncate(full, size) < 0)
    {
      return -get_errno();
    }

  return 0;
}

static int vhost_fs_utimens(FAR const char *path,
                            FAR const struct timespec tv[2],
                            FAR struct fuse_file_info *fi)
{
  char full[PATH_MAX];
  int ret;

  ret = vhost_fs_rooted(full, sizeof(full), path);
  if (ret < 0)
    {
      return ret;
    }

  if (utimens(full, tv) < 0)
    {
      return -get_errno();
    }

  return 0;
}

static int vhost_fs_mknod(FAR const char *path, mode_t mode, dev_t rdev)
{
  char full[PATH_MAX];
  int ret;

  ret = vhost_fs_rooted(full, sizeof(full), path);
  if (ret < 0)
    {
      return ret;
    }

  if (mknod(full, mode, rdev) < 0)
    {
      return -get_errno();
    }

  return 0;
}

static int vhost_fs_mkdir(FAR const char *path, mode_t mode)
{
  char full[PATH_MAX];
  int ret;

  ret = vhost_fs_rooted(full, sizeof(full), path);
  if (ret < 0)
    {
      return ret;
    }

  if (mkdir(full, mode) < 0)
    {
      return -get_errno();
    }

  return 0;
}

static int vhost_fs_unlink(FAR const char *path)
{
  char full[PATH_MAX];
  int ret;

  ret = vhost_fs_rooted(full, sizeof(full), path);
  if (ret < 0)
    {
      return ret;
    }

  if (nx_unlink(full) < 0)
    {
      return -get_errno();
    }

  return 0;
}

static int vhost_fs_rmdir(FAR const char *path)
{
  char full[PATH_MAX];
  int ret;

  ret = vhost_fs_rooted(full, sizeof(full), path);
  if (ret < 0)
    {
      return ret;
    }

  if (rmdir(full) < 0)
    {
      return -get_errno();
    }

  return 0;
}

static int vhost_fs_symlink(FAR const char *link, FAR const char *path)
{
  char full[PATH_MAX];
  int ret;

  ret = vhost_fs_rooted(full, sizeof(full), path);
  if (ret < 0)
    {
      return ret;
    }

  if (symlink(link, full) < 0)
    {
      return -get_errno();
    }

  return 0;
}

static int vhost_fs_rename(FAR const char *oldpath, FAR const char *newpath,
                           unsigned int flags)
{
  char oldfull[PATH_MAX];
  char newfull[PATH_MAX];
  int ret;

  ret = vhost_fs_rooted(oldfull, sizeof(oldfull), oldpath);
  if (ret < 0)
    {
      return ret;
    }

  ret = vhost_fs_rooted(newfull, sizeof(newfull), newpath);
  if (ret < 0)
    {
      return ret;
    }

  if (rename(oldfull, newfull) < 0)
    {
      return -get_errno();
    }

  return 0;
}

static int vhost_fs_link(FAR const char *oldpath, FAR const char *newpath)
{
  char oldfull[PATH_MAX];
  char newfull[PATH_MAX];
  int ret;

  ret = vhost_fs_rooted(oldfull, sizeof(oldfull), oldpath);
  if (ret < 0)
    {
      return ret;
    }

  ret = vhost_fs_rooted(newfull, sizeof(newfull), newpath);
  if (ret < 0)
    {
      return ret;
    }

  if (link(oldfull, newfull) < 0)
    {
      return -get_errno();
    }

  return 0;
}

static int vhost_fs_readlink(FAR const char *path, FAR char *buf,
                             size_t size)
{
  char full[PATH_MAX];
  ssize_t ret;

  ret = vhost_fs_rooted(full, sizeof(full), path);
  if (ret < 0)
    {
      return ret;
    }

  ret = readlink(full, buf, size);
  if (ret < 0)
    {
      return -get_errno();
    }

  if (ret < size)
    {
      buf[ret] = '\0';
    }

  return 0;
}

static int vhost_fs_open(FAR const char *path,
                         FAR struct fuse_file_info *fi)
{
  char full[PATH_MAX];
  FAR struct file *filep;
  int ret;

  ret = vhost_fs_rooted(full, sizeof(full), path);
  if (ret < 0)
    {
      return ret;
    }

  filep = kmm_zalloc(sizeof(*filep));
  if (filep == NULL)
    {
      return -ENOMEM;
    }

  if (file_open(filep, full, fi->flags) < 0)
    {
      ret = -get_errno();
      kmm_free(filep);
      return ret;
    }

  fi->fh = (uintptr_t)filep;
  return 0;
}

static int vhost_fs_read(FAR const char *path, FAR char *buf, size_t size,
                         off_t off, FAR struct fuse_file_info *fi)
{
  FAR struct file *filep = (FAR struct file *)(uintptr_t)fi->fh;

  return file_pread(filep, buf, size, off);
}

static int vhost_fs_write(FAR const char *path, FAR const char *buf,
                          size_t size, off_t off,
                          FAR struct fuse_file_info *fi)
{
  FAR struct file *filep = (FAR struct file *)(uintptr_t)fi->fh;

  return file_pwrite(filep, buf, size, off);
}

static int vhost_fs_flush(FAR const char *path,
                          FAR struct fuse_file_info *fi)
{
  FAR struct file *filep = (FAR struct file *)(uintptr_t)fi->fh;

  if (file_fsync(filep) < 0)
    {
      return -get_errno();
    }

  return 0;
}

static int vhost_fs_release(FAR const char *path,
                            FAR struct fuse_file_info *fi)
{
  FAR struct file *filep = (FAR struct file *)(uintptr_t)fi->fh;

  file_close(filep);
  kmm_free(filep);
  fi->fh = 0;
  return 0;
}

static int vhost_fs_fsync(FAR const char *path, int datasync,
                          FAR struct fuse_file_info *fi)
{
  FAR struct file *filep = (FAR struct file *)(uintptr_t)fi->fh;

  if (file_fsync(filep) < 0)
    {
      return -get_errno();
    }

  return 0;
}

static int vhost_fs_opendir(FAR const char *path,
                            FAR struct fuse_file_info *fi)
{
  char full[PATH_MAX];
  FAR DIR *dir;
  int ret;

  ret = vhost_fs_rooted(full, sizeof(full), path);
  if (ret < 0)
    {
      return ret;
    }

  dir = opendir(full);
  if (dir == NULL)
    {
      return -get_errno();
    }

  fi->fh = (uintptr_t)dir;
  return 0;
}

/* Cookie-based readdir resume: on offset != 0 seek there and hand the
 * filler the telldir() position of the next entry; the engine collects
 * entries from that point on (see the fuse_operations readdir contract).
 */

static int vhost_fs_readdir(FAR const char *path, FAR void *buf,
                            fuse_fill_dir_t filler, off_t off,
                            FAR struct fuse_file_info *fi,
                            enum fuse_readdir_flags flags)
{
  FAR DIR *dir = (FAR DIR *)(uintptr_t)fi->fh;
  FAR struct dirent *entry;
  off_t next;

  UNUSED(flags);

  if (off != 0)
    {
      seekdir(dir, off);
    }

  for (; ; )
    {
      entry = readdir(dir);
      if (entry == NULL)
        {
          return 0;
        }

      next = telldir(dir);
      if (filler(buf, entry->d_name, NULL, next, 0))
        {
          return 0;
        }
    }
}

static int vhost_fs_releasedir(FAR const char *path,
                               FAR struct fuse_file_info *fi)
{
  FAR DIR *dir = (FAR DIR *)(uintptr_t)fi->fh;

  closedir(dir);
  fi->fh = 0;
  return 0;
}

static int vhost_fs_statfs(FAR const char *path, FAR struct statvfs *st)
{
  FAR struct vhost_fs_priv_s *priv = vhost_fs_priv();
  struct statfs sf;

  if (statfs(priv->root, &sf) < 0)
    {
      return -get_errno();
    }

  memset(st, 0, sizeof(*st));
  st->f_bsize   = sf.f_bsize;
  st->f_frsize  = sf.f_bsize;
  st->f_blocks  = sf.f_blocks;
  st->f_bfree   = sf.f_bfree;
  st->f_bavail  = sf.f_bavail;
  st->f_files   = sf.f_files;
  st->f_ffree   = sf.f_ffree;
  st->f_namemax = sf.f_namelen;
  return 0;
}

static int vhost_fs_create(FAR const char *path, mode_t mode,
                           FAR struct fuse_file_info *fi)
{
  char full[PATH_MAX];
  FAR struct file *filep;
  int ret;

  ret = vhost_fs_rooted(full, sizeof(full), path);
  if (ret < 0)
    {
      return ret;
    }

  filep = kmm_zalloc(sizeof(*filep));
  if (filep == NULL)
    {
      return -ENOMEM;
    }

  if (file_open(filep, full, fi->flags | O_CREAT, mode) < 0)
    {
      ret = -get_errno();
      kmm_free(filep);
      return ret;
    }

  fi->fh = (uintptr_t)filep;
  return 0;
}

static int vhost_fs_access(FAR const char *path, int mask)
{
  char full[PATH_MAX];
  int ret;

  ret = vhost_fs_rooted(full, sizeof(full), path);
  if (ret < 0)
    {
      return ret;
    }

  if (access(full, mask) < 0)
    {
      return -get_errno();
    }

  return 0;
}

static off_t vhost_fs_lseek(FAR const char *path, off_t off, int whence,
                            FAR struct fuse_file_info *fi)
{
  FAR struct file *filep = (FAR struct file *)(uintptr_t)fi->fh;

  return file_seek(filep, off, whence);
}

static const struct fuse_operations g_vhost_fs_ops =
{
  .getattr    = vhost_fs_getattr,
  .chmod      = vhost_fs_chmod,
  .chown      = vhost_fs_chown,
  .truncate   = vhost_fs_truncate,
  .utimens    = vhost_fs_utimens,
  .readlink   = vhost_fs_readlink,
  .mknod      = vhost_fs_mknod,
  .mkdir      = vhost_fs_mkdir,
  .unlink     = vhost_fs_unlink,
  .rmdir      = vhost_fs_rmdir,
  .symlink    = vhost_fs_symlink,
  .rename     = vhost_fs_rename,
  .link       = vhost_fs_link,
  .open       = vhost_fs_open,
  .read       = vhost_fs_read,
  .write      = vhost_fs_write,
  .flush      = vhost_fs_flush,
  .release    = vhost_fs_release,
  .fsync      = vhost_fs_fsync,
  .opendir    = vhost_fs_opendir,
  .readdir    = vhost_fs_readdir,
  .releasedir = vhost_fs_releasedir,
  .statfs     = vhost_fs_statfs,
  .create     = vhost_fs_create,
  .access     = vhost_fs_access,
  .lseek      = vhost_fs_lseek,
};

/****************************************************************************
 * Name: vhost_fs_req_complete
 *
 * Description:
 *   Return the current request chain to the guest.  Every chain carries
 *   the submitter's completion cookie, so it must be returned exactly
 *   once even for requests that get no reply (FUSE_FORGET):  the guest
 *   waits for the used-ring entry before it reclaims the buffers.
 *
 ****************************************************************************/

static void vhost_fs_req_complete(FAR struct vhost_fs_priv_s *priv,
                                  uint32_t written)
{
  FAR struct virtqueue *vq =
      priv->hdev->vrings_info[VHOST_FS_REQUEST_VQ].vq;

  virtqueue_add_consumed_buffer(vq, priv->req.head, written);
  virtqueue_kick(vq);
  priv->req.busy = false;
}

/****************************************************************************
 * Name: vhost_fs_io_read
 *
 * Description:
 *   fuse_custom_io read: gather one request chain (in header plus
 *   arguments, queued device-readable at the head of the chain) into
 *   the engine receive buffer.  Parks on the virtqueue callback
 *   semaphore when no request is pending, mirroring a blocking
 *   channel read.
 *
 ****************************************************************************/

static ssize_t vhost_fs_io_read(int fd, FAR void *buf, size_t buf_len,
                                FAR void *userdata)
{
  FAR struct vhost_fs_priv_s *priv = userdata;
  FAR struct virtqueue_buf *vb = priv->req.vb;
  FAR struct fuse_in_header *hdr = buf;
  size_t want;
  size_t copied;
  size_t i;
  int head;

  if (priv->req.busy)
    {
      /* Protocol violation: reply before the next request */

      set_errno(EIO);
      return -1;
    }

  /* Park until the virtqueue callback reports activity */

  nxsem_wait_uninterruptible(&priv->semrx);

  head = vhost_get_vq_buffers(priv->hdev->vrings_info[fd].vq, vb,
                              VHOST_FS_MAX_DESCS, &priv->req.vbcnt);
  if (head < 0)
    {
      /* Spurious wakeup: nothing queued.  A zero return ends the engine
       * loop, so report EAGAIN and let it retry instead.
       */

      set_errno(EAGAIN);
      return -1;
    }

  priv->req.head = (uint16_t)head;
  priv->req.busy = true;

  /* Copy the request: the device-readable prefix of the chain holds the
   * in header followed by the request arguments.
   */

  copied = 0;
  for (i = 0; i < priv->req.vbcnt && copied < sizeof(*hdr); i++)
    {
      size_t chunk = vb[i].len < sizeof(*hdr) - copied ?
                     vb[i].len : sizeof(*hdr) - copied;

      memcpy((FAR char *)buf + copied, vb[i].buf, chunk);
      copied += chunk;
    }

  if (copied < sizeof(*hdr))
    {
      priv->req.busy = false;
      set_errno(EIO);
      return -1;
    }

  priv->req.hdr = *hdr;

  /* hdr->len covers the whole request including the header itself */

  want = hdr->len;
  if (want > buf_len)
    {
      /* Engine buffer too small:  drop the request with an EIO reply so
       * the guest does not hang on it.
       */

      hdr->opcode = FUSE_DESTROY;
      hdr->len    = sizeof(*hdr);
      return sizeof(*hdr);
    }

  for (; i < priv->req.vbcnt && copied < want; i++)
    {
      size_t chunk = vb[i].len < want - copied ? vb[i].len : want - copied;

      memcpy((FAR char *)buf + copied, vb[i].buf, chunk);
      copied += chunk;
    }

  if (copied < want)
    {
      priv->req.busy = false;
      set_errno(EIO);
      return -1;
    }

  return want;
}

/****************************************************************************
 * Name: vhost_fs_io_request_done
 *
 * Description:
 *   fuse_custom_io request_done: called when a request completes without
 *   a reply (FUSE_FORGET).  The chain must still be returned to the guest,
 *   which waits for the used-ring entry before it reclaims the buffers.
 *
 ****************************************************************************/

static void vhost_fs_io_request_done(int fd, uint64_t unique,
                                     FAR void *userdata)
{
  vhost_fs_req_complete(userdata, 0);
}

/****************************************************************************
 * Name: vhost_fs_io_writev
 *
 * Description:
 *   fuse_custom_io writev: scatter one reply (out header plus payload,
 *   one iovec each) into the device-writable descriptors of the request
 *   chain, then return the whole chain to the guest.  The guest queues
 *   the reply region right after the request descriptors, so the
 *   writable region is found by skipping the request prefix announced
 *   by the in header length.
 *
 ****************************************************************************/

static ssize_t vhost_fs_io_writev(int fd, FAR struct iovec *iov,
                                  int count, FAR void *userdata)
{
  FAR struct vhost_fs_priv_s *priv = userdata;
  FAR struct virtqueue_buf *vb = priv->req.vb;
  size_t req_len;
  size_t woff;
  size_t total;
  size_t i;
  int j;

  if (!priv->req.busy || count < 1)
    {
      set_errno(EIO);
      return -1;
    }

  /* Skip the request prefix (in header plus arguments) to reach the
   * device-writable reply region of the chain.
   */

  req_len = priv->req.hdr.len;

  for (i = 0; i < priv->req.vbcnt; i++)
    {
      if (req_len >= vb[i].len)
        {
          req_len -= vb[i].len;
        }
      else
        {
          break;
        }
    }

  if (i >= priv->req.vbcnt)
    {
      /* No writable descriptor in the chain */

      vhost_fs_req_complete(priv, 0);
      set_errno(EIO);
      return -1;
    }

  /* Scatter the reply: iov[0] is the out header, iov[1..] the payload.
   * Writable descriptor i may start mid-way inside the request prefix.
   */

  woff  = req_len;
  total = 0;
  for (j = 0; j < count; j++)
    {
      total += iov[j].iov_len;
    }

  for (j = 0; j < count && i < priv->req.vbcnt; j++)
    {
      FAR const char *src = iov[j].iov_base;
      size_t left = iov[j].iov_len;

      while (left > 0 && i < priv->req.vbcnt)
        {
          size_t room = vb[i].len - woff;
          size_t chunk;

          if (room == 0)
            {
              i++;
              woff = 0;
              continue;
            }

          chunk = left < room ? left : room;
          memcpy((FAR char *)vb[i].buf + woff, src, chunk);
          woff += chunk;
          src  += chunk;
          left -= chunk;
        }
    }

  vhost_fs_req_complete(priv, total);
  return total;
}

/****************************************************************************
 * Name: g_vhost_fs_io
 ****************************************************************************/

static const struct fuse_custom_io g_vhost_fs_io =
{
  .read         = vhost_fs_io_read,
  .writev       = vhost_fs_io_writev,
  .request_done = vhost_fs_io_request_done,
};

/****************************************************************************
 * Name: vhost_fs_thread
 ****************************************************************************/

static int vhost_fs_thread(int argc, FAR char *argv[])
{
  FAR struct vhost_fs_priv_s *priv =
    (FAR struct vhost_fs_priv_s *)((uintptr_t)strtoul(argv[1], NULL, 16));

  fuse_loop(priv->f);
  return 0;
}

/****************************************************************************
 * Name: vhost_fs_callback
 ****************************************************************************/

static void vhost_fs_callback(FAR struct virtqueue *vq)
{
  FAR struct vhost_fs_priv_s *priv = vq->vq_dev->priv;
  int semcount;

  nxsem_get_value(&priv->semrx, &semcount);
  while (semcount++ < 1)
    {
      nxsem_post(&priv->semrx);
    }
}

/****************************************************************************
 * Name: vhost_fs_probe
 ****************************************************************************/

static int vhost_fs_probe(FAR struct vhost_device *hdev)
{
  FAR struct vhost_fs_priv_s *priv;
  FAR const char *vqnames[VHOST_FS_NUM_VQS];
  vq_callback callbacks[VHOST_FS_NUM_VQS];
  struct virtio_fs_config_s config;
  struct fuse_args args = FUSE_ARGS_INIT(0, NULL);
  FAR char *argv[2];
  char arg0[32];
  int ret;

  priv = kmm_zalloc(sizeof(*priv));
  if (priv == NULL)
    {
      return -ENOMEM;
    }

  priv->hdev = hdev;
  hdev->priv = priv;
  nxsem_init(&priv->semrx, 0, 0);

  /* Read config: the tag names the exported root directory */

  vhost_read_config_member(hdev, struct virtio_fs_config_s, tag,
                           &config.tag);
  config.tag[sizeof(config.tag) - 1] = '\0';
  strlcpy(priv->root, config.tag, sizeof(priv->root));

  /* Write the config back so the client can read it.  */

  config.num_request_queues = 1;
  vhost_write_config(hdev, 0, &config, sizeof(config));

  /* Create the engine session.  fd doubles as the request virtqueue
   * index used by the custom_io callbacks.
   */

  /* Create the engine filesystem object.  The private data reaches the
   * fuse_operations callbacks through fuse_get_context(); fd doubles as
   * the request virtqueue index used by the custom_io callbacks.
   */

  priv->f = fuse_new(&args, &g_vhost_fs_ops, sizeof(g_vhost_fs_ops), priv);
  if (priv->f == NULL)
    {
      ret = -ENOMEM;
      goto err;
    }

  ret = fuse_mount_custom(priv->f, &g_vhost_fs_io, VHOST_FS_REQUEST_VQ);
  if (ret < 0)
    {
      goto err_f;
    }

  /* Create virtqueues */

  vqnames[VHOST_FS_HIPRIO_VQ]    = "virtiofs-hiprio";
  vqnames[VHOST_FS_REQUEST_VQ]   = "virtiofs-requests";
  callbacks[VHOST_FS_HIPRIO_VQ]  = vhost_fs_callback;
  callbacks[VHOST_FS_REQUEST_VQ] = vhost_fs_callback;

  ret = vhost_create_virtqueues(hdev, 0, VHOST_FS_NUM_VQS,
                                vqnames, callbacks, NULL);
  if (ret < 0)
    {
      vhosterr("vhost_create_virtqueues failed, ret=%d\n", ret);
      goto err_f;
    }

  snprintf(arg0, sizeof(arg0), "%p", priv);
  argv[0] = arg0;
  argv[1] = NULL;

  priv->pid = kthread_create("vhost-fs",
                             CONFIG_DRIVERS_VHOST_FS_PRIORITY,
                             CONFIG_DRIVERS_VHOST_FS_STACKSIZE,
                             vhost_fs_thread, argv);
  if (priv->pid < 0)
    {
      vhosterr("vhost fs thread create failed %d\n", priv->pid);
      ret = priv->pid;
      goto err_vq;
    }

  virtqueue_enable_cb(hdev->vrings_info[VHOST_FS_HIPRIO_VQ].vq);
  virtqueue_enable_cb(hdev->vrings_info[VHOST_FS_REQUEST_VQ].vq);
  return OK;

err_vq:
  vhost_delete_virtqueues(hdev);
err_f:
  fuse_destroy(priv->f);
err:
  nxsem_destroy(&priv->semrx);
  kmm_free(priv);
  return ret;
}

/****************************************************************************
 * Name: vhost_fs_remove
 ****************************************************************************/

static void vhost_fs_remove(FAR struct vhost_device *hdev)
{
  FAR struct vhost_fs_priv_s *priv = hdev->priv;

  virtqueue_disable_cb(hdev->vrings_info[VHOST_FS_HIPRIO_VQ].vq);
  virtqueue_disable_cb(hdev->vrings_info[VHOST_FS_REQUEST_VQ].vq);

  if (priv->pid > 0)
    {
      kthread_delete(priv->pid);
    }

  fuse_destroy(priv->f);
  vhost_delete_virtqueues(hdev);
  nxsem_destroy(&priv->semrx);
  kmm_free(priv);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: vhost_register_fs_driver
 ****************************************************************************/

int vhost_register_fs_driver(void)
{
  return vhost_register_driver(&g_vhost_fs_driver);
}
