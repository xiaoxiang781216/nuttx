/****************************************************************************
 * drivers/rpmsg/rpmsg.c
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

#include <metal/sys.h>
#include <nuttx/kmalloc.h>
#include <nuttx/mutex.h>
#include <nuttx/rwsem.h>
#include <nuttx/semaphore.h>
#include <nuttx/rpmsg/rpmsg_note.h>

#include "rpmsg.h"
#include "rpmsg_ping.h"
#include "rpmsg_test.h"
#include "rpmsg_router.h"
#include "rpmsg_procfs.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct rpmsg_bind_s
{
  char              name[RPMSG_NAME_SIZE];
  uint32_t          dest;
  struct metal_list node;
};

struct rpmsg_cb_s
{
  FAR void          *priv;
  rpmsg_dev_cb_t    device_created;
  rpmsg_dev_cb_t    device_destroy;
  rpmsg_match_cb_t  ns_match;
  rpmsg_bind_cb_t   ns_bind;
  struct metal_list node;
};

struct rpmsg_ioctl_s
{
  FAR const char *cpuname;
  int             cmd;
  unsigned long   arg;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int rpmsg_dev_ioctl(FAR struct file *filep, int cmd,
                           unsigned long arg);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static METAL_DECLARE_LIST(g_rpmsg_cb);
static METAL_DECLARE_LIST(g_rpmsg);

static rw_semaphore_t g_rpmsg_lock = RWSEM_INITIALIZER;

static const struct file_operations g_rpmsg_dev_ops =
{
  NULL,             /* open */
  NULL,             /* close */
  NULL,             /* read */
  NULL,             /* write */
  NULL,             /* seek */
  rpmsg_dev_ioctl,  /* ioctl */
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static inline FAR struct rpmsg_s *
rpmsg_get_by_rdev(FAR struct rpmsg_device *rdev)
{
  if (!rdev)
    {
      return NULL;
    }

  return (FAR struct rpmsg_s *)((FAR char *)rdev - sizeof(struct rpmsg_s));
}

static int rpmsg_dev_ioctl_(FAR struct rpmsg_s *rpmsg, int cmd,
                            unsigned long arg)
{
  int ret = -ENOTTY;

  switch (cmd)
    {
      case RPMSGIOC_PANIC:
        if (rpmsg->ops->panic)
          {
            rpmsg->ops->panic(rpmsg);
            ret = OK;
          }

        break;
      case RPMSGIOC_DUMP:
        if (rpmsg->ops->dump)
          {
            rpmsg->ops->dump(rpmsg);
            ret = OK;
          }

        break;
#ifdef CONFIG_RPMSG_PING
      case RPMSGIOC_PING:
        ret = rpmsg_ping(&rpmsg->ping, (FAR const struct rpmsg_ping_s *)arg);
        break;
#endif
#ifdef CONFIG_RPMSG_TEST
      case RPMSGIOC_TEST:
        ret = rpmsg_test(&rpmsg->test, arg);
        break;
#endif
      default:
        if (rpmsg->ops->ioctl)
          {
            ret = rpmsg->ops->ioctl(rpmsg, cmd, arg);
          }

        break;
    }

  return ret;
}

static int rpmsg_dev_ioctl(FAR struct file *filep, int cmd,
                           unsigned long arg)
{
  FAR struct rpmsg_s *rpmsg =
    (FAR struct rpmsg_s *)filep->f_inode->i_private;

  return rpmsg_dev_ioctl_(rpmsg, cmd, arg);
}

#if CONFIG_RPMSG_DEFER_WORK_COUNT > 0
static void rpmsg_defer_worker(FAR void *arg)
{
  FAR struct rpmsg_s *rpmsg = arg;
  FAR struct rpmsg_device *rdev = rpmsg_get_rdev_by_rpmsg(rpmsg);
  FAR struct rpmsg_defer_node_s *defer_node;
  FAR struct metal_list *node;
  irqstate_t flags;
  int ret;

  flags = spin_lock_irqsave(&rpmsg->defer_lock);
  while ((node = metal_list_first(&rpmsg->defer_used)) != NULL)
    {
      metal_list_del(node);
      spin_unlock_irqrestore(&rpmsg->defer_lock, flags);

      /* Get one defer_node form defer list rpmsg->defer */

      defer_node = metal_container_of(node, struct rpmsg_defer_node_s,
                                      node);

      /* Process the defer handler */

      ret = defer_node->handler(defer_node->ept, defer_node->data,
                                defer_node->len, defer_node->src,
                                defer_node->priv);
      if (ret < 0)
        {
          rpmsgerr("Handle failed: ept: %p, data: %p, priv: %p, len: %zu,"
                   "src: 0x%" PRIx32 ", handler: %p\n",
                   defer_node->ept , defer_node->data, defer_node->priv,
                   defer_node->len, defer_node->src, defer_node->handler);
          PANIC();
        }

      rpmsg_release_rx_buffer(defer_node->ept, defer_node->data);

      /* Need decrease the endpoint reference count */

      metal_mutex_acquire(&rdev->lock);
      rpmsg_ept_decref(defer_node->ept);
      metal_mutex_release(&rdev->lock);

      /* Add the defer_node to defer_free list */

      flags = spin_lock_irqsave(&rpmsg->defer_lock);
      metal_list_add_tail(&rpmsg->defer_free, &defer_node->node);
    }

  spin_unlock_irqrestore(&rpmsg->defer_lock, flags);
}
#endif

static int rpmsg_ioctl_foreach_cb(FAR struct rpmsg_s *rpmsg, FAR void *arg)
{
  FAR struct rpmsg_ioctl_s *info = (FAR struct rpmsg_ioctl_s *)arg;

  if (!info->cpuname || !strcmp(rpmsg->cpuname, info->cpuname))
    {
      return rpmsg_dev_ioctl_(rpmsg, info->cmd, info->arg);
    }

  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void rpmsg_initialize(void)
{
  rpmsg_note_initialize();
  rpmsg_procfs_initialize();
}

int rpmsg_wait(FAR struct rpmsg_endpoint *ept, FAR sem_t *sem)
{
  FAR struct rpmsg_s *rpmsg;

  if (!ept || !sem)
    {
      return -EINVAL;
    }

  rpmsg = rpmsg_get_by_rdev(ept->rdev);
  if (!rpmsg || !rpmsg->ops->wait)
    {
      return nxsem_wait_uninterruptible(sem);
    }

  return rpmsg->ops->wait(rpmsg, sem);
}

int rpmsg_post(FAR struct rpmsg_endpoint *ept, FAR sem_t *sem)
{
  FAR struct rpmsg_s *rpmsg;

  if (!ept || !sem)
    {
      return -EINVAL;
    }

  rpmsg = rpmsg_get_by_rdev(ept->rdev);
  if (!rpmsg || !rpmsg->ops->post)
    {
      return nxsem_post(sem);
    }

  return rpmsg->ops->post(rpmsg, sem);
}

FAR const char *rpmsg_get_local_cpuname(FAR struct rpmsg_device *rdev)
{
  FAR struct rpmsg_s *rpmsg = rpmsg_get_by_rdev(rdev);

  if (rpmsg == NULL)
    {
      return NULL;
    }

  return rpmsg->local_cpuname[0] ? rpmsg->local_cpuname :
         CONFIG_RPMSG_LOCAL_CPUNAME;
}

FAR const char *rpmsg_get_cpuname(FAR struct rpmsg_device *rdev)
{
  FAR struct rpmsg_s *rpmsg = rpmsg_get_by_rdev(rdev);
  return rpmsg ? rpmsg->cpuname : NULL;
}

int rpmsg_get_signals(FAR struct rpmsg_device *rdev)
{
  FAR struct rpmsg_s *rpmsg = rpmsg_get_by_rdev(rdev);

  return atomic_read(&rpmsg->signals);
}

int rpmsg_defer_work(FAR struct rpmsg_endpoint *ept, FAR void *data,
                     size_t len, uint32_t src, FAR void *priv,
                     rpmsg_ept_cb handler)
{
  int ret;
#if CONFIG_RPMSG_DEFER_WORK_COUNT > 0
  FAR struct rpmsg_device *rdev = ept->rdev;
  FAR struct rpmsg_s *rpmsg = rpmsg_get_by_rdev(rdev);
  FAR struct rpmsg_defer_node_s *defer_node;
  FAR struct metal_list *node;
  irqstate_t flags;

  flags = spin_lock_irqsave(&rpmsg->defer_lock);
  node = metal_list_first(&rpmsg->defer_free);
  if (node == NULL)
    {
      rpmsgwarn("Defer list is full, drop the defer work\n");
      goto handle;
    }

  metal_list_del(node);
  spin_unlock_irqrestore(&rpmsg->defer_lock, flags);

  /* Need increase ept reference count and hold rx buffer */

  metal_mutex_acquire(&rdev->lock);
  rpmsg_ept_incref(ept);
  metal_mutex_release(&rdev->lock);

  defer_node = metal_container_of(node, struct rpmsg_defer_node_s, node);
  defer_node->ept = ept;
  defer_node->data = data;
  defer_node->len = len;
  defer_node->src =  src;
  defer_node->priv = priv;
  defer_node->handler = handler;

  /* Add to the defer_used list */

  flags = spin_lock_irqsave(&rpmsg->defer_lock);
  metal_list_add_tail(&rpmsg->defer_used, &defer_node->node);
  spin_unlock_irqrestore(&rpmsg->defer_lock, flags);

  /* Wake up the defer thread/workqueue to process the defer work */

  work_queue_wq(rpmsg->defer_wqueue, &rpmsg->defer_work,
                rpmsg_defer_worker, rpmsg, 0);
  return RPMSG_SUCCESS_BUFFER_RELEASED;

handle:
  spin_unlock_irqrestore(&rpmsg->defer_lock, flags);
#endif

  ret = handler(ept, data, len, src, priv);
  if (ret < 0)
    {
      rpmsgerr("Handle failed: ept: %p, data: %p, priv: %p, len: %zu,"
               "src: 0x%" PRIx32 ", handler: %p\n",
               ept, data, priv, len, src, handler);
      PANIC();
    }

  return ret;
}

int rpmsg_get_timestamp(FAR struct rpmsg_device *rdev, FAR const void *data,
                        FAR struct rpmsg_timestamp_s *ts)
{
  FAR struct rpmsg_s *rpmsg = rpmsg_get_by_rdev(rdev);

  if (!rpmsg || !data || !ts || !rpmsg->ops->get_timestamp)
    {
      return -EINVAL;
    }

  return rpmsg->ops->get_timestamp(rpmsg, data, ts);
}

int rpmsg_register_callback(FAR void *priv,
                            rpmsg_dev_cb_t device_created,
                            rpmsg_dev_cb_t device_destroy,
                            rpmsg_match_cb_t ns_match,
                            rpmsg_bind_cb_t ns_bind)
{
  FAR struct metal_list *node;
  FAR struct metal_list *bnode;
  FAR struct rpmsg_cb_s *cb;

  cb = kmm_zalloc(sizeof(struct rpmsg_cb_s));
  if (cb == NULL)
    {
      return -ENOMEM;
    }

  cb->priv           = priv;
  cb->device_created = device_created;
  cb->device_destroy = device_destroy;
  cb->ns_match       = ns_match;
  cb->ns_bind        = ns_bind;

  down_write(&g_rpmsg_lock);
  metal_list_add_tail(&g_rpmsg_cb, &cb->node);
  downgrade_write(&g_rpmsg_lock);
  metal_list_for_each(&g_rpmsg, node)
    {
      FAR struct rpmsg_s *rpmsg =
        metal_container_of(node, struct rpmsg_s, node);
      FAR struct rpmsg_device *rdev = rpmsg_get_rdev_by_rpmsg(rpmsg);

      if (!rpmsg->init)
        {
          continue;
        }

      if (device_created)
        {
          device_created(rdev, priv);
        }

      if (ns_bind == NULL)
        {
          continue;
        }

      DEBUGASSERT(ns_match != NULL);

again:
      nxrmutex_lock(&rpmsg->lock);
      metal_list_for_each(&rpmsg->bind, bnode)
        {
          FAR struct rpmsg_bind_s *bind =
            metal_container_of(bnode, struct rpmsg_bind_s, node);

          if (ns_match(rdev, priv, bind->name, bind->dest))
            {
              metal_list_del(bnode);
              nxrmutex_unlock(&rpmsg->lock);
              ns_bind(rdev, priv, bind->name, bind->dest);

              kmm_free(bind);
              goto again;
            }
        }

       nxrmutex_unlock(&rpmsg->lock);
    }

  up_read(&g_rpmsg_lock);

  return 0;
}

void rpmsg_unregister_callback(FAR void *priv,
                               rpmsg_dev_cb_t device_created,
                               rpmsg_dev_cb_t device_destroy,
                               rpmsg_match_cb_t ns_match,
                               rpmsg_bind_cb_t ns_bind)
{
  FAR struct metal_list *node;
  FAR struct metal_list *pnode;

  down_write(&g_rpmsg_lock);
  metal_list_for_each(&g_rpmsg_cb, node)
    {
      FAR struct rpmsg_cb_s *cb =
        metal_container_of(node, struct rpmsg_cb_s, node);

      if (cb->priv == priv &&
          cb->device_created == device_created &&
          cb->device_destroy == device_destroy &&
          cb->ns_match == ns_match &&
          cb->ns_bind == ns_bind)
        {
          metal_list_del(&cb->node);
          kmm_free(cb);
          break;
        }
    }

  downgrade_write(&g_rpmsg_lock);
  if (device_destroy)
    {
      metal_list_for_each(&g_rpmsg, pnode)
        {
          FAR struct rpmsg_s *rpmsg =
            metal_container_of(pnode, struct rpmsg_s, node);
          FAR struct rpmsg_device *rdev = rpmsg_get_rdev_by_rpmsg(rpmsg);

          if (rpmsg->init)
            {
              device_destroy(rdev, priv);
            }
        }
    }

  up_read(&g_rpmsg_lock);
}

void rpmsg_ns_bind(FAR struct rpmsg_device *rdev,
                   FAR const char *name, uint32_t dest)
{
  FAR struct rpmsg_s *rpmsg = rpmsg_get_by_rdev(rdev);
  FAR struct rpmsg_bind_s *bind;
  FAR struct metal_list *node;

  down_read(&g_rpmsg_lock);
  metal_list_for_each(&g_rpmsg_cb, node)
    {
      FAR struct rpmsg_cb_s *cb =
        metal_container_of(node, struct rpmsg_cb_s, node);

      if (cb->ns_match && cb->ns_match(rdev, cb->priv, name, dest))
        {
          rpmsg_bind_cb_t ns_bind = cb->ns_bind;
          FAR void *cb_priv = cb->priv;

          up_read(&g_rpmsg_lock);

          ns_bind(rdev, cb_priv, name, dest);
          return;
        }
    }

  bind = kmm_malloc(sizeof(struct rpmsg_bind_s));
  if (bind == NULL)
    {
      return;
    }

  bind->dest = dest;
  strlcpy(bind->name, name, RPMSG_NAME_SIZE);

  nxrmutex_lock(&rpmsg->lock);
  metal_list_add_tail(&rpmsg->bind, &bind->node);
  nxrmutex_unlock(&rpmsg->lock);

  up_read(&g_rpmsg_lock);
}

void rpmsg_ns_unbind(FAR struct rpmsg_device *rdev,
                     FAR const char *name, uint32_t dest)
{
  FAR struct rpmsg_s *rpmsg = rpmsg_get_by_rdev(rdev);
  FAR struct metal_list *node;

  nxrmutex_lock(&rpmsg->lock);
  metal_list_for_each(&rpmsg->bind, node)
    {
      FAR struct rpmsg_bind_s *bind =
        metal_container_of(node, struct rpmsg_bind_s, node);

      if (bind->dest == dest && !strncmp(bind->name, name, RPMSG_NAME_SIZE))
        {
          metal_list_del(node);
          kmm_free(bind);
          break;
        }
    }

  nxrmutex_unlock(&rpmsg->lock);
}

void rpmsg_device_created(FAR struct rpmsg_s *rpmsg)
{
  FAR struct rpmsg_device *rdev = rpmsg_get_rdev_by_rpmsg(rpmsg);
  FAR struct metal_list *node;
  FAR struct metal_list *tmp;

  down_write(&g_rpmsg_lock);
  metal_list_for_each_safe(&g_rpmsg_cb, tmp, node)
    {
      FAR struct rpmsg_cb_s *cb =
        metal_container_of(node, struct rpmsg_cb_s, node);

      if (cb->device_created)
        {
          cb->device_created(rdev, cb->priv);
        }
    }

  rpmsg->init = true;
  up_write(&g_rpmsg_lock);

#ifdef CONFIG_RPMSG_PING
  rpmsg_ping_init(rdev, &rpmsg->ping);
#endif
#ifdef CONFIG_RPMSG_TEST
  rpmsg_test_init(rdev, &rpmsg->test);
#endif
}

void rpmsg_device_destory(FAR struct rpmsg_s *rpmsg)
{
  FAR struct rpmsg_device *rdev = rpmsg_get_rdev_by_rpmsg(rpmsg);
  FAR struct metal_list *node;
  FAR struct metal_list *tmp;
  FAR struct rpmsg_endpoint *ept;

#ifdef CONFIG_RPMSG_PING
  rpmsg_ping_deinit(&rpmsg->ping);
#endif

#ifdef CONFIG_RPMSG_TEST
  rpmsg_test_deinit(&rpmsg->test);
#endif

  nxrmutex_lock(&rpmsg->lock);
  metal_list_for_each_safe(&rpmsg->bind, tmp, node)
    {
      FAR struct rpmsg_bind_s *bind =
        metal_container_of(node, struct rpmsg_bind_s, node);

      metal_list_del(node);
      kmm_free(bind);
    }

  nxrmutex_unlock(&rpmsg->lock);

  /* Broadcast device_destroy to all registers */

  down_write(&g_rpmsg_lock);
  rpmsg->init = false;
  downgrade_write(&g_rpmsg_lock);
  metal_list_for_each_safe(&g_rpmsg_cb, tmp, node)
    {
      FAR struct rpmsg_cb_s *cb =
        metal_container_of(node, struct rpmsg_cb_s, node);

      if (cb->device_destroy)
        {
          cb->device_destroy(rdev, cb->priv);
        }
    }

  up_read(&g_rpmsg_lock);

  /* Release all ept attached to current rpmsg device */

  metal_list_for_each_safe(&rdev->endpoints, tmp, node)
    {
      ept = metal_container_of(node, struct rpmsg_endpoint, node);
      if (ept->ns_unbind_cb)
        {
          ept->ns_unbind_cb(ept);
        }
      else
        {
          rpmsg_destroy_ept(ept);
        }
    }
}

int rpmsg_register(FAR const char *path, FAR struct rpmsg_s *rpmsg,
                   FAR const struct rpmsg_ops_s *ops)
{
  struct metal_init_params params = METAL_INIT_DEFAULTS;
#if CONFIG_RPMSG_DEFER_WORK_COUNT > 0
  FAR struct rpmsg_device *rdev = rpmsg_get_rdev_by_rpmsg(rpmsg);
  char name[32];
  size_t i;
#endif
  int ret;

  ret = metal_init(&params);
  if (ret < 0)
    {
      return ret;
    }

  ret = register_driver(path, &g_rpmsg_dev_ops, 0222, rpmsg);
  if (ret < 0)
    {
      metal_finish();
      return ret;
    }

#if CONFIG_RPMSG_DEFER_WORK_COUNT > 0
  spin_lock_init(&rpmsg->defer_lock);
  metal_list_init(&rpmsg->defer_free);
  metal_list_init(&rpmsg->defer_used);
  for (i = 0; i < nitems(rpmsg->defer_nodes); i++)
    {
      metal_list_add_tail(&rpmsg->defer_free, &rpmsg->defer_nodes[i].node);
    }

  snprintf(name, sizeof(name), "rpmsg_defer_worker_%s",
           rpmsg_get_cpuname(rdev));
  rpmsg->defer_wqueue = work_queue_create(name,
                                          CONFIG_RPMSG_DEFER_WORK_PRIORITY,
                                          NULL,
                                          CONFIG_RPMSG_DEFER_WORK_STACKSIZE,
                                          1);
  if (rpmsg->defer_wqueue == NULL)
    {
      rpmsgerr("rpmsg defer work queue create failed\n");
      unregister_driver(path);
      return -ENOMEM;
    }
#endif

  metal_list_init(&rpmsg->bind);
  nxrmutex_init(&rpmsg->lock);
  rpmsg->ops = ops;
  atomic_set(&rpmsg->signals, RPMSG_SIGNAL_RUNNING);

  /* Add priv to list */

  down_write(&g_rpmsg_lock);
  metal_list_add_tail(&g_rpmsg, &rpmsg->node);
  up_write(&g_rpmsg_lock);

  return ret;
}

void rpmsg_unregister(FAR const char *path, FAR struct rpmsg_s *rpmsg)
{
#if CONFIG_RPMSG_DEFER_WORK_COUNT > 0
  work_cancel_sync_wq(rpmsg->defer_wqueue, &rpmsg->defer_work);
  rpmsg_defer_worker(rpmsg);
#endif

  down_write(&g_rpmsg_lock);
  metal_list_del(&rpmsg->node);
  up_write(&g_rpmsg_lock);

  nxrmutex_destroy(&rpmsg->lock);
  unregister_driver(path);

  metal_finish();
}

void rpmsg_dump_epts(FAR struct rpmsg_device *rdev)
{
  FAR struct rpmsg_endpoint *ept;
  FAR struct metal_list *node;

  metal_log(METAL_LOG_EMERGENCY, "  rpmsg ept list:\n");
  metal_list_for_each(&rdev->endpoints, node)
    {
      ept = metal_container_of(node, struct rpmsg_endpoint, node);
      metal_log(METAL_LOG_EMERGENCY, "    ept %s %p: addr=%" PRIu32 " "
                "dest=%" PRIu32 " refcnt=%" PRIu32 " "
                "priority=%" PRIu8 " priv=%p\n",
                ept, ept->name, ept->addr, ept->dest_addr,
                ept->refcnt, ept->priority, ept->priv);
    }
}

int rpmsg_ioctl(FAR const char *cpuname, int cmd, unsigned long arg)
{
  struct rpmsg_ioctl_s info;

  info.cpuname = cpuname;
  info.cmd = cmd;
  info.arg = arg;

  return rpmsg_foreach(rpmsg_ioctl_foreach_cb, &info);
}

int rpmsg_foreach(rpmsg_foreach_t handler, FAR void *arg)
{
  FAR struct metal_list *node;
  bool needlock = !up_interrupt_context() && !sched_idletask();
  int ret = OK;

  if (needlock)
    {
      down_read(&g_rpmsg_lock);
    }

  metal_list_for_each(&g_rpmsg, node)
    {
      FAR struct rpmsg_s *rpmsg =
        metal_container_of(node, struct rpmsg_s, node);

      ret = handler(rpmsg, arg);
      if (ret < 0)
        {
          break;
        }
    }

  if (needlock)
    {
      up_read(&g_rpmsg_lock);
    }

  return ret;
}

int rpmsg_panic(FAR const char *cpuname)
{
  return rpmsg_ioctl(cpuname, RPMSGIOC_PANIC, 0);
}

void rpmsg_dump_all(void)
{
  rpmsg_ioctl(NULL, RPMSGIOC_DUMP, 0);
}

void rpmsg_modify_signals(FAR struct rpmsg_s *rpmsg,
                          int setflags, int clrflags)
{
  FAR struct rpmsg_device *rdev = rpmsg_get_rdev_by_rpmsg(rpmsg);
  FAR struct rpmsg_endpoint *ept;
  FAR struct metal_list *node;
  bool needlock;

  atomic_fetch_and_acquire(&rpmsg->signals, ~clrflags);
  atomic_fetch_or_acquire(&rpmsg->signals, setflags);

  /* Send signal to Router Hub */

  needlock = !up_interrupt_context() && !sched_idletask();
  if (needlock)
    {
      metal_mutex_acquire(&rdev->lock);
    }

  metal_list_for_each(&rdev->endpoints, node)
    {
      ept = metal_container_of(node, struct rpmsg_endpoint, node);
      if (!strncmp(ept->name, RPMSG_ROUTER_NAME,
                   RPMSG_ROUTER_NAME_LEN))
        {
          rpmsg_ept_incref(ept);
          ept->cb(ept, NULL, 0, RPMSG_ADDR_ANY, NULL);
          rpmsg_ept_decref(ept);
        }
    }

  if (needlock)
    {
      metal_mutex_release(&rdev->lock);
    }
}
