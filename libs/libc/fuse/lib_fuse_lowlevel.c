/****************************************************************************
 * libs/libc/fuse/lib_fuse_lowlevel.c
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/mount.h>
#include <unistd.h>
#include <fuse_lowlevel.h>

#include <nuttx/atomic.h>
#include <nuttx/lib/lib.h>

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef CONFIG_FS_CUSE
int cuse_lowlevel_init(fuse_req_t req,
                       FAR struct fuse_conn_info *conn,
                       FAR const void *body, size_t bodylen);
#endif

/****************************************************************************
 * Private Type Definitions
 ****************************************************************************/

struct fuse_pollhandle
{
  uint64_t kh;
  FAR struct fuse_session *se;
};

/* In-flight request context */

struct fuse_req
{
  FAR struct fuse_session *se;
  uint64_t unique;
  struct fuse_ctx ctx;
};

/* Session: one connection to the kernel (or a custom channel) */

struct fuse_session
{
  char mountpoint[PATH_MAX];       /* Userspace mountpoint */
  int fd;                          /* fd channel fd, or transport-
                                    * private custom_io identifier */
  FAR struct fuse_custom_io *io;   /* Transport, NULL = fd channel */
  struct fuse_lowlevel_ops op;
  int got_init;
  FAR struct cuse_data *cuse_data; /* CUSE session state */
  FAR void *userdata;
  struct fuse_conn_info conn;      /* INIT negotiation result */
  int got_destroy;
  atomic_t exited;                 /* Atomic exit flag (SMP safe) */
  int eventfd;                     /* loop_mt wakeup, -1 = unused */
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void convert_stat(FAR const struct stat *stbuf,
                         FAR struct fuse_attr *attr)
{
  memset(attr, 0, sizeof(*attr));
  attr->ino       = stbuf->st_ino;
  attr->mode      = stbuf->st_mode;
  attr->nlink     = stbuf->st_nlink;
  attr->uid       = stbuf->st_uid;
  attr->gid       = stbuf->st_gid;
  attr->rdev      = stbuf->st_rdev;
  attr->size      = stbuf->st_size;
  attr->blksize   = stbuf->st_blksize;
  attr->blocks    = stbuf->st_blocks;
  attr->atime     = stbuf->st_atim.tv_sec;
  attr->mtime     = stbuf->st_mtim.tv_sec;
  attr->ctime     = stbuf->st_ctim.tv_sec;
  attr->atimensec = stbuf->st_atim.tv_nsec;
  attr->mtimensec = stbuf->st_mtim.tv_nsec;
  attr->ctimensec = stbuf->st_ctim.tv_nsec;
}

static inline void
convert_attr(FAR const struct fuse_setattr_in *attr,
             FAR struct stat *stbuf)
{
  memset(stbuf, 0, sizeof(*stbuf));
  stbuf->st_mode         = attr->mode;
  stbuf->st_uid          = attr->uid;
  stbuf->st_gid          = attr->gid;
  stbuf->st_size         = attr->size;
  stbuf->st_atim.tv_sec  = attr->atime;
  stbuf->st_mtim.tv_sec  = attr->mtime;
  stbuf->st_ctim.tv_sec  = attr->ctime;
  stbuf->st_atim.tv_nsec = attr->atimensec;
  stbuf->st_mtim.tv_nsec = attr->mtimensec;
  stbuf->st_ctim.tv_nsec = attr->ctimensec;
}

static inline void
convert_statfs(FAR const struct statvfs *stbuf,
               FAR struct fuse_kstatfs *kstatfs)
{
  memset(kstatfs, 0, sizeof(*kstatfs));
  kstatfs->bsize   = stbuf->f_bsize;
  kstatfs->frsize  = stbuf->f_frsize;
  kstatfs->blocks  = stbuf->f_blocks;
  kstatfs->bfree   = stbuf->f_bfree;
  kstatfs->bavail  = stbuf->f_bavail;
  kstatfs->files   = stbuf->f_files;
  kstatfs->ffree   = stbuf->f_ffree;
  kstatfs->namelen = stbuf->f_namemax;
}

static inline unsigned long calc_timeout_sec(double t)
{
  if (t > (double)ULONG_MAX)
    {
      return ULONG_MAX;
    }
  else if (t < 0.0)
    {
      return 0;
    }
  else
    {
      return (unsigned long)t;
    }
}

static inline unsigned int calc_timeout_nsec(double t)
{
  double f = t - (double)calc_timeout_sec(t);

  if (f < 0.0)
    {
      return 0;
    }
  else if (f >= 0.999999999)
    {
      return 999999999;
    }
  else
    {
      return (unsigned int)(f * 1.0e9);
    }
}

static void fill_entry(FAR struct fuse_entry_out *arg,
                       FAR const struct fuse_entry_param *e)
{
  arg->nodeid           = e->ino;
  arg->generation       = e->generation;
  arg->entry_valid      = calc_timeout_sec(e->entry_timeout);
  arg->entry_valid_nsec = calc_timeout_nsec(e->entry_timeout);
  arg->attr_valid       = calc_timeout_sec(e->attr_timeout);
  arg->attr_valid_nsec  = calc_timeout_nsec(e->attr_timeout);
  convert_stat(&e->attr, &arg->attr);
}

static void fill_open(FAR struct fuse_open_out *arg,
                      FAR const struct fuse_file_info *fi)
{
  memset(arg, 0, sizeof(*arg));
  arg->fh = fi->fh;

  if (fi->backing_id > 0)
    {
      arg->backing_id = fi->backing_id;
      arg->open_flags |= FOPEN_PASSTHROUGH;
    }

  if (fi->direct_io)
    {
      arg->open_flags |= FOPEN_DIRECT_IO;
    }

  if (fi->keep_cache)
    {
      arg->open_flags |= FOPEN_KEEP_CACHE;
    }

  if (fi->cache_readdir)
    {
      arg->open_flags |= FOPEN_CACHE_DIR;
    }

  if (fi->nonseekable)
    {
      arg->open_flags |= FOPEN_NONSEEKABLE;
    }

  if (fi->noflush)
    {
      arg->open_flags |= FOPEN_NOFLUSH;
    }

  if (fi->parallel_direct_writes)
    {
      arg->open_flags |= FOPEN_PARALLEL_DIRECT_WRITES;
    }
}

static inline size_t
iov_length(FAR const struct iovec *iov, int count)
{
  size_t ret = 0;
  int i;

  for (i = 0; i < count; i++)
    {
      ret += iov[i].iov_len;
    }

  return ret;
}

static inline size_t buf_size(FAR const struct fuse_session *se)
{
  return 256 + se->conn.max_write; /* +256 for header */
}

static int fuse_send_msg(FAR struct fuse_session *se,
                         FAR struct iovec *iov, int count)
{
  FAR struct fuse_out_header *out = iov[0].iov_base;
  ssize_t ret;

  out->len = iov_length(iov, count);

  if (se->io != NULL)
    {
      ret = se->io->writev(se->fd, iov, count, se->userdata);
    }
  else
    {
      ret = writev(se->fd, iov, count);
    }

  return ret < 0 ? -get_errno() : 0;
}

static int fuse_send_reply_iov(fuse_req_t req, int error,
                               FAR struct iovec *iov, int count)
{
  struct fuse_out_header out;

  out.unique = req->unique;
  out.error  = error;

  iov[0].iov_base = &out;
  iov[0].iov_len  = sizeof(out);

  return fuse_send_msg(req->se, iov, count);
}

static int fuse_send_reply(fuse_req_t req, int error,
                           FAR const void *arg, size_t argsize)
{
  struct iovec iov[2];
  int count = 1;

  if (argsize)
    {
      iov[1].iov_base = (FAR void *)arg;
      iov[1].iov_len = argsize;
      count++;
    }

  return fuse_send_reply_iov(req, error, iov, count);
}

static inline int
fuse_reply_ok(fuse_req_t req, FAR const void *arg, size_t argsize)
{
  return fuse_send_reply(req, 0, arg, argsize);
}

static int fuse_send_notify_iov(FAR struct fuse_session *se, int notify_code,
                                FAR struct iovec *iov, int count)
{
  struct fuse_out_header out;

  if (!se->got_init)
    {
      return -ENOTCONN;
    }

  out.unique = 0;
  out.error = notify_code;

  iov[0].iov_base = &out;
  iov[0].iov_len = sizeof(out);

  return fuse_send_msg(se, iov, count);
}

static int fuse_send_notify(FAR struct fuse_session *se, int notify_code,
                            FAR const void *arg, size_t argsize)
{
  struct iovec iov[2];
  int count = 1;

  if (argsize)
    {
      iov[1].iov_base = (FAR void *)arg;
      iov[1].iov_len = argsize;
      count++;
    }

  return fuse_send_notify_iov(se, notify_code, iov, count);
}

static int fuse_notify_entry(FAR struct fuse_session *se,
                             fuse_ino_t parent,
                             FAR const char *name, size_t namelen,
                             bool expire)
{
  struct fuse_notify_inval_entry_out arg;
  struct iovec iov[3];

  arg.parent  = parent;
  arg.namelen = namelen;
  arg.flag    = expire ? FUSE_EXPIRE_ONLY : 0;

  iov[1].iov_base = &arg;
  iov[1].iov_len = sizeof(arg);
  iov[2].iov_base = (FAR void *)name;
  iov[2].iov_len = namelen + 1;

  return fuse_send_notify_iov(se, FUSE_NOTIFY_INVAL_ENTRY, iov, 3);
}

static inline int
fuse_lowlevel_init(fuse_req_t req,
                   FAR struct fuse_conn_info *conn,
                   FAR const void *body, size_t bodylen)
{
  FAR const struct fuse_init_in *arg = body;
  FAR struct fuse_session *se = req->se;
  struct fuse_init_out out;

  conn->proto_major   = arg->major;
  conn->proto_minor   = arg->minor;
  conn->max_readahead = arg->max_readahead;
  conn->time_gran     = 1;
  conn->want          = 0;
  conn->want_ext      = 0;
  conn->capable       = arg->flags | FUSE_CAP_IOCTL_DIR;
  conn->capable_ext   = conn->capable;
  if (arg->flags & FUSE_INIT_EXT)
    {
      conn->capable_ext |= (uint64_t)arg->flags2 << 32;
    }

  /* Default settings for modern filesystems.
   *
   * Most of these capabilities were disabled by default in
   * libfuse2 for backwards compatibility reasons. In libfuse3,
   * we can finally enable them by default (as long as they're
   * supported by the kernel).
   */

#define FUSE_SET_DEFAULT(cond, cap) \
  if (cond)                       \
      fuse_set_feature_flag(conn, cap)

  FUSE_SET_DEFAULT(1, FUSE_CAP_ASYNC_READ);
  FUSE_SET_DEFAULT(1, FUSE_CAP_AUTO_INVAL_DATA);
  FUSE_SET_DEFAULT(1, FUSE_CAP_ASYNC_DIO);
  FUSE_SET_DEFAULT(1, FUSE_CAP_IOCTL_DIR);
  FUSE_SET_DEFAULT(1, FUSE_CAP_ATOMIC_O_TRUNC);
  FUSE_SET_DEFAULT(se->op.write_buf, FUSE_CAP_SPLICE_READ);
  FUSE_SET_DEFAULT(se->op.getlk && se->op.setlk, FUSE_CAP_POSIX_LOCKS);
  FUSE_SET_DEFAULT(se->op.flock, FUSE_CAP_FLOCK_LOCKS);
  FUSE_SET_DEFAULT(se->op.readdirplus, FUSE_CAP_READDIRPLUS);
  FUSE_SET_DEFAULT(se->op.readdirplus && se->op.readdir,
                   FUSE_CAP_READDIRPLUS_AUTO);
  FUSE_SET_DEFAULT(1, FUSE_CAP_OVER_IO_URING);
  FUSE_SET_DEFAULT(1, FUSE_CAP_SETXATTR_EXT);
  FUSE_SET_DEFAULT(1, FUSE_MAX_PAGES);
  FUSE_SET_DEFAULT(1, FUSE_REQUEST_TIMEOUT);
  FUSE_SET_DEFAULT(1, FUSE_INIT_EXT);

#undef FUSE_SET_DEFAULT

  if (se->op.init != NULL)
    {
      se->op.init(se->userdata, conn);

      /* conn->want is 32-bit value and deprecated in favour of
       * conn->want_ext Userspace might still use conn->want -
       * we need to convert it
       */

      conn->want_ext &= 0xffffffff00000000;
      conn->want_ext |= conn->want;
    }

  if (conn->want_ext & ~conn->capable_ext)
    {
      fuse_reply_err(req, EPROTO);
      fuse_session_exit(se);
      return -EPROTO;
    }

  if (!conn->congestion_threshold)
    {
      conn->congestion_threshold = conn->max_background * 3 / 4;
    }

  memset(&out, 0, sizeof(out));
  out.major                = FUSE_KERNEL_VERSION;
  out.minor                = FUSE_KERNEL_MINOR_VERSION;
  out.max_readahead        = conn->max_readahead;
  out.flags                = conn->want_ext | FUSE_BIG_WRITES;
  out.max_background       = conn->max_background;
  out.congestion_threshold = conn->congestion_threshold;
  out.max_write            = conn->max_write;
  out.time_gran            = conn->time_gran;
  out.max_pages            = (conn->max_write - 1) / getpagesize() + 1;
  out.flags2               = conn->want_ext >> 32;
  out.max_stack_depth      = conn->max_backing_stack_depth + 1;
  out.request_timeout      = conn->request_timeout;

  return fuse_reply_ok(req, &out, sizeof(out));
}

static inline void
fuse_dispatch(FAR struct fuse_session *se,
              FAR struct fuse_in_header *in,
              FAR const void *body, size_t bodylen)
{
  FAR const struct fuse_lowlevel_ops *op = &se->op;
  struct fuse_req reqobj;
  fuse_req_t req = &reqobj;
  fuse_ino_t nodeid = in->nodeid;

  req->se        = se;
  req->unique    = in->unique;
  req->ctx.uid   = in->uid;
  req->ctx.gid   = in->gid;
  req->ctx.pid   = in->pid;
  req->ctx.umask = 0;

  switch (in->opcode)
    {
      case FUSE_LOOKUP:
        if (op->loopup)
          {
            op->lookup(req, nodeid, body);
          }
        else
          {
            fuse_reply_err(req, ENOSYS);
          }
        break;

      case FUSE_FORGET:
        if (op->forget != NULL)
          {
            FAR const struct fuse_forget_in *arg = body;

            op->forget(req, nodeid, arg->nlookup);
          }
        else
          {
            fuse_reply_none(req);
          }
        break;

      case FUSE_GETATTR:
        if (op->getattr != NULL)
          {
            FAR const struct fuse_getattr_in *arg = body;

            if (arg->getattr_flags & FUSE_GETATTR_FH)
              {
                struct fuse_file_info fi;

                memset(&fi, 0, sizeof(fi));
                fi.fh = arg->fh;

                op->getattr(req, nodeid, &fi);
              }
            else
              {
                op->getattr(req, nodeid, NULL);
              }
          }
        else
          {
            fuse_reply_err(req, ENOSYS);
          }
        break;

      case FUSE_SETATTR:
        if (op->setattr != NULL)
          {
            FAR const struct fuse_setattr_in *arg = body;
            struct stat stbuf;

            convert_attr(arg, &stbuf);

            if (arg->valid & FATTR_FH)
              {
                struct fuse_file_info fi;

                memset(&fi, 0, sizeof(fi));
                fi.fh = arg->fh;

                op->setattr(req, nodeid, &stbuf, arg->valid, &fi);
              }
            else
              {
                op->setattr(req, nodeid, &stbuf, arg->valid, NULL);
              }
          }
        else
          {
            fuse_reply_err(req, ENOSYS);
          }
        break;

      case FUSE_READLINK:
        if (op->readlink != NULL)
          {
            op->readlink(req, nodeid);
          }
        else
          {
            fuse_reply_err(req, ENOSYS);
          }
        break;

      case FUSE_SYMLINK:
        if (op->symlink != NULL)
          {
            FAR const char *name = body;
            FAR const char *link = name + strlen(name) + 1;

            op->symlink(req, link, nodeid, name);
          }
        else
          {
            fuse_reply_err(req, ENOSYS);
          }
        break;

      case FUSE_MKNOD:
        if (op->mknod != NULL)
          {
            FAR const struct fuse_mknod_in *arg = body;
            FAR const char *name = (FAR const char *)(arg + 1);

            req->ctx.umask = arg->umask;
            op->mknod(req, nodeid, name, arg->mode, arg->rdev);
          }
        else
          {
            fuse_reply_err(req, ENOSYS);
          }
        break;

      case FUSE_MKDIR:
        if (op->mkdir != NULL)
          {
            FAR const struct fuse_mkdir_in *arg = body;
            FAR const char *name = (FAR const char *)(arg + 1);

            req->ctx.umask = arg->umask;
            op->mkdir(req, nodeid, name, arg->mode);
          }
        else
          {
            fuse_reply_err(req, ENOSYS);
          }
        break;

      case FUSE_UNLINK:
        if (op->unlink != NULL)
          {
            op->unlink(req, nodeid, body);
          }
        else
          {
            fuse_reply_err(req, ENOSYS);
          }
        break;

      case FUSE_RMDIR:
        if (op->rmdir != NULL)
          {
            op->rmdir(req, nodeid, body);
          }
        else
          {
            fuse_reply_err(req, ENOSYS);
          }
        break;

      case FUSE_RENAME:
        if (op->rename != NULL)
          {
            FAR const struct fuse_rename_in *arg = body;
            FAR const char *oldname = (FAR const char *)(arg + 1);
            FAR const char *newname = oldname + strlen(oldname) + 1;

            op->rename(req, nodeid, oldname, arg->newdir, newname, 0);
          }
        else
          {
            fuse_reply_err(req, ENOSYS);
          }
        break;

      case FUSE_LINK:
        if (op->link != NULL)
          {
            FAR const struct fuse_link_in *arg = body;
            FAR const char *name = (FAR const char *)(arg + 1);

            op->link(req, arg->oldnodeid, nodeid, name);
          }
        else
          {
            fuse_reply_err(req, ENOSYS);
          }
        break;

      case FUSE_OPEN:
        {
          FAR const struct fuse_open_in *arg = body;
          struct fuse_file_info fi;

          memset(&fi, 0, sizeof(fi));
          fi.flags = arg->flags;

          if (op->open != NULL)
            {
              op->open(req, nodeid, &fi);
            }
          else if (se->conn.want_ext & FUSE_CAP_NO_OPEN_SUPPORT)
            {
              fuse_reply_err(req, ENOSYS);
            }
          else
            {
              fuse_reply_open(req, &fi);
            }
        }
        break;

      case FUSE_READ:
        if (op->read != NULL)
          {
            FAR const struct fuse_read_in *arg = body;
            struct fuse_file_info fi;

            memset(&fi, 0, sizeof(fi));
            fi.fh         = arg->fh;
            fi.lock_owner = arg->lock_owner;
            fi.flags      = arg->flags;

            op->read(req, nodeid, arg->size, arg->offset, &fi);
          }
        else
          {
            fuse_reply_err(req, ENOSYS);
          }
        break;

      case FUSE_WRITE:
        {
          FAR const struct fuse_write_in *arg = body;
          FAR char *buf = (FAR char *)(arg + 1);
          struct fuse_file_info fi;

          memset(&fi, 0, sizeof(fi));
          fi.fh         = arg->fh;
          fi.lock_owner = arg->lock_owner;
          fi.flags      = arg->flags;
          fi.writepage  = !!(arg->write_flags & FUSE_WRITE_CACHE);

          if (op->write_buf != NULL)
            {
              struct fuse_bufvec bufv = FUSE_BUFVEC_INIT(arg->size);

              bufv.buf[0].mem = buf;
              op->write_buf(req, nodeid, &bufv, arg->offset, &fi);
            }
          else if (op->write != NULL)
            {
              op->write(req, nodeid, buf, arg->size, arg->offset, &fi);
            }
          else
            {
              fuse_reply_err(req, ENOSYS);
            }
        }
        break;

      case FUSE_STATFS:
        if (op->statfs != NULL)
          {
            op->statfs(req, nodeid);
          }
        else
          {
            struct statvfs stbuf =
            {
              .f_namemax = 255,
              .f_bsize = 512,
            };

            fuse_reply_statfs(req, &stbuf);
          }
        break;

      case FUSE_RELEASE:
        if (op->release != NULL)
          {
            FAR const struct fuse_release_in *arg = body;
            struct fuse_file_info fi;

            memset(&fi, 0, sizeof(fi));
            fi.fh            = arg->fh;
            fi.flags         = arg->flags;
            fi.lock_owner    = arg->lock_owner;
            fi.flush         = !!(arg->release_flags & FUSE_RELEASE_FLUSH);
            fi.flock_release = !!(arg->release_flags &
                                  FUSE_RELEASE_FLOCK_UNLOCK);

            op->release(req, nodeid, &fi);
          }
        else
          {
            fuse_reply_err(req, 0);
          }
        break;

      case FUSE_FSYNC:
        if (op->fsync != NULL)
          {
            FAR const struct fuse_fsync_in *arg = body;
            int datasync = !!(arg->fsync_flags & 1);
            struct fuse_file_info fi;

            memset(&fi, 0, sizeof(fi));
            fi.fh = arg->fh;

            op->fsync(req, nodeid, datasync, &fi);
          }
        else
          {
            fuse_reply_err(req, ENOSYS);
          }
        break;

      case FUSE_SETXATTR:
        if (op->setxattr != NULL)
          {
            FAR const struct fuse_setxattr_in *arg = body;
            FAR const char *name = (FAR const char *)(arg + 1);
            FAR const char *value = name + strlen(name) + 1;

            op->setxattr(req, nodeid, name, value, arg->size, arg->flags);
          }
        else
          {
            fuse_reply_err(req, ENOSYS);
          }
        break;

      case FUSE_GETXATTR:
        if (op->getxattr != NULL)
          {
            FAR const struct fuse_getxattr_in *arg = body;
            FAR const char *name = (FAR const char *)(arg + 1);

            op->getxattr(req, nodeid, name, arg->size);
          }
        else
          {
            fuse_reply_err(req, ENOSYS);
          }
        break;

      case FUSE_LISTXATTR:
        if (op->listxattr != NULL)
          {
            FAR const struct fuse_getxattr_in *arg = body;

            op->listxattr(req, nodeid, arg->size);
          }
        else
          {
            fuse_reply_err(req, ENOSYS);
          }
        break;

      case FUSE_REMOVEXATTR:
        if (op->removexattr != NULL)
          {
            op->removexattr(req, nodeid, body);
          }
        else
          {
            fuse_reply_err(req, ENOSYS);
          }
        break;

      case FUSE_FLUSH:
        if (op->flush != NULL)
          {
            FAR const struct fuse_flush_in *arg = body;
            struct fuse_file_info fi;

            memset(&fi, 0, sizeof(fi));
            fi.fh         = arg->fh;
            fi.lock_owner = arg->lock_owner;
            fi.flush      = 1;

            op->flush(req, nodeid, &fi);
          }
        else
          {
            fuse_reply_err(req, ENOSYS);
          }
        break;

      case FUSE_INIT:
        if (!se->got_init && se->cuse_data == NULL)
          {
            se->got_init = 1;
            fuse_lowlevel_init(req, &se->conn, body, bodylen);
          }
        else
          {
            fuse_reply_err(req, EIO);
          }
        break;

      case FUSE_OPENDIR:
        {
          FAR const struct fuse_open_in *arg = body;
          struct fuse_file_info fi;

          memset(&fi, 0, sizeof(fi));
          fi.flags = arg->flags;

          if (op->opendir != NULL)
            {
              op->opendir(req, nodeid, &fi);
            }
          else if (se->conn.want_ext & FUSE_CAP_NO_OPENDIR_SUPPORT)
            {
              fuse_reply_err(req, ENOSYS);
            }
          else
            {
              fuse_reply_open(req, &fi);
            }
        }
        break;

      case FUSE_READDIR:
      case FUSE_READDIRPLUS:
        {
          FAR const struct fuse_read_in *arg = body;
          struct fuse_file_info fi;

          memset(&fi, 0, sizeof(fi));
          fi.fh = arg->fh;

          if (in->opcode == FUSE_READDIR)
            {
              if (op->readdir != NULL)
                {
                  op->readdir(req, nodeid, arg->size, arg->offset, &fi);
                }
              else
                {
                  fuse_reply_err(req, ENOSYS);
                }
            }
          else if (op->readdirplus != NULL)
            {
              op->readdirplus(req, nodeid, arg->size, arg->offset, &fi);
            }
          else
            {
              fuse_reply_err(req, ENOSYS);
            }
        }
        break;

      case FUSE_RELEASEDIR:
        if (op->releasedir != NULL)
          {
            FAR struct fuse_release_in *arg = body;
            struct fuse_file_info fi;

            memset(&fi, 0, sizeof(fi));
            fi.fh    = arg->fh;
            fi.flags = arg->flags;

            op->releasedir(req, nodeid, &fi);
          }
        else
          {
            fuse_reply_err(req, ENOSYS);
          }
        break;

      case FUSE_FSYNCDIR:
        if (op->fsyncdir != NULL)
          {
            FAR const struct fuse_fsync_in *arg = body;
            int datasync = !!(arg->fsync_flags & 1);
            struct fuse_file_info fi;

            memset(&fi, 0, sizeof(fi));
            fi.fh = arg->fh;

            op->fsyncdir(req, nodeid, datasync, &fi);
          }
        else
          {
            fuse_reply_err(req, ENOSYS);
          }
        break;

      case FUSE_GETLK:
      case FUSE_SETLK:
      case FUSE_SETLKW:
        {
          FAR const struct fuse_lk_in *arg = body;
          struct fuse_file_info fi;
          struct flock lock;

          memset(&fi, 0, sizeof(fi));
          fi.fh         = arg->fh;
          fi.lock_owner = arg->owner;

          lock.l_type   = arg->lk.type;
          lock.l_whence = SEEK_SET;
          lock.l_pid    = arg->lk.pid;
          lock.l_start  = arg->lk.start;
          if (arg->lk.end == INT64_MAX)
            {
              lock.l_len = 0;
            }
          else
            {
              lock.l_len = arg->lk.end - arg->lk.start + 1;
            }

          if (in->opcode == FUSE_GETLK)
            {
              if (op->getlk != NULL)
                {
                  op->getlk(req, nodeid, &fi, &lock);
                }
              else
                {
                  fuse_reply_err(req, ENOSYS);
                }
            }
          else if (arg->lk_flags & FUSE_LK_FLOCK)
            {
              int op = 0;

              switch (arg->lk.type)
                {
                  case F_RDLCK:
                    op = LOCK_SH;
                    break;

                  case F_WRLCK:
                    op = LOCK_EX;
                    break;

                  case F_UNLCK:
                    op = LOCK_UN;
                    break;
                }

              if (in->opcode == FUSE_SETLK)
                {
                  op |= LOCK_NB;
                }

              if (op->flock != NULL)
                {
                  op->flock(req, nodeid, &fi, op);
                }
              else
                {
                  fuse_reply_err(req, ENOSYS);
                }
            }
          else if (op->setlk != NULL)
            {
              op->setlk(req, nodeid, &fi, &lock, in->opcode == FUSE_SETLKW);
            }
          else
            {
              fuse_reply_err(req, ENOSYS);
            }
        }
        break;

      case FUSE_ACCESS:
        if (op->access != NULL)
          {
            FAR const struct fuse_access_in *arg = body;

            op->access(req, nodeid, arg->mask);
          }
        else
          {
            fuse_reply_err(req, ENOSYS);
          }
        break;

      case FUSE_CREATE:
        if (op->create != NULL)
          {
            FAR const struct fuse_create_in *arg = body;
            FAR const char *name = (FAR const char *)(arg + 1);
            struct fuse_file_info fi;

            memset(&fi, 0, sizeof(fi));
            fi.flags = arg->flags;

            req->ctx.umask = arg->umask;
            op->create(req, nodeid, name, arg->mode, &fi);
          }
        else
          {
            fuse_reply_err(req, ENOSYS);
          }
        break;

      case FUSE_INTERRUPT:
        break; /* No reply */

      case FUSE_BMAP:
        if (op->bmap != NULL)
          {
            FAR const struct fuse_bmap_in *arg = body;

            op->bmap(req, nodeid, arg->blocksize, arg->block);
          }
        else
          {
            fuse_reply_err(req, ENOSYS);
          }
        break;

      case FUSE_DESTROY:
        se->got_init      = 0;
        se->got_destroy   = 1;
        se->mountpoint[0] = 0;

        if (op->destroy != NULL)
          {
            op->destroy(se->userdata);
          }

        fuse_reply_ok(req, NULL, 0);
        break;

      case FUSE_IOCTL:
        if (op->ioctl != NULL)
          {
            FAR const struct fuse_ioctl_in *arg = body;
            FAR const void *buf = arg->in_size ? (arg + 1) : NULL;
            struct fuse_file_info fi;

            memset(&fi, 0, sizeof(fi));
            fi.fh = arg->fh;

            if ((arg->flags & FUSE_IOCTL_DIR) &&
                !(se->conn.want_ext & FUSE_CAP_IOCTL_DIR))
              {
                fuse_reply_err(req, ENOTTY);
              }
            else
              {
                op->ioctl(req, nodeid, arg->cmd,
                          (FAR void *)(uintptr_t)arg->arg, &fi,
                          arg->flags, buf, arg->in_size, arg->out_size);
              }
          }
        else
          {
            fuse_reply_err(req, ENOSYS);
          }
        break;

      case FUSE_POLL:
        if (op->poll != NULL)
          {
            FAR const struct fuse_poll_in *arg = body;
            FAR struct fuse_pollhandle *ph = NULL;
            struct fuse_file_info fi;

            memset(&fi, 0, sizeof(fi));
            fi.fh          = arg->fh;
            fi.poll_events = arg->events;

            if (arg->flags & FUSE_POLL_SCHEDULE_NOTIFY)
              {
                ph = lib_malloc(sizeof(*ph));
                if (ph == NULL)
                  {
                    fuse_reply_err(req, ENOMEM);
                    break;
                  }

                ph->kh = in->kh;
                ph->se = se;
              }

            op->poll(req, nodeid, &fi, ph);
          }
        else
          {
            fuse_reply_err(req, ENOSYS);
          }
        break;

      case FUSE_NOTIFY_REPLY:
        if (op->retrieve_reply != NULL)
          {
            FAR const struct fuse_notify_retrieve_in *arg = body;
            struct fuse_bufvec bufv = FUSE_BUFVEC_INIT(arg->size);

            bufv.buf[0].mem = (FAR void *)(body + 1);
            op->retrieve_reply(req, (FAR void *)(uintptr_t)in->unique,
                               nodeid, arg->offset, &bufv);
          }
        else
          {
            fuse_reply_none(req);
          }
        break;

      case FUSE_BATCH_FORGET:
        {
          FAR const struct fuse_batch_forget_in *arg = body;
          FAR const struct fuse_forget_data *forgets =
            (FAR const struct fuse_forget_data *)(arg + 1);

          if (op->forget_multi != NULL)
            {
              op->forget_multi(req, arg->count, forgets);
            }
          else if (op->forget != NULL)
            {
              uint32_t i;

              for (i = 0; i < arg->count; i++)
                {
                  op->forget(req, forgets[i].ino, forgets[i].nlookup);
                }
            }
          else
            {
              fuse_reply_none(req);
            }
        }
        break;

      case FUSE_FALLOCATE:
        if (op->fallocate != NULL)
          {
            FAR const struct fuse_fallocate_in *arg = body;
            struct fuse_file_info fi;

            memset(&fi, 0, sizeof(fi));
            fi.fh = arg->fh;

            op->fallocate(req, nodeid, arg->mode,
                          arg->offset, arg->length, &fi);
          }
        else
          {
            fuse_reply_err(req, ENOSYS);
          }
        break;

      case FUSE_RENAME2:
        if (op->rename != NULL)
          {
            FAR const struct fuse_rename2_in *arg = body;
            FAR const char *oldname = (FAR const char *)(arg + 1);
            FAR const char *newname = oldname + strlen(oldname) + 1;

            op->rename(req, nodeid, oldname,
                       arg->newdir, newname, arg->flags);
          }
        else
          {
            fuse_reply_err(req, ENOSYS);
          }
        break;

      case FUSE_LSEEK:
        if (op->lseek != NULL)
          {
            FAR const struct fuse_lseek_in *arg = body;
            struct fuse_file_info fi;

            memset(&fi, 0, sizeof(fi));
            fi.fh = arg->fh;

            op->lseek(req, nodeid, arg->offset, arg->whence, &fi);
          }
        else
          {
            fuse_reply_err(req, ENOSYS);
          }
        break;

      case FUSE_COPY_FILE_RANGE:
      case FUSE_COPY_FILE_RANGE_64:
        if (op->copy_file_range != NULL)
          {
            FAR const struct fuse_copy_file_range_in *arg = body;
            struct fuse_file_info fi_out;
            struct fuse_file_info fi_in;

            memset(&fi_out, 0, sizeof(fi_out));
            fi_out.fh = arg->fh_out;

            memset(&fi_in, 0, sizeof(fi_in));
            fi_in.fh = arg->fh_in;

            op->copy_file_range(req, nodeid, arg->off_in, &fi_in,
                                arg->nodeid_out, arg->off_out, &fi_out,
                                arg->len, arg->flags);
          }
        else
          {
            fuse_reply_err(req, ENOSYS);
          }
        break;

      case FUSE_SYNCFS:
        if (op->syncfs != NULL)
          {
            op->syncfs(req, nodeid);
          }
        else
          {
            fuse_reply_err(req, ENOSYS);
          }
        break;

      case FUSE_TMPFILE:
        if (op->tmpfile != NULL)
          {
            FAR const struct fuse_create_in *arg = body;
            struct fuse_file_info fi;

            memset(&fi, 0, sizeof(fi));
            fi.flags = arg->flags;

            req->ctx.umask = arg->umask;
            op->tmpfile(req, nodeid, arg->mode, &fi);
          }
        else
          {
            fuse_reply_err(req, ENOSYS);
          }
        break;

      case FUSE_STATX:
        if (op->statx != NULL)
          {
            FAR const struct fuse_statx_in *arg = body;

            if (arg->getattr_flags & FUSE_GETATTR_FH)
              {
                struct fuse_file_info fi;

                memset(&fi, 0, sizeof(fi));
                fi.fh = arg->fh;

                op->statx(req, nodeid, arg->sx_flags, arg->sx_mask, &fi);
              }
            else
              {
                op->statx(req, nodeid, arg->sx_flags, arg->sx_mask, NULL);
              }
          }
        else
          {
            fuse_reply_err(req, ENOSYS);
          }
        break;

#ifdef CONFIG_FS_CUSE
      case CUSE_INIT:
        if (!se->got_init && se->cuse_data != NULL)
          {
            se->got_init = 1;
            cuse_lowlevel_init(req, &se->conn, body, bodylen);
          }
        else
          {
            fuse_reply_err(req, EIO);
          }
        break;
#endif

      default:
        fuse_reply_err(req, ENOSYS);
        break;
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Replies
 ****************************************************************************/

int fuse_reply_iov(fuse_req_t req, FAR struct iovec *iov, int count)
{
  struct iovec padded_iov[1 + count];

  memcpy(padded_iov + 1, iov, count * sizeof(struct iovec));
  return fuse_send_reply_iov(req, 0, padded_iov, count + 1);
}

int fuse_reply_err(fuse_req_t req, int err)
{
  return fuse_send_reply(req, -err, NULL, 0);
}

void fuse_reply_none(fuse_req_t req)
{
  FAR struct fuse_custom_io *io = req->se->io;

  /* No reply goes on the wire, but the transport still learns that
   * the request completed:  custom channels with per-request
   * resources (vhost virtqueue chains) release them here.
   */

  if (io != NULL && io->request_done != NULL)
    {
      io->request_done(req->se->fd, req->unique, req->se->userdata);
    }
}

size_t fuse_add_direntry(fuse_req_t req, FAR char *buf, size_t bufsize,
                         FAR const char *name, FAR const struct stat *st,
                         off_t off)
{
  FAR struct fuse_dirent *de = (FAR struct fuse_dirent *)buf;
  size_t namelen = strlen(name);
  size_t entlen = FUSE_NAME_OFFSET + namelen;
  size_t entlen_padded = FUSE_DIRENT_ALIGN(entlen);

  if (buf == NULL || bufsize < entlen_padded)
    {
      return entlen_padded;
    }

  de->ino     = st->st_ino;
  de->off     = off;
  de->namelen = namelen;
  de->type    = IFTODT(st->st_mode);
  memcpy(de->name, name, namelen);
  memset(de->name + namelen, 0, entlen_padded - entlen);

  return entlen_padded;
}

size_t fuse_add_direntry_plus(fuse_req_t req, FAR char *buf,
                              size_t bufsize, FAR const char *name,
                              FAR const struct fuse_entry_param *e,
                              off_t off)
{
  FAR struct fuse_direntplus *de = (FAR struct fuse_direntplus *)buf;
  size_t namelen = strlen(name);
  size_t entlen = FUSE_NAME_OFFSET_DIRENTPLUS + namelen;
  size_t entlen_padded = FUSE_DIRENT_ALIGN(entlen);

  if (buf == NULL || bufsize < entlen_padded)
    {
      return entlen_padded;
    }

  de->dirent.ino     = e->ino;
  de->dirent.off     = off;
  de->dirent.namelen = namelen;
  de->dirent.type    = IFTODT(e->attr.st_mode);
  memcpy(de->dirent.name, name, namelen);
  memset(de->dirent.name + namelen, 0, entlen_padded - entlen);
  fill_entry(&de->entry_out, e);

  return entlen_padded;
}

int fuse_reply_entry(fuse_req_t req, FAR const struct fuse_entry_param *e)
{
  struct fuse_entry_out arg;

  fill_entry(&arg, e);
  return fuse_reply_ok(req, &arg, sizeof(arg));
}

int fuse_reply_create(fuse_req_t req, FAR const struct fuse_entry_param *e,
                      FAR struct fuse_file_info *fi)
{
  struct
  {
    struct fuse_entry_out e;
    struct fuse_open_out o;
  } arg;

  fill_entry(&arg.e, e);
  fill_open(&arg.o, fi);

  return fuse_reply_ok(req, &arg, sizeof(arg));
}

int fuse_reply_attr(fuse_req_t req, FAR const struct stat *attr,
                    double attr_timeout)
{
  struct fuse_attr_out arg;

  arg.attr_valid = calc_timeout_sec(attr_timeout);
  arg.attr_valid_nsec = calc_timeout_nsec(attr_timeout);
  convert_stat(attr, &arg.attr);

  return fuse_reply_ok(req, &arg, sizeof(arg));
}

int fuse_reply_readlink(fuse_req_t req, FAR const char *link)
{
  return fuse_reply_ok(req, link, strlen(link));
}

int fuse_passthrough_open(fuse_req_t req, int fd)
{
  struct fuse_backing_map map;
  int ret;

  memset(&map, 0, sizeof(map));
  map.fd = fd;

  ret = ioctl(req->se->fd, FUSE_DEV_IOC_BACKING_OPEN, &map);
  if (ret < 0)
    {
      ret = -get_errno();
    }

  return ret;
}

int fuse_passthrough_close(fuse_req_t req, int backing_id)
{
  int ret;

  ret = ioctl(req->se->fd, FUSE_DEV_IOC_BACKING_CLOSE, &backing_id);
  if (ret < 0)
    {
      ret = -get_errno();
    }

  return ret;
}

int fuse_reply_open(fuse_req_t req, FAR struct fuse_file_info *fi)
{
  struct fuse_open_out arg;

  fill_open(&arg, fi);
  return fuse_reply_ok(req, &arg, sizeof(arg));
}

int fuse_reply_write(fuse_req_t req, size_t count)
{
  struct fuse_write_out arg;

  memset(&arg, 0, sizeof(arg));
  arg.size = count;

  return fuse_reply_ok(req, &arg, sizeof(arg));
}

int fuse_reply_buf(fuse_req_t req, FAR const char *buf, size_t size)
{
  return fuse_reply_ok(req, buf, size);
}

int fuse_reply_data(fuse_req_t req, FAR struct fuse_bufvec *bufv,
                    enum fuse_buf_copy_flags flags)
{
  struct iovec iov[1 + bufv.count];
  bool free;
  int ret;

  ret = fuse_buf_to_iov(bufv, flags, iov + 1, &free);
  if (ret < 0)
    {
      return fuse_reply_err(req, -ret);
    }

  ret = fuse_send_reply_iov(req, 0, iov, ret + 1);
  if (free)
    {
      lib_free(iov[1].iov_base);
    }

  return ret;
}

int fuse_reply_statfs(fuse_req_t req, FAR const struct statvfs *stbuf)
{
  struct fuse_statfs_out arg;

  convert_statfs(stbuf, &arg.st);
  return fuse_reply_ok(req, &arg, sizeof(arg));
}

int fuse_reply_xattr(fuse_req_t req, size_t count)
{
  struct fuse_getxattr_out arg;

  memset(&arg, 0, sizeof(arg));
  arg.size = count;

  return fuse_reply_ok(req, &arg, sizeof(arg));
}

int fuse_reply_lock(fuse_req_t req, FAR const struct flock *lock)
{
  struct fuse_lk_out arg;

  memset(&arg, 0, sizeof(arg));
  arg.lk.type = lock->l_type;
  arg.lk.pid  = lock->l_pid;
  if (lock->l_type != F_UNLCK)
    {
      arg.lk.start = lock->l_start;

      if (lock->l_len == 0)
        {
          arg.lk.end = INT64_MAX;
        }
      else
        {
          arg.lk.end = lock->l_start + lock->l_len - 1;
        }
    }

  return fuse_reply_ok(req, &arg, sizeof(arg));
}

int fuse_reply_bmap(fuse_req_t req, uint64_t block)
{
  struct fuse_bmap_out arg;

  arg.block = block;
  return fuse_reply_ok(req->se, &arg, sizeof(arg));
}

int fuse_reply_ioctl_retry(fuse_req_t req, FAR const struct iovec *in_iov,
                           int in_count, FAR const struct iovec *out_iov,
                           int out_count)
{
  struct fuse_ioctl_iovec out_fiov[out_count];
  struct fuse_ioctl_iovec in_fiov[in_count];
  struct fuse_ioctl_out arg;
  struct iovec iov[4];
  int count = 2;
  int i;

  memset(&arg, 0, sizeof(arg));
  out.flags    = FUSE_IOCTL_RETRY;
  out.in_iovs  = in_count;
  out.out_iovs = out_count;

  iov[1].iov_base = &arg;
  iov[1].iov_len  = sizeof(arg);

  if (in_count)
    {
      for (i = 0; i < in_count; i++)
        {
          in_fiov[i].base = (uintptr_t)in_iov[i].iov_base;
          in_fiov[i].len  = in_iov[i].iov_len;
        }

      iov[count].iov_base = in_fiov;
      iov[count].iov_len  = sizeof(in_fiov[0]) * in_count;
      count++;
    }

  if (out_count)
    {
      for (i = 0; i < out_count; i++)
        {
          out_fiov[i].base = (uintptr_t)out_iov[i].iov_base;
          out_fiov[i].len  = out_iov[i].iov_len;
        }

      iov[count].iov_base = out_fiov;
      iov[count].iov_len  = sizeof(out_fiov[0]) * out_count;
      count++;
    }

  return fuse_send_reply_iov(req, 0, iov, count);
}

int fuse_reply_ioctl(fuse_req_t req, int result, FAR const void *buf,
                     size_t size)
{
  struct fuse_ioctl_out arg;
  struct iovec iov[3];
  int count = 2;

  memset(&arg, 0, sizeof(arg));
  arg.result = result;

  iov[1].iov_base = &arg;
  iov[1].iov_len  = sizeof(arg);

  if (size)
    {
      iov[2].iov_base = (FAR void *)buf;
      iov[2].iov_len  = size;
      count = 3;
    }

  return fuse_send_reply_iov(req, 0, iov, count);
}

int fuse_reply_ioctl_iov(fuse_req_t req, int result,
                         FAR const struct iovec *iov, int count)
{
  struct iovec padded_iov[2 + count];
  struct fuse_ioctl_out arg;

  memset(&arg, 0, sizeof(arg));
  arg.result = result;

  padded_iov[1].iov_base = &arg;
  padded_iov[1].iov_len  = sizeof(arg);
  memcpy(&padded_iov[2], iov, count * sizeof(struct iovec));

  return fuse_send_reply_iov(req, 0, padded_iov, count + 2);
}

int fuse_reply_poll(fuse_req_t req, unsigned int revents)
{
  struct fuse_poll_out arg;

  memset(&arg, 0, sizeof(arg));
  arg.revents = revents;

  return fuse_reply_ok(req->se, &arg, sizeof(arg));
}

int fuse_reply_lseek(fuse_req_t req, off_t off)
{
  struct fuse_lseek_out arg;

  arg.offset = off;

  return fuse_reply_ok(req, &arg, sizeof(arg));
}

int fuse_reply_statx(fuse_req_t req, int flags,
                     FAR const struct statx *stx,
                     double attr_timeout)
{
  struct fuse_statx_out arg;

  memset(&arg, 0, sizeof(arg));
  arg.flags = flags;
  arg.attr_valid = calc_timeout_sec(attr_timeout);
  arg.attr_valid_nsec = calc_timeout_nsec(attr_timeout);
  memcpy(&arg.stat, stx, sizeof(arg.stat));

  return fuse_reply_ok(req, &arg, sizeof(arg));
}

void fuse_pollhandle_destroy(FAR struct fuse_pollhandle *ph)
{
  lib_free(ph);
}

bool fuse_set_feature_flag(FAR struct fuse_conn_info *conn,
                           uint64_t flag)
{
  if ((conn->capable_ext & flag) == 0)
    {
      return false;
    }

  conn->want_ext |= flag;
  conn->want     |= flag;

  return true;
}

void fuse_unset_feature_flag(FAR struct fuse_conn_info *conn,
                             uint64_t flag)
{
  conn->want_ext &= ~flag;
  conn->want     &= ~flag;
}

bool fuse_get_feature_flag(FAR const struct fuse_conn_info *conn,
                           uint64_t flag)
{
  return (conn->capable_ext & flag) != 0;
}

void fuse_set_conn_flag(FAR struct fuse_conn_info *conn, uint64_t flag)
{
  switch (flag)
    {
      case FUSE_CONN_FLAG_SINGLE_ISSUER:
        conn->io_uring_single_issuer = 1;
        return true;

      case FUSE_CONN_FLAG_NO_INTERRUPT:
        conn->no_interrupt = 1;
        return true;

      default:
        return false;
    }
}

/****************************************************************************
 * Notifications
 ****************************************************************************/

int fuse_lowlevel_notify_poll(FAR struct fuse_pollhandle *ph)
{
  struct fuse_notify_poll_wakeup_out arg;

  if (ph == NULL)
    {
      return -EINVAL;
    }

  arg.kh = ph->kh;

  return fuse_send_notify(ph->se, FUSE_NOTIFY_POLL, &arg, sizeof(arg));
}

int fuse_lowlevel_notify_inval_inode(FAR struct fuse_session *se,
                                     fuse_ino_t ino, off_t off, off_t len)
{
  struct fuse_notify_inval_inode_out arg;

  arg.ino = ino;
  arg.off = off;
  arg.len = len;

  return fuse_send_notify(se, FUSE_NOTIFY_INVAL_INODE, &arg, sizeof(arg));
}

int fuse_lowlevel_notify_increment_epoch(FAR struct fuse_session *se)
{
  return fuse_send_notify(se, FUSE_NOTIFY_INC_EPOCH, NULL, 0);
}

int fuse_lowlevel_notify_inval_entry(FAR struct fuse_session *se,
                                     fuse_ino_t parent,
                                     FAR const char *name, size_t namelen)
{
  return fuse_notify_entry(se, parent, name, namelen, false);
}

int fuse_lowlevel_notify_expire_entry(FAR struct fuse_session *se,
                                      fuse_ino_t parent,
                                      FAR const char *name, size_t namelen)
{
  if (!(se->conn.capable_ext & FUSE_CAP_EXPIRE_ONLY))
    {
      return -ENOSYS;
    }

  return fuse_notify_entry(se, parent, name, namelen, true);
}

int fuse_lowlevel_notify_delete(FAR struct fuse_session *se,
                                fuse_ino_t parent, fuse_ino_t child,
                                FAR const char *name, size_t namelen)
{
  struct fuse_notify_delete_out arg;
  struct iovec iov[3];

  memset(&arg, 0, sizeof(arg));
  arg.parent  = parent;
  arg.child   = child;
  arg.namelen = namelen;

  iov[1].iov_base = &arg;
  iov[1].iov_len = sizeof(arg);
  iov[2].iov_base = (FAR void *)name;
  iov[2].iov_len = namelen + 1;

  return fuse_send_notify_iov(se, FUSE_NOTIFY_DELETE, iov, 3);
}

int fuse_lowlevel_notify_store(FAR struct fuse_session *se, fuse_ino_t ino,
                               off_t offset, FAR struct fuse_bufvec *bufv,
                               enum fuse_buf_copy_flags flags)
{
  struct fuse_notify_store_out arg;
  struct iovec iov[2 + bufv.count];
  bool free;
  int ret;

  ret = fuse_buf_to_iov(bufv, flags, iov + 2, &free);
  if (ret < 0)
    {
      return ret;
    }

  memset(&arg, 0, sizeof(arg));
  arg.nodeid = ino;
  arg.offset = offset;
  arg.size   = fuse_buf_size(bufv);

  iov[1].iov_base = &arg;
  iov[1].iov_len = sizeof(arg);

  ret = fuse_send_notify_iov(se, FUSE_NOTIFY_STORE, iov, ret + 2);
  if (free)
    {
      lib_free(iov[1].iov_base);
    }

  return ret;
}

int fuse_lowlevel_notify_retrieve(FAR struct fuse_session *se,
                                  fuse_ino_t ino, size_t size, off_t offset,
                                  FAR void *cookie)
{
  struct fuse_notify_retrieve_out arg;

  memset(&arg, 0, sizeof(arg));
  arg.notify_unique = (uintptr_t)cookie;
  arg.nodeid        = ino;
  arg.offset        = offset;
  arg.size          = size;

  return fuse_send_notify(se, FUSE_NOTIFY_RETRIEVE, &arg, sizeof(arg));
}

int fuse_lowlevel_notify_prune(FAR struct fuse_session *se,
                               FAR fuse_ino_t *nodeids, uint32_t count)
{
  struct fuse_notify_prune_out arg;
  struct iovec iov[3];

  memset(&arg, 0, sizeof(arg));
  arg.count = count;

  iov[1].iov_base = &arg;
  iov[1].iov_len = sizeof(arg);
  iov[2].iov_base = nodeids;
  iov[2].iov_len = sizeof(fuse_ino_t) * count;

  return fuse_send_notify_iov(se, FUSE_NOTIFY_PRUNE, iov, 3);
}

/****************************************************************************
 * Utilities
 ****************************************************************************/

FAR void *fuse_req_userdata(fuse_req_t req)
{
  return req->se->userdata;
}

FAR struct cuse_data *fuse_req_cusedata(fuse_req_t req)
{
  return req->se->cuse_data;
}

FAR const struct fuse_ctx *fuse_req_ctx(fuse_req_t req)
{
  return &req->ctx;
}

void fuse_session_process_buf(FAR struct fuse_session *se,
                              FAR const struct fuse_buf *buf)
{
  FAR struct fuse_in_header *in = buf->mem;

  if (buf->size >= sizeof(*in))
    {
      fuse_dispatch(se, in, in + 1, buf->size - sizeof(*in));
    }
}

/****************************************************************************
 * Inquiry
 ****************************************************************************/

void fuse_lowlevel_version(void)
{
  printf("using FUSE kernel interface version %d.%d\n",
         FUSE_KERNEL_VERSION, FUSE_KERNEL_MINOR_VERSION);
}

void fuse_lowlevel_help(void)
{
}

void fuse_cmdline_help(void)
{
  printf("usage: fuse mountpoint [options]\n"
         "\n"
         "general options:\n"
         "    -h   --help            print help\n"
         "    -V   --version         print version\n"
         "    -d   -o debug          enable debug output\n"
         "    -f                     foreground operation (default)\n"
         "    -s                     disable multi-threaded operation\n"
         "    -o clone_fd            accepted, ignored\n"
         "    -o max_threads=N       worker thread count\n");
}

/****************************************************************************
 * Setup & teardown
 ****************************************************************************/

int fuse_parse_cmdline(FAR struct fuse_args *args,
                       FAR struct fuse_cmdline_opts *opts)
{
  int i;

  memset(opts, 0, sizeof(*opts));

  for (i = 1; i < args->argc; i++)
    {
      FAR const char *arg = args->argv[i];

      if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0)
        {
          opts->show_help = 1;
        }
      else if (strcmp(arg, "-V") == 0 || strcmp(arg, "--version") == 0)
        {
          opts->show_version = 1;
        }
      else if (strcmp(arg, "-d") == 0 || strcmp(arg, "-o") == 0)
        {
          /* -d or -o debug: both enable debug; -o consumes the next
           * argument and inspects known keys.
           */

          if (strcmp(arg, "-d") == 0)
            {
              opts->debug = 1;
            }
          else if (i + 1 < args->argc)
            {
              FAR const char *key = args->argv[++i];

              if (strcmp(key, "debug") == 0)
                {
                  opts->debug = 1;
                }
              else if (strcmp(key, "clone_fd") == 0)
                {
                  opts->clone_fd = 1;
                }
              else if (strncmp(key, "max_threads=", 12) == 0)
                {
                  opts->max_threads = atoi(key + 12);
                }
              else if (strncmp(key, "max_idle_threads=", 17) == 0)
                {
                  opts->max_idle_threads = atoi(key + 17);
                }

              /* Other -o keys are accepted and ignored */
            }
        }
      else if (strcmp(arg, "-f") == 0)
        {
          opts->foreground = 1;   /* Always foreground on NuttX */
        }
      else if (strcmp(arg, "-s") == 0)
        {
          opts->singlethread = 1;
        }
      else if (arg[0] != '-')
        {
          if (opts->mountpoint == NULL)
            {
              opts->mountpoint = strdup(arg);
            }
        }
    }

  return 0;
}

void fuse_session_destroy(FAR struct fuse_session *se)
{
  if (se->got_init && !se->got_destroy)
    {
      if (se->op.destroy)
        {
          se->op.destroy(se->userdata);
        }
    }

  if (se->io == NULL && se->fd >= 0)
    {
      /* Only fd-based channels own their fd; a custom_io channel
       * identifier is private to the transport (e.g. a virtqueue
       * index) and is never closed here.
       */

      close(se->fd);
    }

  lib_free(se);
}

int fuse_session_receive_buf(FAR struct fuse_session *se,
                             FAR struct fuse_buf *buf)
{
  ssize_t ret;

  if (buf->mem == NULL)
    {
      buf->mem = lib_malloc(buf_size(se));
      if (buf->mem == NULL)
        {
          fuse_log(FUSE_LOG_ERR,
                   "fuse: failed to allocate read buffer\n");
          return -ENOMEM;
        }

      buf->mem_size = buf_size(se);
    }
  else if (buf->mem_size < buf_size(se))
    {
      /* FUSE_INIT might have increased the required max_write */

      FAR void *newbuf = lib_malloc(buf_size(se));

      if (newbuf == NULL)
        {
          fuse_log(FUSE_LOG_ERR,
                   "fuse: failed to (re)allocate read buffer\n");
          return -ENOMEM;
        }

      fuse_buf_free(buf);
      buf->mem      = newbuf;
      buf->mem_size = buf_size(se);
    }

  if (se->io != NULL)
    {
      ret = se->io->read(se->fd, buf->mem, buf->mem_size, se->userdata);
    }
  else
    {
      ret = read(se->fd, buf->mem, buf->mem_size);
    }

  if (fuse_session_exited(se))
    {
      return 0;
    }

  if (ret < 0)
    {
      ret = -get_errno();

      /* ENOENT means the operation was interrupted, it's safe
       * to restart
       */

      if (ret == -ENOENT)
        {
          return fuse_session_receive_buf(se, buf);
        }

      if (ret == -ENODEV)
        {
          /* Filesystem was unmounted, or connection was aborted
           * via /sys/fs/fuse/connections
           */

          fuse_session_exit(se);
          return 0;
        }

      /* Errors occurring during normal operation: EINTR (read
       * interrupted), EAGAIN (nonblocking I/O), ENODEV (filesystem
       * unmounted)
       */

      if (ret != -EINTR && ret != -EAGAIN)
        {
          fuse_log(FUSE_LOG_ERR, "fuse: reading device: %s\n",
                   strerror(-ret));
        }

      return ret;
    }

  if (ret < sizeof(struct fuse_in_header))
    {
      fuse_log(FUSE_LOG_ERR, "short read on fuse device\n");
      return -EIO;
    }

  return buf->size = ret;
}

FAR struct fuse_session *
fuse_session_new(FAR struct fuse_args *args,
                 FAR const struct fuse_lowlevel_ops *op, size_t op_size,
                 FAR void *userdata)
{
  FAR struct fuse_session *se;

  se = lib_calloc(1, sizeof(*se));
  if (se == NULL)
    {
      return NULL;
    }

  se->fd       = -1;
  se->eventfd  = -1;
  se->userdata = userdata;

  se->conn.max_write     = CONFIG_LIBC_FS_FUSE_MAX_WRITE;
  se->conn.max_readahead = UINT32_MAX;

  if (op_size > sizeof(se->op))
    {
      op_size = sizeof(se->op);
    }

  memcpy(&se->op, op, op_size);

  return se;
}

#undef fuse_session_custom_io
int fuse_session_custom_io(FAR struct fuse_session *se,
                           FAR const struct fuse_custom_io *io,
                           size_t op_size, int fd)
{
  if (io == NULL || io->read == NULL || io->writev == NULL)
    {
      return -EINVAL;
    }

  se->io = io;
  se->fd = fd;
  return 0;
}

int fuse_session_mount(FAR struct fuse_session *se,
                       FAR const char *mountpoint)
{
  char options[32];
  int ret;

  se->fd = open("/dev/fuse", O_RDWR | O_CLOEXEC);
  if (se->fd < 0)
    {
      return -get_errno();
    }

  snprintf(options, sizeof(options), "fd=%d", se->fd);
  ret = mount(NULL, mountpoint, "fuse", 0, options);
  if (ret < 0)
    {
      ret = -get_errno();
      close(se->fd);
      se->fd = -1;
      return ret;
    }

  strlcpy(se->mountpoint, mountpoint, sizeof(se->mountpoint));
  return 0;
}

int fuse_session_fd(FAR struct fuse_session *se)
{
  return se->fd;
}

void fuse_session_set_fd(FAR struct fuse_session *se, int fd)
{
  se->fd = fd;
}

int fuse_session_eventfd(FAR struct fuse_session *se)
{
  return se->eventfd;
}

void fuse_session_set_eventfd(FAR struct fuse_session *se, int eventfd)
{
  se->eventfd = eventfd;
}

FAR struct cuse_data *fuse_session_cusedata(FAR struct fuse_session *se)
{
  return se->cuse_data;
}

void fuse_session_set_cusedata(FAR struct fuse_session *se,
                               FAR struct cuse_data *cuse_data)
{
  se->cuse_data = cuse_data;
}

void fuse_session_unmount(FAR struct fuse_session *se)
{
  if (se->fd >= 0)
    {
      close(se->fd);
      se->fd = -1;
    }

  if (se->mountpoint[0])
    {
      umount(se->mountpoint);
      se->mountpoint[0] = 0;
    }
}

void fuse_session_exit(FAR struct fuse_session *se)
{
  atomic_set(&se->exited, 1);

#ifdef CONFIG_EVENT_FD_POLL
  if (se->eventfd >= 0)
    {
      eventfd_write(se->eventfd, 1);
    }
#endif
}

void fuse_session_reset(FAR struct fuse_session *se)
{
  atomic_set(&se->exited, 0);
}

int fuse_session_exited(FAR struct fuse_session *se)
{
  return atomic_read(&se->exited) != 0;
}
