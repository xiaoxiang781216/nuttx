/****************************************************************************
 * fs/fuse/fuse_dev.c
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
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>

#include <nuttx/fs/fs.h>
#include <nuttx/nuttx.h>

#include "fs_heap.h"
#include "fuse.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define FUSE_DEV_NPOLLWAITERS 2

/* Sentinel for fuse_dev_s.conn:  the peer went away (server close or
 * unmount).  NULL means no filesystem has been mounted yet.
 */

#define FUSE_DEV_DEAD         ((FAR struct fuse_conn_s *)-1)

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* Per-open state of the /dev/fuse character device.  A FUSE server opens
 * /dev/fuse, mounts a filesystem with "fd=<n>" (which links a connection to
 * this state through fuse_dev_connect) and then reads requests and writes
 * replies.
 */

struct fuse_dev_s
{
  mutex_t                 lock;       /* Protects the queues and crefs */
  sem_t                   reader;     /* Wakes a blocked reader */
  sq_queue_t              pending;    /* Requests awaiting read by server */
  sq_queue_t              processing; /* Requests awaiting a reply */
  FAR struct fuse_conn_s *conn;       /* NULL, live conn or FUSE_DEV_DEAD */
  uint8_t                 crefs;      /* Reference count */
  FAR struct pollfd      *fds[FUSE_DEV_NPOLLWAITERS];
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int     fuse_dev_open(FAR struct file *filep);
static int     fuse_dev_close(FAR struct file *filep);
static ssize_t fuse_dev_read(FAR struct file *filep, FAR char *buffer,
                             size_t buflen);
static ssize_t fuse_dev_write(FAR struct file *filep,
                              FAR const char *buffer, size_t buflen);
static int     fuse_dev_poll(FAR struct file *filep, FAR struct pollfd *fds,
                             bool setup);

static int     fuse_dev_request(FAR struct fuse_conn_s *conn,
                                FAR struct fuse_req_s *req);
static void    fuse_dev_destroy(FAR struct fuse_conn_s *conn);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct file_operations g_fuse_dev_fops =
{
  fuse_dev_open,   /* open */
  fuse_dev_close,  /* close */
  fuse_dev_read,   /* read */
  fuse_dev_write,  /* write */
  NULL,            /* seek */
  NULL,            /* ioctl */
  NULL,            /* truncate */
  fuse_dev_poll    /* poll */
};

static const struct fuse_transport_ops_s g_fuse_dev_transport =
{
  fuse_dev_request,  /* request */
  fuse_dev_destroy,  /* destroy */
  NULL               /* maxbuf */
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: fuse_dev_notify
 *
 * Description:
 *   Wake the server:  notify pollers with events and post the reader
 *   semaphore.  The post is skipped when enough wakeups are already
 *   pending so that the semaphore count stays bounded.  Must be called
 *   with dev->lock held.
 *
 ****************************************************************************/

static inline void
fuse_dev_notify(FAR struct fuse_dev_s *dev, pollevent_t events)
{
  poll_notify(dev->fds, FUSE_DEV_NPOLLWAITERS, events);

  if (events & (POLLIN | POLLHUP))
    {
      int sval = 0;

      nxsem_get_value(&dev->reader, &sval);
      if (sval <= 1)
        {
          nxsem_post(&dev->reader);
        }
    }
}

/****************************************************************************
 * Name: fuse_dev_abort
 ****************************************************************************/

static void fuse_dev_abort(FAR struct fuse_dev_s *dev)
{
  FAR sq_entry_t *entry;

  nxmutex_lock(&dev->lock);
  dev->conn = FUSE_DEV_DEAD;

  while ((entry = sq_remfirst(&dev->pending)) != NULL)
    {
      FAR struct fuse_req_s *req = container_of(entry, struct fuse_req_s,
                                                entry);
      req->hout.error = -ENOTCONN;
      nxsem_post(&req->wait);
    }

  while ((entry = sq_remfirst(&dev->processing)) != NULL)
    {
      FAR struct fuse_req_s *req = container_of(entry, struct fuse_req_s,
                                                entry);
      req->hout.error = -ENOTCONN;
      nxsem_post(&req->wait);
    }

  fuse_dev_notify(dev, POLLHUP);
  nxmutex_unlock(&dev->lock);
}

/****************************************************************************
 * Name: fuse_dev_open
 ****************************************************************************/

static int fuse_dev_open(FAR struct file *filep)
{
  FAR struct fuse_dev_s *dev;

  dev = fs_heap_zalloc(sizeof(*dev));
  if (dev == NULL)
    {
      return -ENOMEM;
    }

  nxmutex_init(&dev->lock);
  nxsem_init(&dev->reader, 0, 0);
  sq_init(&dev->pending);
  sq_init(&dev->processing);
  dev->crefs = 1;

  filep->f_priv = dev;
  return 0;
}

/****************************************************************************
 * Name: fuse_dev_unref
 *
 * Description:
 *   Drop a reference and free the device state when the last one is gone.
 *
 ****************************************************************************/

static void fuse_dev_unref(FAR struct fuse_dev_s *dev)
{
  bool destroy;

  nxmutex_lock(&dev->lock);
  destroy = (--dev->crefs == 0);
  nxmutex_unlock(&dev->lock);

  if (destroy)
    {
      nxsem_destroy(&dev->reader);
      nxmutex_destroy(&dev->lock);
      fs_heap_free(dev);
    }
}

/****************************************************************************
 * Name: fuse_dev_close
 ****************************************************************************/

static int fuse_dev_close(FAR struct file *filep)
{
  FAR struct fuse_dev_s *dev = filep->f_priv;

  /* The server is gone: abort in-flight requests and wake any reader */

  fuse_dev_abort(dev);
  fuse_dev_unref(dev);

  filep->f_priv = NULL;
  return 0;
}

/****************************************************************************
 * Name: fuse_dev_read
 *
 * Description:
 *   The server reads one FUSE request (fuse_in_header followed by the
 *   request arguments) per call.
 *
 ****************************************************************************/

static ssize_t fuse_dev_read(FAR struct file *filep, FAR char *buffer,
                             size_t buflen)
{
  FAR struct fuse_dev_s *dev = filep->f_priv;
  FAR struct fuse_req_s *req = NULL;
  FAR sq_entry_t *entry;
  size_t off;
  size_t i;
  int ret;

  for (; ; )
    {
      nxmutex_lock(&dev->lock);
      if (dev->conn == FUSE_DEV_DEAD)
        {
          nxmutex_unlock(&dev->lock);
          return -ENODEV;
        }

      entry = sq_remfirst(&dev->pending);
      if (entry != NULL)
        {
          req = container_of(entry, struct fuse_req_s, entry);
          sq_addlast(&req->entry, &dev->processing);
          nxmutex_unlock(&dev->lock);
          break;
        }

      nxmutex_unlock(&dev->lock);

      if ((filep->f_oflags & O_NONBLOCK) != 0)
        {
          return -EAGAIN;
        }

      ret = nxsem_wait(&dev->reader);
      if (ret < 0)
        {
          return ret;
        }
    }

  /* The buffer must be large enough for the whole request */

  if (buflen < req->hin.len)
    {
      /* Fail the request like Linux does and tell the server that its
       * read buffer is too small.
       */

      nxmutex_lock(&dev->lock);
      sq_rem(&req->entry, &dev->processing);
      nxmutex_unlock(&dev->lock);
      req->hout.error = -EIO;
      nxsem_post(&req->wait);
      return -EINVAL;
    }

  memcpy(buffer, &req->hin, sizeof(req->hin));
  off = sizeof(req->hin);

  for (i = 0; i < req->vin_cnt; i++)
    {
      memcpy(buffer + off, req->vin[i].iov_base, req->vin[i].iov_len);
      off += req->vin[i].iov_len;
    }

  return off;
}

/****************************************************************************
 * Name: fuse_dev_write
 *
 * Description:
 *   The server writes one FUSE reply (fuse_out_header followed by the reply
 *   payload) per call.  The matching request is found by unique id.
 *
 ****************************************************************************/

static ssize_t fuse_dev_write(FAR struct file *filep,
                              FAR const char *buffer, size_t buflen)
{
  FAR const struct fuse_out_header *hout =
    (FAR const struct fuse_out_header *)buffer;
  FAR struct fuse_dev_s *dev = filep->f_priv;
  FAR struct fuse_req_s *req = NULL;
  FAR sq_entry_t *entry;
  size_t datalen;
  size_t off;
  size_t i;

  if (buflen < sizeof(*hout))
    {
      return -EINVAL;
    }

  /* Find and detach the matching in-flight request */

  nxmutex_lock(&dev->lock);
  sq_for_every(&dev->processing, entry)
    {
      FAR struct fuse_req_s *tmp = container_of(entry, struct fuse_req_s,
                                                entry);
      if (tmp->hin.unique == hout->unique)
        {
          req = tmp;
          sq_rem(&req->entry, &dev->processing);
          break;
        }
    }

  nxmutex_unlock(&dev->lock);

  if (req == NULL)
    {
      return -ENOENT;
    }

  memcpy(&req->hout, hout, sizeof(*hout));

  /* Scatter the reply payload into the request reply buffers */

  datalen = hout->len > sizeof(*hout) ? hout->len - sizeof(*hout) : 0;
  if (datalen > buflen - sizeof(*hout))
    {
      datalen = buflen - sizeof(*hout);
    }

  off = sizeof(*hout);
  for (i = 0; i < req->vout_cnt && datalen > 0; i++)
    {
      size_t chunk = MIN(req->vout[i].iov_len, datalen);
      memcpy(req->vout[i].iov_base, buffer + off, chunk);
      off     += chunk;
      datalen -= chunk;
    }

  nxsem_post(&req->wait);
  return buflen;
}

/****************************************************************************
 * Name: fuse_dev_poll
 ****************************************************************************/

static int fuse_dev_poll(FAR struct file *filep, FAR struct pollfd *fds,
                         bool setup)
{
  FAR struct fuse_dev_s *dev = filep->f_priv;
  pollevent_t eventset;
  int ret = 0;
  int i;

  nxmutex_lock(&dev->lock);

  if (setup)
    {
      for (i = 0; i < FUSE_DEV_NPOLLWAITERS; i++)
        {
          if (dev->fds[i] == NULL)
            {
              dev->fds[i] = fds;
              fds->priv   = &dev->fds[i];
              break;
            }
        }

      if (i >= FUSE_DEV_NPOLLWAITERS)
        {
          ret = -EBUSY;
          goto errout;
        }

      eventset = POLLOUT;
      if (!sq_empty(&dev->pending))
        {
          eventset |= POLLIN;
        }

      if (dev->conn == FUSE_DEV_DEAD)
        {
          eventset |= POLLHUP;
        }

      poll_notify(&fds, 1, eventset);
    }
  else if (fds->priv != NULL)
    {
      FAR struct pollfd **slot = fds->priv;

      *slot     = NULL;
      fds->priv = NULL;
    }

errout:
  nxmutex_unlock(&dev->lock);
  return ret;
}

/****************************************************************************
 * Name: fuse_dev_request
 ****************************************************************************/

static int fuse_dev_request(FAR struct fuse_conn_s *conn,
                            FAR struct fuse_req_s *req)
{
  FAR struct fuse_dev_s *dev = conn->priv;

  nxmutex_lock(&dev->lock);

  /* Refuse unless this connection is still the one attached here */

  if (dev->conn != conn)
    {
      nxmutex_unlock(&dev->lock);
      return -ENOTCONN;
    }

  sq_addlast(&req->entry, &dev->pending);
  fuse_dev_notify(dev, POLLIN);
  nxmutex_unlock(&dev->lock);

  return 0;
}

/****************************************************************************
 * Name: fuse_dev_destroy
 ****************************************************************************/

static void fuse_dev_destroy(FAR struct fuse_conn_s *conn)
{
  FAR struct fuse_dev_s *dev = conn->priv;

  /* The mount is gone: disconnect, fail any in-flight request and wake a
   * blocked server so its read loop can exit.
   */

  fuse_dev_abort(dev);
  fuse_dev_unref(dev);

  conn->priv = NULL;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: fuse_dev_connect
 *
 * Description:
 *   Attach a connection to an open /dev/fuse instance identified by the
 *   "fd=<n>" mount option.
 *
 ****************************************************************************/

int fuse_dev_connect(FAR struct fuse_conn_s *conn, FAR const char *options)
{
  FAR struct fuse_dev_s *dev;
  FAR struct file *filep;
  FAR const char *opt;
  int ret;
  int fd;

  opt = strstr(options, "fd=");
  if (opt == NULL)
    {
      return -EINVAL;
    }

  fd = atoi(opt + 3);

  ret = file_get(fd, &filep);
  if (ret < 0)
    {
      return ret;
    }

  /* The fd must refer to a /dev/fuse instance */

  if (filep->f_priv == NULL || filep->f_inode == NULL ||
      filep->f_inode->u.i_ops != &g_fuse_dev_fops)
    {
      file_put(filep);
      return -EINVAL;
    }

  dev = filep->f_priv;
  nxmutex_lock(&dev->lock);

  /* Only a fresh, never attached instance accepts a connection */

  if (dev->conn != NULL)
    {
      nxmutex_unlock(&dev->lock);
      file_put(filep);
      return -EBUSY;
    }

  dev->conn = conn;
  dev->crefs++;
  nxmutex_unlock(&dev->lock);

  file_put(filep);

  conn->priv      = dev;
  conn->transport = &g_fuse_dev_transport;

  return 0;
}

/****************************************************************************
 * Name: fuse_dev_initialize
 *
 * Description:
 *   Register the /dev/fuse character device.
 *
 ****************************************************************************/

int fuse_dev_initialize(void)
{
  return register_driver("/dev/fuse", &g_fuse_dev_fops, 0600, NULL);
}
