/****************************************************************************
 * fs/fuse/fuse.h
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

#ifndef __FS_FUSE_FUSE_H
#define __FS_FUSE_FUSE_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/uio.h>
#include <stdbool.h>

#include <nuttx/atomic.h>
#include <nuttx/fuse.h>
#include <nuttx/mutex.h>
#include <nuttx/queue.h>
#include <nuttx/semaphore.h>
#include <nuttx/spinlock.h>

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct fuse_conn_s;
struct fuse_req_s;

/* Transport abstraction.  A transport moves FUSE request/reply bytes
 * between the core and a server.  request() submits a request and returns
 * immediately (non-blocking); the completion path posts req->wait.  The
 * core (fuse_request) computes the unique/len, waits for completion and
 * returns the FUSE error code.  The optional maxbuf() reports the largest
 * payload one request can carry at this moment (bouncing transports
 * bounded by their shared memory); NULL means no limit.
 */

struct fuse_transport_ops_s
{
  CODE int    (*request)(FAR struct fuse_conn_s *conn,
                         FAR struct fuse_req_s *req);
  CODE void   (*destroy)(FAR struct fuse_conn_s *conn);
  CODE size_t (*maxbuf)(FAR struct fuse_conn_s *conn);
};

/* A single in-flight FUSE request */

struct fuse_req_s
{
  struct fuse_in_header   hin;   /* Request header (filled by core) */
  struct fuse_out_header  hout;  /* Reply header (filled by transport) */
  sem_t                   wait;  /* Per-request completion semaphore */
  FAR const struct iovec *vin;   /* Request args (sent to server) */
  size_t                  vin_cnt;
  FAR const struct iovec *vout;  /* Reply buffers (received from server) */
  size_t                  vout_cnt;
  sq_entry_t              entry; /* Pending/processing queue linkage */
};

/* A FUSE connection (one per mountpoint) */

struct fuse_conn_s
{
  FAR const struct fuse_transport_ops_s *transport;
  FAR void  *priv;        /* Transport private data */
  spinlock_t lock;        /* Protects unique and transport queues */
  uint64_t   unique;      /* Monotonic request id generator */
  uint32_t   max_write;   /* Negotiated maximum write size */
  mutex_t    initlock;    /* Serializes the lazy FUSE_INIT */
  atomic_t   initialized; /* FUSE_INIT handshake completed */
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: fuse_request
 *
 * Description:
 *   Submit a FUSE request through the connection transport and block until
 *   the reply arrives.  vout/outcnt describe the request arguments that
 *   follow the fuse_in_header; vin/incnt describe the buffers that receive
 *   the reply payload following the fuse_out_header.
 *
 * Returned Value:
 *   The FUSE reply 0 on success, or a negated errno on transport
 *   failure.
 *
 ****************************************************************************/

int fuse_request(FAR struct fuse_conn_s *conn, FAR struct fuse_req_s *req,
                 FAR const struct iovec *vin, size_t vin_cnt,
                 FAR const struct iovec *vout, size_t out_cnt);

#ifdef CONFIG_FS_FUSE_DEV
/* Implemented by fs/fuse/fuse_dev.c */

int fuse_dev_connect(FAR struct fuse_conn_s *conn, FAR const char *options);
int fuse_dev_initialize(void);
#endif

#ifdef CONFIG_FS_VIRTIOFS
/* Implemented by fs/fuse/fuse_virtio.c */

int fuse_virtio_connect(FAR struct fuse_conn_s *conn,
                        FAR const char *options);
#endif

#endif /* __FS_FUSE_FUSE_H */
