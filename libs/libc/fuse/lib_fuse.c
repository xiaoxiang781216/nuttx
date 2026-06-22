/****************************************************************************
 * libs/libc/fuse/lib_fuse.c
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

#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <nuttx/fuse.h>

#include <fuse.h>
#include <fuse_lowlevel.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define FUSE_LL_MAX_WRITE  (64 * 1024)
#define FUSE_LL_BUFSIZE    (FUSE_LL_MAX_WRITE + FUSE_MIN_READ_BUFFER)

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct fuse_req
{
  FAR struct fuse_session *se;
  uint64_t                 unique;
  struct fuse_ctx          ctx;
};

struct fuse_session
{
  int                                 fd;
  FAR const struct fuse_lowlevel_ops *op;
  FAR void                           *userdata;
  bool                                exited;
  uint32_t                            max_write;
  FAR char                           *mountpoint;
  FAR char                           *buf;
  size_t                              bufsize;
};

/* High level inode/path node */

struct fuse_node
{
  FAR struct fuse_node *next;
  uint64_t              nodeid;
  uint64_t              refs;
  FAR char             *path;
};

struct fuse
{
  FAR struct fuse_session *se;
  struct fuse_operations   op;
  FAR void                *private_data;
  FAR struct fuse_node    *nodes;
  uint64_t                 next_nodeid;
};

/* readdir filler context */

struct fuse_dirbuf
{
  fuse_req_t req;
  FAR char  *buf;
  size_t     size;
  size_t     pos;
  off_t      want_off;
  off_t      index;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct fuse_context g_fuse_context;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: fuse_send
 *
 * Description:
 *   Send a single FUSE reply (out header followed by an optional payload)
 *   to the kernel through /dev/fuse.
 *
 ****************************************************************************/

static int fuse_send(FAR struct fuse_session *se, uint64_t unique,
                     int error, FAR const void *data, size_t datalen)
{
  FAR struct fuse_out_header *oh;
  FAR char *msg;
  size_t msglen;
  ssize_t n;

  msglen = sizeof(*oh) + datalen;
  msg = malloc(msglen);
  if (msg == NULL)
    {
      return -ENOMEM;
    }

  oh         = (FAR struct fuse_out_header *)msg;
  oh->len    = msglen;
  oh->error  = error;
  oh->unique = unique;
  if (datalen > 0)
    {
      memcpy(msg + sizeof(*oh), data, datalen);
    }

  n = write(se->fd, msg, msglen);
  free(msg);
  return n < 0 ? -errno : 0;
}

/****************************************************************************
 * Name: fuse_stat_to_attr
 ****************************************************************************/

static void fuse_stat_to_attr(FAR const struct stat *st,
                              FAR struct fuse_attr *a)
{
  memset(a, 0, sizeof(*a));
  a->ino       = st->st_ino;
  a->size      = st->st_size;
  a->blocks    = st->st_blocks;
  a->blksize   = st->st_blksize;
  a->mode      = st->st_mode;
  a->nlink     = st->st_nlink ? st->st_nlink : 1;
  a->uid       = st->st_uid;
  a->gid       = st->st_gid;
  a->rdev      = st->st_rdev;
  a->atime     = st->st_atim.tv_sec;
  a->atimensec = st->st_atim.tv_nsec;
  a->mtime     = st->st_mtim.tv_sec;
  a->mtimensec = st->st_mtim.tv_nsec;
  a->ctime     = st->st_ctim.tv_sec;
  a->ctimensec = st->st_ctim.tv_nsec;
}

/****************************************************************************
 * Public Functions: reply helpers
 ****************************************************************************/

int fuse_reply_err(fuse_req_t req, int err)
{
  return fuse_send(req->se, req->unique, -err, NULL, 0);
}

void fuse_reply_none(fuse_req_t req)
{
  UNUSED(req);
}

int fuse_reply_entry(fuse_req_t req, FAR const struct fuse_entry_param *e)
{
  struct fuse_entry_out out;

  memset(&out, 0, sizeof(out));
  out.nodeid           = e->ino;
  out.generation       = e->generation;
  out.entry_valid      = (uint64_t)e->entry_timeout;
  out.attr_valid       = (uint64_t)e->attr_timeout;
  fuse_stat_to_attr(&e->attr, &out.attr);

  return fuse_send(req->se, req->unique, 0, &out, sizeof(out));
}

int fuse_reply_create(fuse_req_t req, FAR const struct fuse_entry_param *e,
                      FAR const struct fuse_file_info *fi)
{
  struct
  {
    struct fuse_entry_out entry;
    struct fuse_open_out  open;
  } out;

  memset(&out, 0, sizeof(out));
  out.entry.nodeid      = e->ino;
  out.entry.generation  = e->generation;
  out.entry.entry_valid = (uint64_t)e->entry_timeout;
  out.entry.attr_valid  = (uint64_t)e->attr_timeout;
  fuse_stat_to_attr(&e->attr, &out.entry.attr);
  out.open.fh           = fi->fh;

  return fuse_send(req->se, req->unique, 0, &out, sizeof(out));
}

int fuse_reply_attr(fuse_req_t req, FAR const struct stat *attr,
                    double attr_timeout)
{
  struct fuse_attr_out out;

  memset(&out, 0, sizeof(out));
  out.attr_valid = (uint64_t)attr_timeout;
  fuse_stat_to_attr(attr, &out.attr);

  return fuse_send(req->se, req->unique, 0, &out, sizeof(out));
}

int fuse_reply_readlink(fuse_req_t req, FAR const char *link)
{
  return fuse_send(req->se, req->unique, 0, link, strlen(link));
}

int fuse_reply_open(fuse_req_t req, FAR const struct fuse_file_info *fi)
{
  struct fuse_open_out out;

  memset(&out, 0, sizeof(out));
  out.fh = fi->fh;

  return fuse_send(req->se, req->unique, 0, &out, sizeof(out));
}

int fuse_reply_write(fuse_req_t req, size_t count)
{
  struct fuse_write_out out;

  memset(&out, 0, sizeof(out));
  out.size = count;

  return fuse_send(req->se, req->unique, 0, &out, sizeof(out));
}

int fuse_reply_buf(fuse_req_t req, FAR const char *buf, size_t size)
{
  return fuse_send(req->se, req->unique, 0, buf, size);
}

int fuse_reply_statfs(fuse_req_t req, FAR const struct statvfs *stbuf)
{
  struct fuse_statfs_out out;

  memset(&out, 0, sizeof(out));
  out.st.blocks  = stbuf->f_blocks;
  out.st.bfree   = stbuf->f_bfree;
  out.st.bavail  = stbuf->f_bavail;
  out.st.files   = stbuf->f_files;
  out.st.ffree   = stbuf->f_ffree;
  out.st.bsize   = stbuf->f_bsize;
  out.st.namelen = stbuf->f_namemax;
  out.st.frsize  = stbuf->f_frsize;

  return fuse_send(req->se, req->unique, 0, &out, sizeof(out));
}

size_t fuse_add_direntry(fuse_req_t req, FAR char *buf, size_t bufsize,
                         FAR const char *name, FAR const struct stat *stbuf,
                         off_t off)
{
  size_t namelen = strlen(name);
  size_t entlen  = FUSE_NAME_OFFSET + namelen;
  size_t entsize = FUSE_DIRENT_ALIGN(entlen);

  UNUSED(req);

  if (entsize <= bufsize)
    {
      FAR struct fuse_dirent *de = (FAR struct fuse_dirent *)buf;

      de->ino     = stbuf != NULL ? stbuf->st_ino : 0;
      de->off     = off;
      de->namelen = namelen;
      de->type    = stbuf != NULL ? (stbuf->st_mode >> 12) : 0;
      memcpy(de->name, name, namelen);
      if (entsize > entlen)
        {
          memset(buf + entlen, 0, entsize - entlen);
        }
    }

  return entsize;
}

FAR void *fuse_req_userdata(fuse_req_t req)
{
  return req->se->userdata;
}

FAR const struct fuse_ctx *fuse_req_ctx(fuse_req_t req)
{
  return &req->ctx;
}

/****************************************************************************
 * Private Functions: high level node management
 ****************************************************************************/

static FAR struct fuse_node *node_find_id(FAR struct fuse *f, uint64_t id)
{
  FAR struct fuse_node *n;

  for (n = f->nodes; n != NULL; n = n->next)
    {
      if (n->nodeid == id)
        {
          return n;
        }
    }

  return NULL;
}

static FAR const char *node_path(FAR struct fuse *f, uint64_t id)
{
  FAR struct fuse_node *n = node_find_id(f, id);
  return n != NULL ? n->path : NULL;
}

static FAR struct fuse_node *node_get(FAR struct fuse *f,
                                      FAR const char *path)
{
  FAR struct fuse_node *n;

  for (n = f->nodes; n != NULL; n = n->next)
    {
      if (strcmp(n->path, path) == 0)
        {
          n->refs++;
          return n;
        }
    }

  n = malloc(sizeof(*n));
  if (n == NULL)
    {
      return NULL;
    }

  n->path = strdup(path);
  if (n->path == NULL)
    {
      free(n);
      return NULL;
    }

  n->nodeid   = f->next_nodeid++;
  n->refs     = 1;
  n->next     = f->nodes;
  f->nodes    = n;
  return n;
}

static void node_forget(FAR struct fuse *f, uint64_t id, uint64_t nlookup)
{
  FAR struct fuse_node *n;
  FAR struct fuse_node *prev = NULL;

  for (n = f->nodes; n != NULL; prev = n, n = n->next)
    {
      if (n->nodeid != id)
        {
          continue;
        }

      if (n->refs > nlookup)
        {
          n->refs -= nlookup;
          return;
        }

      if (prev != NULL)
        {
          prev->next = n->next;
        }
      else
        {
          f->nodes = n->next;
        }

      free(n->path);
      free(n);
      return;
    }
}

static FAR char *build_path(FAR const char *parent, FAR const char *name)
{
  bool rootp = parent[0] == '/' && parent[1] == '\0';
  size_t len = strlen(parent) + 1 + strlen(name) + 1;
  FAR char *p = malloc(len);

  if (p == NULL)
    {
      return NULL;
    }

  if (rootp)
    {
      snprintf(p, len, "/%s", name);
    }
  else
    {
      snprintf(p, len, "%s/%s", parent, name);
    }

  return p;
}

static void hl_setcontext(fuse_req_t req, FAR struct fuse *f)
{
  FAR const struct fuse_ctx *ctx = fuse_req_ctx(req);

  g_fuse_context.fuse         = f;
  g_fuse_context.private_data = f->private_data;
  g_fuse_context.uid          = ctx->uid;
  g_fuse_context.gid          = ctx->gid;
  g_fuse_context.pid          = ctx->pid;
  g_fuse_context.umask        = ctx->umask;
}

/****************************************************************************
 * Private Functions: high level operations (built-in low level ops)
 ****************************************************************************/

static void hl_init(FAR void *userdata, FAR struct fuse_conn_info *conn)
{
  FAR struct fuse *f = userdata;

  if (f->op.init != NULL)
    {
      struct fuse_config cfg;
      FAR void *pd;

      memset(&cfg, 0, sizeof(cfg));
      pd = f->op.init(conn, &cfg);
      if (pd != NULL)
        {
          f->private_data = pd;
        }
    }
}

static void hl_destroy(FAR void *userdata)
{
  FAR struct fuse *f = userdata;

  if (f->op.destroy != NULL)
    {
      f->op.destroy(f->private_data);
    }
}

static void hl_lookup(fuse_req_t req, uint64_t parent, FAR const char *name)
{
  FAR struct fuse *f = fuse_req_userdata(req);
  FAR const char *ppath;
  FAR struct fuse_node *node;
  struct fuse_entry_param e;
  struct stat st;
  FAR char *cpath;
  int ret;

  hl_setcontext(req, f);

  ppath = node_path(f, parent);
  if (ppath == NULL)
    {
      fuse_reply_err(req, ENOENT);
      return;
    }

  cpath = build_path(ppath, name);
  if (cpath == NULL)
    {
      fuse_reply_err(req, ENOMEM);
      return;
    }

  ret = f->op.getattr != NULL ? f->op.getattr(cpath, &st, NULL) : -ENOSYS;
  if (ret < 0)
    {
      free(cpath);
      fuse_reply_err(req, -ret);
      return;
    }

  node = node_get(f, cpath);
  free(cpath);
  if (node == NULL)
    {
      fuse_reply_err(req, ENOMEM);
      return;
    }

  memset(&e, 0, sizeof(e));
  e.ino           = node->nodeid;
  e.generation    = 1;
  e.attr          = st;
  e.attr_timeout  = 1.0;
  e.entry_timeout = 1.0;
  fuse_reply_entry(req, &e);
}

static void hl_forget(fuse_req_t req, uint64_t ino, uint64_t nlookup)
{
  FAR struct fuse *f = fuse_req_userdata(req);

  node_forget(f, ino, nlookup);
  fuse_reply_none(req);
}

static void hl_getattr(fuse_req_t req, uint64_t ino,
                       FAR struct fuse_file_info *fi)
{
  FAR struct fuse *f = fuse_req_userdata(req);
  FAR const char *path;
  struct stat st;
  int ret;

  hl_setcontext(req, f);

  path = node_path(f, ino);
  if (path == NULL)
    {
      fuse_reply_err(req, ENOENT);
      return;
    }

  memset(&st, 0, sizeof(st));
  ret = f->op.getattr != NULL ? f->op.getattr(path, &st, fi) : -ENOSYS;
  if (ret < 0)
    {
      fuse_reply_err(req, -ret);
    }
  else
    {
      fuse_reply_attr(req, &st, 1.0);
    }
}

static void hl_setattr(fuse_req_t req, uint64_t ino, FAR struct stat *attr,
                       int to_set, FAR struct fuse_file_info *fi)
{
  FAR struct fuse *f = fuse_req_userdata(req);
  FAR const char *path;
  struct stat st;
  int ret = 0;

  hl_setcontext(req, f);

  path = node_path(f, ino);
  if (path == NULL)
    {
      fuse_reply_err(req, ENOENT);
      return;
    }

  if ((to_set & FATTR_SIZE) != 0 && f->op.truncate != NULL)
    {
      ret = f->op.truncate(path, attr->st_size, fi);
    }

  if (ret >= 0 && (to_set & FATTR_MODE) != 0 && f->op.chmod != NULL)
    {
      ret = f->op.chmod(path, attr->st_mode, fi);
    }

  if (ret >= 0 && (to_set & (FATTR_UID | FATTR_GID)) != 0 &&
      f->op.chown != NULL)
    {
      ret = f->op.chown(path, attr->st_uid, attr->st_gid, fi);
    }

  if (ret >= 0 && (to_set & (FATTR_ATIME | FATTR_MTIME)) != 0 &&
      f->op.utimens != NULL)
    {
      struct timespec tv[2];

      tv[0].tv_sec  = attr->st_atim.tv_sec;
      tv[0].tv_nsec = attr->st_atim.tv_nsec;
      tv[1].tv_sec  = attr->st_mtim.tv_sec;
      tv[1].tv_nsec = attr->st_mtim.tv_nsec;
      ret = f->op.utimens(path, tv, fi);
    }

  if (ret < 0)
    {
      fuse_reply_err(req, -ret);
      return;
    }

  memset(&st, 0, sizeof(st));
  ret = f->op.getattr != NULL ? f->op.getattr(path, &st, fi) : -ENOSYS;
  if (ret < 0)
    {
      fuse_reply_err(req, -ret);
    }
  else
    {
      fuse_reply_attr(req, &st, 1.0);
    }
}

static void hl_open(fuse_req_t req, uint64_t ino,
                    FAR struct fuse_file_info *fi)
{
  FAR struct fuse *f = fuse_req_userdata(req);
  FAR const char *path;
  int ret = 0;

  hl_setcontext(req, f);

  path = node_path(f, ino);
  if (path == NULL)
    {
      fuse_reply_err(req, ENOENT);
      return;
    }

  if (f->op.open != NULL)
    {
      ret = f->op.open(path, fi);
    }

  if (ret < 0)
    {
      fuse_reply_err(req, -ret);
    }
  else
    {
      fuse_reply_open(req, fi);
    }
}

static void hl_read(fuse_req_t req, uint64_t ino, size_t size, off_t off,
                    FAR struct fuse_file_info *fi)
{
  FAR struct fuse *f = fuse_req_userdata(req);
  FAR const char *path;
  FAR char *buf;
  int ret;

  hl_setcontext(req, f);

  path = node_path(f, ino);
  if (path == NULL)
    {
      fuse_reply_err(req, ENOENT);
      return;
    }

  buf = malloc(size);
  if (buf == NULL)
    {
      fuse_reply_err(req, ENOMEM);
      return;
    }

  ret = f->op.read != NULL ? f->op.read(path, buf, size, off, fi) : -ENOSYS;
  if (ret < 0)
    {
      fuse_reply_err(req, -ret);
    }
  else
    {
      fuse_reply_buf(req, buf, ret);
    }

  free(buf);
}

static void hl_write(fuse_req_t req, uint64_t ino, FAR const char *buf,
                     size_t size, off_t off, FAR struct fuse_file_info *fi)
{
  FAR struct fuse *f = fuse_req_userdata(req);
  FAR const char *path;
  int ret;

  hl_setcontext(req, f);

  path = node_path(f, ino);
  if (path == NULL)
    {
      fuse_reply_err(req, ENOENT);
      return;
    }

  ret = f->op.write != NULL ?
        f->op.write(path, buf, size, off, fi) : -ENOSYS;
  if (ret < 0)
    {
      fuse_reply_err(req, -ret);
    }
  else
    {
      fuse_reply_write(req, ret);
    }
}

static void hl_flush(fuse_req_t req, uint64_t ino,
                     FAR struct fuse_file_info *fi)
{
  FAR struct fuse *f = fuse_req_userdata(req);
  FAR const char *path;
  int ret = 0;

  hl_setcontext(req, f);

  path = node_path(f, ino);
  if (path != NULL && f->op.flush != NULL)
    {
      ret = f->op.flush(path, fi);
    }

  fuse_reply_err(req, ret < 0 ? -ret : 0);
}

static void hl_release(fuse_req_t req, uint64_t ino,
                       FAR struct fuse_file_info *fi)
{
  FAR struct fuse *f = fuse_req_userdata(req);
  FAR const char *path;

  hl_setcontext(req, f);

  path = node_path(f, ino);
  if (path != NULL && f->op.release != NULL)
    {
      f->op.release(path, fi);
    }

  fuse_reply_err(req, 0);
}

static void hl_fsync(fuse_req_t req, uint64_t ino, int datasync,
                     FAR struct fuse_file_info *fi)
{
  FAR struct fuse *f = fuse_req_userdata(req);
  FAR const char *path;
  int ret = 0;

  hl_setcontext(req, f);

  path = node_path(f, ino);
  if (path != NULL && f->op.fsync != NULL)
    {
      ret = f->op.fsync(path, datasync, fi);
    }

  fuse_reply_err(req, ret < 0 ? -ret : 0);
}

static void hl_opendir(fuse_req_t req, uint64_t ino,
                       FAR struct fuse_file_info *fi)
{
  FAR struct fuse *f = fuse_req_userdata(req);
  FAR const char *path;
  int ret = 0;

  hl_setcontext(req, f);

  path = node_path(f, ino);
  if (path == NULL)
    {
      fuse_reply_err(req, ENOENT);
      return;
    }

  if (f->op.opendir != NULL)
    {
      ret = f->op.opendir(path, fi);
    }

  if (ret < 0)
    {
      fuse_reply_err(req, -ret);
    }
  else
    {
      fuse_reply_open(req, fi);
    }
}

static int hl_filler(FAR void *buf, FAR const char *name,
                     FAR const struct stat *stbuf, off_t off,
                     enum fuse_fill_dir_flags flags)
{
  FAR struct fuse_dirbuf *dh = buf;
  off_t entoff;
  size_t entsize;

  UNUSED(flags);

  dh->index++;
  entoff = off != 0 ? off : dh->index;
  if (entoff <= dh->want_off)
    {
      return 0;
    }

  entsize = fuse_add_direntry(dh->req, dh->buf + dh->pos,
                              dh->size - dh->pos, name, stbuf, entoff);
  if (entsize > dh->size - dh->pos)
    {
      return 1;
    }

  dh->pos += entsize;
  return 0;
}

static void hl_readdir(fuse_req_t req, uint64_t ino, size_t size, off_t off,
                       FAR struct fuse_file_info *fi)
{
  FAR struct fuse *f = fuse_req_userdata(req);
  FAR const char *path;
  struct fuse_dirbuf dh;
  int ret;

  hl_setcontext(req, f);

  path = node_path(f, ino);
  if (path == NULL)
    {
      fuse_reply_err(req, ENOENT);
      return;
    }

  dh.req      = req;
  dh.buf      = malloc(size);
  dh.size     = size;
  dh.pos      = 0;
  dh.want_off = off;
  dh.index    = 0;
  if (dh.buf == NULL)
    {
      fuse_reply_err(req, ENOMEM);
      return;
    }

  ret = f->op.readdir != NULL ?
        f->op.readdir(path, &dh, hl_filler, off, fi, 0) : -ENOSYS;
  if (ret < 0)
    {
      fuse_reply_err(req, -ret);
    }
  else
    {
      fuse_reply_buf(req, dh.buf, dh.pos);
    }

  free(dh.buf);
}

static void hl_releasedir(fuse_req_t req, uint64_t ino,
                          FAR struct fuse_file_info *fi)
{
  FAR struct fuse *f = fuse_req_userdata(req);
  FAR const char *path;

  hl_setcontext(req, f);

  path = node_path(f, ino);
  if (path != NULL && f->op.releasedir != NULL)
    {
      f->op.releasedir(path, fi);
    }

  fuse_reply_err(req, 0);
}

static void hl_create(fuse_req_t req, uint64_t parent, FAR const char *name,
                      mode_t mode, FAR struct fuse_file_info *fi)
{
  FAR struct fuse *f = fuse_req_userdata(req);
  FAR const char *ppath;
  FAR struct fuse_node *node;
  struct fuse_entry_param e;
  struct stat st;
  FAR char *cpath;
  int ret;

  hl_setcontext(req, f);

  ppath = node_path(f, parent);
  if (ppath == NULL)
    {
      fuse_reply_err(req, ENOENT);
      return;
    }

  cpath = build_path(ppath, name);
  if (cpath == NULL)
    {
      fuse_reply_err(req, ENOMEM);
      return;
    }

  ret = f->op.create != NULL ? f->op.create(cpath, mode, fi) : -ENOSYS;
  if (ret < 0)
    {
      free(cpath);
      fuse_reply_err(req, -ret);
      return;
    }

  node = node_get(f, cpath);
  if (node == NULL)
    {
      free(cpath);
      fuse_reply_err(req, ENOMEM);
      return;
    }

  memset(&st, 0, sizeof(st));
  if (f->op.getattr != NULL)
    {
      f->op.getattr(cpath, &st, fi);
    }
  else
    {
      st.st_mode = S_IFREG | mode;
    }

  free(cpath);

  memset(&e, 0, sizeof(e));
  e.ino           = node->nodeid;
  e.generation    = 1;
  e.attr          = st;
  e.attr_timeout  = 1.0;
  e.entry_timeout = 1.0;
  fuse_reply_create(req, &e, fi);
}

static void hl_mkdir(fuse_req_t req, uint64_t parent, FAR const char *name,
                     mode_t mode)
{
  FAR struct fuse *f = fuse_req_userdata(req);
  FAR const char *ppath;
  FAR struct fuse_node *node;
  struct fuse_entry_param e;
  struct stat st;
  FAR char *cpath;
  int ret;

  hl_setcontext(req, f);

  ppath = node_path(f, parent);
  if (ppath == NULL)
    {
      fuse_reply_err(req, ENOENT);
      return;
    }

  cpath = build_path(ppath, name);
  if (cpath == NULL)
    {
      fuse_reply_err(req, ENOMEM);
      return;
    }

  ret = f->op.mkdir != NULL ? f->op.mkdir(cpath, mode) : -ENOSYS;
  if (ret < 0)
    {
      free(cpath);
      fuse_reply_err(req, -ret);
      return;
    }

  node = node_get(f, cpath);
  if (node == NULL)
    {
      free(cpath);
      fuse_reply_err(req, ENOMEM);
      return;
    }

  memset(&st, 0, sizeof(st));
  if (f->op.getattr != NULL)
    {
      f->op.getattr(cpath, &st, NULL);
    }
  else
    {
      st.st_mode = S_IFDIR | mode;
    }

  free(cpath);

  memset(&e, 0, sizeof(e));
  e.ino           = node->nodeid;
  e.generation    = 1;
  e.attr          = st;
  e.attr_timeout  = 1.0;
  e.entry_timeout = 1.0;
  fuse_reply_entry(req, &e);
}

static void hl_remove(fuse_req_t req, uint64_t parent, FAR const char *name,
                      bool isdir)
{
  FAR struct fuse *f = fuse_req_userdata(req);
  FAR struct fuse_node *node;
  FAR const char *ppath;
  FAR char *cpath;
  int ret;

  hl_setcontext(req, f);

  ppath = node_path(f, parent);
  if (ppath == NULL)
    {
      fuse_reply_err(req, ENOENT);
      return;
    }

  cpath = build_path(ppath, name);
  if (cpath == NULL)
    {
      fuse_reply_err(req, ENOMEM);
      return;
    }

  if (isdir)
    {
      ret = f->op.rmdir != NULL ? f->op.rmdir(cpath) : -ENOSYS;
    }
  else
    {
      ret = f->op.unlink != NULL ? f->op.unlink(cpath) : -ENOSYS;
    }

  if (ret >= 0)
    {
      for (node = f->nodes; node != NULL; node = node->next)
        {
          if (strcmp(node->path, cpath) == 0)
            {
              node_forget(f, node->nodeid, node->refs);
              break;
            }
        }
    }

  free(cpath);
  fuse_reply_err(req, ret < 0 ? -ret : 0);
}

static void hl_unlink(fuse_req_t req, uint64_t parent, FAR const char *name)
{
  hl_remove(req, parent, name, false);
}

static void hl_rmdir(fuse_req_t req, uint64_t parent, FAR const char *name)
{
  hl_remove(req, parent, name, true);
}

static void hl_rename(fuse_req_t req, uint64_t parent, FAR const char *name,
                      uint64_t newparent, FAR const char *newname,
                      unsigned int flags)
{
  FAR struct fuse *f = fuse_req_userdata(req);
  FAR struct fuse_node *node;
  FAR const char *oppath;
  FAR const char *nppath;
  FAR char *oldpath;
  FAR char *newpath;
  int ret;

  hl_setcontext(req, f);

  oppath = node_path(f, parent);
  nppath = node_path(f, newparent);
  if (oppath == NULL || nppath == NULL)
    {
      fuse_reply_err(req, ENOENT);
      return;
    }

  oldpath = build_path(oppath, name);
  newpath = build_path(nppath, newname);
  if (oldpath == NULL || newpath == NULL)
    {
      free(oldpath);
      free(newpath);
      fuse_reply_err(req, ENOMEM);
      return;
    }

  ret = f->op.rename != NULL ?
        f->op.rename(oldpath, newpath, flags) : -ENOSYS;
  if (ret >= 0)
    {
      for (node = f->nodes; node != NULL; node = node->next)
        {
          if (strcmp(node->path, oldpath) == 0)
            {
              FAR char *dup = strdup(newpath);
              if (dup != NULL)
                {
                  free(node->path);
                  node->path = dup;
                }

              break;
            }
        }
    }

  free(oldpath);
  free(newpath);
  fuse_reply_err(req, ret < 0 ? -ret : 0);
}

static void hl_statfs(fuse_req_t req, uint64_t ino)
{
  FAR struct fuse *f = fuse_req_userdata(req);
  FAR const char *path;
  struct statvfs sv;
  int ret;

  hl_setcontext(req, f);

  path = node_path(f, ino);
  if (path == NULL)
    {
      path = "/";
    }

  memset(&sv, 0, sizeof(sv));
  ret = f->op.statfs != NULL ? f->op.statfs(path, &sv) : -ENOSYS;
  if (ret < 0)
    {
      fuse_reply_err(req, -ret);
    }
  else
    {
      fuse_reply_statfs(req, &sv);
    }
}

static const struct fuse_lowlevel_ops g_fuse_hl_ops =
{
  hl_init,        /* init */
  hl_destroy,     /* destroy */
  hl_lookup,      /* lookup */
  hl_forget,      /* forget */
  hl_getattr,     /* getattr */
  hl_setattr,     /* setattr */
  NULL,           /* readlink */
  hl_mkdir,       /* mkdir */
  hl_unlink,      /* unlink */
  hl_rmdir,       /* rmdir */
  NULL,           /* symlink */
  hl_rename,      /* rename */
  NULL,           /* link */
  hl_open,        /* open */
  hl_read,        /* read */
  hl_write,       /* write */
  hl_flush,       /* flush */
  hl_release,     /* release */
  hl_fsync,       /* fsync */
  hl_opendir,     /* opendir */
  hl_readdir,     /* readdir */
  hl_releasedir,  /* releasedir */
  hl_statfs,      /* statfs */
  hl_create       /* create */
};

/****************************************************************************
 * Private Functions: session request dispatch
 ****************************************************************************/

static void fuse_do_init(FAR struct fuse_session *se,
                         FAR struct fuse_in_header *ih, FAR void *body)
{
  FAR struct fuse_init_in *in = body;
  struct fuse_init_out out;

  memset(&out, 0, sizeof(out));
  out.major         = FUSE_KERNEL_VERSION;
  out.minor         = FUSE_KERNEL_MINOR_VERSION;
  out.max_readahead = in->max_readahead;
  out.max_write     = se->max_write;

  if (se->op->init != NULL)
    {
      struct fuse_conn_info conn;

      memset(&conn, 0, sizeof(conn));
      conn.proto_major   = in->major;
      conn.proto_minor   = in->minor;
      conn.max_write     = se->max_write;
      conn.max_readahead = in->max_readahead;
      se->op->init(se->userdata, &conn);
      se->max_write  = conn.max_write != 0 ? conn.max_write : se->max_write;
      out.max_write  = se->max_write;
    }

  fuse_send(se, ih->unique, 0, &out, sizeof(out));
}

static void fuse_dispatch(FAR struct fuse_session *se, size_t len)
{
  FAR struct fuse_in_header *ih = (FAR struct fuse_in_header *)se->buf;
  FAR void *body = se->buf + sizeof(*ih);
  FAR const struct fuse_lowlevel_ops *op = se->op;
  struct fuse_req reqobj;
  fuse_req_t req = &reqobj;
  uint64_t nodeid = ih->nodeid;
  struct fuse_file_info fi;

  if (len < sizeof(*ih))
    {
      return;
    }

  reqobj.se         = se;
  reqobj.unique     = ih->unique;
  reqobj.ctx.uid    = ih->uid;
  reqobj.ctx.gid    = ih->gid;
  reqobj.ctx.pid    = ih->pid;
  reqobj.ctx.umask  = 0;

  memset(&fi, 0, sizeof(fi));

  switch (ih->opcode)
    {
      case FUSE_INIT:
        fuse_do_init(se, ih, body);
        break;

      case FUSE_DESTROY:
        if (op->destroy != NULL)
          {
            op->destroy(se->userdata);
          }

        fuse_reply_err(req, 0);
        break;

      case FUSE_LOOKUP:
        op->lookup(req, nodeid, (FAR const char *)body);
        break;

      case FUSE_FORGET:
        {
          FAR struct fuse_forget_in *in = body;
          op->forget(req, nodeid, in->nlookup);
        }
        break;

      case FUSE_GETATTR:
        op->getattr(req, nodeid, NULL);
        break;

      case FUSE_SETATTR:
        {
          FAR struct fuse_setattr_in *in = body;
          struct stat st;

          memset(&st, 0, sizeof(st));
          st.st_mode          = in->mode;
          st.st_uid           = in->uid;
          st.st_gid           = in->gid;
          st.st_size          = in->size;
          st.st_atim.tv_sec   = in->atime;
          st.st_atim.tv_nsec  = in->atimensec;
          st.st_mtim.tv_sec   = in->mtime;
          st.st_mtim.tv_nsec  = in->mtimensec;
          if ((in->valid & FATTR_FH) != 0)
            {
              fi.fh = in->fh;
              op->setattr(req, nodeid, &st, in->valid, &fi);
            }
          else
            {
              op->setattr(req, nodeid, &st, in->valid, NULL);
            }
        }
        break;

      case FUSE_OPEN:
        {
          FAR struct fuse_open_in *in = body;
          fi.flags = in->flags;
          op->open(req, nodeid, &fi);
        }
        break;

      case FUSE_READ:
        {
          FAR struct fuse_read_in *in = body;
          fi.fh = in->fh;
          op->read(req, nodeid, in->size, in->offset, &fi);
        }
        break;

      case FUSE_WRITE:
        {
          FAR struct fuse_write_in *in = body;
          fi.fh = in->fh;
          op->write(req, nodeid, (FAR const char *)body + sizeof(*in),
                    in->size, in->offset, &fi);
        }
        break;

      case FUSE_FLUSH:
        {
          FAR struct fuse_flush_in *in = body;
          fi.fh = in->fh;
          op->flush(req, nodeid, &fi);
        }
        break;

      case FUSE_RELEASE:
        {
          FAR struct fuse_release_in *in = body;
          fi.fh = in->fh;
          op->release(req, nodeid, &fi);
        }
        break;

      case FUSE_FSYNC:
        {
          FAR struct fuse_fsync_in *in = body;
          fi.fh = in->fh;
          op->fsync(req, nodeid, in->fsync_flags & 1, &fi);
        }
        break;

      case FUSE_OPENDIR:
        {
          FAR struct fuse_open_in *in = body;
          fi.flags = in->flags;
          op->opendir(req, nodeid, &fi);
        }
        break;

      case FUSE_READDIR:
        {
          FAR struct fuse_read_in *in = body;
          fi.fh = in->fh;
          op->readdir(req, nodeid, in->size, in->offset, &fi);
        }
        break;

      case FUSE_RELEASEDIR:
        {
          FAR struct fuse_release_in *in = body;
          fi.fh = in->fh;
          op->releasedir(req, nodeid, &fi);
        }
        break;

      case FUSE_CREATE:
        {
          FAR struct fuse_create_in *in = body;
          fi.flags = in->flags;
          op->create(req, nodeid, (FAR const char *)body + sizeof(*in),
                     in->mode, &fi);
        }
        break;

      case FUSE_MKDIR:
        {
          FAR struct fuse_mkdir_in *in = body;
          op->mkdir(req, nodeid, (FAR const char *)body + sizeof(*in),
                    in->mode);
        }
        break;

      case FUSE_UNLINK:
        op->unlink(req, nodeid, (FAR const char *)body);
        break;

      case FUSE_RMDIR:
        op->rmdir(req, nodeid, (FAR const char *)body);
        break;

      case FUSE_RENAME:
        {
          FAR struct fuse_rename_in *in = body;
          FAR const char *oldname = (FAR const char *)body + sizeof(*in);
          FAR const char *newname = oldname + strlen(oldname) + 1;
          op->rename(req, nodeid, oldname, in->newdir, newname, 0);
        }
        break;

      case FUSE_STATFS:
        op->statfs(req, nodeid);
        break;

      default:
        fuse_reply_err(req, ENOSYS);
        break;
    }
}

/****************************************************************************
 * Public Functions: session
 ****************************************************************************/

FAR struct fuse_session *
fuse_session_new(FAR struct fuse_args *args,
                 FAR const struct fuse_lowlevel_ops *op, size_t op_size,
                 FAR void *userdata)
{
  FAR struct fuse_session *se;

  UNUSED(args);
  UNUSED(op_size);

  se = calloc(1, sizeof(*se));
  if (se == NULL)
    {
      return NULL;
    }

  se->fd        = -1;
  se->op        = op;
  se->userdata  = userdata;
  se->max_write = FUSE_LL_MAX_WRITE;
  se->bufsize   = FUSE_LL_BUFSIZE;
  se->buf       = malloc(se->bufsize);
  if (se->buf == NULL)
    {
      free(se);
      return NULL;
    }

  return se;
}

int fuse_session_mount(FAR struct fuse_session *se,
                       FAR const char *mountpoint)
{
  char options[32];
  int ret;

  se->fd = open("/dev/fuse", O_RDWR);
  if (se->fd < 0)
    {
      return -errno;
    }

  snprintf(options, sizeof(options), "fd=%d", se->fd);
  ret = mount(NULL, mountpoint, "fuse", 0, options);
  if (ret < 0)
    {
      ret = -errno;
      close(se->fd);
      se->fd = -1;
      return ret;
    }

  se->mountpoint = strdup(mountpoint);
  return 0;
}

int fuse_session_loop(FAR struct fuse_session *se)
{
  ssize_t n;

  while (!se->exited)
    {
      n = read(se->fd, se->buf, se->bufsize);
      if (n < 0)
        {
          if (errno == EINTR)
            {
              continue;
            }

          return -errno;
        }

      if (n == 0)
        {
          break;
        }

      fuse_dispatch(se, (size_t)n);
    }

  return 0;
}

void fuse_session_exit(FAR struct fuse_session *se)
{
  se->exited = true;
}

void fuse_session_unmount(FAR struct fuse_session *se)
{
  if (se->mountpoint != NULL)
    {
      umount(se->mountpoint);
      free(se->mountpoint);
      se->mountpoint = NULL;
    }

  if (se->fd >= 0)
    {
      close(se->fd);
      se->fd = -1;
    }
}

void fuse_session_destroy(FAR struct fuse_session *se)
{
  free(se->buf);
  free(se);
}

/****************************************************************************
 * Public Functions: high level
 ****************************************************************************/

FAR struct fuse *fuse_new(FAR struct fuse_args *args,
                          FAR const struct fuse_operations *op,
                          size_t op_size, FAR void *private_data)
{
  FAR struct fuse *f;
  FAR struct fuse_node *root;

  f = calloc(1, sizeof(*f));
  if (f == NULL)
    {
      return NULL;
    }

  memcpy(&f->op, op, op_size < sizeof(f->op) ? op_size : sizeof(f->op));
  f->private_data = private_data;
  f->next_nodeid  = FUSE_ROOT_ID + 1;

  root = malloc(sizeof(*root));
  if (root == NULL)
    {
      free(f);
      return NULL;
    }

  root->nodeid = FUSE_ROOT_ID;
  root->refs   = 1;
  root->path   = strdup("/");
  root->next   = NULL;
  f->nodes     = root;

  f->se = fuse_session_new(args, &g_fuse_hl_ops, sizeof(g_fuse_hl_ops), f);
  if (f->se == NULL)
    {
      free(root->path);
      free(root);
      free(f);
      return NULL;
    }

  return f;
}

int fuse_mount(FAR struct fuse *f, FAR const char *mountpoint)
{
  return fuse_session_mount(f->se, mountpoint);
}

void fuse_unmount(FAR struct fuse *f)
{
  fuse_session_unmount(f->se);
}

int fuse_loop(FAR struct fuse *f)
{
  return fuse_session_loop(f->se);
}

void fuse_destroy(FAR struct fuse *f)
{
  FAR struct fuse_node *n;
  FAR struct fuse_node *next;

  for (n = f->nodes; n != NULL; n = next)
    {
      next = n->next;
      free(n->path);
      free(n);
    }

  fuse_session_destroy(f->se);
  free(f);
}

FAR struct fuse_context *fuse_get_context(void)
{
  return &g_fuse_context;
}

int fuse_main_real(int argc, FAR char *argv[],
                   FAR const struct fuse_operations *op, size_t op_size,
                   FAR void *private_data)
{
  struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
  FAR struct fuse *f;
  FAR const char *mountpoint = NULL;
  int ret;
  int i;

  /* The mountpoint is the last non-option argument */

  for (i = 1; i < argc; i++)
    {
      if (argv[i][0] != '-')
        {
          mountpoint = argv[i];
        }
    }

  if (mountpoint == NULL)
    {
      return -EINVAL;
    }

  f = fuse_new(&args, op, op_size, private_data);
  if (f == NULL)
    {
      return -ENOMEM;
    }

  ret = fuse_mount(f, mountpoint);
  if (ret < 0)
    {
      fuse_destroy(f);
      return ret;
    }

  ret = fuse_loop(f);

  fuse_unmount(f);
  fuse_destroy(f);
  return ret;
}

/****************************************************************************
 * Public Functions: fuse_opt
 ****************************************************************************/

int fuse_opt_add_arg(FAR struct fuse_args *args, FAR const char *arg)
{
  FAR char **argv;
  FAR char *dup;

  dup = strdup(arg);
  if (dup == NULL)
    {
      return -ENOMEM;
    }

  argv = realloc(args->argv, (args->argc + 2) * sizeof(FAR char *));
  if (argv == NULL)
    {
      free(dup);
      return -ENOMEM;
    }

  argv[args->argc++] = dup;
  argv[args->argc]   = NULL;
  args->argv         = argv;
  args->allocated    = 1;
  return 0;
}

void fuse_opt_free_args(FAR struct fuse_args *args)
{
  int i;

  if (args == NULL)
    {
      return;
    }

  if (args->allocated && args->argv != NULL)
    {
      for (i = 0; i < args->argc; i++)
        {
          free(args->argv[i]);
        }

      free(args->argv);
    }

  args->argv      = NULL;
  args->argc      = 0;
  args->allocated = 0;
}
