/****************************************************************************
 * include/fuse_lowlevel.h
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

#ifndef __INCLUDE_FUSE_LOWLEVEL_H
#define __INCLUDE_FUSE_LOWLEVEL_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <utime.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/uio.h>
#include <fuse_common.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* setattr valid bits */

#define FUSE_SET_ATTR_MODE            FATTR_MODE
#define FUSE_SET_ATTR_UID             FATTR_UID
#define FUSE_SET_ATTR_GID             FATTR_GID
#define FUSE_SET_ATTR_SIZE            FATTR_SIZE
#define FUSE_SET_ATTR_ATIME           FATTR_ATIME
#define FUSE_SET_ATTR_MTIME           FATTR_MTIME
#define FUSE_SET_ATTR_ATIME_NOW       FATTR_ATIME_NOW
#define FUSE_SET_ATTR_MTIME_NOW       FATTR_MTIME_NOW
#define FUSE_SET_ATTR_CTIME           FATTR_CTIME
#define FUSE_SET_ATTR_KILL_SUID       FATTR_KILL_SUIDGID
#define FUSE_SET_ATTR_FORCE	          (1 << 9)
#define FUSE_SET_ATTR_KILL_SGID       (1 << 12)
#define FUSE_SET_ATTR_FILE            (1 << 13)
#define FUSE_SET_ATTR_KILL_PRIV       (1 << 14)
#define FUSE_SET_ATTR_OPEN            (1 << 15)
#define FUSE_SET_ATTR_TIMES_SET       (1 << 16)
#define FUSE_SET_ATTR_TOUCH           (1 << 17)

/****************************************************************************
 * Public Type Definitions
 ****************************************************************************/

struct cuse_data;
struct fuse_req;
struct fuse_session;

typedef uint64_t             fuse_ino_t;
typedef FAR struct fuse_req *fuse_req_t;

typedef CODE void (*fuse_timeout_cb)(FAR void *data);

/* Reply to a lookup/create request */

struct fuse_entry_param
{
  fuse_ino_t ino;
  uint64_t generation;
  struct stat attr;
  double attr_timeout;
  double entry_timeout;
};

/* The caller context of a request */

struct fuse_ctx
{
  uid_t uid;
  gid_t gid;
  pid_t pid;
  mode_t umask;
};

/* Batch forget entry */

struct fuse_forget_data
{
  fuse_ino_t ino;
  uint64_t nlookup;
};

/* Low level filesystem operations
 *
 * Most of the methods (with the exception of init and destroy)
 * receive a request handle (fuse_req_t) as their first argument.
 * This handle must be passed to one of the specified reply functions.
 *
 * This may be done inside the method invocation, or after the call
 * has returned.  The request handle is valid until one of the reply
 * functions is called.
 *
 * Other pointer arguments (name, fuse_file_info, etc) are not valid
 * after the call has returned, so if they are needed later, their
 * contents have to be copied.
 *
 * It is generally not really necessary to check the fuse_reply_* return
 * values for errors, as any error in sending a reply indicates an
 * unrecoverable problem with the kernel fuse connection, which will also
 * terminate the session loop anyway.
 *
 * Every callback except forget and forget_multi must eventually answer
 * with one of the fuse_reply_* helpers.  Operations the NuttX kernel never
 * issues are kept so upstream filesystems register unchanged; a missing
 * callback answers -ENOSYS at dispatch time.
 */

struct fuse_lowlevel_ops
{
  CODE void (*init)(FAR void *userdata, FAR struct fuse_conn_info *conn);
  CODE void (*destroy)(FAR void *userdata);
  CODE void (*lookup)(fuse_req_t req, fuse_ino_t parent,
                      FAR const char *name);
  CODE void (*forget)(fuse_req_t req, fuse_ino_t ino, uint64_t nlookup);
  CODE void (*getattr)(fuse_req_t req, fuse_ino_t ino,
                       FAR struct fuse_file_info *fi);
  CODE void (*setattr)(fuse_req_t req, fuse_ino_t ino,
                       FAR struct stat *attr, int to_set,
                       FAR struct fuse_file_info *fi);
  CODE void (*readlink)(fuse_req_t req, fuse_ino_t ino);
  CODE void (*mknod)(fuse_req_t req, fuse_ino_t parent,
                     FAR const char *name, mode_t mode, dev_t rdev);
  CODE void (*mkdir)(fuse_req_t req, fuse_ino_t parent,
                     FAR const char *name, mode_t mode);
  CODE void (*unlink)(fuse_req_t req, fuse_ino_t parent,
                      FAR const char *name);
  CODE void (*rmdir)(fuse_req_t req, fuse_ino_t parent,
                     FAR const char *name);
  CODE void (*symlink)(fuse_req_t req, FAR const char *link,
                       fuse_ino_t parent, FAR const char *name);
  CODE void (*rename)(fuse_req_t req, fuse_ino_t parent,
                      FAR const char *name, fuse_ino_t newparent,
                      FAR const char *newname, unsigned int flags);
  CODE void (*link)(fuse_req_t req, fuse_ino_t ino, fuse_ino_t newparent,
                    FAR const char *newname);
  CODE void (*open)(fuse_req_t req, fuse_ino_t ino,
                    FAR struct fuse_file_info *fi);
  CODE void (*read)(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off,
                    FAR struct fuse_file_info *fi);
  CODE void (*write)(fuse_req_t req, fuse_ino_t ino, FAR const char *buf,
                     size_t size, off_t off, FAR struct fuse_file_info *fi);
  CODE void (*flush)(fuse_req_t req, fuse_ino_t ino,
                     FAR struct fuse_file_info *fi);
  CODE void (*release)(fuse_req_t req, fuse_ino_t ino,
                       FAR struct fuse_file_info *fi);
  CODE void (*fsync)(fuse_req_t req, fuse_ino_t ino, int datasync,
                     FAR struct fuse_file_info *fi);
  CODE void (*opendir)(fuse_req_t req, fuse_ino_t ino,
                       FAR struct fuse_file_info *fi);
  CODE void (*readdir)(fuse_req_t req, fuse_ino_t ino, size_t size,
                       off_t off, FAR struct fuse_file_info *fi);
  CODE void (*releasedir)(fuse_req_t req, fuse_ino_t ino,
                          FAR struct fuse_file_info *fi);
  CODE void (*fsyncdir)(fuse_req_t req, fuse_ino_t ino, int datasync,
                        FAR struct fuse_file_info *fi);
  CODE void (*statfs)(fuse_req_t req, fuse_ino_t ino);
  CODE void (*setxattr)(fuse_req_t req, fuse_ino_t ino, FAR const char *name,
                        FAR const char *value, size_t size, int flags);
  CODE void (*getxattr)(fuse_req_t req, fuse_ino_t ino,
                        FAR const char *name, size_t size);
  CODE void (*listxattr)(fuse_req_t req, fuse_ino_t ino, size_t size);
  CODE void (*removexattr)(fuse_req_t req, fuse_ino_t ino,
                           FAR const char *name);
  CODE void (*access)(fuse_req_t req, fuse_ino_t ino, int mask);
  CODE void (*create)(fuse_req_t req, fuse_ino_t parent,
                      FAR const char *name, mode_t mode,
                      FAR struct fuse_file_info *fi);
  CODE void (*getlk)(fuse_req_t req, fuse_ino_t ino,
                     FAR struct fuse_file_info *fi,
                     FAR struct flock *lock);
  CODE void (*setlk)(fuse_req_t req, fuse_ino_t ino,
                     FAR struct fuse_file_info *fi,
                     FAR struct flock *lock, int sleep);
  CODE void (*bmap)(fuse_req_t req, fuse_ino_t ino, size_t blocksize,
                    uint64_t idx);
  CODE void (*ioctl)(fuse_req_t req, fuse_ino_t ino, unsigned int cmd,
                     FAR void *arg, FAR struct fuse_file_info *fi,
                     unsigned int flags, FAR const void *in_buf,
                     size_t in_bufsz, size_t out_bufsz);
  CODE void (*poll)(fuse_req_t req, fuse_ino_t ino,
                    FAR struct fuse_file_info *fi,
                    FAR struct fuse_pollhandle *ph);
  CODE void (*write_buf)(fuse_req_t req, fuse_ino_t ino,
                         FAR struct fuse_bufvec *bufv, off_t off,
                         FAR struct fuse_file_info *fi);
  CODE void (*retrieve_reply)(fuse_req_t req, FAR void *cookie,
                              fuse_ino_t ino, off_t offset,
                              FAR struct fuse_bufvec *bufv);
  CODE void (*forget_multi)(fuse_req_t req, size_t count,
                            FAR struct fuse_forget_data *forgets);
  CODE void (*flock)(fuse_req_t req, fuse_ino_t ino,
                     FAR struct fuse_file_info *fi, int op);
  CODE void (*fallocate)(fuse_req_t req, fuse_ino_t ino, int mode,
                         off_t offset, off_t length,
                         FAR struct fuse_file_info *fi);
  CODE void (*readdirplus)(fuse_req_t req, fuse_ino_t ino, size_t size,
                           off_t off, FAR struct fuse_file_info *fi);
  CODE void (*copy_file_range)(fuse_req_t req, fuse_ino_t ino_in,
                               off_t off_in,
                               FAR struct fuse_file_info *fi_in,
                               fuse_ino_t ino_out, off_t off_out,
                               FAR struct fuse_file_info *fi_out,
                               size_t len, int flags);
  CODE void (*lseek)(fuse_req_t req, fuse_ino_t ino, off_t off, int whence,
                     FAR struct fuse_file_info *fi);
  CODE void (*tmpfile)(fuse_req_t req, fuse_ino_t parent,
                       mode_t mode, FAR struct fuse_file_info *fi);
  CODE void (*statx)(fuse_req_t req, fuse_ino_t ino, uint32_t sx_flags,
                     uint32_t sx_mask,
                     FAR struct fuse_file_info *fi);
  CODE void (*syncfs)(fuse_req_t req, fuse_ino_t ino);
};

/* Parsed command line options */

struct fuse_cmdline_opts
{
  int singlethread;
  int foreground;
  int debug;
  int nodefault_subtype;
  FAR char *mountpoint;
  int show_version;
  int show_help;
  int clone_fd;
  unsigned int max_idle_threads;
  unsigned int max_threads;
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

/****************************************************************************
 * Replies
 ****************************************************************************/

int fuse_reply_err(fuse_req_t req, int err);
void fuse_reply_none(fuse_req_t req);
int fuse_reply_entry(fuse_req_t req, FAR const struct fuse_entry_param *e);
int fuse_reply_create(fuse_req_t req, FAR const struct fuse_entry_param *e,
                      FAR struct fuse_file_info *fi);
int fuse_reply_attr(fuse_req_t req, FAR const struct stat *attr,
                    double attr_timeout);
int fuse_reply_readlink(fuse_req_t req, FAR const char *link);
int fuse_passthrough_open(fuse_req_t req, int fd);
int fuse_passthrough_close(fuse_req_t req, int backing_id);
int fuse_reply_open(fuse_req_t req, FAR struct fuse_file_info *fi);
int fuse_reply_write(fuse_req_t req, size_t count);
int fuse_reply_buf(fuse_req_t req, FAR const char *buf, size_t size);
int fuse_reply_data(fuse_req_t req, FAR struct fuse_bufvec *bufv,
                    enum fuse_buf_copy_flags flags);
int fuse_reply_iov(fuse_req_t req, FAR struct iovec *iov, int count);
int fuse_reply_statfs(fuse_req_t req, FAR const struct statvfs *stbuf);
int fuse_reply_xattr(fuse_req_t req, size_t count);
int fuse_reply_lock(fuse_req_t req, FAR const struct flock *lock);
int fuse_reply_bmap(fuse_req_t req, uint64_t block);
size_t fuse_add_direntry(fuse_req_t req, FAR char *buf, size_t bufsize,
                         FAR const char *name,
                         FAR const struct stat *st, off_t off);
size_t fuse_add_direntry_plus(fuse_req_t req, FAR char *buf,
                              size_t bufsize, FAR const char *name,
                              FAR const struct fuse_entry_param *e,
                              off_t off);
int fuse_reply_ioctl_retry(fuse_req_t req, FAR const struct iovec *in_iov,
                           int in_count, FAR const struct iovec *out_iov,
                           int out_count);
int fuse_reply_ioctl(fuse_req_t req, int result, FAR const void *buf,
                     size_t size);
int fuse_reply_ioctl_iov(fuse_req_t req, int result,
                         FAR const struct iovec *iov, int count);
int fuse_reply_poll(fuse_req_t req, unsigned int revents);
int fuse_reply_lseek(fuse_req_t req, off_t off);
int fuse_reply_statx(fuse_req_t req, int flags,
                     FAR const struct statx *stx,
                     double attr_timeout);

/****************************************************************************
 * Notifications
 ****************************************************************************/

int fuse_lowlevel_notify_poll(FAR struct fuse_pollhandle *ph);
int fuse_lowlevel_notify_inval_inode(FAR struct fuse_session *se,
                                     fuse_ino_t ino, off_t off, off_t len);
int fuse_lowlevel_notify_increment_epoch(FAR struct fuse_session *se);
int fuse_lowlevel_notify_inval_entry(FAR struct fuse_session *se,
                                     fuse_ino_t parent,
                                     FAR const char *name, size_t namelen);
int fuse_lowlevel_notify_expire_entry(FAR struct fuse_session *se,
                                      fuse_ino_t parent,
                                      FAR const char *name, size_t namelen);
int fuse_lowlevel_notify_delete(FAR struct fuse_session *se,
                                fuse_ino_t parent, fuse_ino_t child,
                                FAR const char *name, size_t namelen);
int fuse_lowlevel_notify_store(FAR struct fuse_session *se, fuse_ino_t ino,
                               off_t offset,
                               FAR struct fuse_bufvec *bufv,
                               enum fuse_buf_copy_flags flags);
int fuse_lowlevel_notify_retrieve(FAR struct fuse_session *se,
                                  fuse_ino_t ino, size_t size, off_t offset,
                                  FAR void *cookie);
int fuse_lowlevel_notify_prune(FAR struct fuse_session *se,
                               FAR fuse_ino_t *nodeids, uint32_t count);

/****************************************************************************
 * Utilities
 ****************************************************************************/

FAR void *fuse_req_userdata(fuse_req_t req);
FAR struct cuse_data *fuse_req_cusedata(fuse_req_t req);
FAR const struct fuse_ctx *fuse_req_ctx(fuse_req_t req);

static inline int fuse_req_getgroups(fuse_req_t req,
                                     int size, FAR gid_t list[])
{
  return -ENOSYS;
}

static inline int fuse_req_interrupted(fuse_req_t req)
{
  return 0;
}

/****************************************************************************
 * Inquiry
 ****************************************************************************/

void fuse_lowlevel_version(void);
void fuse_lowlevel_help(void);
void fuse_cmdline_help(void);

/****************************************************************************
 * Setup & teardown
 ****************************************************************************/

int fuse_parse_cmdline(FAR struct fuse_args *args,
                       FAR struct fuse_cmdline_opts *opts);

FAR struct fuse_session *
fuse_session_new(FAR struct fuse_args *args,
                 FAR const struct fuse_lowlevel_ops *op,
                 size_t op_size,
                 FAR void *userdata);

int fuse_session_custom_io(FAR struct fuse_session *se,
                           FAR const struct fuse_custom_io *io,
                            size_t op_size, int fd);
#if FUSE_USE_VERSION < FUSE_MAKE_VERSION(3, 17)
#  define fuse_session_custom_io(se, io, fd) \
      fuse_session_custom_io(se, io,         \
         offsetof(struct fuse_custom_io, clone_fd), fd)
#endif

int fuse_session_mount(FAR struct fuse_session *se,
                       FAR const char *mountpoint);
void fuse_session_unmount(FAR struct fuse_session *se);

int fuse_session_loop(FAR struct fuse_session *se);
int fuse_session_loop_mt(FAR struct fuse_session *se,
                         FAR struct fuse_loop_config *config);

void fuse_session_exit(FAR struct fuse_session *se);
void fuse_session_reset(FAR struct fuse_session *se);
int fuse_session_exited(FAR struct fuse_session *se);

void fuse_session_destroy(FAR struct fuse_session *se);

static inline FAR void *
fuse_session_start_teardown_watchdog(FAR struct fuse_session *se,
                                     int timeout_sec, fuse_timeout_cb cb,
                                     FAR void *cb_data)
{
  return NULL;
}

static inline void fuse_session_stop_teardown_watchdog(FAR void *data)
{
}

/****************************************************************************
 * Custom event loop
 ****************************************************************************/

int fuse_session_fd(FAR struct fuse_session *se);
void fuse_session_set_fd(FAR struct fuse_session *se, int fd);

int fuse_session_eventfd(FAR struct fuse_session *se);
void fuse_session_set_eventfd(FAR struct fuse_session *se, int eventfd);

FAR struct cuse_data *fuse_session_cusedata(FAR struct fuse_session *se);
void fuse_session_set_cusedata(FAR struct fuse_session *se,
                               FAR struct cuse_data *cuse_data);

void fuse_session_process_buf(FAR struct fuse_session *se,
                              FAR const struct fuse_buf *buf);
int fuse_session_receive_buf(FAR struct fuse_session *se,
                             FAR struct fuse_buf *buf);

static inline void fuse_session_set_debug(FAR struct fuse_session *se)
{
}

static inline int fuse_req_get_payload(fuse_req_t req, FAR char **payload,
                                       FAR size_t *payload_size,
                                       FAR void **mr)
{
  return -ENOTSUP;
}

#undef EXTERN
#ifdef __cplusplus
}
#endif

#endif /* __INCLUDE_FUSE_LOWLEVEL_H */
