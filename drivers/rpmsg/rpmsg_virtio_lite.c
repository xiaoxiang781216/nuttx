/****************************************************************************
 * drivers/rpmsg/rpmsg_virtio_lite.c
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

#include <nuttx/config.h>

#include <debug.h>
#include <stdio.h>
#include <sys/param.h>

#include <metal/cache.h>
#include <nuttx/kmalloc.h>
#include <nuttx/kthread.h>
#include <nuttx/nuttx.h>
#include <nuttx/panic_notifier.h>
#include <nuttx/power/pm.h>
#include <nuttx/semaphore.h>
#include <nuttx/spinlock.h>
#include <nuttx/rpmsg/rpmsg_virtio.h>
#include <nuttx/rpmsg/rpmsg_virtio_lite.h>

#include "rpmsg.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define RPMSG_VIRTIO_LITE_TIMEOUT_MS      20

#define RPMSG_VIRTIO_LITE_VDEV_NOTIFYID   0
#define RPMSG_VIRTIO_LITE_VRING0_NOTIFYID 1
#define RPMSG_VIRTIO_LITE_VRING1_NOTIFYID 2

#ifdef CONFIG_OPENAMP_CACHE
#  define RPMSG_VIRTIO_INVALIDATE(x) metal_cache_invalidate(&x, sizeof(x))
#else
#  define RPMSG_VIRTIO_INVALIDATE(x)
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct rpmsg_virtio_lite_priv_s
{
  struct rpmsg_s                     rpmsg;
  struct rpmsg_virtio_device         rvdev;
  FAR struct rpmsg_virtio_lite_s     *dev;
  FAR struct rpmsg_virtio_lite_rsc_s *rsc;
  struct virtio_device               vdev;
  struct virtio_vring_info           rvrings[2];
  FAR void                           *shmbuf;
  size_t                             shmlen;
  struct notifier_block              nb;
  bool                               rpanic;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int
rpmsg_virtio_lite_create_virtqueues_(FAR struct virtio_device *vdev,
                                     unsigned int flags,
                                     unsigned int nvqs,
                                     FAR const char *names[],
                                     vq_callback callbacks[],
                                     FAR void *callback_args[]);
static uint8_t rpmsg_virtio_lite_get_status_(FAR struct virtio_device *dev);
static void rpmsg_virtio_lite_set_status_(FAR struct virtio_device *dev,
                                          uint8_t status);
static uint64_t
rpmsg_virtio_lite_get_features_(FAR struct virtio_device *dev);
static void rpmsg_virtio_lite_set_features(FAR struct virtio_device *dev,
                                           uint64_t feature);
static uint64_t
rpmsg_virtio_lite_negotiate_features(FAR struct virtio_device *vdev,
                                     uint64_t features);
static void rpmsg_virtio_lite_read_config(struct virtio_device *vdev,
                                          uint32_t offset, void *dst,
                                          int length);
static void rpmsg_virtio_lite_notify(FAR struct virtqueue *vq);
static void
rpmsg_virtio_lite_send_command(FAR struct rpmsg_virtio_lite_priv_s *priv,
                               uint32_t cmd, bool wait);
static FAR void *rpmsg_virtio_lite_alloc_buf(FAR struct virtio_device *vdev,
                                             size_t size, size_t align);
static void rpmsg_virtio_lite_free_buf(FAR struct virtio_device *vdev,
                                       FAR void *buf);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct virtio_dispatch g_rpmsg_virtio_lite_dispatch =
{
  .create_virtqueues  = rpmsg_virtio_lite_create_virtqueues_,
  .get_status         = rpmsg_virtio_lite_get_status_,
  .set_status         = rpmsg_virtio_lite_set_status_,
  .get_features       = rpmsg_virtio_lite_get_features_,
  .set_features       = rpmsg_virtio_lite_set_features,
  .negotiate_features = rpmsg_virtio_lite_negotiate_features,
  .read_config        = rpmsg_virtio_lite_read_config,
  .notify             = rpmsg_virtio_lite_notify,
};

static const struct virtio_memory_ops g_rpmsg_virtio_lite_mmops =
{
  .alloc = rpmsg_virtio_lite_alloc_buf,
  .free = rpmsg_virtio_lite_free_buf,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: rpmsg_virtio_lite_get_priv
 ****************************************************************************/

static FAR struct rpmsg_virtio_lite_priv_s *
rpmsg_virtio_lite_get_priv(FAR struct virtio_device *vdev)
{
  return metal_container_of(vdev, struct rpmsg_virtio_lite_priv_s, vdev);
}

/****************************************************************************
 * Name: rpmsg_virtio_lite_create_virtqueues_
 ****************************************************************************/

static int
rpmsg_virtio_lite_create_virtqueues_(FAR struct virtio_device *vdev,
                                     unsigned int flags,
                                     unsigned int nvqs,
                                     FAR const char *names[],
                                     vq_callback callbacks[],
                                     FAR void *callback_args[])
{
  int ret;
  int i;

  if (nvqs > vdev->vrings_num)
    {
      return ERROR_VQUEUE_INVLD_PARAM;
    }

  /* Initialize virtqueue for each vring */

  for (i = 0; i < nvqs; i++)
    {
      FAR struct virtio_vring_info *vinfo = &vdev->vrings_info[i];
      FAR struct vring_alloc_info *valloc = &vinfo->info;
#ifndef CONFIG_OPENAMP_VIRTIO_DEVICE_ONLY
      if (vdev->role == VIRTIO_DEV_DRIVER)
        {
          size_t offset;

          offset = metal_io_virt_to_offset(vinfo->io, valloc->vaddr);
          metal_io_block_set(vinfo->io, offset, 0,
                             vring_size(valloc->num_descs, valloc->align));
        }
#endif

      ret = virtqueue_create(vdev, i, names[i], valloc,
                             callbacks[i], vdev->func->notify,
                             vinfo->vq);
      if (ret < 0)
        {
          return ret;
        }
    }

  return 0;
}

/****************************************************************************
 * Name: rpmsg_virtio_lite_create_virtqueues
 ****************************************************************************/

static uint8_t rpmsg_virtio_lite_get_status_(FAR struct virtio_device *vdev)
{
  FAR struct rpmsg_virtio_lite_priv_s *priv =
    rpmsg_virtio_lite_get_priv(vdev);

  return priv->rsc->rpmsg_vdev.status;
}

/****************************************************************************
 * Name: rpmsg_virtio_lite_set_status_
 ****************************************************************************/

static void rpmsg_virtio_lite_set_status_(FAR struct virtio_device *vdev,
                                          uint8_t status)
{
  FAR struct rpmsg_virtio_lite_priv_s *priv =
    rpmsg_virtio_lite_get_priv(vdev);

  priv->rsc->rpmsg_vdev.status = status;
}

/****************************************************************************
 * Name: rpmsg_virtio_lite_get_features_
 ****************************************************************************/

static uint64_t
rpmsg_virtio_lite_get_features_(FAR struct virtio_device *vdev)
{
  FAR struct rpmsg_virtio_lite_priv_s *priv =
    rpmsg_virtio_lite_get_priv(vdev);

  vdev->features = priv->rsc->rpmsg_vdev.gfeatures;

  return priv->rsc->rpmsg_vdev.dfeatures;
}

/****************************************************************************
 * Name: rpmsg_virtio_lite_set_features_
 ****************************************************************************/

static void rpmsg_virtio_lite_set_features(FAR struct virtio_device *vdev,
                                           uint64_t features)
{
  FAR struct rpmsg_virtio_lite_priv_s *priv =
    rpmsg_virtio_lite_get_priv(vdev);

  priv->rsc->rpmsg_vdev.gfeatures = features;
  vdev->features = features;
}

/****************************************************************************
 * Name: rpmsg_virtio_lite_negotiate_features
 ****************************************************************************/

static uint64_t
rpmsg_virtio_lite_negotiate_features(FAR struct virtio_device *vdev,
                                     uint64_t features)
{
  features = features & rpmsg_virtio_lite_get_features_(vdev);
  rpmsg_virtio_lite_set_features(vdev, features);
  return features;
}

/****************************************************************************
 * Name: rpmsg_virtio_lite_read_config
 ****************************************************************************/

static void rpmsg_virtio_lite_read_config(struct virtio_device *vdev,
                                          uint32_t offset, void *dst,
                                          int length)
{
  FAR struct rpmsg_virtio_lite_priv_s *priv =
    rpmsg_virtio_lite_get_priv(vdev);
  FAR char *config = (FAR char *)&priv->rsc->config;

  if (offset + length <= priv->rsc->rpmsg_vdev.config_len)
    {
      memcpy(dst, config + offset, length);
    }
}

/****************************************************************************
 * Name: rpmsg_virtio_lite_notify
 ****************************************************************************/

static void rpmsg_virtio_lite_notify(FAR struct virtqueue *vq)
{
  FAR struct virtio_device *vdev = vq->vq_dev;
  FAR struct rpmsg_virtio_lite_priv_s *priv =
    rpmsg_virtio_lite_get_priv(vdev);

  RPMSG_VIRTIO_LITE_NOTIFY(priv->dev,
                           vdev->vrings_info->notifyid);
}

/****************************************************************************
 * Name: rpmsg_virtio_lite_alloc_buf
 ****************************************************************************/

static FAR void *rpmsg_virtio_lite_alloc_buf(FAR struct virtio_device *vdev,
                                             size_t size, size_t align)
{
  FAR struct rpmsg_virtio_lite_priv_s *priv =
    rpmsg_virtio_lite_get_priv(vdev);

  DEBUGASSERT(vdev->role != VIRTIO_DEV_DEVICE && priv->shmlen >= size &&
              ((uintptr_t)priv->shmbuf % align) == 0);
  return priv->shmbuf;
}

/****************************************************************************
 * Name: rpmsg_virtio_lite_free_buf
 ****************************************************************************/

static void rpmsg_virtio_lite_free_buf(FAR struct virtio_device *vdev,
                                       FAR void *buf)
{
  rpmsgerr("Do not support memory free\n");
  PANIC();
}

/****************************************************************************
 * Name: rpmsg_virtio_lite_panic
 ****************************************************************************/

static void rpmsg_virtio_lite_panic(FAR struct rpmsg_s *rpmsg)
{
  FAR struct rpmsg_virtio_lite_priv_s *priv =
    (FAR struct rpmsg_virtio_lite_priv_s *)rpmsg;

  if (priv->rpanic)
    {
      return;
    }

  metal_log(METAL_LOG_EMERGENCY, "Panic remote cpu %s:\n",
    RPMSG_VIRTIO_LITE_GET_CPUNAME(priv->dev));

  rpmsg_virtio_lite_send_command(priv,
    RPMSG_VIRTIO_LITE_CMD(RPMSG_VIRTIO_LITE_CMD_PANIC, 0), true);

  priv->rpanic = true;
}

/****************************************************************************
 * Name: rpmsg_virtio_lite_panic_notifier
 ****************************************************************************/

static int rpmsg_virtio_lite_panic_notifier(FAR struct notifier_block *block,
                                            unsigned long action, void *data)
{
  FAR struct rpmsg_virtio_lite_priv_s *priv =
    container_of(block, struct rpmsg_virtio_lite_priv_s, nb);

  if (action == PANIC_KERNEL_FINAL)
    {
      /* PANIC all the remote core */

      rpmsg_virtio_lite_panic(&priv->rpmsg);
    }

  return 0;
}

/****************************************************************************
 * Name: rpmsg_virtio_lite_send_command
 ****************************************************************************/

static void
rpmsg_virtio_lite_send_command(FAR struct rpmsg_virtio_lite_priv_s *priv,
                               uint32_t cmd, bool wait)
{
  FAR struct rpmsg_virtio_lite_cmd_s *rpmsg_virtio_cmd =
    RPMSG_VIRTIO_LITE_RSC2CMD(priv->rsc);
  uint32_t timeout = CONFIG_RPMSG_VIRTIO_LITE_CMD_TIMEOUT_MS;

  if (priv->dev->ops->send_command)
    {
      priv->dev->ops->send_command(priv->dev, cmd, wait);
      return;
    }

  if (RPMSG_VIRTIO_LITE_IS_MASTER(priv->dev))
    {
      rpmsg_virtio_cmd->cmd_master = cmd;
    }
  else
    {
      rpmsg_virtio_cmd->cmd_slave = cmd;
    }

  rpmsg_virtio_lite_notify(priv->vdev.vrings_info->vq);

  /* Wait until the peer side has received the commnand */

  while (wait && timeout-- > 0)
    {
      if (RPMSG_VIRTIO_LITE_IS_MASTER(priv->dev))
        {
          cmd = rpmsg_virtio_cmd->cmd_master;
        }
      else
        {
          cmd = rpmsg_virtio_cmd->cmd_slave;
        }

      if (RPMSG_VIRTIO_LITE_GET_CMD(cmd) == RPMSG_VIRTIO_LITE_CMD_DONE)
        {
          break;
        }

      up_mdelay(1);
    }
}

/****************************************************************************
 * Name: rpmsg_virtio_lite_recv_command
 ****************************************************************************/

static uint32_t
rpmsg_virtio_lite_recv_command(FAR struct rpmsg_virtio_lite_priv_s *priv)
{
  FAR struct rpmsg_virtio_lite_cmd_s *rpmsg_virtio_cmd =
    RPMSG_VIRTIO_LITE_RSC2CMD(priv->rsc);
  uint32_t cmd;

  if (priv->dev->ops->recv_command)
    {
      cmd = priv->dev->ops->recv_command(priv->dev);
      return cmd;
    }

  if (RPMSG_VIRTIO_LITE_IS_MASTER(priv->dev))
    {
      cmd = rpmsg_virtio_cmd->cmd_slave;
      rpmsg_virtio_cmd->cmd_slave = RPMSG_VIRTIO_LITE_CMD_DONE;
    }
  else
    {
      cmd = rpmsg_virtio_cmd->cmd_master;
      rpmsg_virtio_cmd->cmd_master = RPMSG_VIRTIO_LITE_CMD_DONE;
    }

  return cmd;
}

/****************************************************************************
 * Name: rpmsg_virtio_lite_check_command
 ****************************************************************************/

static void
rpmsg_virtio_lite_check_command(FAR struct rpmsg_virtio_lite_priv_s *priv)
{
  uint32_t cmd = rpmsg_virtio_lite_recv_command(priv);

  switch (RPMSG_VIRTIO_LITE_GET_CMD(cmd))
    {
      case RPMSG_VIRTIO_LITE_CMD_PANIC:
        priv->rpanic = true;
        PANIC();
        break;

      default:
        break;
    }
}

/****************************************************************************
 * Name: rpmsg_virtio_lite_callback
 ****************************************************************************/

static int rpmsg_virtio_lite_callback(FAR void *arg, uint32_t vqid)
{
  FAR struct rpmsg_virtio_lite_priv_s *priv = arg;
  FAR struct virtio_device *vdev = &priv->vdev;
  FAR struct virtqueue *rvq;
  FAR struct virtqueue *svq;

  rpmsg_virtio_lite_check_command(priv);

  if (VIRTIO_ROLE_IS_DRIVER(vdev))
    {
      rvq  = vdev->vrings_info[0].vq;
      svq  = vdev->vrings_info[1].vq;
    }
  else
    {
      rvq  = vdev->vrings_info[1].vq;
      svq  = vdev->vrings_info[0].vq;
    }

  if (vqid == RPMSG_VIRTIO_LITE_NOTIFY_ALL ||
      vqid == vdev->vrings_info[rvq->vq_queue_index].notifyid)
    {
      virtqueue_notification(rvq);
    }

  if (vqid == RPMSG_VIRTIO_LITE_NOTIFY_ALL ||
      vqid == vdev->vrings_info[svq->vq_queue_index].notifyid)
    {
      virtqueue_notification(svq);
    }

  return OK;
}

/****************************************************************************
 * Name: rpmsg_virtio_lite_da_to_pa
 ****************************************************************************/

static metal_phys_addr_t
rpmsg_virtio_lite_da_to_pa(FAR struct rpmsg_virtio_lite_s *dev,
                           metal_phys_addr_t da)
{
  FAR const struct rpmsg_virtio_lite_addrenv_s *addrenv;
  int i;

  addrenv = RPMSG_VIRTIO_LITE_GET_ADDRENV(dev);
  if (addrenv == NULL)
    {
      return da;
    }

  for (i = 0; addrenv[i].size; i++)
    {
      if (da - addrenv[i].da < addrenv[i].size)
        {
          return addrenv[i].pa + (da - addrenv[i].da);
        }
    }

  return da;
}

/****************************************************************************
 * Name: rpmsg_virtio_lite_start
 ****************************************************************************/

static int rpmsg_virtio_lite_thread(int argc, FAR char *argv[])
{
  FAR struct rpmsg_virtio_lite_priv_s *priv =
    (FAR struct rpmsg_virtio_lite_priv_s *)
    ((uintptr_t)strtoul(argv[2], NULL, 16));
  FAR struct virtio_vring_info *rvrings = priv->rvrings;
  FAR struct virtio_device *vdev = &priv->vdev;
  FAR struct rpmsg_virtio_lite_rsc_s *rsc;
  FAR char *shmbase;
  int ret = -EINVAL;
  uint32_t align0;
  uint32_t align1;
  uint32_t tbsz;
  uint32_t v0sz;
  uint32_t v1sz;

  rsc = RPMSG_VIRTIO_LITE_GET_RESOURCE(priv->dev);
  if (!rsc)
    {
      goto err;
    }

  priv->rsc = rsc;

  vdev->notifyid = RPMSG_VIRTIO_LITE_VDEV_NOTIFYID;
  vdev->vrings_num = rsc->rpmsg_vdev.num_of_vrings;
  vdev->role = RPMSG_VIRTIO_LITE_IS_MASTER(priv->dev) ?
               RPMSG_HOST : RPMSG_REMOTE;
  vdev->func = &g_rpmsg_virtio_lite_dispatch;
  vdev->mmops = &g_rpmsg_virtio_lite_mmops;

  align0 = rsc->rpmsg_vring0.align;
  align1 = rsc->rpmsg_vring1.align;
  tbsz = ALIGN_UP(sizeof(*rsc), MAX(align0, align1));
  v0sz = ALIGN_UP(vring_size(rsc->rpmsg_vring0.num, align0), align0);
  v1sz = ALIGN_UP(vring_size(rsc->rpmsg_vring1.num, align1), align1);

  shmbase = up_addrenv_pa_to_va(
    rpmsg_virtio_lite_da_to_pa(priv->dev, rsc->carveout.da));
  if (shmbase == NULL)
    {
      goto err;
    }

  rvrings[0].io = metal_io_get_region();
  rvrings[0].info.vaddr = shmbase + tbsz;
  rvrings[0].info.num_descs = rsc->rpmsg_vring0.num;
  rvrings[0].info.align = rsc->rpmsg_vring0.align;
  rvrings[0].notifyid = RPMSG_VIRTIO_LITE_VRING0_NOTIFYID;
  rvrings[0].vq = virtqueue_allocate(rsc->rpmsg_vring0.num);
  if (rvrings[0].vq == NULL)
    {
      ret = -ENOMEM;
      goto err;
    }

  rvrings[1].io = metal_io_get_region();
  rvrings[1].info.vaddr = shmbase + tbsz + v0sz;
  rvrings[1].info.num_descs = rsc->rpmsg_vring1.num;
  rvrings[1].info.align = rsc->rpmsg_vring1.align;
  rvrings[1].notifyid = RPMSG_VIRTIO_LITE_VRING1_NOTIFYID;
  rvrings[1].vq = virtqueue_allocate(rsc->rpmsg_vring1.num);
  if (rvrings[1].vq == NULL)
    {
      ret = -ENOMEM;
      goto err_vq0;
    }

  vdev->vrings_info = &rvrings[0];

  priv->shmbuf = shmbase + tbsz + v0sz + v1sz;
  priv->shmlen = rsc->carveout.len - tbsz - v0sz - v1sz;

  RPMSG_VIRTIO_LITE_REGISTER_CALLBACK(priv->dev, rpmsg_virtio_lite_callback,
                                      priv);

  priv->nb.notifier_call = rpmsg_virtio_lite_panic_notifier;
  panic_notifier_chain_register(&priv->nb);

  ret = rpmsg_virtio_probe(vdev);
  if (ret < 0)
    {
      goto err_vq1;
    }

  return ret;

err_vq1:
  panic_notifier_chain_unregister(&priv->nb);
  RPMSG_VIRTIO_LITE_REGISTER_CALLBACK(priv->dev, NULL, NULL);
  virtqueue_free(rvrings[1].vq);
err_vq0:
  virtqueue_free(rvrings[0].vq);
err:
  kmm_free(priv);
  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int rpmsg_virtio_lite_initialize(FAR struct rpmsg_virtio_lite_s *dev)
{
  FAR struct rpmsg_virtio_lite_priv_s *priv;
  FAR char *argv[3];
  char arg1[32];
  int ret;

  priv = kmm_zalloc(sizeof(struct rpmsg_virtio_lite_priv_s));
  if (priv == NULL)
    {
      return -ENOMEM;
    }

  priv->dev = dev;

  snprintf(arg1, sizeof(arg1), "%p", priv);
  argv[0] = (FAR char *)priv->rpmsg.cpuname;
  argv[1] = arg1;
  argv[2] = NULL;

  ret = kthread_create("rpmsg_virtio_lite",
                       CONFIG_RPMSG_VIRTIO_LITE_PRIORITY,
                       CONFIG_RPMSG_VIRTIO_LITE_STACKSIZE,
                       rpmsg_virtio_lite_thread, argv);
  if (ret < 0)
    {
      kmm_free(priv);
    }

  return ret;
}
