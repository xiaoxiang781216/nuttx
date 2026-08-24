/****************************************************************************
 * include/cuse_lowlevel.h
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

#ifndef __INCLUDE_CUSE_LOWLEVEL_H
#define __INCLUDE_CUSE_LOWLEVEL_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <fuse_lowlevel.h>

/****************************************************************************
 * Public Type Definitions
 ****************************************************************************/

struct cuse_info
{
  unsigned int dev_major;
  unsigned int dev_minor;
  int          dev_info_argc;
  FAR const char **dev_info_argv;
  unsigned int flags;
};

/* Most ops behave almost identically to the matching fuse_lowlevel
 * ops except that they don't take the ino argument.
 *
 * init_done  : called after initialization is complete
 * read/write : always direct IO, simultaneous operations allowed
 * ioctl      : might be in unrestricted mode depending on ci->flags
 */

struct cuse_lowlevel_ops
{
  CODE void (*init)(FAR void *userdata, FAR struct fuse_conn_info *conn);
  CODE void (*init_done)(FAR void *userdata);
  CODE void (*destroy)(FAR void *userdata);
  CODE void (*open)(fuse_req_t req, FAR struct fuse_file_info *fi);
  CODE void (*read)(fuse_req_t req, size_t size, off_t off,
                    FAR struct fuse_file_info *fi);
  CODE void (*write)(fuse_req_t req, FAR const char *buf, size_t size,
                     off_t off, FAR struct fuse_file_info *fi);
  CODE void (*flush)(fuse_req_t req, FAR struct fuse_file_info *fi);
  CODE void (*release)(fuse_req_t req, FAR struct fuse_file_info *fi);
  CODE void (*fsync)(fuse_req_t req, int datasync,
                     FAR struct fuse_file_info *fi);
  CODE void (*ioctl)(fuse_req_t req, unsigned int cmd, FAR void *arg,
                     FAR struct fuse_file_info *fi, unsigned int flags,
                     FAR const void *in_buf, size_t in_bufsz,
                     size_t out_bufsz);
  CODE void (*poll)(fuse_req_t req, FAR struct fuse_file_info *fi,
                    FAR struct fuse_pollhandle *ph);
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

FAR struct fuse_session *
cuse_lowlevel_new(FAR struct fuse_args *args,
                  FAR const struct cuse_info *ci,
                  FAR const struct cuse_lowlevel_ops *clop,
                  FAR void *userdata);

FAR struct fuse_session *
cuse_lowlevel_setup(int argc, FAR char *argv[],
                    FAR const struct cuse_info *ci,
                    FAR const struct cuse_lowlevel_ops *clop,
                    FAR int *multithreaded,
                    FAR void *userdata);
void cuse_lowlevel_teardown(FAR struct fuse_session *se);

int cuse_lowlevel_main(int argc, FAR char *argv[],
                       FAR const struct cuse_info *ci,
                       FAR const struct cuse_lowlevel_ops *clop,
                       FAR void *userdata);

#undef EXTERN
#ifdef __cplusplus
}
#endif

#endif /* __INCLUDE_CUSE_LOWLEVEL_H */
