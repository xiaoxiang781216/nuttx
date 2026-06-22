/****************************************************************************
 * include/fuse.h
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

#ifndef __INCLUDE_FUSE_H
#define __INCLUDE_FUSE_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

#include <time.h>

#include "fuse_lowlevel.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define fuse_main(argc, argv, op, private_data) \
  fuse_main_real(argc, argv, op, sizeof(*(op)), private_data)

/****************************************************************************
 * Public Types
 ****************************************************************************/

enum fuse_readdir_flags
{
  FUSE_READDIR_PLUS = (1 << 0)
};

enum fuse_fill_dir_flags
{
  FUSE_FILL_DIR_PLUS = (1 << 1)
};

/* Directory entry filler used by readdir */

typedef CODE int (*fuse_fill_dir_t)(FAR void *buf, FAR const char *name,
                                    FAR const struct stat *stbuf, off_t off,
                                    enum fuse_fill_dir_flags flags);

/* High level filesystem configuration (subset) */

struct fuse_config
{
  int          set_gid;
  unsigned int gid;
  int          set_uid;
  unsigned int uid;
  int          set_mode;
  unsigned int umask;
  double       entry_timeout;
  double       negative_timeout;
  double       attr_timeout;
  int          use_ino;
  int          direct_io;
  int          kernel_cache;
};

/* High level filesystem operations.  Unused callbacks must be NULL. */

struct fuse_operations
{
  CODE int  (*getattr)(FAR const char *path, FAR struct stat *stbuf,
                       FAR struct fuse_file_info *fi);
  CODE int  (*readlink)(FAR const char *path, FAR char *buf, size_t size);
  CODE int  (*mknod)(FAR const char *path, mode_t mode, dev_t rdev);
  CODE int  (*mkdir)(FAR const char *path, mode_t mode);
  CODE int  (*unlink)(FAR const char *path);
  CODE int  (*rmdir)(FAR const char *path);
  CODE int  (*symlink)(FAR const char *from, FAR const char *to);
  CODE int  (*rename)(FAR const char *from, FAR const char *to,
                      unsigned int flags);
  CODE int  (*link)(FAR const char *from, FAR const char *to);
  CODE int  (*chmod)(FAR const char *path, mode_t mode,
                     FAR struct fuse_file_info *fi);
  CODE int  (*chown)(FAR const char *path, uid_t uid, gid_t gid,
                     FAR struct fuse_file_info *fi);
  CODE int  (*truncate)(FAR const char *path, off_t size,
                        FAR struct fuse_file_info *fi);
  CODE int  (*open)(FAR const char *path, FAR struct fuse_file_info *fi);
  CODE int  (*read)(FAR const char *path, FAR char *buf, size_t size,
                    off_t off, FAR struct fuse_file_info *fi);
  CODE int  (*write)(FAR const char *path, FAR const char *buf, size_t size,
                     off_t off, FAR struct fuse_file_info *fi);
  CODE int  (*statfs)(FAR const char *path, FAR struct statvfs *stbuf);
  CODE int  (*flush)(FAR const char *path, FAR struct fuse_file_info *fi);
  CODE int  (*release)(FAR const char *path, FAR struct fuse_file_info *fi);
  CODE int  (*fsync)(FAR const char *path, int datasync,
                     FAR struct fuse_file_info *fi);
  CODE int  (*opendir)(FAR const char *path, FAR struct fuse_file_info *fi);
  CODE int  (*readdir)(FAR const char *path, FAR void *buf,
                       fuse_fill_dir_t filler, off_t off,
                       FAR struct fuse_file_info *fi,
                       enum fuse_readdir_flags flags);
  CODE int  (*releasedir)(FAR const char *path,
                          FAR struct fuse_file_info *fi);
  CODE int  (*fsyncdir)(FAR const char *path, int datasync,
                        FAR struct fuse_file_info *fi);
  CODE FAR void *(*init)(FAR struct fuse_conn_info *conn,
                         FAR struct fuse_config *cfg);
  CODE void (*destroy)(FAR void *private_data);
  CODE int  (*access)(FAR const char *path, int mask);
  CODE int  (*create)(FAR const char *path, mode_t mode,
                      FAR struct fuse_file_info *fi);
  CODE int  (*utimens)(FAR const char *path, const struct timespec tv[2],
                       FAR struct fuse_file_info *fi);
};

/* Per-request context */

struct fuse_context
{
  FAR struct fuse *fuse;
  uid_t            uid;
  gid_t            gid;
  pid_t            pid;
  FAR void        *private_data;
  mode_t           umask;
};

struct fuse;

#if defined(__cplusplus)
extern "C"
{
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: fuse_main_real
 *
 * Description:
 *   Create, mount and run a high level FUSE filesystem until unmounted.
 *   Use the fuse_main() macro instead of calling this directly.
 *
 ****************************************************************************/

int fuse_main_real(int argc, FAR char *argv[],
                   FAR const struct fuse_operations *op, size_t op_size,
                   FAR void *private_data);

/****************************************************************************
 * Name: fuse_new
 ****************************************************************************/

FAR struct fuse *fuse_new(FAR struct fuse_args *args,
                          FAR const struct fuse_operations *op,
                          size_t op_size, FAR void *private_data);

/****************************************************************************
 * Name: fuse_mount / fuse_unmount
 ****************************************************************************/

int  fuse_mount(FAR struct fuse *f, FAR const char *mountpoint);
void fuse_unmount(FAR struct fuse *f);

/****************************************************************************
 * Name: fuse_loop
 ****************************************************************************/

int fuse_loop(FAR struct fuse *f);

/****************************************************************************
 * Name: fuse_destroy
 ****************************************************************************/

void fuse_destroy(FAR struct fuse *f);

/****************************************************************************
 * Name: fuse_get_context
 ****************************************************************************/

FAR struct fuse_context *fuse_get_context(void);

#if defined(__cplusplus)
}
#endif

#endif /* __INCLUDE_FUSE_H */
