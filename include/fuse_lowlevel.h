/****************************************************************************
 * include/fuse_lowlevel.h
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

#ifndef __INCLUDE_FUSE_LOWLEVEL_H
#define __INCLUDE_FUSE_LOWLEVEL_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

#include <stddef.h>
#include <stdint.h>

#include "fuse_opt.h"

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Information about an open file (shared by the high and low level API) */

struct fuse_file_info
{
  int      flags;            /* Open flags */
  unsigned int writepage : 1;
  unsigned int direct_io  : 1;
  unsigned int keep_cache : 1;
  unsigned int flush      : 1;
  unsigned int nonseekable : 1;
  uint64_t fh;               /* File handle set by the filesystem */
  uint64_t lock_owner;       /* Lock owner id */
};

/* Connection information passed to init() */

struct fuse_conn_info
{
  uint32_t proto_major;
  uint32_t proto_minor;
  uint32_t max_write;
  uint32_t max_read;
  uint32_t max_readahead;
  uint32_t capable;
  uint32_t want;
};

/* Reply to a lookup/create request */

struct fuse_entry_param
{
  uint64_t    ino;           /* Unique inode number (nodeid) */
  uint64_t    generation;    /* Inode generation */
  struct stat attr;          /* Inode attributes */
  double      attr_timeout;  /* Attribute cache timeout (seconds) */
  double      entry_timeout; /* Name cache timeout (seconds) */
};

/* The caller context of a request */

struct fuse_ctx
{
  uid_t uid;
  gid_t gid;
  pid_t pid;
  mode_t umask;
};

/* Opaque request and session handles */

struct fuse_req;
typedef FAR struct fuse_req *fuse_req_t;

struct fuse_session;

/* Low level filesystem operations.  Every callback (except forget) must
 * eventually answer with one of the fuse_reply_* helpers.
 */

struct fuse_lowlevel_ops
{
  CODE void (*init)(FAR void *userdata, FAR struct fuse_conn_info *conn);
  CODE void (*destroy)(FAR void *userdata);
  CODE void (*lookup)(fuse_req_t req, uint64_t parent, FAR const char *name);
  CODE void (*forget)(fuse_req_t req, uint64_t ino, uint64_t nlookup);
  CODE void (*getattr)(fuse_req_t req, uint64_t ino,
                       FAR struct fuse_file_info *fi);
  CODE void (*setattr)(fuse_req_t req, uint64_t ino, FAR struct stat *attr,
                       int to_set, FAR struct fuse_file_info *fi);
  CODE void (*readlink)(fuse_req_t req, uint64_t ino);
  CODE void (*mkdir)(fuse_req_t req, uint64_t parent, FAR const char *name,
                     mode_t mode);
  CODE void (*unlink)(fuse_req_t req, uint64_t parent, FAR const char *name);
  CODE void (*rmdir)(fuse_req_t req, uint64_t parent, FAR const char *name);
  CODE void (*symlink)(fuse_req_t req, FAR const char *link, uint64_t parent,
                       FAR const char *name);
  CODE void (*rename)(fuse_req_t req, uint64_t parent, FAR const char *name,
                      uint64_t newparent, FAR const char *newname,
                      unsigned int flags);
  CODE void (*link)(fuse_req_t req, uint64_t ino, uint64_t newparent,
                    FAR const char *newname);
  CODE void (*open)(fuse_req_t req, uint64_t ino,
                    FAR struct fuse_file_info *fi);
  CODE void (*read)(fuse_req_t req, uint64_t ino, size_t size, off_t off,
                    FAR struct fuse_file_info *fi);
  CODE void (*write)(fuse_req_t req, uint64_t ino, FAR const char *buf,
                     size_t size, off_t off, FAR struct fuse_file_info *fi);
  CODE void (*flush)(fuse_req_t req, uint64_t ino,
                     FAR struct fuse_file_info *fi);
  CODE void (*release)(fuse_req_t req, uint64_t ino,
                       FAR struct fuse_file_info *fi);
  CODE void (*fsync)(fuse_req_t req, uint64_t ino, int datasync,
                     FAR struct fuse_file_info *fi);
  CODE void (*opendir)(fuse_req_t req, uint64_t ino,
                       FAR struct fuse_file_info *fi);
  CODE void (*readdir)(fuse_req_t req, uint64_t ino, size_t size, off_t off,
                       FAR struct fuse_file_info *fi);
  CODE void (*releasedir)(fuse_req_t req, uint64_t ino,
                          FAR struct fuse_file_info *fi);
  CODE void (*statfs)(fuse_req_t req, uint64_t ino);
  CODE void (*create)(fuse_req_t req, uint64_t parent, FAR const char *name,
                      mode_t mode, FAR struct fuse_file_info *fi);
};

#if defined(__cplusplus)
extern "C"
{
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/* Reply helpers */

int    fuse_reply_err(fuse_req_t req, int err);
void   fuse_reply_none(fuse_req_t req);
int    fuse_reply_entry(fuse_req_t req,
                        FAR const struct fuse_entry_param *e);
int    fuse_reply_create(fuse_req_t req,
                         FAR const struct fuse_entry_param *e,
                         FAR const struct fuse_file_info *fi);
int    fuse_reply_attr(fuse_req_t req, FAR const struct stat *attr,
                       double attr_timeout);
int    fuse_reply_readlink(fuse_req_t req, FAR const char *link);
int    fuse_reply_open(fuse_req_t req, FAR const struct fuse_file_info *fi);
int    fuse_reply_write(fuse_req_t req, size_t count);
int    fuse_reply_buf(fuse_req_t req, FAR const char *buf, size_t size);
int    fuse_reply_statfs(fuse_req_t req, FAR const struct statvfs *stbuf);
size_t fuse_add_direntry(fuse_req_t req, FAR char *buf, size_t bufsize,
                         FAR const char *name, FAR const struct stat *stbuf,
                         off_t off);

/* Request context accessors */

FAR void                 *fuse_req_userdata(fuse_req_t req);
FAR const struct fuse_ctx *fuse_req_ctx(fuse_req_t req);

/* Session lifecycle */

FAR struct fuse_session *
fuse_session_new(FAR struct fuse_args *args,
                 FAR const struct fuse_lowlevel_ops *op, size_t op_size,
                 FAR void *userdata);
int  fuse_session_mount(FAR struct fuse_session *se,
                        FAR const char *mountpoint);
int  fuse_session_loop(FAR struct fuse_session *se);
void fuse_session_exit(FAR struct fuse_session *se);
void fuse_session_unmount(FAR struct fuse_session *se);
void fuse_session_destroy(FAR struct fuse_session *se);

#if defined(__cplusplus)
}
#endif

#endif /* __INCLUDE_FUSE_LOWLEVEL_H */
