/****************************************************************************
 * libs/libc/fuse/lib_cuse_lowlevel.c
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

#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <cuse_lowlevel.h>

#include <nuttx/lib/lib.h>

/****************************************************************************
 * Private Type Definitions
 ****************************************************************************/

struct cuse_data
{
  struct cuse_lowlevel_ops clop;
  unsigned int             dev_major;
  unsigned int             dev_minor;
  unsigned int             flags;
  unsigned int             dev_info_len;
  char                     dev_info[];
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static inline FAR struct cuse_lowlevel_ops *req_clop(fuse_req_t req)
{
  FAR struct cuse_data *cd = fuse_req_cusedata(req);

  return &(cd->clop);
}

/* Adapt the CUSE ops onto fuse_lowlevel_ops by dropping the ino argument */

static void cuse_fll_open(fuse_req_t req, fuse_ino_t ino,
                          FAR struct fuse_file_info *fi)
{
  req_clop(req)->open(req, fi);
}

static void cuse_fll_read(fuse_req_t req, fuse_ino_t ino, size_t size,
                          off_t off, FAR struct fuse_file_info *fi)
{
  req_clop(req)->read(req, size, off, fi);
}

static void cuse_fll_write(fuse_req_t req, fuse_ino_t ino,
                           FAR const char *buf, size_t size, off_t off,
                           FAR struct fuse_file_info *fi)
{
  req_clop(req)->write(req, buf, size, off, fi);
}

static void cuse_fll_flush(fuse_req_t req, fuse_ino_t ino,
                           FAR struct fuse_file_info *fi)
{
  req_clop(req)->flush(req, fi);
}

static void cuse_fll_release(fuse_req_t req, fuse_ino_t ino,
                             FAR struct fuse_file_info *fi)
{
  req_clop(req)->release(req, fi);
}

static void cuse_fll_fsync(fuse_req_t req, fuse_ino_t ino, int datasync,
                           FAR struct fuse_file_info *fi)
{
  req_clop(req)->fsync(req, datasync, fi);
}

static void cuse_fll_ioctl(fuse_req_t req, fuse_ino_t ino, unsigned int cmd,
                           FAR void *arg, FAR struct fuse_file_info *fi,
                           unsigned int flags, FAR const void *in_buf,
                           size_t in_bufsz, size_t out_bufsz)
{
  req_clop(req)->ioctl(req, cmd, arg, fi, flags,
                       in_buf, in_bufsz, out_bufsz);
}

static void cuse_fll_poll(fuse_req_t req, fuse_ino_t ino,
                          FAR struct fuse_file_info *fi,
                          FAR struct fuse_pollhandle *ph)
{
  req_clop(req)->poll(req, fi, ph);
}

static size_t cuse_pack_info(int argc, FAR const char **argv,
                             FAR char *buf)
{
  size_t size = 0;
  int i;

  for (i = 0; i < argc; i++)
    {
      size_t len = strlen(argv[i]) + 1;

      size += len;
      if (buf != NULL)
        {
          memcpy(buf, argv[i], len);
          buf += len;
        }
    }

  return size;
}

static inline FAR struct cuse_data *
cuse_prep_data(FAR const struct cuse_info *ci,
               FAR const struct cuse_lowlevel_ops *clop)
{
  FAR struct cuse_data *cd;
  size_t dev_info_len;

  dev_info_len = cuse_pack_info(ci->dev_info_argc, ci->dev_info_argv,
                                NULL);
  if (dev_info_len > CUSE_INIT_INFO_MAX)
    {
      return NULL;
    }

  cd = lib_calloc(1, sizeof(*cd) + dev_info_len);
  if (cd == NULL)
    {
      return NULL;
    }

  memcpy(&cd->clop, clop, sizeof(cd->clop));
  cd->dev_major    = ci->dev_major;
  cd->dev_minor    = ci->dev_minor;
  cd->dev_info_len = dev_info_len;
  cd->flags        = ci->flags;
  cuse_pack_info(ci->dev_info_argc, ci->dev_info_argv, cd->dev_info);

  return cd;
}

static inline int
cuse_reply_init(fuse_req_t req, FAR struct cuse_init_out *out,
                FAR char *dev_info, unsigned int dev_info_len)
{
  struct iovec iov[2];

  iov[0].iov_base = out;
  iov[0].iov_len  = sizeof(*out);
  iov[1].iov_base = dev_info;
  iov[1].iov_len  = dev_info_len;

  return fuse_reply_iov(req, iov, 2);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: cuse_lowlevel_new
 ****************************************************************************/

FAR struct fuse_session *
cuse_lowlevel_new(FAR struct fuse_args *args,
                  FAR const struct cuse_info *ci,
                  FAR const struct cuse_lowlevel_ops *clop,
                  FAR void *userdata)
{
  struct fuse_lowlevel_ops lop;
  FAR struct fuse_session *se;
  FAR struct cuse_data *cd;

  cd = cuse_prep_data(ci, clop);
  if (cd == NULL)
    {
      return NULL;
    }

  memset(&lop, 0, sizeof(lop));
  lop.init    = clop->init;
  lop.destroy = clop->destroy;
  lop.open    = clop->open    ? cuse_fll_open    : NULL;
  lop.read    = clop->read    ? cuse_fll_read    : NULL;
  lop.write   = clop->write   ? cuse_fll_write   : NULL;
  lop.flush   = clop->flush   ? cuse_fll_flush   : NULL;
  lop.release = clop->release ? cuse_fll_release : NULL;
  lop.fsync   = clop->fsync   ? cuse_fll_fsync   : NULL;
  lop.ioctl   = clop->ioctl   ? cuse_fll_ioctl   : NULL;
  lop.poll    = clop->poll    ? cuse_fll_poll    : NULL;

  se = fuse_session_new(args, &lop, sizeof(lop), userdata);
  if (se == NULL)
    {
      lib_free(cd);
      return NULL;
    }

  fuse_session_set_cusedata(se, cd);
  return se;
}

/****************************************************************************
 * Name: cuse_lowlevel_init
 *
 * Description:
 *   Answer the kernel CUSE_INIT handshake:  negotiate the protocol
 *   version, call the user init callback and reply with cuse_init_out
 *   followed by the packed device info (DEVNAME=<name>, ...).
 *
 ****************************************************************************/

int cuse_lowlevel_init(fuse_req_t req,
                       FAR struct fuse_conn_info *conn,
                       FAR const void *body, size_t bodylen)
{
  FAR struct cuse_lowlevel_ops *clop = req_clop(req);
  FAR struct cuse_data *cd = fuse_req_cusedata(req);
  FAR const struct cuse_init_in *arg = body;
  struct cuse_init_out out;
  int ret;

  conn->proto_major = arg->major;
  conn->proto_minor = arg->minor;
  conn->want        = 0;
  conn->want_ext    = 0;
  conn->capable     = arg->flags;
  conn->capable_ext = arg->flags;

  if (clop->init != NULL)
    {
      clop->init(fuse_req_userdata(req), conn);
    }

  memset(&out, 0, sizeof(out));
  out.major     = FUSE_KERNEL_VERSION;
  out.minor     = FUSE_KERNEL_MINOR_VERSION;
  out.flags     = cd->flags;
  out.max_read  = conn->max_read;
  out.max_write = conn->max_write;
  out.dev_major = cd->dev_major;
  out.dev_minor = cd->dev_minor;

  ret = cuse_reply_init(req, &out, cd->dev_info, cd->dev_info_len);;
  if (clop->init_done != NULL)
    {
      clop->init_done(fuse_req_userdata(req));
    }

  return ret;
}

/****************************************************************************
 * Name: cuse_lowlevel_setup
 *
 * Description:
 *   Parse the command line, create the session and open the /dev/cuse
 *   control channel.  Unlike fuse_session_mount() no mount is involved:
 *   the kernel registers the character device when it receives the
 *   CUSE_INIT reply.
 *
 ****************************************************************************/

FAR struct fuse_session *
cuse_lowlevel_setup(int argc, FAR char *argv[],
                    FAR const struct cuse_info *ci,
                    FAR const struct cuse_lowlevel_ops *clop,
                    FAR int *multithreaded, FAR void *userdata)
{
  struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
  struct fuse_cmdline_opts opts;
  FAR struct fuse_session *se;
  int fd;

  if (fuse_parse_cmdline(&args, &opts) != 0)
    {
      return NULL;
    }

  *multithreaded = !opts.singlethread;
  lib_free(opts.mountpoint);

  se = cuse_lowlevel_new(&args, ci, clop, userdata);
  fuse_opt_free_args(&args);
  if (se == NULL)
    {
      return NULL;
    }

  fd = open("/dev/cuse", O_RDWR | O_CLOEXEC);
  if (fd < 0)
    {
      cuse_lowlevel_teardown(se);
      return NULL;
    }

  fuse_session_set_fd(se, fd);
  fuse_set_signal_handlers(se);
  fuse_daemonize(opts.foreground);

  return se;
}

/****************************************************************************
 * Name: cuse_lowlevel_teardown
 ****************************************************************************/

void cuse_lowlevel_teardown(FAR struct fuse_session *se)
{
  FAR struct cuse_data *cd = fuse_session_cusedata(se);

  fuse_remove_signal_handlers(se);
  fuse_session_destroy(se);

  if (cd != NULL)
    {
      lib_free(cd);
    }
}

/****************************************************************************
 * Name: cuse_lowlevel_main
 ****************************************************************************/

int cuse_lowlevel_main(int argc, FAR char *argv[],
                       FAR const struct cuse_info *ci,
                       FAR const struct cuse_lowlevel_ops *clop,
                       FAR void *userdata)
{
  FAR struct fuse_session *se;
  int multithreaded = 0;
  int ret;

  se = cuse_lowlevel_setup(argc, argv, ci, clop,
                           &multithreaded, userdata);
  if (se == NULL)
    {
      return 1;
    }

  if (multithreaded)
    {
      ret = fuse_session_loop_mt(se, NULL);
    }
  else
    {
      ret = fuse_session_loop(se);
    }

  cuse_lowlevel_teardown(se);
  return ret < 0 ? 1 : 0;
}
