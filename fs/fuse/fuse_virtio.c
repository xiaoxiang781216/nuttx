/****************************************************************************
 * fs/fuse/fuse_virtio.c
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
#include <string.h>

#include <nuttx/nuttx.h>
#include <nuttx/virtio/virtio.h>
#include <nuttx/virtio/virtio-fs.h>

#include "fs_heap.h"
#include "fuse.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define VIRTIOFS_NUM_VQS    2
#define VIRTIOFS_HIPRIO_VQ  0
#define VIRTIOFS_REQUEST_VQ 1

/* Room reserved out of the mappable space for the fuse headers and the
 * per-segment alignment of the bounce carveout, so that mapping them
 * cannot fail once the payload fits.
 */

#define VIRTIOFS_HEADROOM   512

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* virtio-fs transport private data (one per mountpoint) */

struct fuse_virtio_s
{
  FAR struct virtio_device *vdev;
  struct virtio_driver      vdrv;
  char                      tag[VIRTIO_FS_TAG_LEN];
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int  fuse_virtio_request(FAR struct fuse_conn_s *conn,
                                FAR struct fuse_req_s *req);
static void fuse_virtio_destroy(FAR struct fuse_conn_s *conn);
static size_t fuse_virtio_maxbuf(FAR struct fuse_conn_s *conn);
static int  fuse_virtio_probe(FAR struct virtio_device *vdev);
static void fuse_virtio_remove(FAR struct virtio_device *vdev);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct fuse_transport_ops_s g_fuse_virtio_transport =
{
  fuse_virtio_request,  /* request */
  fuse_virtio_destroy,  /* destroy */
  fuse_virtio_maxbuf    /* maxbuf */
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: fuse_virtio_done
 *
 * Description:
 *   Request virtqueue completion callback.  Each completed buffer carries
 *   the submitter's semaphore as its cookie; wake it.
 *
 ****************************************************************************/

static void fuse_virtio_done(FAR struct virtqueue *vq)
{
  for (; ; )
    {
      FAR sem_t *sem = virtqueue_get_buffer(vq, NULL, NULL);
      if (sem == NULL)
        {
          break;
        }

      nxsem_post(sem);
    }
}

/****************************************************************************
 * Name: fuse_virtio_slot
 *
 * Description:
 *   Describe the i-th virtqueue segment of a request:  the device
 *   readable fuse_in_header and request arguments come first, then the
 *   device writable fuse_out_header and reply buffers.
 *
 ****************************************************************************/

static inline enum virtio_dma_map
fuse_virtio_slot(FAR struct fuse_req_s *req, size_t i,
                 FAR void **buf, FAR size_t *len)
{
  if (i == 0)
    {
      *buf = &req->hin;
      *len = sizeof(req->hin);
      return VIRTIO_DMA_TO_DEVICE;
    }
  else if (i <= req->vin_cnt)
    {
      *buf = req->vin[i - 1].iov_base;
      *len = req->vin[i - 1].iov_len;
      return VIRTIO_DMA_TO_DEVICE;
    }
  else if (i == req->vin_cnt + 1)
    {
      *buf = &req->hout;
      *len = sizeof(req->hout);
      return VIRTIO_DMA_FROM_DEVICE;
    }
  else
    {
      *buf = req->vout[i - req->vin_cnt - 2].iov_base;
      *len = req->vout[i - req->vin_cnt - 2].iov_len;
      return VIRTIO_DMA_FROM_DEVICE;
    }
}

/****************************************************************************
 * Name: fuse_virtio_request
 *
 * Description:
 *   Submit a FUSE request through the request virtqueue and wait for the
 *   host to complete it.  Every segment is wrapped with virtio_map_buf so
 *   that a bouncing transport (e.g. rptun), whose peer cannot access
 *   arbitrary memory, can reach it; the reply lands in the original
 *   buffers when the segments are unmapped.  On MMIO/PCI map is a no-op
 *   and this stays a single zero-copy pass.  The request wait is posted
 *   before returning to satisfy the transport contract.
 *
 ****************************************************************************/

static int fuse_virtio_request(FAR struct fuse_conn_s *conn,
                               FAR struct fuse_req_s *req)
{
  FAR struct fuse_virtio_s *fs = conn->priv;
  FAR struct virtio_device *vdev = fs->vdev;
  FAR struct virtqueue *vq = vdev->vrings_info[VIRTIOFS_REQUEST_VQ].vq;
  size_t total = req->vin_cnt + req->vout_cnt + 2;
  struct virtqueue_buf vb[total];
  enum virtio_dma_map dir;
  irqstate_t flags;
  FAR void *buf;
  sem_t done;
  size_t len;
  size_t i;
  int ret;

  /* Map every segment:  a bouncing transport copies device readable
   * segments into shared memory now and returns -ENOMEM when its
   * carveout cannot satisfy the map
   */

  for (i = 0; i < total; i++)
    {
      dir = fuse_virtio_slot(req, i, &buf, &len);
      ret = virtio_map_buf(vdev, buf, len, dir, &buf);
      if (ret < 0)
        {
          goto out;
        }

      vb[i].buf = buf;
      vb[i].len = len;
    }

  nxsem_init(&done, 0, 0);

  flags = spin_lock_irqsave(&conn->lock);
  ret = virtqueue_add_buffer(vq, vb, req->vin_cnt + 1,
                             req->vout_cnt + 1, &done);
  if (ret >= 0)
    {
      virtqueue_kick(vq);
    }

  spin_unlock_irqrestore(&conn->lock, flags);

  /* Wait for the completion here:  the unmap below must run after the
   * host is done with the buffers, since it copies the reply data back
   * to the original buffers on a bouncing transport.
   */

  if (ret >= 0)
    {
      nxsem_wait_uninterruptible(&done);
    }

  nxsem_destroy(&done);

out:
  while (i-- > 0)
    {
      dir = fuse_virtio_slot(req, i, &buf, &len);
      virtio_unmap_buf(vdev, buf, len, dir, vb[i].buf);
    }

  /* The reply is already in place:  post the request wait that the core
   * blocks on so its wait returns immediately.
   */

  if (ret >= 0)
    {
      nxsem_post(&req->wait);
    }

  return ret;
}

/****************************************************************************
 * Name: fuse_virtio_maxbuf
 *
 * Description:
 *   Largest payload one request can carry right now.  On a bouncing
 *   transport this follows the free shared memory; on MMIO/PCI
 *   virtio_free_largest reports SIZE_MAX and reads/writes stay
 *   unsplit.
 *
 ****************************************************************************/

static size_t fuse_virtio_maxbuf(FAR struct fuse_conn_s *conn)
{
  FAR struct fuse_virtio_s *fs = conn->priv;
  size_t largest = virtio_free_largest(fs->vdev);

  return largest > VIRTIOFS_HEADROOM ? largest - VIRTIOFS_HEADROOM : 0;
}

/****************************************************************************
 * Name: fuse_virtio_probe
 ****************************************************************************/

static int fuse_virtio_probe(FAR struct virtio_device *vdev)
{
  FAR struct fuse_virtio_s *fs = container_of(vdev->priv,
                                 struct fuse_virtio_s, vdrv);
  FAR const char *vqnames[VIRTIOFS_NUM_VQS];
  vq_callback callbacks[VIRTIOFS_NUM_VQS];
  char tag[VIRTIO_FS_TAG_LEN];
  int ret;

  virtio_set_status(vdev, VIRTIO_CONFIG_STATUS_DRIVER);
  virtio_negotiate_features(vdev, 0, NULL);
  virtio_set_status(vdev, VIRTIO_CONFIG_FEATURES_OK);

  /* Read and match the device tag */

  virtio_read_config_member(vdev, struct virtio_fs_config_s, tag, tag);
  tag[sizeof(tag) - 1] = '\0';
  if (strcmp(tag, fs->tag) != 0)
    {
      virtio_reset_device(vdev);
      return -ENOENT;
    }

  /* Create the hiprio and request virtqueues */

  vqnames[VIRTIOFS_HIPRIO_VQ]    = "virtiofs-hiprio";
  vqnames[VIRTIOFS_REQUEST_VQ]   = "virtiofs-requests";
  callbacks[VIRTIOFS_HIPRIO_VQ]  = NULL;
  callbacks[VIRTIOFS_REQUEST_VQ] = fuse_virtio_done;

  ret = virtio_create_virtqueues(vdev, 0, VIRTIOFS_NUM_VQS,
                                 vqnames, callbacks, NULL);
  if (ret < 0)
    {
      virtio_reset_device(vdev);
      return ret;
    }

  fs->vdev   = vdev;
  vdev->priv = fs;

  virtio_set_status(vdev, VIRTIO_CONFIG_STATUS_DRIVER_OK);
  virtqueue_enable_cb(vdev->vrings_info[VIRTIOFS_REQUEST_VQ].vq);

  return 0;
}

/****************************************************************************
 * Name: fuse_virtio_remove
 ****************************************************************************/

static void fuse_virtio_remove(FAR struct virtio_device *vdev)
{
  FAR struct fuse_virtio_s *fs = vdev->priv;

  fs->vdev = NULL;
  virtio_delete_virtqueues(vdev);
  virtio_reset_device(vdev);
}

/****************************************************************************
 * Name: fuse_virtio_destroy
 ****************************************************************************/

static void fuse_virtio_destroy(FAR struct fuse_conn_s *conn)
{
  FAR struct fuse_virtio_s *fs = conn->priv;

  virtio_unregister_driver(&fs->vdrv);
  fs_heap_free(fs);
  conn->priv = NULL;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: fuse_virtio_connect
 *
 * Description:
 *   Attach a FUSE connection to a virtio-fs device selected by the
 *   "tag=<name>" mount option.
 *
 ****************************************************************************/

int fuse_virtio_connect(FAR struct fuse_conn_s *conn,
                        FAR const char *options)
{
  FAR struct fuse_virtio_s *fs;
  FAR const char *tag;
  int ret;

  tag = strstr(options, "tag=");
  if (tag == NULL)
    {
      return -EINVAL;
    }

  tag += 4;

  fs = fs_heap_zalloc(sizeof(*fs));
  if (fs == NULL)
    {
      return -ENOMEM;
    }

  strlcpy(fs->tag, tag, sizeof(fs->tag));

  /* Register the virtio driver to trigger probe and bind the device */

  fs->vdrv.device = VIRTIO_ID_FS;
  fs->vdrv.probe  = fuse_virtio_probe;
  fs->vdrv.remove = fuse_virtio_remove;

  ret = virtio_register_driver(&fs->vdrv);
  if (ret >= 0 && fs->vdev == NULL)
    {
      virtio_unregister_driver(&fs->vdrv);
      ret = -ENODEV;
    }

  if (ret < 0)
    {
      fs_heap_free(fs);
      return ret;
    }

  conn->priv      = fs;
  conn->transport = &g_fuse_virtio_transport;

  return 0;
}
