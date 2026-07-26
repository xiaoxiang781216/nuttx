/****************************************************************************
 * drivers/virtio/virtio-rpmb.c
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

#include <debug.h>
#include <errno.h>
#include <stdio.h>

#include <nuttx/fs/fs.h>
#include <nuttx/fs/ioctl.h>
#include <nuttx/percpu.h>
#include <nuttx/semaphore.h>
#include <nuttx/spinlock.h>
#include <nuttx/mmcsd.h>
#include <nuttx/virtio/virtio.h>
#include <nuttx/virtio/virtio-rpmb.h>

#include "virtio-rpmb.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define VIRTIO_RPMB_DEVNAME          "/dev/rpmb"

/* RPMB EXT_CSD opcode (eMMC standard command 8) */

#define VIRTIO_RPMB_EXT_CSD          8

/* Maximum number of commands in a multi-command request */

#define VIRTIO_RPMB_MMC_IOC_MAX_CMDS 3

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct virtio_rpmb_priv_s
{
  /* The virtio device we're associated with */

  FAR struct virtio_device *vdev;
  spinlock_t                lock;
};

struct virtio_rpmb_cookie_s
{
  sem_t  sem;
  size_t len;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int virtio_rpmb_probe(FAR struct virtio_device *vdev);
static void virtio_rpmb_remove(FAR struct virtio_device *vdev);
static int virtio_rpmb_ioctl(FAR struct file *, int, unsigned long);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static DEFINE_PER_CPU_BMP(struct virtio_driver, g_virtio_rpmb_driver) =
{
  VIRTIO_ID_RPMB,     /* device id */
  virtio_rpmb_probe,  /* probe */
  virtio_rpmb_remove, /* remove */
};
#define g_virtio_rpmb_driver this_cpu_var_bmp(g_virtio_rpmb_driver)

static const struct file_operations g_virtio_rpmb_ops =
{
  NULL,               /* open */
  NULL,               /* close */
  NULL,               /* read */
  NULL,               /* write */
  NULL,               /* seek */
  virtio_rpmb_ioctl,  /* ioctl */
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* virtio_rpmb_done() - virtual queue completion callback */

static void virtio_rpmb_done(FAR struct virtqueue *vq)
{
  FAR struct virtio_rpmb_priv_s *priv = vq->vq_dev->priv;
  FAR struct virtio_rpmb_cookie_s *cookie;
  uint32_t len;

  for (; ; )
    {
      cookie = virtqueue_get_buffer_lock(vq, &len, NULL, &priv->lock);
      if (cookie == NULL)
        {
          break;
        }

      /* Assign the return length */

      cookie->len = len;

      /* Read completed, post the sem */

      nxsem_post(&cookie->sem);
    }
}

/* This is just a simple helper for processing the virtqueue buf. It will
 * block until the resp arrives. Returns number of bytes written back
 * or negative if it failed.
 */

static int virtio_rpmb_transact(FAR struct virtio_rpmb_priv_s *priv,
                                FAR struct virtqueue_buf *vb,
                                int out, int in)
{
  FAR struct virtqueue *vq = priv->vdev->vrings_info[0].vq;
  struct virtio_rpmb_cookie_s cookie;
  irqstate_t flags;
  int ret;

  /* Init the cookie */

  cookie.len = 0;
  nxsem_init(&cookie.sem, 0, 0);

  /* Add the input buffer to the virtqueue, and the cookie as the virtqueue
   * cookie. (virtqueue_get_buffer() will return cookie).
   */

  flags = spin_lock_irqsave(&priv->lock);
  ret = virtqueue_add_buffer(vq, vb, out, in, &cookie);
  if (ret < 0)
    {
      spin_unlock_irqrestore(&priv->lock, flags);
      return ret;
    }

  /* Notify the other side to process the added virtqueue buffer */

  virtqueue_kick(vq);
  spin_unlock_irqrestore(&priv->lock, flags);

  /* Wait fot completion */

  nxsem_wait_uninterruptible(&cookie.sem);
  return cookie.len;
}

/****************************************************************************
 * Name: virtio_rpmb_ext_csd
 *
 * Description:
 *   Synthesize an EXT_CSD response from the virtio config space. The AP-side
 *   vhost-rpmb fills capacity/max_wr_cnt from the real eMMC EXT_CSD, so no
 *   raw eMMC command needs to be relayed through the virtqueue.
 *
 *   Only the two offsets the TEE actually reads are populated; the rest of
 *   the buffer is zeroed.
 *
 ****************************************************************************/

static int virtio_rpmb_ext_csd(FAR struct virtio_device *vdev,
                               FAR struct mmc_ioc_cmd *ioc)
{
  FAR uint8_t *ext_csd = (FAR uint8_t *)(uintptr_t)ioc->data_ptr;
  size_t len = ioc->blksz * ioc->blocks;
  struct virtio_rpmb_config_s cfg;

  if (ext_csd == NULL || len < VIRTIO_RPMB_EXT_CSD_SIZE)
    {
      return -EINVAL;
    }

  virtio_read_config(vdev, 0, &cfg, sizeof(cfg));

  memset(ext_csd, 0, VIRTIO_RPMB_EXT_CSD_SIZE);
  ext_csd[VIRTIO_RPMB_EXT_CSD_RPMB_SIZE] = cfg.capacity;
  ext_csd[VIRTIO_RPMB_EXT_CSD_REL_WR_SEC_C] = cfg.max_wr_cnt;

  return 0;
}

/****************************************************************************
 * Name: virtio_rpmb_multi_cmd
 *
 * Description:
 *   Relay an RPMB frame sequence to the device side. Each command becomes
 *   one virtqueue buffer: the leading commands are request frames (out),
 *   the last one is the response frame (in).
 *
 ****************************************************************************/

static int virtio_rpmb_multi_cmd(FAR struct virtio_rpmb_priv_s *priv,
                                 FAR struct mmc_ioc_multi_cmd *mioc)
{
  FAR struct virtio_device *vdev = priv->vdev;
  int ncmds = mioc->num_of_cmds;
  struct virtqueue_buf vb[VIRTIO_RPMB_MMC_IOC_MAX_CMDS];
  int ret;
  int i;

  /* At least one request frame and one response frame are required. */

  if (ncmds < 2 || ncmds > VIRTIO_RPMB_MMC_IOC_MAX_CMDS)
    {
      return -EINVAL;
    }

  /* Map each buffer into device visible memory. On MMIO/PCI this returns
   * the original address (zero copy); on rptun it bounces through the
   * carveout shared memory.
   */

  for (i = 0; i < ncmds; i++)
    {
      vb[i].len = mioc->cmds[i].blocks * mioc->cmds[i].blksz;

      ret = virtio_map_buf(vdev,
                           (FAR void *)(uintptr_t)mioc->cmds[i].data_ptr,
                           vb[i].len,
                           i < ncmds - 1 ? VIRTIO_DMA_TO_DEVICE :
                           VIRTIO_DMA_FROM_DEVICE,
                           &vb[i].buf);
      if (ret < 0)
        {
          vrterr("virtio_map_buf failed, i=%d ret=%d\n", i, ret);
          goto out_unmap;
        }
    }

  ret = virtio_rpmb_transact(priv, vb, ncmds - 1, 1);

  /* Unmap in reverse order. Unmapping an in buffer copies the data written
   * by the device back to the original buffer.
   */

out_unmap:
  while (--i >= 0)
    {
      virtio_unmap_buf(vdev,
                       (FAR void *)(uintptr_t)mioc->cmds[i].data_ptr,
                       vb[i].len,
                       i < ncmds - 1 ? VIRTIO_DMA_TO_DEVICE :
                       VIRTIO_DMA_FROM_DEVICE,
                       vb[i].buf);
    }

  return ret < 0 ? ret : 0;
}

/****************************************************************************
 * Name: virtio_rpmb_ioctl
 ****************************************************************************/

static int virtio_rpmb_ioctl(FAR struct file *filep, int cmd,
                             unsigned long arg)
{
  FAR struct virtio_rpmb_priv_s *priv;

  priv = (FAR struct virtio_rpmb_priv_s *)filep->f_inode->i_private;

  switch (cmd)
    {
      case MMC_IOC_CMD:
        {
          FAR struct mmc_ioc_cmd *ioc = (FAR struct mmc_ioc_cmd *)arg;

          if (ioc->opcode != VIRTIO_RPMB_EXT_CSD)
            {
              return -ENOTTY;
            }

          return virtio_rpmb_ext_csd(priv->vdev, ioc);
        }

      case MMC_IOC_MULTI_CMD:
        return virtio_rpmb_multi_cmd(priv,
                                     (FAR struct mmc_ioc_multi_cmd *)arg);

      default:
        return -ENOTTY;
    }
}

static int virtio_rpmb_init(FAR struct virtio_rpmb_priv_s *priv,
                            FAR struct virtio_device *vdev)
{
  FAR const char *vqname[1];
  vq_callback callback[1];
  int ret;

  priv->vdev = vdev;
  vdev->priv = priv;
  spin_lock_init(&priv->lock);

  /* Initialize the virtio device */

  virtio_set_status(vdev, VIRTIO_CONFIG_STATUS_DRIVER);
  virtio_set_features(vdev, 0);
  virtio_set_status(vdev, VIRTIO_CONFIG_FEATURES_OK);

  vqname[0]   = "virtio_rpmb_vq";
  callback[0] = virtio_rpmb_done;
  ret = virtio_create_virtqueues(vdev, 0, 1, vqname, callback, NULL);
  if (ret < 0)
    {
      vrterr("virtio_device_create_virtqueue failed, ret=%d\n", ret);
      goto err_with_lock;
    }

  virtio_set_status(vdev, VIRTIO_CONFIG_STATUS_DRIVER_OK);
  virtqueue_enable_cb(vdev->vrings_info[0].vq);
  return OK;

err_with_lock:
  return ret;
}

static void virtio_rpmb_uninit(FAR struct virtio_rpmb_priv_s *priv)
{
  FAR struct virtio_device *vdev = priv->vdev;

  virtio_reset_device(vdev);
  virtio_delete_virtqueues(vdev);
}

static int virtio_rpmb_probe(FAR struct virtio_device *vdev)
{
  FAR struct virtio_rpmb_priv_s *priv;
  int ret;

  /* Alloc the virtio block driver private data */

  priv = kmm_zalloc(sizeof(*priv));
  if (priv == NULL)
    {
      vrterr("No enough memory\n");
      return -ENOMEM;
    }

  /* Init the virtio block driver */

  ret = virtio_rpmb_init(priv, vdev);
  if (ret < 0)
    {
      vrterr("virtio_rpmb_init failed, ret=%d\n", ret);
      goto err_with_priv;
    }

  ret = register_driver(VIRTIO_RPMB_DEVNAME, &g_virtio_rpmb_ops, 0666,
                        priv);
  if (ret < 0)
    {
      vrterr("Register NuttX driver failed, ret=%d\n", ret);
      goto err_with_init;
    }

  return ret;

err_with_init:
  virtio_rpmb_uninit(priv);
err_with_priv:
  kmm_free(priv);
  return ret;
}

static void virtio_rpmb_remove(FAR struct virtio_device *vdev)
{
  FAR struct virtio_rpmb_priv_s *priv = vdev->priv;

  unregister_driver(VIRTIO_RPMB_DEVNAME);
  virtio_rpmb_uninit(priv);
  kmm_free(priv);
}

int virtio_register_rpmb_driver(void)
{
  return virtio_register_driver(&g_virtio_rpmb_driver);
}
