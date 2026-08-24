/****************************************************************************
 * include/fuse_common.h
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
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ****************************************************************************/

#ifndef __INCLUDE_FUSE_COMMON_H
#define __INCLUDE_FUSE_COMMON_H

#if !defined(__INCLUDE_FUSE_H) && !defined(__INCLUDE_FUSE_LOWLEVEL_H)
#  error "Never include <fuse_common.h> directly; use <fuse.h> or <fuse_lowlevel.h> instead."
#endif

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
#include <fuse_opt.h>
#include <fuse_log.h>
#include <fuse_kernel.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Version macros, aligned with libfuse3 3.19 */

#define FUSE_MAJOR_VERSION            3
#define FUSE_MINOR_VERSION            19
#define FUSE_HOTFIX_VERSION           0
#define FUSE_RC_VERSION               rc0

#define FUSE_MAKE_VERSION(maj, min)   ((maj) * 100 + (min))
#define FUSE_VERSION                  FUSE_MAKE_VERSION(FUSE_MAJOR_VERSION, \
                                                        FUSE_MINOR_VERSION)

#ifndef FUSE_USE_VERSION
#  define FUSE_USE_VERSION            FUSE_VERSION
#endif

/* Capability bits for fuse_conn_info.capable/want and the extended
 * capable_ext/want_ext fields.  Values match the upstream wire protocol.
 */

#define FUSE_CAP_ASYNC_READ           FUSE_ASYNC_READ
#define FUSE_CAP_POSIX_LOCKS          FUSE_POSIX_LOCKS
#define FUSE_CAP_ATOMIC_O_TRUNC       FUSE_ATOMIC_O_TRUNC
#define FUSE_CAP_EXPORT_SUPPORT       FUSE_EXPORT_SUPPORT
#define FUSE_CAP_DONT_MASK            FUSE_DONT_MASK
#define FUSE_CAP_SPLICE_WRITE         FUSE_SPLICE_WRITE
#define FUSE_CAP_SPLICE_MOVE          FUSE_SPLICE_MOVE
#define FUSE_CAP_SPLICE_READ          FUSE_SPLICE_READ
#define FUSE_CAP_FLOCK_LOCKS          FUSE_FLOCK_LOCKS
#define FUSE_CAP_IOCTL_DIR            FUSE_HAS_IOCTL_DIR
#define FUSE_CAP_AUTO_INVAL_DATA      FUSE_AUTO_INVAL_DATA
#define FUSE_CAP_READDIRPLUS          FUSE_DO_READDIRPLUS
#define FUSE_CAP_READDIRPLUS_AUTO     FUSE_READDIRPLUS_AUTO
#define FUSE_CAP_ASYNC_DIO            FUSE_ASYNC_DIO
#define FUSE_CAP_WRITEBACK_CACHE      FUSE_WRITEBACK_CACHE
#define FUSE_CAP_NO_OPEN_SUPPORT      FUSE_NO_OPEN_SUPPORT
#define FUSE_CAP_PARALLEL_DIROPS      FUSE_PARALLEL_DIROPS
#define FUSE_CAP_POSIX_ACL            FUSE_POSIX_ACL
#define FUSE_CAP_HANDLE_KILLPRIV      FUSE_HANDLE_KILLPRIV
#define FUSE_CAP_HANDLE_KILLPRIV_V2   FUSE_HANDLE_KILLPRIV_V2
#define FUSE_CAP_CACHE_SYMLINKS       FUSE_CACHE_SYMLINKS
#define FUSE_CAP_NO_OPENDIR_SUPPORT   FUSE_NO_OPENDIR_SUPPORT
#define FUSE_CAP_EXPLICIT_INVAL_DATA  FUSE_EXPLICIT_INVAL_DATA
#define FUSE_CAP_EXPIRE_ONLY          FUSE_HAS_EXPIRE_ONLY
#define FUSE_CAP_SETXATTR_EXT         FUSE_SETXATTR_EXT
#define FUSE_CAP_DIRECT_IO_ALLOW_MMAP FUSE_DIRECT_IO_ALLOW_MMAP
#define FUSE_CAP_PASSTHROUGH          FUSE_PASSTHROUGH
#define FUSE_CAP_NO_EXPORT_SUPPORT    FUSE_NO_EXPORT_SUPPORT
#define FUSE_CAP_OVER_IO_URING        FUSE_OVER_IO_URING
#define FUSE_CAP_ALLOW_IDMAP          FUSE_ALLOW_IDMAP
#define FUSE_CAP_SECURITY_CTX         FUSE_SECURITY_CTX

#define FUSE_BACKING_STACKED_UNDER    0
#define FUSE_BACKING_STACKED_OVER     1

/* FUSE_CONN_FLAG_SINGLE_ISSUER: Reply from the same thread that received
 *                               requests, allowing io-uring optimizations
 * FUSE_CONN_FLAG_NO_INTERRUPT: Disable fuse interrupt handling and overhead
 */

#define FUSE_CONN_FLAG_SINGLE_ISSUER  (1u << 0)
#define FUSE_CONN_FLAG_NO_INTERRUPT   (1u << 1)

#define FUSE_BUFVEC_INIT(size)                          \
  (struct fuse_bufvec)                                  \
    {                                                   \
      1, 0, 0,                                          \
      {                                                 \
        {                                               \
          size, (enum fuse_buf_flags)0, NULL, -1, 0, 0, \
        }                                               \
      }                                                 \
    }

/****************************************************************************
 * Public Type Definitions
 ****************************************************************************/

/* File information */

struct fuse_file_info
{
  int32_t flags;
  uint32_t writepage : 1;
  uint32_t direct_io : 1;
  uint32_t keep_cache : 1;
  uint32_t flush : 1;
  uint32_t nonseekable : 1;
  uint32_t flock_release : 1;
  uint32_t cache_readdir : 1;
  uint32_t noflush : 1;
  uint32_t parallel_direct_writes : 1;
  uint32_t padding : 23;
  uint32_t padding2 : 32;
  uint32_t padding3 : 32;
  uint64_t fh;
  uint64_t lock_owner;
  uint32_t poll_events;
  int32_t backing_id;
  uint64_t compat_flags;
  uint64_t reserved[2];
};

/* Loop configuration for fuse_session_loop_mt() */

struct fuse_loop_config
{
  int clone_fd;
  int max_idle_threads;
  unsigned int max_threads;
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
  uint32_t max_background;
  uint32_t congestion_threshold;
  uint32_t time_gran;
  uint32_t max_backing_stack_depth;
  uint32_t no_interrupt : 1;
  uint32_t io_uring_single_issuer : 1;
  uint32_t padding : 30;
  uint64_t capable_ext;
  uint64_t want_ext;
  uint16_t request_timeout;
  uint16_t reserved[31];
};

struct fuse_session;
struct fuse_pollhandle;

/* A buffer with either memory or file descriptor backing */

enum fuse_buf_flags
{
  FUSE_BUF_IS_FD = (1 << 1),
  FUSE_BUF_FD_SEEK = (1 << 2),
  FUSE_BUF_FD_RETRY = (1 << 3),
};

enum fuse_buf_copy_flags
{
  FUSE_BUF_NO_SPLICE = (1 << 1),
  FUSE_BUF_FORCE_SPLICE = (1 << 2),
  FUSE_BUF_SPLICE_MOVE = (1 << 3),
  FUSE_BUF_SPLICE_NONBLOCK = (1 << 4),
};

struct fuse_buf
{
  size_t size;               /* Size of data in bytes */
  enum fuse_buf_flags flags; /* Buffer flags */
  FAR void *mem;             /* Memory, unless FUSE_BUF_IS_FD */
  int fd;                    /* Descriptor, if FUSE_BUF_IS_FD */
  off_t pos;                 /* File position, if FUSE_BUF_FD_SEEK */
  size_t mem_size;           /* Size of internally allocated mem */
};

/* A buffer vector */

struct fuse_bufvec
{
  size_t count;
  size_t idx;
  size_t off;
  struct fuse_buf buf[1];
};

/* Custom transport shared by both API levels: mount it with
 * fuse_mount_custom() (high level) or fuse_session_custom_io()
 * (low level).
 */

struct fuse_custom_io
{
  CODE ssize_t (*writev)(int fd, FAR struct iovec *iov, int count,
                         FAR void *userdata);
  CODE ssize_t (*read)(int fd, FAR void *buf, size_t buf_len,
                       FAR void *userdata);
  CODE ssize_t (*splice_receive)(int fdin, FAR off_t *offin, int fdout,
                                 FAR off_t *offout, size_t len,
                                 unsigned int flags,
                                 FAR void *userdata);
  CODE ssize_t (*splice_send)(int fdin, FAR off_t *offin, int fdout,
                              FAR off_t *offout, size_t len,
                              unsigned int flags, FAR void *userdata);
  CODE int (*clone_fd)(int master_fd);

  /* Optional: invoked when a request completes without a reply
   * (FUSE_FORGET).  Transports that own per-request resources, e.g.
   * a vhost virtqueue chain, release them here.  May be NULL.
   */

  CODE void (*request_done)(int fd, uint64_t unique,
                            FAR void *userdata);
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

static inline int fuse_daemonize(int foreground)
{
  return 0;
}

int fuse_version(void);
FAR const char *fuse_pkgversion(void);

void fuse_pollhandle_destroy(FAR struct fuse_pollhandle *ph);

/* Data buffer */

void fuse_buf_free(FAR struct fuse_buf *buf);
size_t fuse_buf_size(FAR const struct fuse_bufvec *bufv);
ssize_t fuse_buf_copy(FAR struct fuse_bufvec *dst,
                      FAR struct fuse_bufvec *src,
                      enum fuse_buf_copy_flags flags);
ssize_t fuse_buf_to_iov(FAR struct fuse_bufvec *bufv,
                        enum fuse_buf_copy_flags flags,
                        FAR struct iovec *iov, FAR bool *free);

/* Signal handling */

static inline int fuse_set_signal_handlers(FAR struct fuse_session *se)
{
  return 0;
}

static inline int fuse_set_fail_signal_handlers(FAR struct fuse_session *se)
{
  return 0;
}

static inline int fuse_remove_signal_handlers(FAR struct fuse_session *se)
{
  return 0;
}

/* Loop configuration */

FAR struct fuse_loop_config *fuse_loop_cfg_create(void);
void fuse_loop_cfg_destroy(FAR struct fuse_loop_config *config);
void fuse_loop_cfg_set_idle_threads(FAR struct fuse_loop_config *config,
                                    unsigned int value);
void fuse_loop_cfg_set_max_threads(FAR struct fuse_loop_config *config,
                                   unsigned int value);
void fuse_loop_cfg_set_clone_fd(FAR struct fuse_loop_config *config,
                                unsigned int value);

bool fuse_set_feature_flag(FAR struct fuse_conn_info *conn, uint64_t flag);
void fuse_unset_feature_flag(FAR struct fuse_conn_info *conn,
                             uint64_t flag);
bool fuse_get_feature_flag(FAR const struct fuse_conn_info *conn,
                           uint64_t flag);

void fuse_set_conn_flag(FAR struct fuse_conn_info *conn, uint64_t flag);

#undef EXTERN
#ifdef __cplusplus
}
#endif

#endif /* __INCLUDE_FUSE_COMMON_H */
