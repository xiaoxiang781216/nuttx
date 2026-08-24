/****************************************************************************
 * include/fuse.h
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

#ifndef __INCLUDE_FUSE_H
#define __INCLUDE_FUSE_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <fcntl.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/uio.h>
#include <fuse_common.h>

#include <nuttx/tls.h>

/****************************************************************************
 * Public Type Definitions
 ****************************************************************************/

struct fuse;
struct fuse_fs;

/* Readdir flags and filler callback */

enum fuse_readdir_flags
{
  FUSE_READDIR_DEFAULTS = 0,
  FUSE_READDIR_PLUS = (1 << 0),
};

enum fuse_fill_dir_flags
{
  FUSE_FILL_DIR_DEFAULTS = 0,
  FUSE_FILL_DIR_PLUS = (1 << 1),
};

typedef CODE int (*fuse_fill_dir_t)(FAR void *buf, FAR const char *name,
                                    FAR const struct stat *st, off_t off,
                                    enum fuse_fill_dir_flags flags);

/* High level filesystem configuration */

struct fuse_config
{
  int32_t set_gid;
  uint32_t gid;
  int32_t set_uid;
  uint32_t uid;
  int32_t set_mode;
  uint32_t umask;
  double entry_timeout;
  double negative_timeout;
  double attr_timeout;
  int32_t intr;
  int32_t intr_signal;
  int32_t remember;
  int32_t hard_remove;
  int32_t use_ino;
  int32_t readdir_ino;
  int32_t direct_io;
  int32_t kernel_cache;
  int32_t auto_cache;
  int32_t ac_attr_timeout_set;
  double ac_attr_timeout;
  int32_t nullpath_ok;
  int32_t show_help;
  FAR char *modules;
  int32_t debug;
  uint32_t fmask;
  uint32_t dmask;
  int32_t no_rofd_flush;
  int32_t parallel_direct_writes;
  uint32_t flags;
  uint64_t reserved[48];
};

/* The file system operations:
 *
 * Most of these should work very similarly to the well known UNIX
 * file system operations.  A major exception is that instead of
 * returning an error in 'errno', the operation should return the
 * negated error value (-errno) directly.
 *
 * All methods are optional, but some are essential for a useful
 * filesystem (e.g. getattr).  Open, flush, release, fsync, opendir,
 * releasedir, fsyncdir, access, create, truncate, lock, init and
 * destroy are special purpose methods, without which a full featured
 * filesystem can still be implemented.
 *
 * Operations the NuttX kernel never issues are kept so upstream
 * filesystems register unchanged; they answer -ENOSYS if reached.
 *
 * readdir offset contract: a callback that supports offset-based
 * resume seeks by the given offset and passes the telldir() position
 * of the next entry to the filler; the filler cookie then lets the
 * kernel resume at that entry.  A callback that always fills from
 * the start may pass 0 to the filler instead.  One callback must not
 * mix both modes.
 */

struct fuse_operations
{
  CODE int (*getattr)(FAR const char *path, FAR struct stat *stbuf,
                      FAR struct fuse_file_info *fi);
  CODE int (*readlink)(FAR const char *path, FAR char *buf, size_t size);
  CODE int (*mknod)(FAR const char *path, mode_t mode, dev_t dev);
  CODE int (*mkdir)(FAR const char *path, mode_t mode);
  CODE int (*unlink)(FAR const char *path);
  CODE int (*rmdir)(FAR const char *path);
  CODE int (*symlink)(FAR const char *from, FAR const char *to);
  CODE int (*rename)(FAR const char *from, FAR const char *to,
                     unsigned int flags);
  CODE int (*link)(FAR const char *from, FAR const char *to);
  CODE int (*chmod)(FAR const char *path, mode_t mode,
                    FAR struct fuse_file_info *fi);
  CODE int (*chown)(FAR const char *path, uid_t uid, gid_t gid,
                    FAR struct fuse_file_info *fi);
  CODE int (*truncate)(FAR const char *path, off_t size,
                       FAR struct fuse_file_info *fi);
  CODE int (*open)(FAR const char *path, FAR struct fuse_file_info *fi);
  CODE int (*read)(FAR const char *path, FAR char *buf, size_t size,
                   off_t offset, FAR struct fuse_file_info *fi);
  CODE int (*write)(FAR const char *path, FAR const char *buf, size_t size,
                    off_t offset, FAR struct fuse_file_info *fi);
  CODE int (*statfs)(FAR const char *path, FAR struct statvfs *stbuf);
  CODE int (*flush)(FAR const char *path,
                    FAR struct fuse_file_info *fi);
  CODE int (*release)(FAR const char *path,
                      FAR struct fuse_file_info *fi);
  CODE int (*fsync)(FAR const char *path, int isdatasync,
                    FAR struct fuse_file_info *fi);
  CODE int (*setxattr)(FAR const char *path, FAR const char *name,
                       FAR const char *value, size_t size, int flags);
  CODE int (*getxattr)(FAR const char *path, FAR const char *name,
                       FAR char *value, size_t size);
  CODE int (*listxattr)(FAR const char *path, FAR char *list,
                        size_t size);
  CODE int (*removexattr)(FAR const char *path, FAR const char *name);
  CODE int (*opendir)(FAR const char *path, FAR struct fuse_file_info *fi);
  CODE int (*readdir)(FAR const char *path, FAR void *buf,
                      fuse_fill_dir_t filler, off_t offset,
                      FAR struct fuse_file_info *fi,
                      enum fuse_readdir_flags flags);
  CODE int (*releasedir)(FAR const char *path,
                         FAR struct fuse_file_info *fi);
  CODE int (*fsyncdir)(FAR const char *path, int isdatasync,
                       FAR struct fuse_file_info *fi);
  CODE void *(*init)(FAR struct fuse_conn_info *conn,
                     FAR struct fuse_config *cfg);
  CODE void (*destroy)(FAR void *private_data);
  CODE int (*access)(FAR const char *path, int mask);
  CODE int (*create)(FAR const char *path, mode_t mode,
                     FAR struct fuse_file_info *fi);
  CODE int (*lock)(FAR const char *path, FAR struct fuse_file_info *fi,
                   int cmd, FAR struct flock *lock);
  CODE int (*utimens)(FAR const char *path,
                      FAR const struct timespec tv[2],
                      FAR struct fuse_file_info *fi);
  CODE int (*bmap)(FAR const char *path, size_t blocksize, uint64_t *idx);
  CODE int (*ioctl)(FAR const char *path, unsigned int cmd, FAR void *arg,
                    FAR struct fuse_file_info *fi, unsigned int flags,
                    FAR void *data);
  CODE int (*poll)(FAR const char *path, FAR struct fuse_file_info *fi,
                   FAR struct fuse_pollhandle *ph, unsigned *reventsp);
  CODE int (*write_buf)(FAR const char *path,
                        FAR struct fuse_bufvec *bufv, off_t off,
                        FAR struct fuse_file_info *fi);
  CODE int (*read_buf)(FAR const char *path,
                       FAR struct fuse_bufvec **bufvp, size_t size,
                       off_t off, FAR struct fuse_file_info *fi);
  CODE int (*flock)(FAR const char *path, FAR struct fuse_file_info *fi,
                    int op);
  CODE int (*fallocate)(FAR const char *path, int mode, off_t offset,
                        off_t length, FAR struct fuse_file_info *fi);
  CODE ssize_t (*copy_file_range)(FAR const char *src_path,
                                  FAR struct fuse_file_info *src_fi,
                                  off_t src_off, FAR const char *dst_path,
                                  FAR struct fuse_file_info *dst_fi,
                                  off_t dst_off, size_t len, int flags);
  CODE off_t (*lseek)(FAR const char *path, off_t off, int whence,
                      FAR struct fuse_file_info *fi);
  CODE int (*statx)(FAR const char *path, FAR struct statx *stx,
                    unsigned int request_mask, unsigned int flags,
                    FAR struct fuse_file_info *fi);
  CODE int (*syncfs)(FAR const char *path,
                     FAR struct fuse_file_info *fi);
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef __cplusplus
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

int fuse_main(int argc, FAR char *argv[],
              FAR const struct fuse_operations *op,
              FAR void *private_data);

/****************************************************************************
 * More detailed API
 ****************************************************************************/

void fuse_lib_help(FAR struct fuse_args *args);

FAR struct fuse *fuse_new(FAR struct fuse_args *args,
                          FAR const struct fuse_operations *op,
                          size_t op_size, FAR void *private_data);

int fuse_mount(FAR struct fuse *f, FAR const char *mountpoint);
int fuse_mount_custom(FAR struct fuse *f,
                      FAR const struct fuse_custom_io *io, int fd);
void fuse_unmount(FAR struct fuse *f);

void fuse_destroy(FAR struct fuse *f);

int fuse_loop(FAR struct fuse *f);
int fuse_loop_mt(FAR struct fuse *f, FAR struct fuse_loop_config *config);
void fuse_exit(FAR struct fuse *f);

FAR struct fuse_context *fuse_get_context(void);

int fuse_invalidate_path(FAR struct fuse *f, FAR const char *path);

/****************************************************************************
 * Stacking API
 ****************************************************************************/

int fuse_fs_getattr(FAR struct fuse_fs *fs, FAR const char *path,
                    FAR struct stat *buf, FAR struct fuse_file_info *fi);
int fuse_fs_rename(FAR struct fuse_fs *fs, FAR const char *oldpath,
                   FAR const char *newpath, unsigned int flags);
int fuse_fs_unlink(FAR struct fuse_fs *fs, FAR const char *path);
int fuse_fs_rmdir(FAR struct fuse_fs *fs, FAR const char *path);
int fuse_fs_symlink(FAR struct fuse_fs *fs, FAR const char *linkname,
                    FAR const char *path);
int fuse_fs_link(FAR struct fuse_fs *fs, FAR const char *oldpath,
                 FAR const char *newpath);
int fuse_fs_release(FAR struct fuse_fs *fs, FAR const char *path,
                    FAR struct fuse_file_info *fi);
int fuse_fs_open(FAR struct fuse_fs *fs, FAR const char *path,
                 FAR struct fuse_file_info *fi);
int fuse_fs_read(FAR struct fuse_fs *fs, FAR const char *path,
                 FAR char *buf, size_t size, off_t off,
                 FAR struct fuse_file_info *fi);
int fuse_fs_read_buf(FAR struct fuse_fs *fs, FAR const char *path,
                     FAR struct fuse_bufvec **bufp, size_t size,
                     off_t off, FAR struct fuse_file_info *fi);
int fuse_fs_write(FAR struct fuse_fs *fs, FAR const char *path,
                  FAR const char *buf, size_t size, off_t off,
                  FAR struct fuse_file_info *fi);
int fuse_fs_write_buf(FAR struct fuse_fs *fs, FAR const char *path,
                      FAR struct fuse_bufvec *buf, off_t off,
                      FAR struct fuse_file_info *fi);
int fuse_fs_fsync(FAR struct fuse_fs *fs, FAR const char *path,
                  int isdatasync, FAR struct fuse_file_info *fi);
int fuse_fs_flush(FAR struct fuse_fs *fs, FAR const char *path,
                  FAR struct fuse_file_info *fi);
int fuse_fs_statfs(FAR struct fuse_fs *fs, FAR const char *path,
                   FAR struct statvfs *buf);
int fuse_fs_opendir(FAR struct fuse_fs *fs, FAR const char *path,
                    FAR struct fuse_file_info *fi);
int fuse_fs_readdir(FAR struct fuse_fs *fs, FAR const char *path,
                    FAR void *buf, fuse_fill_dir_t filler, off_t offset,
                    FAR struct fuse_file_info *fi,
                    enum fuse_readdir_flags flags);
int fuse_fs_fsyncdir(FAR struct fuse_fs *fs, FAR const char *path,
                     int isdatasync, FAR struct fuse_file_info *fi);
int fuse_fs_releasedir(FAR struct fuse_fs *fs, FAR const char *path,
                       FAR struct fuse_file_info *fi);
int fuse_fs_create(FAR struct fuse_fs *fs, FAR const char *path,
                   mode_t mode, FAR struct fuse_file_info *fi);
int fuse_fs_lock(FAR struct fuse_fs *fs, FAR const char *path,
                 FAR struct fuse_file_info *fi, int cmd,
                 FAR struct flock *lock);
int fuse_fs_flock(FAR struct fuse_fs *fs, FAR const char *path,
                  FAR struct fuse_file_info *fi, int op);
int fuse_fs_chmod(FAR struct fuse_fs *fs, FAR const char *path,
                  mode_t mode, FAR struct fuse_file_info *fi);
int fuse_fs_chown(FAR struct fuse_fs *fs, FAR const char *path,
                  uid_t uid, gid_t gid, FAR struct fuse_file_info *fi);
int fuse_fs_truncate(FAR struct fuse_fs *fs, FAR const char *path,
                     off_t size, FAR struct fuse_file_info *fi);
int fuse_fs_utimens(FAR struct fuse_fs *fs, FAR const char *path,
                    FAR const struct timespec tv[2],
                    FAR struct fuse_file_info *fi);
int fuse_fs_access(FAR struct fuse_fs *fs, FAR const char *path, int mask);
int fuse_fs_readlink(FAR struct fuse_fs *fs, FAR const char *path,
                     FAR char *buf, size_t len);
int fuse_fs_mknod(FAR struct fuse_fs *fs, FAR const char *path,
                  mode_t mode, dev_t dev);
int fuse_fs_mkdir(FAR struct fuse_fs *fs, FAR const char *path, mode_t mode);
int fuse_fs_setxattr(FAR struct fuse_fs *fs, FAR const char *path,
                     FAR const char *name, FAR const char *value,
                     size_t size, int flags);
int fuse_fs_getxattr(FAR struct fuse_fs *fs, FAR const char *path,
                     FAR const char *name, FAR char *value, size_t size);
int fuse_fs_listxattr(FAR struct fuse_fs *fs, FAR const char *path,
                      FAR char *list, size_t size);
int fuse_fs_removexattr(FAR struct fuse_fs *fs, FAR const char *path,
                        FAR const char *name);
int fuse_fs_bmap(FAR struct fuse_fs *fs, FAR const char *path,
                 size_t blocksize, FAR uint64_t *idx);
int fuse_fs_ioctl(FAR struct fuse_fs *fs, FAR const char *path,
                  unsigned int cmd, FAR void *arg,
                  FAR struct fuse_file_info *fi,
                  unsigned int flags, FAR void *data);
int fuse_fs_poll(FAR struct fuse_fs *fs, FAR const char *path,
                 FAR struct fuse_file_info *fi,
                 FAR struct fuse_pollhandle *ph,
                 FAR unsigned int *reventsp);
int fuse_fs_fallocate(FAR struct fuse_fs *fs, FAR const char *path,
                      int mode, off_t offset, off_t length,
                      FAR struct fuse_file_info *fi);
ssize_t fuse_fs_copy_file_range(FAR struct fuse_fs *fs,
                                FAR const char *path_in,
                                FAR struct fuse_file_info *fi_in,
                                off_t offset_in, FAR const char *path_out,
                                FAR struct fuse_file_info *fi_out,
                                off_t offset_out, size_t size, int flags);
off_t fuse_fs_lseek(FAR struct fuse_fs *fs, FAR const char *path,
                    off_t off, int whence, FAR struct fuse_file_info *fi);
int fuse_fs_statx(FAR struct fuse_fs *fs, FAR const char *path,
                  FAR struct statx *stx, unsigned int request_mask,
                  unsigned int flags, FAR struct fuse_file_info *fi);
int fuse_fs_syncfs(FAR struct fuse_fs *fs, FAR const char *path,
                   FAR struct fuse_file_info *fi);
void fuse_fs_init(FAR struct fuse_fs *fs, FAR struct fuse_conn_info *conn,
                  FAR struct fuse_config *cfg);
void fuse_fs_destroy(FAR struct fuse_fs *fs);

int fuse_notify_poll(FAR struct fuse_pollhandle *ph);

FAR struct fuse_fs *fuse_fs_new(FAR const struct fuse_operations *op,
                                size_t op_size, FAR void *private_data);

FAR struct fuse_session *fuse_get_session(FAR struct fuse *f);

#undef EXTERN
#ifdef __cplusplus
}
#endif

#endif /* __INCLUDE_FUSE_H */
