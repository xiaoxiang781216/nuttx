/****************************************************************************
 * drivers/rpmsg/rpmsg_virtio_bmp.c
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
#include <stdbool.h>
#include <stdio.h>

#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <nuttx/rpmsg/rpmsg_virtio_bmp.h>

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct rpmsg_virtio_bmp_dev_s
{
  struct rpmsg_virtio_lite_s          rpmsg;
  rpmsg_virtio_callback_t             callback;
  FAR void                           *arg;
  bool                                master;
  FAR struct rpmsg_virtio_lite_rsc_s *rsc;
  char                                cpuname[RPMSG_NAME_SIZE + 1];
  int                                 irq_event;
  int                                 irq_trigger;
  cpu_set_t                           cpuset;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static FAR const char *
rpmsg_virtio_bmp_get_cpuname(FAR struct rpmsg_virtio_lite_s *dev);
static FAR struct rpmsg_virtio_lite_rsc_s *
rpmsg_virtio_bmp_get_resource(FAR struct rpmsg_virtio_lite_s *dev);
static int rpmsg_virtio_bmp_is_master(FAR struct rpmsg_virtio_lite_s *dev);
static int rpmsg_virtio_bmp_notify(FAR struct rpmsg_virtio_lite_s *dev,
                                   uint32_t vqid);
static int
rpmsg_virtio_bmp_register_callback(FAR struct rpmsg_virtio_lite_s *dev,
                                   rpmsg_virtio_callback_t callback,
                                   FAR void *arg);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct rpmsg_virtio_lite_ops_s g_rpmsg_virtio_bmp_ops =
{
  .get_cpuname       = rpmsg_virtio_bmp_get_cpuname,
  .get_resource      = rpmsg_virtio_bmp_get_resource,
  .is_master         = rpmsg_virtio_bmp_is_master,
  .notify            = rpmsg_virtio_bmp_notify,
  .register_callback = rpmsg_virtio_bmp_register_callback,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: rpmsg_virtio_bmp_get_cpuname
 ****************************************************************************/

static FAR const char *
rpmsg_virtio_bmp_get_cpuname(FAR struct rpmsg_virtio_lite_s *dev)
{
  FAR struct rpmsg_virtio_bmp_dev_s *priv =
    (FAR struct rpmsg_virtio_bmp_dev_s *)dev;

  return priv->cpuname;
}

/****************************************************************************
 * Name: rpmsg_virtio_bmp_get_resource
 ****************************************************************************/

static FAR struct rpmsg_virtio_lite_rsc_s *
rpmsg_virtio_bmp_get_resource(FAR struct rpmsg_virtio_lite_s *dev)
{
  FAR struct rpmsg_virtio_bmp_dev_s *priv =
    (FAR struct rpmsg_virtio_bmp_dev_s *)dev;
  FAR struct rpmsg_virtio_lite_cmd_s *cmd =
    RPMSG_VIRTIO_LITE_RSC2CMD(priv->rsc);

  if (priv->master)
    {
      cmd->cmd_slave = RPMSG_VIRTIO_LITE_CMD(RPMSG_VIRTIO_LITE_CMD_READY, 0);
    }
  else
    {
      while (RPMSG_VIRTIO_LITE_GET_CMD(cmd->cmd_slave) !=
             RPMSG_VIRTIO_LITE_CMD_READY)
        {
          nxsig_usleep(100);
        }

      cmd->cmd_slave = RPMSG_VIRTIO_LITE_CMD(RPMSG_VIRTIO_LITE_CMD_DONE, 0);
    }

  return priv->rsc;
}

/****************************************************************************
 * Name: rpmsg_virtio_bmp_is_master
 ****************************************************************************/

static int rpmsg_virtio_bmp_is_master(FAR struct rpmsg_virtio_lite_s *dev)
{
  FAR struct rpmsg_virtio_bmp_dev_s *priv =
    (FAR struct rpmsg_virtio_bmp_dev_s *)dev;

  return priv->master;
}

/****************************************************************************
 * Name: rpmsg_virtio_bmp_interrupt
 *
 * Description:
 *   This is the interrupt handler.
 *
 * Input Parameters:
 *   irq      - unused
 *   context  - context, unused
 *   arg      - private data pointer
 *
 * Returned Value:
 *   OK always
 *
 ****************************************************************************/

static int rpmsg_virtio_bmp_interrupt(int irq, FAR void *context,
                                      FAR void *arg)
{
  FAR struct rpmsg_virtio_bmp_dev_s *priv = arg;

  if (priv->callback != NULL)
    {
      priv->callback(priv->arg, RPMSG_VIRTIO_LITE_NOTIFY_ALL);
    }

  return OK;
}

/****************************************************************************
 * Name: rpmsg_virtio_bmp_notify
 ****************************************************************************/

static int rpmsg_virtio_bmp_notify(FAR struct rpmsg_virtio_lite_s *dev,
                                   uint32_t vqid)
{
  FAR struct rpmsg_virtio_bmp_dev_s *priv =
    (FAR struct rpmsg_virtio_bmp_dev_s *)dev;

  up_trigger_irq(priv->irq_trigger, priv->cpuset);

  return OK;
}

/****************************************************************************
 * Name: rpmsg_virtio_bmp_register_callback
 ****************************************************************************/

static int
rpmsg_virtio_bmp_register_callback(FAR struct rpmsg_virtio_lite_s *dev,
                                   rpmsg_virtio_callback_t callback,
                                   FAR void *arg)
{
  FAR struct rpmsg_virtio_bmp_dev_s *priv =
    (FAR struct rpmsg_virtio_bmp_dev_s *)dev;

  if (callback)
    {
      priv->callback = callback;
      priv->arg      = arg;
      up_enable_irq(priv->irq_event);
    }
  else
    {
      up_disable_irq(priv->irq_event);
      priv->callback = callback;
      priv->arg      = arg;
    }

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: rpmsg_virtio_bmp_init
 ****************************************************************************/

int rpmsg_virtio_bmp_init(FAR const char *cpuname, bool master,
                          FAR struct rpmsg_virtio_lite_rsc_s *rsc,
                          int irq_event, int irq_trigger,
                          cpu_set_t remote_cpu)
{
  FAR struct rpmsg_virtio_bmp_dev_s *dev;
  int ret;

  dev = kmm_zalloc(sizeof(struct rpmsg_virtio_bmp_dev_s));
  if (dev == NULL)
    {
      return -ENOMEM;
    }

  dev->master = master;
  dev->irq_trigger = irq_trigger;
  dev->irq_event = irq_event;
  dev->rpmsg.ops = &g_rpmsg_virtio_bmp_ops;
  dev->rsc = rsc;
  dev->cpuset = remote_cpu;
  strlcpy(dev->cpuname, cpuname, sizeof(dev->cpuname));

  ret = irq_attach(dev->irq_event, rpmsg_virtio_bmp_interrupt, dev);
  if (ret < 0)
    {
      rpmsgerr("irq attach interrupt failed, ret = %d\n", ret);
      kmm_free(dev);
      return ret;
    }

  ret = rpmsg_virtio_lite_initialize(&dev->rpmsg);
  if (ret < 0)
    {
      rpmsgerr("Rpmsg virtio lite intialize failed, ret = %d\n", ret);
      irq_detach(dev->irq_event);
      kmm_free(dev);
      return ret;
    }

  return ret;
}
