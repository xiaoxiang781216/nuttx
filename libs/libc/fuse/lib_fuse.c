/****************************************************************************
 * libs/libc/fuse/lib_fuse.c
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
#include <unistd.h>
#include <fuse.h>
#include <fuse_lowlevel.h>

#include <nuttx/lib/lib.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define PACKAGE_VERSION "3.19.0-rc0"

/****************************************************************************
 * Private Type Definitions
 ****************************************************************************/

struct node
{
  FAR struct node *next;
  fuse_ino_t nodeid;
  FAR char *path;
  uint64_t nlookup;
};

struct fuse
{
  FAR struct fuse_session *se;
  FAR struct node *nodes;
  fuse_ino_t ctr;
  struct fuse_config conf;
  struct fuse_operations op;
  FAR void *private_data;
};

struct fuse_dh
{
  fuse_req_t req;
  FAR char *buf;
  size_t size;
  size_t pos;
  off_t want_off;
  off_t index;
  bool cookie_mode;
};

/****************************************************************************
 * High level bridge: nodeid to path table
 ****************************************************************************/

/* Forward declarations for the high level bridge */

static FAR const char *node_path(FAR struct fuse *f, fuse_ino_t id);

/* Publish the request context in tls_info_s::tl_fuse before entering a
 * fuse_operations callback (upstream: req_fuse_prepare).
 */

static FAR struct fuse *hl_prepare(fuse_req_t req)
{
  FAR struct fuse *f = req->se->userdata;
  FAR struct tls_info_s *tls = tls_get_info();

  if (tls != NULL)
    {
      FAR const struct fuse_ctx *ctx = fuse_req_ctx(req);

      tls->tl_fuse.fuse         = f;
      tls->tl_fuse.uid          = ctx->uid;
      tls->tl_fuse.gid          = ctx->gid;
      tls->tl_fuse.pid          = ctx->pid;
      tls->tl_fuse.private_data = f->private_data;
      tls->tl_fuse.umask        = ctx->umask;
    }

  return f;
}

/* High-level operations report errors by returning -errno (a positive
 * or zero return means success); translate to the errno value the
 * reply helpers expect, EIO for unexpected positive codes.
 */

static int hl_retcode(int ret)
{
  if (ret == 0)
    {
      return 0;
    }

  if (ret < 0)
    {
      return -ret;
    }

  return EIO;
}

static FAR struct node *node_find(FAR struct fuse *f, fuse_ino_t id)
{
  FAR struct node *n;

  for (n = f->nodes; n != NULL; n = n->next)
    {
      if (n->nodeid == id)
        {
          return n;
        }
    }

  return NULL;
}

static FAR const char *node_path(FAR struct fuse *f, fuse_ino_t id)
{
  FAR struct node *n;

  if (id == FUSE_ROOT_ID)
    {
      return "/";
    }

  n = node_find(f, id);
  return n != NULL ? n->path : NULL;
}

/* Build the full path of (parent, name) into buf; high-level callbacks
 * receive full paths, never bare names.  Returns NULL when the buffer
 * is too small or the parent is unknown.
 */

static FAR const char *node_join(FAR struct fuse *f, fuse_ino_t parent,
                                  FAR const char *name,
                                  FAR char *buf, size_t buflen)
{
  FAR const char *pp;
  size_t len;

  if (parent == FUSE_ROOT_ID)
    {
      pp = "/";
    }
  else
    {
      pp = node_path(f, parent);
      if (pp == NULL)
        {
          return NULL;
        }
    }

  len = strlen(pp) + strlen(name) + 2;
  if (len > buflen)
    {
      return NULL;
    }

  if (pp[strlen(pp) - 1] == '/')
    {
      snprintf(buf, buflen, "%s%s", pp, name);
    }
  else
    {
      snprintf(buf, buflen, "%s/%s", pp, name);
    }

  return buf;
}

static FAR struct node *node_get(FAR struct fuse *f, fuse_ino_t parent,
                                 FAR const char *name)
{
  FAR struct node *n;
  FAR char *path;
  size_t len;

  if (parent == FUSE_ROOT_ID)
    {
      if (name[0] == '\0')
        {
          return NULL;
        }

      len = strlen(name) + 2;
      path = lib_malloc(len);
      if (path == NULL)
        {
          return NULL;
        }

      snprintf(path, len, "/%s", name);
    }
  else
    {
      FAR const char *pp = node_path(f, parent);

      if (pp == NULL)
        {
          return NULL;
        }

      len = strlen(pp) + strlen(name) + 2;
      path = lib_malloc(len);
      if (path == NULL)
        {
          return NULL;
        }

      snprintf(path, len, "%s/%s", pp, name);
    }

  n = lib_malloc(sizeof(*n));
  if (n == NULL)
    {
      lib_free(path);
      return NULL;
    }

  n->nodeid = ++f->ctr;
  n->nlookup = 0;
  n->path   = path;
  n->next   = f->nodes;
  f->nodes  = n;
  return n;
}

static void node_forget(FAR struct fuse *f, fuse_ino_t id,
                        uint64_t nlookup)
{
  FAR struct node *n;
  FAR struct node *prev = NULL;

  for (n = f->nodes; n != NULL; prev = n, n = n->next)
    {
      if (n->nodeid == id)
        {
          break;
        }
    }

  if (n == NULL)
    {
      return;
    }

  if (n->nlookup >= nlookup)
    {
      n->nlookup -= nlookup;
    }
  else
    {
      n->nlookup = 0;
    }

  if (n->nlookup == 0)
    {
      if (prev == NULL)
        {
          f->nodes = n->next;
        }
      else
        {
          prev->next = n->next;
        }

      lib_free(n->path);
      lib_free(n);
    }
}

/* Drop the node cache entry for a path (after unlink/rmdir).  Children
 * of a removed directory are dropped as well; the server would otherwise
 * answer lookups for them from a stale cache.
 */

static void node_forget_path(FAR struct fuse *f, FAR const char *path)
{
  FAR struct node *n;
  FAR struct node *prev = NULL;
  size_t plen = strlen(path);

  for (n = f->nodes; n != NULL; )
    {
      bool drop = strcmp(n->path, path) == 0 ||
                  (strncmp(n->path, path, plen) == 0 &&
                   n->path[plen] == '/');

      if (drop)
        {
          FAR struct node *next = n->next;

          if (prev == NULL)
            {
              f->nodes = next;
            }
          else
            {
              prev->next = next;
            }

          lib_free(n->path);
          lib_free(n);
          n = next;
        }
      else
        {
          prev = n;
          n = n->next;
        }
    }
}

/* Update the node cache after a successful rename: re-path the node and
 * every descendant.
 */

static void node_rename_path(FAR struct fuse *f, FAR const char *oldpath,
                             FAR const char *newpath)
{
  FAR struct node *n;
  size_t olen = strlen(oldpath);

  for (n = f->nodes; n != NULL; n = n->next)
    {
      if (strcmp(n->path, oldpath) == 0)
        {
          FAR char *p = strdup(newpath);

          if (p != NULL)
            {
              lib_free(n->path);
              n->path = p;
            }
        }
      else if (strncmp(n->path, oldpath, olen) == 0 &&
               n->path[olen] == '/')
        {
          char joined[PATH_MAX];

          if (snprintf(joined, sizeof(joined), "%s%s", newpath,
                       n->path + olen) < (int)sizeof(joined))
            {
              FAR char *p = strdup(joined);

              if (p != NULL)
                {
                  lib_free(n->path);
                  n->path = p;
                }
            }
        }
    }
}

/****************************************************************************
 * High level bridge: readdir filler
 ****************************************************************************/

static int hl_filler(FAR void *buf, FAR const char *name,
                     FAR const struct stat *st, off_t off,
                     enum fuse_fill_dir_flags flags)
{
  FAR struct fuse_dh *db = buf;
  struct stat dummy;
  size_t len;

  UNUSED(flags);

  /* Callers may pass a NULL stat: synthesize a minimal regular-file
   * one so the entry still reaches the kernel.
   */

  if (st == NULL)
    {
      memset(&dummy, 0, sizeof(dummy));
      dummy.st_ino   = 0;
      dummy.st_mode  = S_IFREG;
      st = &dummy;
    }

  if (db->cookie_mode || off != 0)
    {
      /* Cookie mode: the callback seeks by offset, so entries arrive
       * from the right position - collect from here on.  One callback
       * must not mix both modes.
       */

      db->cookie_mode = true;
      len = fuse_add_direntry(db->req, db->buf + db->pos,
                              db->size - db->pos, name, st, off);
    }
  else if (db->want_off != 0 && db->index != db->want_off)
    {
      /* Legacy mode: the callback always fills from the start, skip
       * the prefix below the requested offset.
       */

      db->index++;
      return 0;
    }
  else
    {
      db->index++;
      len = fuse_add_direntry(db->req, db->buf + db->pos,
                              db->size - db->pos, name, st, db->index);
    }

  if (len == 0)
    {
      return 1;
    }

  db->pos += len;
  return 0;
}

/****************************************************************************
 * High level bridge: lowlevel ops
 ****************************************************************************/

static void hl_init(FAR void *userdata, FAR struct fuse_conn_info *conn)
{
  FAR struct fuse *f = userdata;
  FAR struct tls_info_s *tls = tls_get_info();
  FAR void *ret = NULL;

  if (tls != NULL)
    {
      tls->tl_fuse.fuse         = f;
      tls->tl_fuse.private_data = f->private_data;
    }

  if (f->op.init != NULL)
    {
      ret = f->op.init(conn, &f->conf);
      if (ret != NULL)
        {
          f->private_data = ret;

          if (tls != NULL)
            {
              tls->tl_fuse.private_data = ret;
            }
        }
    }
}

static void hl_destroy(FAR void *userdata)
{
  FAR struct fuse *f = userdata;
  FAR struct tls_info_s *tls = tls_get_info();

  if (tls != NULL)
    {
      tls->tl_fuse.fuse = f;
    }

  if (f->op.destroy != NULL)
    {
      f->op.destroy(f->private_data);
    }
}

static void hl_lookup(fuse_req_t req, fuse_ino_t parent,
                      FAR const char *name)
{
  FAR struct fuse *f = hl_prepare(req);
  struct fuse_entry_param e;
  FAR struct node *n;
  char pathbuf[PATH_MAX];
  FAR const char *path;

  memset(&e, 0, sizeof(e));
  e.attr_timeout  = 1.0;
  e.entry_timeout = 1.0;

  path = node_join(f, parent, name, pathbuf, sizeof(pathbuf));
  if (path != NULL && f->op.getattr != NULL &&
      f->op.getattr(path, &e.attr, NULL) == 0)
    {
      n = node_get(f, parent, name);
      if (n != NULL)
        {
          n->nlookup++;
          e.ino = n->nodeid;
          fuse_reply_entry(req, &e);
          return;
        }

      fuse_reply_err(req, ENOMEM);
      return;
    }

  fuse_reply_err(req, ENOENT);
}

static void hl_forget(fuse_req_t req, fuse_ino_t ino, uint64_t nlookup)
{
  node_forget(hl_prepare(req), ino, nlookup);
}

static void hl_getattr(fuse_req_t req, fuse_ino_t ino,
                       FAR struct fuse_file_info *fi)
{
  FAR struct fuse *f = hl_prepare(req);
  FAR const char *path = node_path(f, ino);
  struct stat st;
  int ret;

  if (path == NULL || f->op.getattr == NULL)
    {
      fuse_reply_err(req, ENOENT);
      return;
    }

  ret = f->op.getattr(path, &st, fi);
  if (ret != 0)
    {
      fuse_reply_err(req, hl_retcode(ret));
      return;
    }

  st.st_ino = ino;
  fuse_reply_attr(req, &st, 1.0);
}

static void hl_setattr(fuse_req_t req, fuse_ino_t ino,
                       FAR struct stat *attr, int to_set,
                       FAR struct fuse_file_info *fi)
{
  FAR struct fuse *f = hl_prepare(req);
  FAR const char *path;
  struct stat st;
  struct timespec tv[2];
  int err = 0;

  path = node_path(f, ino);

  if (path == NULL)
    {
      fuse_reply_err(req, ENOENT);
      return;
    }

  if ((to_set & FATTR_MODE) != 0 && f->op.chmod != NULL)
    {
      err = f->op.chmod(path, attr->st_mode, fi);
    }

  if (err == 0 && (to_set & (FATTR_UID | FATTR_GID)) != 0 &&
      f->op.chown != NULL)
    {
      err = f->op.chown(path,
                        (to_set & FATTR_UID) != 0 ? attr->st_uid : (uid_t)-1,
                        (to_set & FATTR_GID) != 0 ? attr->st_gid : (gid_t)-1,
                        fi);
    }

  if (err == 0 && (to_set & FATTR_SIZE) != 0 && f->op.truncate != NULL)
    {
      err = f->op.truncate(path, attr->st_size, fi);
    }

  if (err == 0 && (to_set & (FATTR_ATIME | FATTR_MTIME)) != 0 &&
      f->op.utimens != NULL)
    {
      tv[0].tv_sec  = attr->st_atim.tv_sec;
      tv[0].tv_nsec = attr->st_atim.tv_nsec;
      tv[1].tv_sec  = attr->st_mtim.tv_sec;
      tv[1].tv_nsec = attr->st_mtim.tv_nsec;
      err = f->op.utimens(path, tv, fi);
    }

  if (err != 0)
    {
      fuse_reply_err(req, hl_retcode(err));
      return;
    }

  /* Re-read the attributes */

  if (f->op.getattr != NULL && f->op.getattr(path, &st, fi) == 0)
    {
      st.st_ino = ino;
      fuse_reply_attr(req, &st, 1.0);
    }
  else
    {
      fuse_reply_err(req, ENOENT);
    }
}

static void hl_readlink(fuse_req_t req, fuse_ino_t ino)
{
  FAR struct fuse *f = hl_prepare(req);
  FAR const char *path;
  char buf[PATH_MAX];
  int ret;

  path = node_path(f, ino);

  if (path == NULL || f->op.readlink == NULL)
    {
      fuse_reply_err(req, ENOSYS);
      return;
    }

  ret = f->op.readlink(path, buf, sizeof(buf));
  if (ret != 0)
    {
      fuse_reply_err(req, hl_retcode(ret));
      return;
    }

  fuse_reply_readlink(req, buf);
}

static void hl_mknod(fuse_req_t req, fuse_ino_t parent,
                     FAR const char *name, mode_t mode, dev_t rdev)
{
  FAR struct fuse *f = hl_prepare(req);
  char pathbuf[PATH_MAX];
  FAR const char *path;
  int ret;

  if (f->op.mknod == NULL)
    {
      fuse_reply_err(req, ENOSYS);
      return;
    }

  path = node_join(f, parent, name, pathbuf, sizeof(pathbuf));
  if (path == NULL)
    {
      fuse_reply_err(req, ENOENT);
      return;
    }

  ret = f->op.mknod(path, mode, rdev);
  if (ret != 0)
    {
      fuse_reply_err(req, hl_retcode(ret));
      return;
    }

  hl_lookup(req, parent, name);
}

static void hl_mkdir(fuse_req_t req, fuse_ino_t parent,
                     FAR const char *name, mode_t mode)
{
  FAR struct fuse *f = hl_prepare(req);
  char pathbuf[PATH_MAX];
  FAR const char *path;
  int ret;

  if (f->op.mkdir == NULL)
    {
      fuse_reply_err(req, ENOSYS);
      return;
    }

  path = node_join(f, parent, name, pathbuf, sizeof(pathbuf));
  if (path == NULL)
    {
      fuse_reply_err(req, ENOENT);
      return;
    }

  ret = f->op.mkdir(path, mode);
  if (ret != 0)
    {
      fuse_reply_err(req, hl_retcode(ret));
      return;
    }

  hl_lookup(req, parent, name);
}

static void hl_unlink(fuse_req_t req, fuse_ino_t parent,
                      FAR const char *name)
{
  FAR struct fuse *f = hl_prepare(req);
  char pathbuf[PATH_MAX];
  FAR const char *path;
  int ret;

  if (f->op.unlink == NULL)
    {
      fuse_reply_err(req, ENOSYS);
      return;
    }

  path = node_join(f, parent, name, pathbuf, sizeof(pathbuf));
  if (path == NULL)
    {
      fuse_reply_err(req, ENOENT);
      return;
    }

  ret = f->op.unlink(path);
  node_forget_path(f, path);
  fuse_reply_err(req, hl_retcode(ret));
}

static void hl_rmdir(fuse_req_t req, fuse_ino_t parent,
                     FAR const char *name)
{
  FAR struct fuse *f = hl_prepare(req);
  char pathbuf[PATH_MAX];
  FAR const char *path;
  int ret;

  if (f->op.rmdir == NULL)
    {
      fuse_reply_err(req, ENOSYS);
      return;
    }

  path = node_join(f, parent, name, pathbuf, sizeof(pathbuf));
  if (path == NULL)
    {
      fuse_reply_err(req, ENOENT);
      return;
    }

  ret = f->op.rmdir(path);
  node_forget_path(f, path);
  fuse_reply_err(req, hl_retcode(ret));
}

static void hl_symlink(fuse_req_t req, FAR const char *link,
                       fuse_ino_t parent, FAR const char *name)
{
  FAR struct fuse *f = hl_prepare(req);
  char pathbuf[PATH_MAX];
  FAR const char *path;
  int ret;

  if (f->op.symlink == NULL)
    {
      fuse_reply_err(req, ENOSYS);
      return;
    }

  path = node_join(f, parent, name, pathbuf, sizeof(pathbuf));
  if (path == NULL)
    {
      fuse_reply_err(req, ENOENT);
      return;
    }

  ret = f->op.symlink(link, path);
  if (ret != 0)
    {
      fuse_reply_err(req, hl_retcode(ret));
      return;
    }

  hl_lookup(req, parent, name);
}

static void hl_rename(fuse_req_t req, fuse_ino_t parent,
                      FAR const char *name, fuse_ino_t newparent,
                      FAR const char *newname, unsigned int flags)
{
  FAR struct fuse *f = hl_prepare(req);
  char oldbuf[PATH_MAX];
  char newbuf[PATH_MAX];
  FAR const char *oldpath;
  FAR const char *newpath;
  int ret;

  if (f->op.rename == NULL)
    {
      fuse_reply_err(req, ENOSYS);
      return;
    }

  oldpath = node_join(f, parent, name, oldbuf, sizeof(oldbuf));
  newpath = node_join(f, newparent, newname, newbuf, sizeof(newbuf));
  if (oldpath == NULL || newpath == NULL)
    {
      fuse_reply_err(req, ENOENT);
      return;
    }

  ret = f->op.rename(oldpath, newpath, flags);
  if (ret == 0)
    {
      node_rename_path(f, oldpath, newpath);
    }

  fuse_reply_err(req, hl_retcode(ret));
}

static void hl_link(fuse_req_t req, fuse_ino_t ino, fuse_ino_t newparent,
                    FAR const char *newname)
{
  FAR struct fuse *f = hl_prepare(req);
  char newbuf[PATH_MAX];
  FAR const char *oldpath;
  FAR const char *newpath;
  int ret;

  oldpath = node_path(f, ino);

  if (oldpath == NULL || f->op.link == NULL)
    {
      fuse_reply_err(req, ENOSYS);
      return;
    }

  newpath = node_join(f, newparent, newname, newbuf, sizeof(newbuf));
  if (newpath == NULL)
    {
      fuse_reply_err(req, ENOENT);
      return;
    }

  ret = f->op.link(oldpath, newpath);
  if (ret != 0)
    {
      fuse_reply_err(req, hl_retcode(ret));
      return;
    }

  hl_lookup(req, newparent, newname);
}

static void hl_open(fuse_req_t req, fuse_ino_t ino,
                    FAR struct fuse_file_info *fi)
{
  FAR struct fuse *f = hl_prepare(req);
  FAR const char *path;
  int ret;

  path = node_path(f, ino);

  if (path == NULL || f->op.open == NULL)
    {
      fuse_reply_err(req, ENOSYS);
      return;
    }

  ret = f->op.open(path, fi);
  if (ret != 0)
    {
      fuse_reply_err(req, hl_retcode(ret));
      return;
    }

  fuse_reply_open(req, fi);
}

static void hl_read(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off,
                    FAR struct fuse_file_info *fi)
{
  FAR struct fuse *f = hl_prepare(req);

  FAR const char *path = node_path(f, ino);
  FAR struct fuse_bufvec *bufv = NULL;
  FAR char *buf;
  ssize_t n = -ENOSYS;

  if (path == NULL)
    {
      fuse_reply_err(req, ENOENT);
      return;
    }

  if (f->op.read_buf != NULL)
    {
      n = f->op.read_buf(path, &bufv, size, off, fi);
      if (n == 0)
        {
          fuse_reply_data(req, bufv, 0);
          return;
        }

      fuse_reply_err(req, (int)-n);
      return;
    }

  if (f->op.read == NULL)
    {
      fuse_reply_err(req, ENOSYS);
      return;
    }

  buf = lib_malloc(size);
  if (buf == NULL)
    {
      fuse_reply_err(req, ENOMEM);
      return;
    }

  n = f->op.read(path, buf, size, off, fi);
  if (n < 0)
    {
      fuse_reply_err(req, (int)-n);
    }
  else
    {
      fuse_reply_buf(req, buf, n);
    }

  lib_free(buf);
}

static void hl_write(fuse_req_t req, fuse_ino_t ino, FAR const char *buf,
                     size_t size, off_t off, FAR struct fuse_file_info *fi)
{
  FAR struct fuse *f = hl_prepare(req);

  FAR const char *path = node_path(f, ino);
  ssize_t n;

  if (path == NULL || f->op.write == NULL)
    {
      fuse_reply_err(req, ENOSYS);
      return;
    }

  n = f->op.write(path, buf, size, off, fi);
  if (n < 0)
    {
      fuse_reply_err(req, (int)-n);
    }
  else
    {
      fuse_reply_write(req, n);
    }
}

static void hl_write_buf(fuse_req_t req, fuse_ino_t ino,
                         FAR struct fuse_bufvec *bufv, off_t off,
                         FAR struct fuse_file_info *fi)
{
  FAR struct fuse *f = hl_prepare(req);

  FAR const char *path = node_path(f, ino);
  ssize_t n;

  if (path == NULL || (f->op.write_buf == NULL && f->op.write == NULL))
    {
      fuse_reply_err(req, ENOSYS);
      return;
    }

  if (f->op.write_buf != NULL)
    {
      n = f->op.write_buf(path, bufv, off, fi);
    }
  else
    {
      /* Fall back to the buffered write callback:
       * copy the buffer vector into a linear scratch buffer,
       * so write()-only filesystems keep working when the
       * kernel (or our lowlevel dispatcher) prefers write_buf.
       */

      size_t len = fuse_buf_size(bufv);
      FAR struct fuse_bufvec dst = FUSE_BUFVEC_INIT(len);
      FAR char *scratch;

      if (len == 0)
        {
          fuse_reply_write(req, 0);
          return;
        }

      scratch = lib_malloc(len);
      if (scratch == NULL)
        {
          fuse_reply_err(req, ENOMEM);
          return;
        }

      dst.buf[0].mem = scratch;
      n = fuse_buf_copy(&dst, bufv, 0) == (ssize_t)len ?
          f->op.write(path, scratch, len, off, fi) : -ENOMEM;
      lib_free(scratch);
    }

  if (n < 0)
    {
      fuse_reply_err(req, (int)-n);
    }
  else
    {
      fuse_reply_write(req, n);
    }
}

static void hl_flush(fuse_req_t req, fuse_ino_t ino,
                     FAR struct fuse_file_info *fi)
{
  FAR struct fuse *f = hl_prepare(req);
  FAR const char *path = node_path(f, ino);

  if (path == NULL || f->op.flush == NULL)
    {
      fuse_reply_err(req, 0);
      return;
    }

  fuse_reply_err(req, hl_retcode(f->op.flush(path, fi)));
}

static void hl_release(fuse_req_t req, fuse_ino_t ino,
                       FAR struct fuse_file_info *fi)
{
  FAR struct fuse *f = hl_prepare(req);

  FAR const char *path = node_path(f, ino);

  if (path != NULL && f->op.release != NULL)
    {
      f->op.release(path, fi);
    }

  fuse_reply_err(req, 0);
}

static void hl_fsync(fuse_req_t req, fuse_ino_t ino, int datasync,
                     FAR struct fuse_file_info *fi)
{
  FAR struct fuse *f = hl_prepare(req);
  FAR const char *path = node_path(f, ino);

  if (path == NULL || f->op.fsync == NULL)
    {
      fuse_reply_err(req, ENOSYS);
      return;
    }

  fuse_reply_err(req, hl_retcode(f->op.fsync(path, datasync, fi)));
}

static void hl_opendir(fuse_req_t req, fuse_ino_t ino,
                       FAR struct fuse_file_info *fi)
{
  FAR struct fuse *f = hl_prepare(req);
  FAR const char *path;
  int ret;

  path = node_path(f, ino);

  /* opendir is optional: with no handler the open succeeds */

  if (f->op.opendir == NULL || path == NULL)
    {
      fuse_reply_open(req, fi);
      return;
    }

  ret = f->op.opendir(path, fi);
  if (ret != 0)
    {
      fuse_reply_err(req, hl_retcode(ret));
      return;
    }

  fuse_reply_open(req, fi);
}

static void hl_readdir(fuse_req_t req, fuse_ino_t ino, size_t size,
                       off_t off, FAR struct fuse_file_info *fi)
{
  FAR struct fuse *f = hl_prepare(req);
  FAR const char *path;
  struct fuse_dh db;
  int ret;

  path = node_path(f, ino);

  if (path == NULL || f->op.readdir == NULL)
    {
      fuse_reply_err(req, ENOSYS);
      return;
    }

  db.req      = req;
  db.buf      = lib_malloc(size);
  db.size     = size;
  db.pos      = 0;
  db.want_off    = off;
  db.index       = 0;
  db.cookie_mode = false;

  if (db.buf == NULL)
    {
      fuse_reply_err(req, ENOMEM);
      return;
    }

  ret = f->op.readdir(path, &db, hl_filler, off, fi, 0);
  if (ret != 0)
    {
      lib_free(db.buf);
      fuse_reply_err(req, hl_retcode(ret));
      return;
    }

  fuse_reply_buf(req, db.buf, db.pos);
  lib_free(db.buf);
}

static void hl_releasedir(fuse_req_t req, fuse_ino_t ino,
                          FAR struct fuse_file_info *fi)
{
  FAR struct fuse *f = hl_prepare(req);
  FAR const char *path;

  path = node_path(f, ino);

  /* releasedir is optional */

  if (path != NULL && f->op.releasedir != NULL)
    {
      f->op.releasedir(path, fi);
    }

  fuse_reply_err(req, 0);
}

static void hl_fsyncdir(fuse_req_t req, fuse_ino_t ino, int datasync,
                        FAR struct fuse_file_info *fi)
{
  FAR struct fuse *f = hl_prepare(req);
  FAR const char *path = node_path(f, ino);

  if (path == NULL || f->op.fsyncdir == NULL)
    {
      fuse_reply_err(req, ENOSYS);
      return;
    }

  fuse_reply_err(req, hl_retcode(f->op.fsyncdir(path, datasync, fi)));
}

static void hl_statfs(fuse_req_t req, fuse_ino_t ino)
{
  FAR struct fuse *f = hl_prepare(req);
  FAR const char *path;
  struct statvfs vl;
  int ret;

  path = node_path(f, ino);

  if (path == NULL || f->op.statfs == NULL)
    {
      fuse_reply_err(req, ENOSYS);
      return;
    }

  ret = f->op.statfs(path, &vl);
  if (ret != 0)
    {
      fuse_reply_err(req, hl_retcode(ret));
      return;
    }

  fuse_reply_statfs(req, &vl);
}

static void hl_create(fuse_req_t req, fuse_ino_t parent,
                      FAR const char *name, mode_t mode,
                      FAR struct fuse_file_info *fi)
{
  FAR struct fuse *f = hl_prepare(req);
  FAR struct node *n;
  FAR const char *path;
  struct fuse_entry_param e;
  char pathbuf[PATH_MAX];
  int ret;

  if (f->op.create == NULL)
    {
      fuse_reply_err(req, ENOSYS);
      return;
    }

  path = node_join(f, parent, name, pathbuf, sizeof(pathbuf));
  if (path == NULL)
    {
      fuse_reply_err(req, ENOENT);
      return;
    }

  ret = f->op.create(path, mode, fi);
  if (ret != 0)
    {
      fuse_reply_err(req, hl_retcode(ret));
      return;
    }

  /* Reply entry + open in one shot, mirroring fuse_reply_create */

  n = node_get(f, parent, name);
  memset(&e, 0, sizeof(e));
  e.attr_timeout  = 1.0;
  e.entry_timeout = 1.0;

  if (f->op.getattr != NULL)
    {
      f->op.getattr(path, &e.attr, fi);
    }

  if (n != NULL)
    {
      n->nlookup++;
      e.ino = n->nodeid;
      fuse_reply_create(req, &e, fi);
    }
  else
    {
      fuse_reply_err(req, ENOMEM);
    }
}

static void hl_access(fuse_req_t req, fuse_ino_t ino, int mask)
{
  FAR struct fuse *f = hl_prepare(req);

  FAR const char *path = node_path(f, ino);

  if (f->op.access == NULL)
    {
      /* Upstream default: permit when no access handler is present */

      fuse_reply_err(req, 0);
      return;
    }

  fuse_reply_err(req, hl_retcode(f->op.access(path, mask)));
}

static void hl_poll(fuse_req_t req, fuse_ino_t ino,
                    FAR struct fuse_file_info *fi,
                    FAR struct fuse_pollhandle *ph)
{
  FAR struct fuse *f = hl_prepare(req);

  FAR const char *path = node_path(f, ino);
  unsigned revents = 0;

  if (f->op.poll != NULL)
    {
      if (f->op.poll(path, fi, ph, &revents) != 0)
        {
          fuse_pollhandle_destroy(ph);
          fuse_reply_err(req, EIO);
          return;
        }
    }

  fuse_reply_poll(req, revents);
}

static void hl_ioctl(fuse_req_t req, fuse_ino_t ino, unsigned int cmd,
                     FAR void *arg, FAR struct fuse_file_info *fi,
                     unsigned int flags, FAR const void *in_buf,
                     size_t in_bufsz, size_t out_bufsz)
{
  FAR struct fuse *f = hl_prepare(req);

  FAR const char *path = node_path(f, ino);
  int ret;

  if (path == NULL || f->op.ioctl == NULL)
    {
      fuse_pollhandle_destroy(NULL);
      fuse_reply_err(req, ENOSYS);
      return;
    }

  ret = f->op.ioctl(path, cmd, arg, fi, flags, (FAR void *)in_buf);
  if (ret < 0)
    {
      fuse_reply_err(req, -ret);
    }
  else
    {
      fuse_reply_ioctl(req, ret, in_buf, out_bufsz);
    }
}

static const struct fuse_lowlevel_ops g_fuse_hl_ops =
{
  .init       = hl_init,
  .destroy    = hl_destroy,
  .lookup     = hl_lookup,
  .forget     = hl_forget,
  .getattr    = hl_getattr,
  .setattr    = hl_setattr,
  .readlink   = hl_readlink,
  .mknod      = hl_mknod,
  .mkdir      = hl_mkdir,
  .unlink     = hl_unlink,
  .rmdir      = hl_rmdir,
  .symlink    = hl_symlink,
  .rename     = hl_rename,
  .link       = hl_link,
  .open       = hl_open,
  .read       = hl_read,
  .write      = hl_write,
  .write_buf  = hl_write_buf,
  .flush      = hl_flush,
  .release    = hl_release,
  .fsync      = hl_fsync,
  .opendir    = hl_opendir,
  .readdir    = hl_readdir,
  .releasedir = hl_releasedir,
  .fsyncdir   = hl_fsyncdir,
  .statfs     = hl_statfs,
  .create     = hl_create,
  .access     = hl_access,
  .ioctl      = hl_ioctl,
  .poll       = hl_poll,
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void fuse_lib_help(FAR struct fuse_args *args)
{
  UNUSED(args);
  printf("FUSE library version: %s\n", PACKAGE_VERSION);
}

FAR struct fuse *fuse_new(FAR struct fuse_args *args,
                          FAR const struct fuse_operations *op,
                          size_t op_size, FAR void *private_data)
{
  FAR struct fuse_session *se;
  FAR struct fuse *f;

  f = lib_calloc(1, sizeof(*f));
  if (f == NULL)
    {
      return NULL;
    }

  memcpy(&f->op, op, op_size < sizeof(f->op) ? op_size : sizeof(f->op));
  f->private_data = private_data;
  f->ctr          = FUSE_ROOT_ID;

  se = fuse_session_new(args, &g_fuse_hl_ops, sizeof(g_fuse_hl_ops), f);
  if (se == NULL)
    {
      lib_free(f);
      return NULL;
    }

  f->se = se;
  return f;
}

int fuse_mount(FAR struct fuse *f, FAR const char *mountpoint)
{
  return fuse_session_mount(f->se, mountpoint);
}

int fuse_mount_custom(FAR struct fuse *f,
                      FAR const struct fuse_custom_io *io, int fd)
{
  return fuse_session_custom_io(f->se, io, sizeof(*io), fd);
}

void fuse_unmount(FAR struct fuse *f)
{
  fuse_session_unmount(f->se);
}

int fuse_loop(FAR struct fuse *f)
{
  return fuse_session_loop(f->se);
}

int fuse_loop_mt(FAR struct fuse *f, FAR struct fuse_loop_config *config)
{
  return fuse_session_loop_mt(f->se, config);
}

void fuse_exit(FAR struct fuse *f)
{
  fuse_session_exit(f->se);
}

void fuse_destroy(FAR struct fuse *f)
{
  FAR struct node *n;
  FAR struct node *next;
  FAR struct tls_info_s *tls = tls_get_info();

  /* Drop the context of this instance so a later fuse_get_context()
   * does not report freed memory.
   */

  if (tls != NULL && tls->tl_fuse.fuse == f)
    {
      tls->tl_fuse.fuse = NULL;
    }

  for (n = f->nodes; n != NULL; n = next)
    {
      next = n->next;
      lib_free(n->path);
      lib_free(n);
    }

  fuse_session_destroy(f->se);
  lib_free(f);
}

struct fuse_context *fuse_get_context(void)
{
  FAR struct tls_info_s *tls = tls_get_info();

  if (tls == NULL || tls->tl_fuse.fuse == NULL)
    {
      return NULL;
    }

  return &tls->tl_fuse;
}

FAR struct fuse_session *fuse_get_session(FAR struct fuse *f)
{
  return f->se;
}

int fuse_invalidate_path(FAR struct fuse *f, FAR const char *path)
{
  FAR struct node *n;
  FAR const char *base;

  for (n = f->nodes; n != NULL; n = n->next)
    {
      if (strcmp(n->path, path) == 0)
        {
          break;
        }
    }

  if (n == NULL)
    {
      return -ENOENT;
    }

  fuse_lowlevel_notify_inval_inode(f->se, n->nodeid, 0, 0);

  base = strrchr(path, '/');
  if (base != NULL && base[1] != '\0')
    {
      FAR struct node *parent;
      FAR char *dir = strdup(path);

      if (dir != NULL)
        {
          dir[base - path] = '\0';
          parent = node_find(f, 0);
          for (parent = f->nodes; parent != NULL; parent = parent->next)
            {
              if (strcmp(parent->path, dir) == 0)
                {
                  break;
                }
            }

          if (parent != NULL)
            {
              fuse_lowlevel_notify_inval_entry(f->se, parent->nodeid,
                                               base + 1,
                                               strlen(base + 1));
            }

          free(dir);
        }
    }

  return 0;
}

int fuse_notify_poll(FAR struct fuse_pollhandle *ph)
{
  return fuse_lowlevel_notify_poll(ph);
}

int fuse_main(int argc, FAR char *argv[],
              FAR const struct fuse_operations *op,
              FAR void *private_data)
{
  struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
  struct fuse_cmdline_opts opts;
  FAR struct fuse *f;
  int ret;

  if (fuse_parse_cmdline(&args, &opts) != 0)
    {
      return 1;
    }

  if (opts.show_help)
    {
      fuse_cmdline_help();
      return 0;
    }

  if (opts.show_version)
    {
      printf("FUSE library version: %s\n", fuse_pkgversion());
      return 0;
    }

  if (opts.mountpoint == NULL)
    {
      fprintf(stderr, "error: no mountpoint specified\n");
      return 1;
    }

  f = fuse_new(&args, op, sizeof(*op), private_data);
  if (f == NULL)
    {
      return 1;
    }

  if (fuse_mount(f, opts.mountpoint) != 0)
    {
      fprintf(stderr, "error: failed to mount %s\n", opts.mountpoint);
      fuse_destroy(f);
      return 1;
    }

  ret = fuse_loop(f);
  fuse_unmount(f);
  fuse_destroy(f);
  lib_free(opts.mountpoint);
  return ret != 0 ? 1 : 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

struct fuse_fs
{
  struct fuse_operations op;
  FAR void *user_data;
};

struct fuse_fs *fuse_fs_new(FAR const struct fuse_operations *op,
                            size_t op_size, FAR void *private_data)
{
  FAR struct fuse_fs *fs = lib_calloc(1, sizeof(*fs));

  if (fs == NULL)
    {
      return NULL;
    }

  memcpy(&fs->op, op, op_size < sizeof(fs->op) ? op_size : sizeof(fs->op));
  fs->user_data = private_data;
  return fs;
}

void fuse_fs_destroy(FAR struct fuse_fs *fs)
{
  lib_free(fs);
}

void fuse_fs_init(FAR struct fuse_fs *fs, FAR struct fuse_conn_info *conn,
                  FAR struct fuse_config *cfg)
{
  if (fs->op.init != NULL)
    {
      fs->user_data = fs->op.init(conn, cfg);
    }
}

#define FUSE_FS_WRAPPER(name, call, rettype, failval, args...)        \
rettype fuse_fs_##name(FAR struct fuse_fs *fs, args)                  \
{                                                                     \
  if (fs->op.name == NULL)                                            \
    {                                                                 \
      return failval;                                                 \
    }                                                                 \
                                                                       \
  return fs->op.name call;                                            \
}

FUSE_FS_WRAPPER(getattr, (path, buf, fi), int, -ENOSYS,
                FAR const char *path, FAR struct stat *buf,
                FAR struct fuse_file_info *fi)
FUSE_FS_WRAPPER(rename, (oldpath, newpath, flags), int, -ENOSYS,
                FAR const char *oldpath, FAR const char *newpath,
                unsigned int flags)
FUSE_FS_WRAPPER(unlink, (path), int, -ENOSYS, FAR const char *path)
FUSE_FS_WRAPPER(rmdir, (path), int, -ENOSYS, FAR const char *path)
FUSE_FS_WRAPPER(symlink, (linkname, path), int, -ENOSYS,
                FAR const char *linkname, FAR const char *path)
FUSE_FS_WRAPPER(link, (oldpath, newpath), int, -ENOSYS,
                FAR const char *oldpath, FAR const char *newpath)
FUSE_FS_WRAPPER(release, (path, fi), int, -ENOSYS,
                FAR const char *path, FAR struct fuse_file_info *fi)
FUSE_FS_WRAPPER(open, (path, fi), int, -ENOSYS,
                FAR const char *path, FAR struct fuse_file_info *fi)
FUSE_FS_WRAPPER(flush, (path, fi), int, -ENOSYS,
                FAR const char *path, FAR struct fuse_file_info *fi)
FUSE_FS_WRAPPER(fsync, (path, isdatasync, fi), int, -ENOSYS,
                FAR const char *path, int isdatasync,
                FAR struct fuse_file_info *fi)
FUSE_FS_WRAPPER(statfs, (path, buf), int, -ENOSYS,
                FAR const char *path, FAR struct statvfs *buf)
FUSE_FS_WRAPPER(opendir, (path, fi), int, -ENOSYS,
                FAR const char *path, FAR struct fuse_file_info *fi)
FUSE_FS_WRAPPER(readdir, (path, buf, filler, offset, fi, flags), int,
                -ENOSYS, FAR const char *path, FAR void *buf,
                fuse_fill_dir_t filler, off_t offset,
                FAR struct fuse_file_info *fi,
                enum fuse_readdir_flags flags)
FUSE_FS_WRAPPER(releasedir, (path, fi), int, -ENOSYS,
                FAR const char *path, FAR struct fuse_file_info *fi)
FUSE_FS_WRAPPER(fsyncdir, (path, isdatasync, fi), int, -ENOSYS,
                FAR const char *path, int isdatasync,
                FAR struct fuse_file_info *fi)
FUSE_FS_WRAPPER(create, (path, mode, fi), int, -ENOSYS,
                FAR const char *path, mode_t mode,
                FAR struct fuse_file_info *fi)
FUSE_FS_WRAPPER(lock, (path, fi, cmd, lock), int, -ENOSYS,
                FAR const char *path, FAR struct fuse_file_info *fi,
                int cmd, FAR struct flock *lock)
FUSE_FS_WRAPPER(flock, (path, fi, op), int, -ENOSYS,
                FAR const char *path, FAR struct fuse_file_info *fi, int op)
FUSE_FS_WRAPPER(chmod, (path, mode, fi), int, -ENOSYS,
                FAR const char *path, mode_t mode,
                FAR struct fuse_file_info *fi)
FUSE_FS_WRAPPER(chown, (path, uid, gid, fi), int, -ENOSYS,
                FAR const char *path, uid_t uid, gid_t gid,
                FAR struct fuse_file_info *fi)
FUSE_FS_WRAPPER(truncate, (path, size, fi), int, -ENOSYS,
                FAR const char *path, off_t size,
                FAR struct fuse_file_info *fi)
FUSE_FS_WRAPPER(utimens, (path, tv, fi), int, -ENOSYS,
                FAR const char *path, FAR const struct timespec tv[2],
                FAR struct fuse_file_info *fi)
FUSE_FS_WRAPPER(access, (path, mask), int, -ENOSYS,
                FAR const char *path, int mask)
FUSE_FS_WRAPPER(readlink, (path, buf, len), int, -ENOSYS,
                FAR const char *path, FAR char *buf, size_t len)
FUSE_FS_WRAPPER(mknod, (path, mode, dev), int, -ENOSYS,
                FAR const char *path, mode_t mode, dev_t dev)
FUSE_FS_WRAPPER(mkdir, (path, mode), int, -ENOSYS,
                FAR const char *path, mode_t mode)
FUSE_FS_WRAPPER(setxattr, (path, name, value, size, flags), int, -ENOSYS,
                FAR const char *path, FAR const char *name,
                FAR const char *value, size_t size, int flags)
FUSE_FS_WRAPPER(getxattr, (path, name, value, size), int, -ENOSYS,
                FAR const char *path, FAR const char *name,
                FAR char *value, size_t size)
FUSE_FS_WRAPPER(listxattr, (path, list, size), int, -ENOSYS,
                FAR const char *path, FAR char *list, size_t size)
FUSE_FS_WRAPPER(removexattr, (path, name), int, -ENOSYS,
                FAR const char *path, FAR const char *name)
FUSE_FS_WRAPPER(bmap, (path, blocksize, idx), int, -ENOSYS,
                FAR const char *path, size_t blocksize, FAR uint64_t *idx)
FUSE_FS_WRAPPER(lseek, (path, off, whence, fi), off_t, -ENOSYS,
                FAR const char *path, off_t off, int whence,
                FAR struct fuse_file_info *fi)
FUSE_FS_WRAPPER(fallocate, (path, mode, offset, length, fi), int, -ENOSYS,
                FAR const char *path, int mode, off_t offset, off_t length,
                FAR struct fuse_file_info *fi)
FUSE_FS_WRAPPER(statx, (path, stx, request_mask, flags, fi), int, -ENOSYS,
                FAR const char *path, FAR struct statx *stx,
                unsigned int request_mask, unsigned int flags,
                FAR struct fuse_file_info *fi)
FUSE_FS_WRAPPER(syncfs, (path, fi), int, -ENOSYS,
                FAR const char *path, FAR struct fuse_file_info *fi)

/* read/write/copy_file_range return ssize_t */

ssize_t fuse_fs_read(FAR struct fuse_fs *fs, FAR const char *path,
                     FAR char *buf, size_t size, off_t off,
                     FAR struct fuse_file_info *fi)
{
  if (fs->op.read == NULL)
    {
      return -ENOSYS;
    }

  return fs->op.read(path, buf, size, off, fi);
}

ssize_t fuse_fs_write(FAR struct fuse_fs *fs, FAR const char *path,
                      FAR const char *buf, size_t size, off_t off,
                      FAR struct fuse_file_info *fi)
{
  if (fs->op.write == NULL)
    {
      return -ENOSYS;
    }

  return fs->op.write(path, buf, size, off, fi);
}

ssize_t fuse_fs_copy_file_range(FAR struct fuse_fs *fs,
                                FAR const char *path_in,
                                FAR struct fuse_file_info *fi_in,
                                off_t offset_in, FAR const char *path_out,
                                FAR struct fuse_file_info *fi_out,
                                off_t offset_out, size_t size, int flags)
{
  if (fs->op.copy_file_range == NULL)
    {
      return -ENOSYS;
    }

  return fs->op.copy_file_range(path_in, fi_in, offset_in, path_out,
                                fi_out, offset_out, size, flags);
}

int fuse_version(void)
{
  return FUSE_VERSION;
}

FAR const char *fuse_pkgversion(void)
{
  return PACKAGE_VERSION;
}
