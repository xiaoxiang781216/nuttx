/****************************************************************************
 * fs/fuse/fuse.c
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

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statfs.h>

#include <nuttx/fs/fs.h>
#include <nuttx/fs/uio.h>

#include "inode/inode.h"
#include "fs_heap.h"
#include "fuse.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifdef CONFIG_FS_LARGEFILE
#  define OFFSET_MAX INT64_MAX
#else
#  define OFFSET_MAX INT32_MAX
#endif

/* Number of scatter/gather segments in a single FUSE_READ/FUSE_WRITE
 * request.  For writes slot 0 carries the fuse_write_in header, so one
 * segment less is available for caller data.
 */

#define FUSE_IOV_SEGS 8

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct fuse_dir_s
{
  struct fs_dirent_s base;
  uint64_t           fh;
  uint64_t           nodeid;
  uint64_t           offset;
};

struct fuse_file_s
{
  uint64_t fh;
  uint64_t nodeid;
  atomic_t refs;
  char     relpath[];
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int     fuse_open(FAR struct file *filep, FAR const char *relpath,
                         int oflags, mode_t mode);
static int     fuse_close(FAR struct file *filep);
static ssize_t fuse_readv(FAR struct file *filep, FAR struct uio *uio);
static ssize_t fuse_writev(FAR struct file *filep, FAR struct uio *uio);
static off_t   fuse_seek(FAR struct file *filep, off_t offset, int whence);
static int     fuse_ioctl(FAR struct file *filep, int cmd,
                          unsigned long arg);
static int     fuse_truncate(FAR struct file *filep, off_t length);
static int     fuse_sync(FAR struct file *filep);
static int     fuse_dup(FAR const struct file *oldp, FAR struct file *newp);
static int     fuse_fstat(FAR const struct file *filep,
                          FAR struct stat *buf);
static int     fuse_fchstat(FAR const struct file *filep,
                            FAR const struct stat *buf, int flags);
static int     fuse_opendir(FAR struct inode *mountpt,
                            FAR const char *relpath,
                            FAR struct fs_dirent_s **dir);
static int     fuse_closedir(FAR struct inode *mountpt,
                             FAR struct fs_dirent_s *dir);
static int     fuse_readdir(FAR struct inode *mountpt,
                            FAR struct fs_dirent_s *dir,
                            FAR struct dirent *entry);
static int     fuse_rewinddir(FAR struct inode *mountpt,
                              FAR struct fs_dirent_s *dir);
static int     fuse_bind(FAR struct inode *blkdriver, FAR const void *data,
                         FAR void **handle);
static int     fuse_unbind(FAR void *handle, FAR struct inode **blkdriver,
                           unsigned int flags);
static int     fuse_statfs(FAR struct inode *mountpt,
                           FAR struct statfs *buf);
static int     fuse_unlink(FAR struct inode *mountpt,
                           FAR const char *relpath);
static int     fuse_mkdir(FAR struct inode *mountpt,
                          FAR const char *relpath, mode_t mode);
static int     fuse_rmdir(FAR struct inode *mountpt,
                          FAR const char *relpath);
static int     fuse_rename(FAR struct inode *mountpt,
                           FAR const char *oldrelpath,
                           FAR const char *newrelpath);
static int     fuse_stat(FAR struct inode *mountpt,
                         FAR const char *relpath, FAR struct stat *buf);
static int     fuse_chstat(FAR struct inode *mountpt,
                           FAR const char *relpath,
                           FAR const struct stat *buf, int flags);
static int     fuse_syncfs(FAR struct inode *mountpt);
#ifdef CONFIG_FS_LINKS
static int     fuse_link(FAR struct inode *mountpt,
                         FAR const char *relpath1,
                         FAR const char *relpath2);
static int     fuse_symlink(FAR struct inode *mountpt,
                            FAR const char *path1,
                            FAR const char *relpath2);
static ssize_t fuse_readlink(FAR struct inode *mountpt,
                             FAR const char *relpath,
                             FAR char *buf, size_t buflen);
static int     fuse_lstat(FAR struct inode *mountpt,
                          FAR const char *relpath, FAR struct stat *buf);
#endif

/****************************************************************************
 * Public Data
 ****************************************************************************/

const struct mountpt_operations g_fuse_operations =
{
  fuse_open,      /* open */
  fuse_close,     /* close */
  NULL,           /* read */
  NULL,           /* write */
  fuse_seek,      /* seek */
  fuse_ioctl,     /* ioctl */
  fuse_truncate,  /* truncate */
  NULL,           /* poll */
  fuse_readv,     /* readv */
  fuse_writev,    /* writev */
#ifdef CONFIG_FS_FILEMAP
  NULL,           /* mmap */
#endif
  fuse_sync,      /* sync */
  fuse_dup,       /* dup */
  fuse_fstat,     /* fstat */
  fuse_fchstat,   /* fchstat */
  fuse_opendir,   /* opendir */
  fuse_closedir,  /* closedir */
  fuse_readdir,   /* readdir */
  fuse_rewinddir, /* rewinddir */
  fuse_bind,      /* bind */
  fuse_unbind,    /* unbind */
  fuse_statfs,    /* statfs */
  fuse_unlink,    /* unlink */
  fuse_mkdir,     /* mkdir */
  fuse_rmdir,     /* rmdir */
  fuse_rename,    /* rename */
  fuse_stat,      /* stat */
  fuse_chstat,    /* chstat */
  fuse_syncfs,    /* syncfs */
#ifdef CONFIG_FS_LINKS
  fuse_link,      /* link */
  fuse_symlink,   /* symlink */
  fuse_readlink,  /* readlink */
  fuse_lstat,     /* lstat */
#endif
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: fuse_attr_to_stat
 ****************************************************************************/

static void fuse_attr_to_stat(FAR const struct fuse_attr *attr,
                              FAR struct stat *st)
{
  memset(st, 0, sizeof(*st));

  st->st_ino     = attr->ino;
  st->st_size    = attr->size;
  st->st_blocks  = attr->blocks;
  st->st_blksize = attr->blksize;
  st->st_mode    = attr->mode;
  st->st_nlink   = attr->nlink;
  st->st_uid     = attr->uid;
  st->st_gid     = attr->gid;
  st->st_rdev    = attr->rdev;

  st->st_atim.tv_sec  = attr->atime;
  st->st_atim.tv_nsec = attr->atimensec;
  st->st_mtim.tv_sec  = attr->mtime;
  st->st_mtim.tv_nsec = attr->mtimensec;
  st->st_ctim.tv_sec  = attr->ctime;
  st->st_ctim.tv_nsec = attr->ctimensec;
}

/****************************************************************************
 * Name: fuse_lookup
 ****************************************************************************/

static int fuse_lookup(FAR struct fuse_conn_s *conn, uint64_t parent,
                       FAR const char *name, size_t namelen,
                       FAR struct fuse_entry_out *entry)
{
  struct fuse_req_s req;
  struct iovec vout[1];
  struct iovec vin[2];
  char nul = '\0';

  memset(&req, 0, sizeof(req));
  memset(entry, 0, sizeof(*entry));

  req.hin.opcode = FUSE_LOOKUP;
  req.hin.nodeid = parent;

  /* FUSE requires the name to be NUL terminated on the wire.  Send the
   * component and its terminator as two segments so the caller does not
   * need to copy the name out of the (unmodifiable) path.
   */

  vin[0].iov_base = (FAR void *)name;
  vin[0].iov_len  = namelen;
  vin[1].iov_base = &nul;
  vin[1].iov_len  = 1;

  vout[0].iov_base = entry;
  vout[0].iov_len  = sizeof(*entry);

  return fuse_request(conn, &req, vin, 2, vout, 1);
}

/****************************************************************************
 * Name: fuse_readlink_node
 ****************************************************************************/

static ssize_t fuse_readlink_node(FAR struct fuse_conn_s *conn,
                                  uint64_t nodeid, FAR char *buf,
                                  size_t buflen)
{
  struct fuse_req_s req;
  struct iovec vout[1];
  int ret;

  memset(&req, 0, sizeof(req));

  req.hin.opcode = FUSE_READLINK;
  req.hin.nodeid = nodeid;

  vout[0].iov_base = buf;
  vout[0].iov_len  = buflen;

  ret = fuse_request(conn, &req, NULL, 0, vout, 1);
  if (ret < 0)
    {
      return ret;
    }

  /* Actual bytes = total response len - out_header size */

  return req.hout.len - sizeof(req.hout);
}

/****************************************************************************
 * Name: fuse_follow_link
 *
 * Description:
 *   Dereference the symbolic link in entry.  The target is read from the
 *   server and resolved component by component, recursing on nested links
 *   up to SYMLOOP_MAX levels.  A relative target is resolved against
 *   *parent (the directory containing the link); an absolute target is
 *   resolved against the mountpoint root.  On success entry holds the
 *   resolved node and *parent the directory containing it.
 *
 ****************************************************************************/

static int fuse_follow_link(FAR struct fuse_conn_s *conn,
                            FAR uint64_t *parent,
                            FAR struct fuse_entry_out *entry,
                            int depth)
{
  char target[PATH_MAX];
  FAR const char *path = target;
  ssize_t len;

  if (depth >= SYMLOOP_MAX)
    {
      return -ELOOP;
    }

  len = fuse_readlink_node(conn, entry->nodeid, target,
                           sizeof(target) - 1);
  if (len < 0)
    {
      return len;
    }
  else if (len >= sizeof(target))
    {
      return -ENAMETOOLONG;
    }
  else if (*target == '/')
    {
      return -ENOTSUP;
    }

  target[len] = '\0';

  for (; ; )
    {
      FAR const char *slash = strchrnul(path, '/');
      int ret;

      ret = fuse_lookup(conn, *parent, path, slash - path, entry);
      if (ret < 0)
        {
          return ret;
        }

      /* Resolve a nested link before deciding how to continue */

      if (S_ISLNK(entry->attr.mode))
        {
          ret = fuse_follow_link(conn, parent, entry, depth + 1);
          if (ret < 0)
            {
              return ret;
            }
        }

      if (slash[0] == '\0' || slash[1] == '\0')
        {
          return 0;
        }

      *parent = entry->nodeid;
      path = slash + 1;
    }
}

/****************************************************************************
 * Name: fuse_lookup_parent
 ****************************************************************************/

static int fuse_lookup_parent(FAR struct fuse_conn_s *conn,
                              FAR const char *relpath,
                              FAR uint64_t *parent,
                              FAR const char **name)
{
  uint64_t node = FUSE_ROOT_ID;

  for (; ; )
    {
      FAR const char *slash = strchrnul(relpath, '/');
      struct fuse_entry_out entry;
      int ret;

      if (slash[0] == '\0' || slash[1] == '\0')
        {
          break;
        }

      ret = fuse_lookup(conn, node, relpath, slash - relpath, &entry);
      if (ret < 0)
        {
          return ret;
        }

      /* Always dereference an intermediate symbolic link */

      if (S_ISLNK(entry.attr.mode))
        {
          ret = fuse_follow_link(conn, &node, &entry, 0);
          if (ret < 0)
            {
              return ret;
            }
        }

      node = entry.nodeid;
      relpath = slash + 1;
    }

  *parent = node;
  *name = relpath;

  return 0;
}

/****************************************************************************
 * Name: fuse_lookup_path
 ****************************************************************************/

static int fuse_lookup_path(FAR struct fuse_conn_s *conn,
                            FAR const char *relpath,
                            FAR struct fuse_entry_out *entry,
                            bool follow)
{
  FAR const char *name;
  uint64_t parent;
  int ret;

  if (relpath[0] == '\0' || (relpath[0] == '/' && relpath[1] == '\0'))
    {
      struct fuse_getattr_in gin;
      struct fuse_attr_out aout;
      struct fuse_req_s req;
      struct iovec vout[1];
      struct iovec vin[1];

      /* Root inode: use GETATTR instead of LOOKUP */

      memset(&req, 0, sizeof(req));
      memset(&gin, 0, sizeof(gin));
      memset(&aout, 0, sizeof(aout));

      req.hin.opcode = FUSE_GETATTR;
      req.hin.nodeid = FUSE_ROOT_ID;

      vin[0].iov_base = &gin;
      vin[0].iov_len  = sizeof(gin);

      vout[0].iov_base = &aout;
      vout[0].iov_len  = sizeof(aout);

      ret = fuse_request(conn, &req, vin, 1, vout, 1);
      if (ret < 0)
        {
          return ret;
        }

      memset(entry, 0, sizeof(*entry));

      entry->nodeid          = FUSE_ROOT_ID;
      entry->attr_valid      = aout.attr_valid;
      entry->attr_valid_nsec = aout.attr_valid_nsec;
      entry->attr            = aout.attr;

      return 0;
    }

  ret = fuse_lookup_parent(conn, relpath, &parent, &name);
  if (ret < 0)
    {
      return ret;
    }

  /* Look up the final component, ignoring a trailing '/' */

  ret = fuse_lookup(conn, parent, name,
                    strchrnul(name, '/') - name, entry);
  if (ret < 0)
    {
      return ret;
    }

  /* Dereference the final component only when the caller asked for
   * follow semantics.
   */

  if (follow && S_ISLNK(entry->attr.mode))
    {
      ret = fuse_follow_link(conn, &parent, entry, 0);
    }

  return ret;
}

/****************************************************************************
 * Name: fuse_init
 *
 * Description:
 *   Perform the FUSE_INIT handshake to negotiate protocol capabilities.
 *
 ****************************************************************************/

static int fuse_init(FAR struct fuse_conn_s *conn)
{
  struct fuse_init_out fout;
  struct fuse_init_in fin;
  struct fuse_req_s req;
  struct iovec vout[1];
  struct iovec vin[1];
  int ret;

  memset(&req, 0, sizeof(req));
  memset(&fin, 0, sizeof(fin));
  memset(&fout, 0, sizeof(fout));

  req.hin.opcode = FUSE_INIT;
  req.hin.nodeid = FUSE_ROOT_ID;

  fin.major         = FUSE_KERNEL_VERSION;
  fin.minor         = FUSE_KERNEL_MINOR_VERSION;
  fin.max_readahead = FUSE_MIN_READ_BUFFER;

  vin[0].iov_base = &fin;
  vin[0].iov_len  = sizeof(fin);

  vout[0].iov_base = &fout;
  vout[0].iov_len  = sizeof(fout);

  ret = fuse_request(conn, &req, vin, 1, vout, 1);
  if (ret < 0)
    {
      return ret;
    }

  if (fout.major != FUSE_KERNEL_VERSION)
    {
      return -EPROTO;
    }

  conn->max_write     = fout.max_write > 0 ?
                        fout.max_write : FUSE_MAX_WRITE_BUFFER;
  conn->max_readahead = fout.max_readahead;

  return 0;
}

/****************************************************************************
 * Name: fuse_open
 ****************************************************************************/

static int fuse_open(FAR struct file *filep, FAR const char *relpath,
                     int oflags, mode_t mode)
{
  FAR struct inode *inode = filep->f_inode;
  FAR struct fuse_conn_s *conn = inode->i_private;
  FAR struct fuse_file_s *fp;
  struct fuse_entry_out entry;
  struct fuse_open_out oout;
  struct fuse_req_s req;
  struct iovec vout[2];
  struct iovec vin[2];
  size_t pathlen;
  int ret;

  memset(&req, 0, sizeof(req));
  memset(&oout, 0, sizeof(oout));

  /* Record the relative path so that FIOC_FILEPATH can report it later:
   * FUSE has no reverse (nodeid to name) query.
   */

  pathlen = strlen(relpath) + 1;
  fp = fs_heap_zalloc(sizeof(*fp) + pathlen);
  if (fp == NULL)
    {
      return -ENOMEM;
    }

  if ((oflags & O_CREAT) != 0)
    {
      struct fuse_create_in cin;
      FAR const char *name;
      uint64_t parent;

      ret = fuse_lookup_parent(conn, relpath, &parent, &name);
      if (ret < 0)
        {
          fs_heap_free(fp);
          return ret;
        }

      memset(&cin, 0, sizeof(cin));
      memset(&entry, 0, sizeof(entry));

      req.hin.opcode = FUSE_CREATE;
      req.hin.nodeid = parent;

      cin.flags = oflags & ~O_CREAT;
      cin.mode  = mode;

      vin[0].iov_base = &cin;
      vin[0].iov_len  = sizeof(cin);
      vin[1].iov_base = (FAR void *)name;
      vin[1].iov_len  = strlen(name) + 1;

      vout[0].iov_base = &entry;
      vout[0].iov_len  = sizeof(entry);
      vout[1].iov_base = &oout;
      vout[1].iov_len  = sizeof(oout);

      ret = fuse_request(conn, &req, vin, 2, vout, 2);
    }
  else /* Normal open: lookup then FUSE_OPEN */
    {
      struct fuse_open_in oin;

      ret = fuse_lookup_path(conn, relpath, &entry, true);
      if (ret < 0)
        {
          fs_heap_free(fp);
          return ret;
        }

      memset(&oin, 0, sizeof(oin));

      req.hin.opcode = FUSE_OPEN;
      req.hin.nodeid = entry.nodeid;

      oin.flags = oflags;

      vin[0].iov_base = &oin;
      vin[0].iov_len  = sizeof(oin);

      vout[0].iov_base = &oout;
      vout[0].iov_len  = sizeof(oout);

      ret = fuse_request(conn, &req, vin, 1, vout, 1);
    }

  if (ret < 0)
    {
      fs_heap_free(fp);
      return ret;
    }

  fp->fh     = oout.fh;
  fp->nodeid = entry.nodeid;
  atomic_set(&fp->refs, 1);
  memcpy(fp->relpath, relpath, pathlen);

  filep->f_priv = fp;
  return 0;
}

/****************************************************************************
 * Name: fuse_close
 ****************************************************************************/

static int fuse_close(FAR struct file *filep)
{
  FAR struct inode *inode = filep->f_inode;
  FAR struct fuse_conn_s *conn = inode->i_private;
  FAR struct fuse_file_s *fp = filep->f_priv;
  struct fuse_release_in rin;
  struct fuse_req_s req;
  struct iovec vin[1];

  /* The handle is shared by dup'ed files: send FUSE_RELEASE only when
   * the last user goes away.
   */

  if (atomic_sub(&fp->refs, 1) == 1)
    {
      memset(&req, 0, sizeof(req));
      memset(&rin, 0, sizeof(rin));

      req.hin.opcode = FUSE_RELEASE;
      req.hin.nodeid = fp->nodeid;

      rin.fh = fp->fh;

      vin[0].iov_base = &rin;
      vin[0].iov_len  = sizeof(rin);

      fuse_request(conn, &req, vin, 1, NULL, 0);

      fs_heap_free(fp);
    }

  filep->f_priv = NULL;

  return 0;
}

/****************************************************************************
 * Name: fuse_maxbuf
 *
 * Description:
 *   Largest payload one request can carry at this moment, as reported by
 *   the transport.  Transports without a limit do not implement the
 *   hook.
 *
 ****************************************************************************/

static inline size_t fuse_maxbuf(FAR struct fuse_conn_s *conn)
{
  if (conn->transport->maxbuf == NULL)
    {
      return SIZE_MAX;
    }

  return conn->transport->maxbuf(conn);
}

/****************************************************************************
 * Name: fuse_uio_slice
 *
 * Description:
 *   Collect up to maxsegs iovec segments covering at most maxbytes bytes
 *   of the uio, starting at its cursor.  Returns the number of segments
 *   collected; *bytes receives the covered length.
 *
 ****************************************************************************/

static size_t fuse_uio_slice(FAR struct uio *uio, FAR struct iovec *iov,
                             size_t maxsegs, size_t maxbytes,
                             FAR size_t *bytes)
{
  size_t offset = uio->uio_offset_in_iov;
  size_t total = 0;
  size_t count = 0;
  int i;

  for (i = 0; count < maxsegs && i < uio->uio_iovcnt; i++)
    {
      size_t len = uio->uio_iov[i].iov_len - offset;

      if (len > maxbytes - total)
        {
          len = maxbytes - total;
        }

      if (len > 0)
        {
          FAR char *buf = uio->uio_iov[i].iov_base;

          iov[count].iov_base = buf + offset;
          iov[count].iov_len  = len;
          total += len;
          count++;
        }

      offset = 0;
      if (total >= maxbytes)
        {
          break;
        }
    }

  *bytes = total;
  return count;
}

/****************************************************************************
 * Name: fuse_readv
 ****************************************************************************/

static ssize_t fuse_readv(FAR struct file *filep, FAR struct uio *uio)
{
  FAR struct inode *inode = filep->f_inode;
  FAR struct fuse_conn_s *conn = inode->i_private;
  FAR struct fuse_file_s *fp = filep->f_priv;
  struct fuse_read_in rin;
  struct fuse_req_s req;
  struct iovec vout[FUSE_IOV_SEGS];
  struct iovec vin[1];
  size_t total = 0;
  ssize_t ret = 0;

  /* Transfer in rounds:  each one is sized by the transport capacity
   * (unlimited on non bouncing transports) and by the slice segment
   * array, scattering the reply directly into the caller's buffers.
   */

  while (uio->uio_resid > 0)
    {
      ssize_t nread;
      size_t chunk;
      size_t count;

      count = fuse_uio_slice(uio, vout, FUSE_IOV_SEGS,
                             fuse_maxbuf(conn), &chunk);
      if (chunk == 0)
        {
          ret = -ENOMEM;
          break;
        }

      memset(&req, 0, sizeof(req));
      memset(&rin, 0, sizeof(rin));

      req.hin.opcode = FUSE_READ;
      req.hin.nodeid = fp->nodeid;

      rin.fh     = fp->fh;
      rin.offset = filep->f_pos;
      rin.size   = chunk;

      vin[0].iov_base = &rin;
      vin[0].iov_len  = sizeof(rin);

      ret = fuse_request(conn, &req, vin, 1, vout, count);
      if (ret < 0)
        {
          break;
        }

      /* Actual bytes read = total response len - out_header size,
       * capped to this round in case of a misbehaving server.
       */

      nread = req.hout.len - sizeof(req.hout);
      if (nread <= 0)
        {
          break;
        }

      if (nread > chunk)
        {
          nread = chunk;
        }

      filep->f_pos += nread;
      total += nread;
      uio_advance(uio, nread);

      /* A short round means EOF or the server capped the reply */

      if (nread < chunk)
        {
          break;
        }
    }

  return total > 0 ? total : ret;
}

/****************************************************************************
 * Name: fuse_writev
 ****************************************************************************/

static ssize_t fuse_writev(FAR struct file *filep, FAR struct uio *uio)
{
  FAR struct inode *inode = filep->f_inode;
  FAR struct fuse_conn_s *conn = inode->i_private;
  FAR struct fuse_file_s *fp = filep->f_priv;
  struct fuse_write_out wout;
  struct fuse_write_in win;
  struct fuse_req_s req;
  struct iovec vout[1];
  struct iovec vin[FUSE_IOV_SEGS];
  size_t total = 0;
  int ret;

  while (uio->uio_resid > 0)
    {
      size_t maxbytes = fuse_maxbuf(conn);
      size_t chunk;
      size_t count;

      /* Each round carries what the smaller of the negotiated max_write
       * and the transport capacity allows, gathered from the uio cursor;
       * uio_advance below keeps the cursor in sync with the bytes the
       * server accepted.
       */

      if (maxbytes > conn->max_write)
        {
          maxbytes = conn->max_write;
        }

      count = fuse_uio_slice(uio, &vin[1], FUSE_IOV_SEGS - 1,
                             maxbytes, &chunk);
      if (chunk == 0)
        {
          return total > 0 ? total : -ENOMEM;
        }

      memset(&req, 0, sizeof(req));
      memset(&win, 0, sizeof(win));
      memset(&wout, 0, sizeof(wout));

      req.hin.opcode = FUSE_WRITE;
      req.hin.nodeid = fp->nodeid;

      win.fh     = fp->fh;
      win.offset = filep->f_pos;
      win.size   = chunk;

      vin[0].iov_base = &win;
      vin[0].iov_len  = sizeof(win);

      vout[0].iov_base = &wout;
      vout[0].iov_len  = sizeof(wout);

      ret = fuse_request(conn, &req, vin, count + 1, vout, 1);
      if (ret < 0)
        {
          return total > 0 ? total : ret;
        }

      /* Cap to the bytes actually gathered:  uio_advance must not move
       * past the sent range even if the server overstates wout.size.
       */

      if (wout.size > chunk)
        {
          wout.size = chunk;
        }

      filep->f_pos += wout.size;
      total += wout.size;
      uio_advance(uio, wout.size);

      if (wout.size < chunk)
        {
          break;
        }
    }

  return total;
}

/****************************************************************************
 * Name: fuse_seek
 ****************************************************************************/

static off_t fuse_seek(FAR struct file *filep, off_t offset, int whence)
{
  FAR struct inode *inode = filep->f_inode;
  FAR struct fuse_conn_s *conn = inode->i_private;
  FAR struct fuse_file_s *fp = filep->f_priv;
  struct fuse_lseek_out lout;
  struct fuse_lseek_in lin;
  struct fuse_req_s req;
  struct iovec vout[1];
  struct iovec vin[1];
  int ret;

  memset(&req, 0, sizeof(req));
  memset(&lin, 0, sizeof(lin));
  memset(&lout, 0, sizeof(lout));

  req.hin.opcode = FUSE_LSEEK;
  req.hin.nodeid = fp->nodeid;

  lin.fh     = fp->fh;
  lin.offset = offset;
  lin.whence = whence;

  vin[0].iov_base = &lin;
  vin[0].iov_len  = sizeof(lin);

  vout[0].iov_base = &lout;
  vout[0].iov_len  = sizeof(lout);

  ret = fuse_request(conn, &req, vin, 1, vout, 1);
  if (ret >= 0)
    {
      filep->f_pos = lout.offset;
    }
  else if (ret != -ENOSYS)
    {
      return ret;
    }
  else /* Fallback: local seek */
    {
      switch (whence)
        {
          case SEEK_SET:
            filep->f_pos = offset;
            break;

          case SEEK_CUR:
            filep->f_pos += offset;
            break;

          case SEEK_END:
            {
              struct stat st;
              ret = fuse_fstat(filep, &st);
              if (ret < 0)
                {
                  return ret;
                }

              filep->f_pos = st.st_size + offset;
            }
            break;

          default:
            return -EINVAL;
        }
    }

  return filep->f_pos;
}

/****************************************************************************
 * Name: fuse_filelock
 *
 * Description:
 *   Translate a FIOC_GETLK/FIOC_SETLK/FIOC_SETLKW request into the
 *   corresponding FUSE lock operation.  The flock range is converted to
 *   the absolute inclusive byte range used on the wire.
 *
 ****************************************************************************/

static int fuse_filelock(FAR struct file *filep, int cmd,
                         FAR struct flock *flock)
{
  FAR struct inode *inode = filep->f_inode;
  FAR struct fuse_conn_s *conn = inode->i_private;
  FAR struct fuse_file_s *fp = filep->f_priv;
  struct fuse_lk_out lout;
  struct fuse_lk_in lin;
  struct fuse_req_s req;
  struct iovec vout[1];
  struct iovec vin[1];
  off_t start;
  off_t end;
  int ret;

  switch (flock->l_type)
    {
      case F_RDLCK:
      case F_WRLCK:
      case F_UNLCK:
        break;

      default:
        return -EINVAL;
    }

  switch (flock->l_whence)
    {
      case SEEK_SET:
        start = 0;
        break;

      case SEEK_CUR:
        start = filep->f_pos;
        break;

      case SEEK_END:
        {
          struct stat st;

          ret = fuse_fstat(filep, &st);
          if (ret < 0)
            {
              return ret;
            }

          start = st.st_size;
        }
        break;

      default:
        return -EINVAL;
    }

  if (flock->l_start > OFFSET_MAX - start)
    {
      return -EOVERFLOW;
    }

  start += flock->l_start;
  if (start < 0)
    {
      return -EINVAL;
    }

  if (flock->l_len > 0)
    {
      if (flock->l_len - 1 > OFFSET_MAX - start)
        {
          return -EOVERFLOW;
        }

      end = start + flock->l_len - 1;
    }
  else if (flock->l_len < 0)
    {
      if (start + flock->l_len < 0)
        {
          return -EINVAL;
        }

      end = start - 1;
      start += flock->l_len;
    }
  else
    {
      end = OFFSET_MAX;
    }

  memset(&req, 0, sizeof(req));
  memset(&lin, 0, sizeof(lin));
  memset(&lout, 0, sizeof(lout));

  req.hin.opcode = cmd == FIOC_GETLK ? FUSE_GETLK :
                   cmd == FIOC_SETLK ? FUSE_SETLK : FUSE_SETLKW;
  req.hin.nodeid = fp->nodeid;

  /* POSIX record locks are owned by the process */

  lin.fh       = fp->fh;
  lin.owner    = getpid();
  lin.lk.start = start;
  lin.lk.end   = end;
  lin.lk.type  = flock->l_type;
  lin.lk.pid   = getpid();

  vin[0].iov_base = &lin;
  vin[0].iov_len  = sizeof(lin);

  vout[0].iov_base = &lout;
  vout[0].iov_len  = sizeof(lout);

  ret = fuse_request(conn, &req, vin, 1, vout,
                     cmd == FIOC_GETLK ? 1 : 0);
  if (ret == -ENOSYS)
    {
      /* No lock support in the server: return ENOTTY so that the VFS
       * falls back to the local advisory lock implementation.
       */

      return -ENOTTY;
    }
  else if (ret < 0 || cmd != FIOC_GETLK)
    {
      return ret;
    }

  /* F_GETLK: report the first conflicting lock, or F_UNLCK if none */

  if (lout.lk.type == F_UNLCK)
    {
      flock->l_type = F_UNLCK;
    }
  else
    {
      flock->l_type   = lout.lk.type;
      flock->l_whence = SEEK_SET;
      flock->l_start  = lout.lk.start;
      flock->l_len    = lout.lk.end >= OFFSET_MAX ? 0 :
                        lout.lk.end - lout.lk.start + 1;
      flock->l_pid    = lout.lk.pid;
    }

  return 0;
}

/****************************************************************************
 * Name: fuse_filepath
 *
 * Description:
 *   Report the path recorded at open time:  the mountpoint path followed
 *   by the file's relative path.  FUSE has no reverse (nodeid to name)
 *   query, so a server-side rename since open is not reflected here.
 *
 ****************************************************************************/

static int fuse_filepath(FAR struct file *filep, FAR char *path)
{
  FAR struct fuse_file_s *fp = filep->f_priv;
  int ret;

  ret = inode_getpath(filep->f_inode, path, PATH_MAX);
  if (ret < 0)
    {
      return ret;
    }

  strlcat(path, fp->relpath, PATH_MAX);
  return 0;
}

/****************************************************************************
 * Name: fuse_ioctl
 ****************************************************************************/

static int fuse_ioctl(FAR struct file *filep, int cmd, unsigned long arg)
{
  switch (cmd)
    {
      case FIOC_SETLK:
      case FIOC_SETLKW:
      case FIOC_GETLK:
        return fuse_filelock(filep, cmd,
                             (FAR struct flock *)(uintptr_t)arg);

      case FIOC_FILEPATH:
        return fuse_filepath(filep, (FAR char *)(uintptr_t)arg);

      default:
        return -ENOTTY;
    }
}

/****************************************************************************
 * Name: fuse_truncate
 ****************************************************************************/

static int fuse_truncate(FAR struct file *filep, off_t length)
{
  FAR struct inode *inode = filep->f_inode;
  FAR struct fuse_conn_s *conn = inode->i_private;
  FAR struct fuse_file_s *fp = filep->f_priv;
  struct fuse_setattr_in sin;
  struct fuse_attr_out aout;
  struct fuse_req_s req;
  struct iovec vout[1];
  struct iovec vin[1];

  memset(&req, 0, sizeof(req));
  memset(&sin, 0, sizeof(sin));
  memset(&aout, 0, sizeof(aout));

  req.hin.opcode = FUSE_SETATTR;
  req.hin.nodeid = fp->nodeid;

  sin.valid = FATTR_SIZE | FATTR_FH;
  sin.size  = length;
  sin.fh    = fp->fh;

  vin[0].iov_base = &sin;
  vin[0].iov_len  = sizeof(sin);

  vout[0].iov_base = &aout;
  vout[0].iov_len  = sizeof(aout);

  return fuse_request(conn, &req, vin, 1, vout, 1);
}

/****************************************************************************
 * Name: fuse_sync
 ****************************************************************************/

static int fuse_sync(FAR struct file *filep)
{
  FAR struct inode *inode = filep->f_inode;
  FAR struct fuse_conn_s *conn = inode->i_private;
  FAR struct fuse_file_s *fp = filep->f_priv;
  struct fuse_fsync_in fin;
  struct fuse_req_s req;
  struct iovec vin[1];

  memset(&req, 0, sizeof(req));
  memset(&fin, 0, sizeof(fin));

  req.hin.opcode = FUSE_FSYNC;
  req.hin.nodeid = fp->nodeid;

  fin.fh = fp->fh;

  vin[0].iov_base = &fin;
  vin[0].iov_len  = sizeof(fin);

  return fuse_request(conn, &req, vin, 1, NULL, 0);
}

/****************************************************************************
 * Name: fuse_dup
 ****************************************************************************/

static int fuse_dup(FAR const struct file *oldp, FAR struct file *newp)
{
  FAR struct fuse_file_s *fp = oldp->f_priv;

  /* Both files share the same server-side handle, so bump the reference
   * instead of duplicating fh, which would make FUSE_RELEASE drop the
   * same handle twice.
   */

  atomic_add(&fp->refs, 1);
  newp->f_priv = fp;

  return 0;
}

/****************************************************************************
 * Name: fuse_fstat
 ****************************************************************************/

static int fuse_fstat(FAR const struct file *filep, FAR struct stat *buf)
{
  FAR struct inode *inode = filep->f_inode;
  FAR struct fuse_conn_s *conn = inode->i_private;
  FAR struct fuse_file_s *fp = filep->f_priv;
  struct fuse_getattr_in gin;
  struct fuse_attr_out aout;
  struct fuse_req_s req;
  struct iovec vout[1];
  struct iovec vin[1];
  int ret;

  memset(&req, 0, sizeof(req));
  memset(&gin, 0, sizeof(gin));
  memset(&aout, 0, sizeof(aout));

  req.hin.opcode = FUSE_GETATTR;
  req.hin.nodeid = fp->nodeid;

  gin.getattr_flags = FUSE_GETATTR_FH;
  gin.fh            = fp->fh;

  vin[0].iov_base = &gin;
  vin[0].iov_len  = sizeof(gin);

  vout[0].iov_base = &aout;
  vout[0].iov_len  = sizeof(aout);

  ret = fuse_request(conn, &req, vin, 1, vout, 1);
  if (ret < 0)
    {
      return ret;
    }

  fuse_attr_to_stat(&aout.attr, buf);
  return 0;
}

/****************************************************************************
 * Name: fuse_fchstat
 ****************************************************************************/

static int fuse_fchstat(FAR const struct file *filep,
                        FAR const struct stat *buf, int flags)
{
  FAR struct inode *inode = filep->f_inode;
  FAR struct fuse_conn_s *conn = inode->i_private;
  FAR struct fuse_file_s *fp = filep->f_priv;
  struct fuse_setattr_in sin;
  struct fuse_attr_out aout;
  struct fuse_req_s req;
  struct iovec vout[1];
  struct iovec vin[1];

  memset(&req, 0, sizeof(req));
  memset(&sin, 0, sizeof(sin));
  memset(&aout, 0, sizeof(aout));

  req.hin.opcode = FUSE_SETATTR;
  req.hin.nodeid = fp->nodeid;

  sin.valid = FATTR_FH;
  sin.fh    = fp->fh;

  if (flags & CH_STAT_MODE)
    {
      sin.valid |= FATTR_MODE;
      sin.mode   = buf->st_mode;
    }

  if (flags & CH_STAT_UID)
    {
      sin.valid |= FATTR_UID;
      sin.uid    = buf->st_uid;
    }

  if (flags & CH_STAT_GID)
    {
      sin.valid |= FATTR_GID;
      sin.gid    = buf->st_gid;
    }

  if (flags & CH_STAT_SIZE)
    {
      sin.valid |= FATTR_SIZE;
      sin.size   = buf->st_size;
    }

  if (flags & CH_STAT_ATIME)
    {
      sin.valid    |= FATTR_ATIME;
      sin.atime     = buf->st_atim.tv_sec;
      sin.atimensec = buf->st_atim.tv_nsec;
    }

  if (flags & CH_STAT_MTIME)
    {
      sin.valid    |= FATTR_MTIME;
      sin.mtime     = buf->st_mtim.tv_sec;
      sin.mtimensec = buf->st_mtim.tv_nsec;
    }

  vin[0].iov_base = &sin;
  vin[0].iov_len  = sizeof(sin);

  vout[0].iov_base = &aout;
  vout[0].iov_len  = sizeof(aout);

  return fuse_request(conn, &req, vin, 1, vout, 1);
}

/****************************************************************************
 * Name: fuse_opendir
 ****************************************************************************/

static int fuse_opendir(FAR struct inode *mountpt, FAR const char *relpath,
                        FAR struct fs_dirent_s **dir)
{
  FAR struct fuse_conn_s *conn = mountpt->i_private;
  FAR struct fuse_dir_s *fdir;
  struct fuse_entry_out entry;
  struct fuse_open_out oout;
  struct fuse_open_in oin;
  struct fuse_req_s req;
  struct iovec vout[1];
  struct iovec vin[1];
  uint64_t nodeid;
  int ret;

  if (relpath[0] == '\0' || (relpath[0] == '/' && relpath[1] == '\0'))
    {
      nodeid = FUSE_ROOT_ID;
    }
  else
    {
      ret = fuse_lookup_path(conn, relpath, &entry, true);
      if (ret < 0)
        {
          return ret;
        }

      nodeid = entry.nodeid;
    }

  fdir = fs_heap_zalloc(sizeof(*fdir));
  if (fdir == NULL)
    {
      return -ENOMEM;
    }

  memset(&req, 0, sizeof(req));
  memset(&oin, 0, sizeof(oin));
  memset(&oout, 0, sizeof(oout));

  req.hin.opcode = FUSE_OPENDIR;
  req.hin.nodeid = nodeid;

  oin.flags = O_RDONLY;

  vin[0].iov_base = &oin;
  vin[0].iov_len  = sizeof(oin);

  vout[0].iov_base = &oout;
  vout[0].iov_len  = sizeof(oout);

  ret = fuse_request(conn, &req, vin, 1, vout, 1);
  if (ret < 0)
    {
      fs_heap_free(fdir);
      return ret;
    }

  fdir->fh     = oout.fh;
  fdir->nodeid = nodeid;

  *dir = &fdir->base;
  return 0;
}

/****************************************************************************
 * Name: fuse_closedir
 ****************************************************************************/

static int fuse_closedir(FAR struct inode *mountpt,
                         FAR struct fs_dirent_s *dir)
{
  FAR struct fuse_conn_s *conn = mountpt->i_private;
  FAR struct fuse_dir_s *fdir = (FAR struct fuse_dir_s *)dir;
  struct fuse_release_in rin;
  struct fuse_req_s req;
  struct iovec vin[1];

  memset(&req, 0, sizeof(req));
  memset(&rin, 0, sizeof(rin));

  req.hin.opcode = FUSE_RELEASEDIR;
  req.hin.nodeid = fdir->nodeid;

  rin.fh = fdir->fh;

  vin[0].iov_base = &rin;
  vin[0].iov_len  = sizeof(rin);

  fuse_request(conn, &req, vin, 1, NULL, 0);
  fs_heap_free(fdir);

  return 0;
}

/****************************************************************************
 * Name: fuse_readdir
 ****************************************************************************/

static int fuse_readdir(FAR struct inode *mountpt,
                        FAR struct fs_dirent_s *dir,
                        FAR struct dirent *entry)
{
  FAR struct fuse_conn_s *conn = mountpt->i_private;
  FAR struct fuse_dir_s *fdir = (FAR struct fuse_dir_s *)dir;
  FAR struct fuse_dirent *fde;
  struct fuse_read_in rin;
  struct fuse_req_s req;
  struct iovec vout[1];
  struct iovec vin[1];

  /* Room for a single entry with the longest representable name:  only
   * the first entry of a reply is consumed before the stream offset is
   * advanced, so requesting more would just be discarded and refetched.
   */

  char buf[FUSE_DIRENT_ALIGN(FUSE_NAME_OFFSET + NAME_MAX)]
       aligned_data(sizeof(uint64_t));
  int ret;
  int len;

  memset(&req, 0, sizeof(req));
  memset(&rin, 0, sizeof(rin));

  req.hin.opcode = FUSE_READDIR;
  req.hin.nodeid = fdir->nodeid;

  rin.fh     = fdir->fh;
  rin.offset = fdir->offset;
  rin.size   = sizeof(buf);

  vin[0].iov_base = &rin;
  vin[0].iov_len  = sizeof(rin);

  vout[0].iov_base = buf;
  vout[0].iov_len  = sizeof(buf);

  ret = fuse_request(conn, &req, vin, 1, vout, 1);
  if (ret < 0)
    {
      return ret;
    }

  len = req.hout.len - sizeof(req.hout);
  if (len < FUSE_NAME_OFFSET)
    {
      return -ENOENT;
    }

  fde = (FAR struct fuse_dirent *)buf;
  if (fde->namelen == 0 || fde->namelen >= sizeof(entry->d_name))
    {
      return -ENOENT;
    }

  memcpy(entry->d_name, fde->name, fde->namelen);
  entry->d_name[fde->namelen] = '\0';
  entry->d_type = fde->type;

  fdir->offset = fde->off;
  return 0;
}

/****************************************************************************
 * Name: fuse_rewinddir
 ****************************************************************************/

static int fuse_rewinddir(FAR struct inode *mountpt,
                          FAR struct fs_dirent_s *dir)
{
  FAR struct fuse_dir_s *fdir = (FAR struct fuse_dir_s *)dir;

  fdir->offset = 0;
  return 0;
}

/****************************************************************************
 * Name: fuse_bind
 ****************************************************************************/

static int fuse_bind(FAR struct inode *blkdriver, FAR const void *data,
                     FAR void **handle)
{
  FAR struct fuse_conn_s *conn;
  int ret = -EINVAL;

  if (data == NULL)
    {
      return -EINVAL;
    }

  conn = fs_heap_zalloc(sizeof(*conn));
  if (conn == NULL)
    {
      return -ENOMEM;
    }

  spin_lock_init(&conn->lock);
  conn->max_write = FUSE_MAX_WRITE_BUFFER;

  /* Select the transport from the mount options.  Each transport claims a
   * distinct option keyword: "fd=" for the local /dev/fuse character
   * device, "tag=" for the virtio-conn device.
   */

#ifdef CONFIG_FS_FUSE_DEV
  ret = fuse_dev_connect(conn, data);
#endif

#ifdef CONFIG_FS_VIRTIOFS
  if (ret < 0)
    {
      ret = fuse_virtio_connect(conn, data);
    }
#endif

  if (ret < 0)
    {
      fs_heap_free(conn);
      return ret;
    }

  /* The FUSE_INIT handshake is deferred to the first request (see
   * fuse_request) so that mount() does not block while a single-threaded
   * server is still starting its read loop.
   */

  nxmutex_init(&conn->initlock);
  *handle = conn;

  return 0;
}

/****************************************************************************
 * Name: fuse_unbind
 ****************************************************************************/

static int fuse_unbind(FAR void *handle, FAR struct inode **blkdriver,
                       unsigned int flags)
{
  FAR struct fuse_conn_s *conn = handle;
  struct fuse_req_s req;

  /* Send FUSE_DESTROY only if the connection completed its handshake */

  if (atomic_read(&conn->initialized))
    {
      memset(&req, 0, sizeof(req));

      req.hin.opcode = FUSE_DESTROY;
      req.hin.nodeid = FUSE_ROOT_ID;

      fuse_request(conn, &req, NULL, 0, NULL, 0);
    }

  nxmutex_destroy(&conn->initlock);
  conn->transport->destroy(conn);
  fs_heap_free(conn);

  return 0;
}

/****************************************************************************
 * Name: fuse_statfs
 ****************************************************************************/

static int fuse_statfs(FAR struct inode *mountpt, FAR struct statfs *buf)
{
  FAR struct fuse_conn_s *conn = mountpt->i_private;
  struct fuse_statfs_out sout;
  struct fuse_req_s req;
  struct iovec vout[1];
  int ret;

  memset(&req, 0, sizeof(req));
  memset(&sout, 0, sizeof(sout));

  req.hin.opcode = FUSE_STATFS;
  req.hin.nodeid = FUSE_ROOT_ID;

  vout[0].iov_base = &sout;
  vout[0].iov_len  = sizeof(sout);

  ret = fuse_request(conn, &req, NULL, 0, vout, 1);
  if (ret < 0)
    {
      return ret;
    }

  memset(buf, 0, sizeof(*buf));

  buf->f_type    = FUSEFS_MAGIC; /* "FUSe" */
  buf->f_bsize   = sout.st.bsize;
  buf->f_blocks  = sout.st.blocks;
  buf->f_bfree   = sout.st.bfree;
  buf->f_bavail  = sout.st.bavail;
  buf->f_files   = sout.st.files;
  buf->f_ffree   = sout.st.ffree;
  buf->f_namelen = sout.st.namelen;

  return 0;
}

/****************************************************************************
 * Name: fuse_unlink
 ****************************************************************************/

static int fuse_unlink(FAR struct inode *mountpt, FAR const char *relpath)
{
  FAR struct fuse_conn_s *conn = mountpt->i_private;
  struct fuse_req_s req;
  struct iovec vin[1];
  FAR const char *name;
  uint64_t parent;
  int ret;

  ret = fuse_lookup_parent(conn, relpath, &parent, &name);
  if (ret < 0)
    {
      return ret;
    }

  memset(&req, 0, sizeof(req));

  req.hin.opcode = FUSE_UNLINK;
  req.hin.nodeid = parent;

  vin[0].iov_base = (FAR void *)name;
  vin[0].iov_len  = strlen(name) + 1;

  return fuse_request(conn, &req, vin, 1, NULL, 0);
}

/****************************************************************************
 * Name: fuse_mkdir
 ****************************************************************************/

static int fuse_mkdir(FAR struct inode *mountpt, FAR const char *relpath,
                      mode_t mode)
{
  FAR struct fuse_conn_s *conn = mountpt->i_private;
  struct fuse_entry_out eout;
  struct fuse_mkdir_in min;
  struct fuse_req_s req;
  struct iovec vout[1];
  struct iovec vin[2];
  FAR const char *name;
  uint64_t parent;
  int ret;

  ret = fuse_lookup_parent(conn, relpath, &parent, &name);
  if (ret < 0)
    {
      return ret;
    }

  memset(&req, 0, sizeof(req));
  memset(&min, 0, sizeof(min));
  memset(&eout, 0, sizeof(eout));

  req.hin.opcode = FUSE_MKDIR;
  req.hin.nodeid = parent;

  min.mode = mode;

  vin[0].iov_base = &min;
  vin[0].iov_len  = sizeof(min);
  vin[1].iov_base = (FAR void *)name;
  vin[1].iov_len  = strlen(name) + 1;

  vout[0].iov_base = &eout;
  vout[0].iov_len  = sizeof(eout);

  return fuse_request(conn, &req, vin, 2, vout, 1);
}

/****************************************************************************
 * Name: fuse_rmdir
 ****************************************************************************/

static int fuse_rmdir(FAR struct inode *mountpt, FAR const char *relpath)
{
  FAR struct fuse_conn_s *conn = mountpt->i_private;
  struct fuse_req_s req;
  struct iovec vin[1];
  FAR const char *name;
  uint64_t parent;
  int ret;

  ret = fuse_lookup_parent(conn, relpath, &parent, &name);
  if (ret < 0)
    {
      return ret;
    }

  memset(&req, 0, sizeof(req));

  req.hin.opcode = FUSE_RMDIR;
  req.hin.nodeid = parent;

  vin[0].iov_base = (FAR void *)name;
  vin[0].iov_len  = strlen(name) + 1;

  return fuse_request(conn, &req, vin, 1, NULL, 0);
}

/****************************************************************************
 * Name: fuse_rename
 ****************************************************************************/

static int fuse_rename(FAR struct inode *mountpt,
                       FAR const char *oldrelpath,
                       FAR const char *newrelpath)
{
  FAR struct fuse_conn_s *conn = mountpt->i_private;
  struct fuse_rename_in rin;
  struct fuse_req_s req;
  struct iovec vin[3];
  FAR const char *oldname;
  FAR const char *newname;
  uint64_t oldparent;
  uint64_t newparent;
  int ret;

  ret = fuse_lookup_parent(conn, oldrelpath, &oldparent, &oldname);
  if (ret < 0)
    {
      return ret;
    }

  ret = fuse_lookup_parent(conn, newrelpath, &newparent, &newname);
  if (ret < 0)
    {
      return ret;
    }

  memset(&req, 0, sizeof(req));
  memset(&rin, 0, sizeof(rin));

  req.hin.opcode = FUSE_RENAME;
  req.hin.nodeid = oldparent;

  rin.newdir = newparent;

  vin[0].iov_base = &rin;
  vin[0].iov_len  = sizeof(rin);
  vin[1].iov_base = (FAR void *)oldname;
  vin[1].iov_len  = strlen(oldname) + 1;
  vin[2].iov_base = (FAR void *)newname;
  vin[2].iov_len  = strlen(newname) + 1;

  return fuse_request(conn, &req, vin, 3, NULL, 0);
}

/****************************************************************************
 * Name: fuse_stat
 ****************************************************************************/

static int fuse_stat(FAR struct inode *mountpt, FAR const char *relpath,
                     FAR struct stat *buf)
{
  FAR struct fuse_conn_s *conn = mountpt->i_private;
  struct fuse_entry_out entry;
  int ret;

  ret = fuse_lookup_path(conn, relpath, &entry, true);
  if (ret < 0)
    {
      return ret;
    }

  fuse_attr_to_stat(&entry.attr, buf);
  return 0;
}

/****************************************************************************
 * Name: fuse_chstat
 ****************************************************************************/

static int fuse_chstat(FAR struct inode *mountpt, FAR const char *relpath,
                       FAR const struct stat *buf, int flags)
{
  FAR struct fuse_conn_s *conn = mountpt->i_private;
  struct fuse_entry_out entry;
  struct fuse_setattr_in sin;
  struct fuse_attr_out aout;
  struct fuse_req_s req;
  struct iovec vout[1];
  struct iovec vin[1];
  int ret;

  ret = fuse_lookup_path(conn, relpath, &entry, true);
  if (ret < 0)
    {
      return ret;
    }

  memset(&req, 0, sizeof(req));
  memset(&sin, 0, sizeof(sin));
  memset(&aout, 0, sizeof(aout));

  req.hin.opcode = FUSE_SETATTR;
  req.hin.nodeid = entry.nodeid;

  if (flags & CH_STAT_MODE)
    {
      sin.valid |= FATTR_MODE;
      sin.mode   = buf->st_mode;
    }

  if (flags & CH_STAT_UID)
    {
      sin.valid |= FATTR_UID;
      sin.uid    = buf->st_uid;
    }

  if (flags & CH_STAT_GID)
    {
      sin.valid |= FATTR_GID;
      sin.gid    = buf->st_gid;
    }

  if (flags & CH_STAT_SIZE)
    {
      sin.valid |= FATTR_SIZE;
      sin.size   = buf->st_size;
    }

  if (flags & CH_STAT_ATIME)
    {
      sin.valid    |= FATTR_ATIME;
      sin.atime     = buf->st_atim.tv_sec;
      sin.atimensec = buf->st_atim.tv_nsec;
    }

  if (flags & CH_STAT_MTIME)
    {
      sin.valid    |= FATTR_MTIME;
      sin.mtime     = buf->st_mtim.tv_sec;
      sin.mtimensec = buf->st_mtim.tv_nsec;
    }

  vin[0].iov_base = &sin;
  vin[0].iov_len  = sizeof(sin);

  vout[0].iov_base = &aout;
  vout[0].iov_len  = sizeof(aout);

  return fuse_request(conn, &req, vin, 1, vout, 1);
}

/****************************************************************************
 * Name: fuse_syncfs
 ****************************************************************************/

static int fuse_syncfs(FAR struct inode *mountpt)
{
  FAR struct fuse_conn_s *conn = mountpt->i_private;
  struct fuse_req_s req;

  memset(&req, 0, sizeof(req));

  req.hin.opcode = FUSE_SYNCFS;
  req.hin.nodeid = FUSE_ROOT_ID;

  return fuse_request(conn, &req, NULL, 0, NULL, 0);
}

#ifdef CONFIG_FS_LINKS
/****************************************************************************
 * Name: fuse_link
 ****************************************************************************/

static int fuse_link(FAR struct inode *mountpt, FAR const char *relpath1,
                     FAR const char *relpath2)
{
  FAR struct fuse_conn_s *conn = mountpt->i_private;
  struct fuse_entry_out entry;
  struct fuse_link_in lin;
  struct fuse_req_s req;
  struct iovec vout[1];
  struct iovec vin[2];
  FAR const char *name;
  uint64_t parent;
  int ret;

  /* Lookup the target to get its nodeid */

  ret = fuse_lookup_path(conn, relpath1, &entry, false);
  if (ret < 0)
    {
      return ret;
    }

  /* Lookup the parent of the new link */

  ret = fuse_lookup_parent(conn, relpath2, &parent, &name);
  if (ret < 0)
    {
      return ret;
    }

  memset(&req, 0, sizeof(req));
  memset(&lin, 0, sizeof(lin));

  req.hin.opcode = FUSE_LINK;
  req.hin.nodeid = parent;

  lin.oldnodeid = entry.nodeid;

  vin[0].iov_base = &lin;
  vin[0].iov_len  = sizeof(lin);
  vin[1].iov_base = (FAR void *)name;
  vin[1].iov_len  = strlen(name) + 1;

  vin[0].iov_base = &entry;
  vin[0].iov_len  = sizeof(entry);

  return fuse_request(conn, &req, vin, 2, vout, 1);
}

/****************************************************************************
 * Name: fuse_symlink
 ****************************************************************************/

static int fuse_symlink(FAR struct inode *mountpt, FAR const char *relpath1,
                        FAR const char *relpath2)
{
  FAR struct fuse_conn_s *conn = mountpt->i_private;
  struct fuse_entry_out entry;
  struct fuse_req_s req;
  struct iovec vout[1];
  struct iovec vin[2];
  FAR const char *name;
  uint64_t parent;
  int ret;

  ret = fuse_lookup_parent(conn, relpath2, &parent, &name);
  if (ret < 0)
    {
      return ret;
    }

  memset(&req, 0, sizeof(req));
  memset(&entry, 0, sizeof(entry));

  req.hin.opcode = FUSE_SYMLINK;
  req.hin.nodeid = parent;

  /* FUSE_SYMLINK: name + '\0' + target + '\0' */

  vin[0].iov_base = (FAR void *)name;
  vin[0].iov_len  = strlen(name) + 1;
  vin[1].iov_base = (FAR void *)relpath1;
  vin[1].iov_len  = strlen(relpath1) + 1;

  vout[0].iov_base = &entry;
  vout[0].iov_len  = sizeof(entry);

  return fuse_request(conn, &req, vin, 2, vout, 1);
}

/****************************************************************************
 * Name: fuse_readlink
 ****************************************************************************/

static ssize_t fuse_readlink(FAR struct inode *mountpt,
                             FAR const char *relpath,
                             FAR char *buf, size_t buflen)
{
  FAR struct fuse_conn_s *conn = mountpt->i_private;
  struct fuse_entry_out entry;
  ssize_t ret;

  ret = fuse_lookup_path(conn, relpath, &entry, false);
  if (ret < 0)
    {
      return ret;
    }

  ret = fuse_readlink_node(conn, entry.nodeid, buf, buflen);
  if (ret > 0 && ret < buflen)
    {
      buf[ret] = '\0';
    }

  return ret;
}

/****************************************************************************
 * Name: fuse_lstat
 ****************************************************************************/

static int fuse_lstat(FAR struct inode *mountpt, FAR const char *relpath,
                      FAR struct stat *buf)
{
  FAR struct fuse_conn_s *conn = mountpt->i_private;
  struct fuse_entry_out entry;
  int ret;

  ret = fuse_lookup_path(conn, relpath, &entry, false);
  if (ret < 0)
    {
      return ret;
    }

  fuse_attr_to_stat(&entry.attr, buf);
  return 0;
}
#endif /* CONFIG_FS_LINKS */

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: fuse_request
 ****************************************************************************/

int fuse_request(FAR struct fuse_conn_s *conn, FAR struct fuse_req_s *req,
                 FAR const struct iovec *vin, size_t vin_cnt,
                 FAR const struct iovec *vout, size_t vout_cnt)
{
  irqstate_t flags;
  size_t i;
  int ret;

  /* Lazily perform the FUSE_INIT handshake on the first non-INIT request so
   * that mount() does not block while a single-threaded server is still
   * starting its read loop.
   */

  if (req->hin.opcode != FUSE_INIT &&
      !atomic_read_acquire(&conn->initialized))
    {
      nxmutex_lock(&conn->initlock);
      if (!atomic_read(&conn->initialized))
        {
          ret = fuse_init(conn);
          if (ret < 0)
            {
              nxmutex_unlock(&conn->initlock);
              return ret;
            }

          /* Publish the fields negotiated by fuse_init before the flag:
           * the release store pairs with the acquire load in the unlocked
           * fast path above.
           */

          atomic_set_release(&conn->initialized, 1);
        }

      nxmutex_unlock(&conn->initlock);
    }

  /* The total request length is the header plus all request arguments */

  req->hin.len = sizeof(req->hin);
  for (i = 0; i < vin_cnt; i++)
    {
      req->hin.len += vin[i].iov_len;
    }

  flags = spin_lock_irqsave(&conn->lock);
  req->hin.unique = ++conn->unique;
  spin_unlock_irqrestore(&conn->lock, flags);

  req->hin.uid = geteuid();
  req->hin.gid = getegid();
  req->hin.pid = getpid();

  req->vin      = vin;
  req->vin_cnt  = vin_cnt;
  req->vout     = vout;
  req->vout_cnt = vout_cnt;

  /* Submit through the transport and wait for the reply.  The completion
   * path posts req->wait.
   */

  nxsem_init(&req->wait, 0, 0);
  ret = conn->transport->request(conn, req);
  if (ret >= 0)
    {
      nxsem_wait_uninterruptible(&req->wait);
    }

  nxsem_destroy(&req->wait);
  return ret < 0 ? ret : req->hout.error;
}
