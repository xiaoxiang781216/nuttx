/****************************************************************************
 * drivers/hwtracing/hwtracing.c
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
#include <debug.h>
#include <stdbool.h>
#include <nuttx/kmalloc.h>
#include <nuttx/irq.h>
#include <nuttx/nuttx.h>

#include <nuttx/hwtracing/hwtracing.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* Used for build path */

struct hwtracing_node_s
{
  FAR struct hwtracing_dev_s *htdev;
  struct list_node link;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct list_node g_htdev_list = LIST_INITIAL_VALUE(g_htdev_list);
static spinlock_t g_htdev_list_lock = SP_UNLOCKED;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#ifdef CONFIG_PM

/****************************************************************************
 * Name: hwtracing_notify_pm
 ****************************************************************************/

static void hwtracing_notify_pm(struct pm_callback_s *cb, int domain,
                                enum pm_state_e pmstate)
{
  FAR struct hwtracing_dev_s *htdev =
    container_of(cb, struct hwtracing_dev_s, pmcb);
  enum pm_state_e oldstate;

  if (htdev->refcnt == 0 || domain != PM_IDLE_DOMAIN)
    {
      return;
    }

  oldstate = pm_querystate(PM_IDLE_DOMAIN);
  switch (oldstate)
    {
      case PM_NORMAL:
      case PM_IDLE:
      case PM_STANDBY:
        if (pmstate == PM_SLEEP)
          {
            clk_disable(htdev->clk);
          }
        break;

      case PM_SLEEP:
        if (pmstate == PM_NORMAL || pmstate == PM_IDLE ||
            pmstate == PM_STANDBY)
          {
            if (clk_enable(htdev->clk) <= 0)
              {
                hterr("clk enable failed when pm state change\n");
              }
          }
        break;

      default:
        break;
    }
}

#endif

#ifdef CONFIG_CLK

/****************************************************************************
 * Name: hwtracing_enable_clk
 ****************************************************************************/

static int hwtracing_enable_clk(FAR struct hwtracing_dev_s *htdev)
{
  int ret;

  if (htdev->clk == NULL)
    {
      return 0;
    }

  ret = clk_enable(htdev->clk);
  if (ret < 0)
    {
      hterr("%s clk enable failed\n", htdev->name);
      return ret;
    }

#ifdef CONFIG_PM
  if (htdev->pmcb.notify == NULL)
    {
      htdev->pmcb.notify = hwtracing_notify_pm;
    }

  ret = pm_register(&htdev->pmcb);
  if (ret < 0)
    {
      clk_disable(htdev->clk);
      hterr("%s register pm failed\n", htdev->name);
      return ret;
    }
#endif

  return ret;
}

/****************************************************************************
 * Name: hwtracing_disable_clk
 ****************************************************************************/

static void hwtracing_disable_clk(FAR struct hwtracing_dev_s *htdev)
{
  if (htdev->clk == NULL)
    {
      return;
    }

#ifdef CONFIG_PM
  pm_unregister(&htdev->pmcb);
#endif
  clk_disable(htdev->clk);
}

#else
#  define hwtracing_enable_clk(htdev) (0)
#  define hwtracing_disable_clk(htdev)
#endif

/****************************************************************************
 * Name: hwtracing_enable_sink
 ****************************************************************************/

static int hwtracing_enable_sink(FAR struct hwtracing_dev_s *htdev)
{
  int ret;

  if (htdev->ops->sink_ops->enable == NULL)
    {
      return -EINVAL;
    }

  if (htdev->refcnt++ != 0)
    {
      return 0;
    }

  ret = hwtracing_enable_clk(htdev);
  if (ret < 0)
    {
      htdev->refcnt--;
      return ret;
    }

  ret = htdev->ops->sink_ops->enable(htdev);
  if (ret >= 0)
    {
      return ret;
    }

  htdev->refcnt--;
  hwtracing_disable_clk(htdev);
  hterr("%s enable failed\n", htdev->name);
  return ret;
}

/****************************************************************************
 * Name: hwtracing_disable_sink
 ****************************************************************************/

static void hwtracing_disable_sink(FAR struct hwtracing_dev_s *htdev)
{
  if (htdev->ops->sink_ops->disable == NULL)
    {
      return;
    }

  if (--htdev->refcnt != 0)
    {
      return;
    }

  htdev->ops->sink_ops->disable(htdev);
  hwtracing_disable_clk(htdev);
}

/****************************************************************************
 * Name: hwtracing_find_link_inport
 ****************************************************************************/

static int hwtracing_find_link_inport(FAR struct hwtracing_dev_s *htdev,
                                      FAR struct hwtracing_dev_s *prev)
{
  FAR struct hwtracing_connect_s *conn;
  int i;

  for (i = 0; i < prev->outport_num; i++)
    {
      conn = &prev->outconns[i];
      if (conn->destdev == htdev)
        {
          return conn->destport;
        }
    }

  return -ENODEV;
}

/****************************************************************************
 * Name: hwtracing_find_link_outport
 ****************************************************************************/

static int hwtracing_find_link_outport(FAR struct hwtracing_dev_s *htdev,
                                       FAR struct hwtracing_dev_s *next)
{
  FAR struct hwtracing_connect_s *conn;
  int i;

  for (i = 0; i < htdev->outport_num; i++)
    {
      conn = &htdev->outconns[i];
      if (conn->destdev == next)
        {
          return conn->srcport;
        }
    }

  return -ENODEV;
}

/****************************************************************************
 * Name: hwtracing_enable_link
 ****************************************************************************/

static int hwtracing_enable_link(FAR struct hwtracing_dev_s *htdev,
                                 FAR struct hwtracing_dev_s *prev,
                                 FAR struct hwtracing_dev_s *next)
{
  int inport = 0;
  int outport = 0;
  int ret;

  if (htdev->ops->link_ops->enable == NULL)
    {
      return -EINVAL;
    }

  if (htdev->subtype.link_subtype == HWTRACING_DEV_SUBTYPE_LINK_MERG)
    {
      inport = hwtracing_find_link_inport(htdev, prev);
      if (inport < 0)
        {
          return inport;
        }
    }

  if (htdev->subtype.link_subtype == HWTRACING_DEV_SUBTYPE_LINK_SPLIT)
    {
      outport = hwtracing_find_link_outport(htdev, next);
      if (outport < 0)
        {
          return outport;
        }
    }

  if (htdev->refcnt++ == 0)
    {
      ret = hwtracing_enable_clk(htdev);
      if (ret < 0)
        {
          htdev->refcnt--;
          return ret;
        }
    }

  ret = htdev->ops->link_ops->enable(htdev, inport, outport);
  if (ret < 0)
    {
      if (--htdev->refcnt == 0)
        {
          hwtracing_disable_clk(htdev);
        }

      return ret;
    }

  return ret;
}

/****************************************************************************
 * Name: hwtracing_disable_link
 ****************************************************************************/

static void hwtracing_disable_link(FAR struct hwtracing_dev_s *htdev,
                                   FAR struct hwtracing_dev_s *prev,
                                   FAR struct hwtracing_dev_s *next)
{
  int inport;
  int outport;

  if (htdev->ops->sink_ops->disable == NULL)
    {
      return;
    }

  inport = hwtracing_find_link_inport(htdev, prev);
  outport = hwtracing_find_link_outport(htdev, next);
  htdev->ops->link_ops->disable(htdev, inport, outport);

  if (--htdev->refcnt == 0)
    {
      hwtracing_disable_clk(htdev);
    }
}

/****************************************************************************
 * Name: hwtracing_enable_source
 ****************************************************************************/

static int hwtracing_enable_source(FAR struct hwtracing_dev_s *htdev)
{
  int ret;

  if (htdev->ops->source_ops->enable == NULL)
    {
      return -EINVAL;
    }

  if (htdev->refcnt++ != 0)
    {
      return 0;
    }

  ret = hwtracing_enable_clk(htdev);
  if (ret < 0)
    {
      htdev->refcnt--;
      return ret;
    }

  ret = htdev->ops->source_ops->enable(htdev);
  if (ret >= 0)
    {
      return ret;
    }

  htdev->refcnt--;
  hwtracing_disable_clk(htdev);
  hterr("%s enable failed\n", htdev->name);
  return ret;
}

/****************************************************************************
 * Name: hwtracing_disable_source
 ****************************************************************************/

static void hwtracing_disable_source(FAR struct hwtracing_dev_s *htdev)
{
  if (htdev->ops->source_ops->disable == NULL)
    {
      return;
    }

  if (--htdev->refcnt != 0)
    {
      return;
    }

  htdev->ops->source_ops->disable(htdev);
  hwtracing_disable_clk(htdev);
}

/****************************************************************************
 * Name: hwtracing_validate_source
 *
 * Description:
 *   Indicate if this hwtracing device is a valid source device.
 *
 ****************************************************************************/

static int hwtracing_validate_source(FAR struct hwtracing_dev_s *htdev)
{
  uint8_t type = htdev->type;
  uint8_t subtype = htdev->subtype.source_subtype;

  if (type != HWTRACING_DEV_TYPE_SOURCE)
    {
      hterr("not a source hwtracing device\n");
      return -EINVAL;
    }

  if (subtype != HWTRACING_DEV_SUBTYPE_SOURCE_PROC &&
      subtype != HWTRACING_DEV_SUBTYPE_SOURCE_SOFTWARE)
    {
      hterr("not a supported subtype of source device\n");
      return -EINVAL;
    }

  return 0;
}

/****************************************************************************
 * Name: hwtracing_build_path
 *
 * Description:
 *   Build path from srcdev to destdev.
 *
 * Input Parameters:
 *   srcdev  - Pointer to the source device.
 *   destdev - Pointer to the destination device.
 *   path    - Pointer to the path which will save all the hwtracing devices
 *             through source device to destination device.
 *
 * Returned Value:
 *   Zero on success; a negative value on failure.
 *
 ****************************************************************************/

static int hwtracing_build_path(FAR struct hwtracing_dev_s *srcdev,
                                FAR struct hwtracing_dev_s *destdev,
                                FAR struct list_node *path)
{
  FAR struct hwtracing_node_s *node;
  bool found = false;
  int i;

  if (srcdev == destdev)
    {
      goto out;
    }

  for (i = 0; i < srcdev->outport_num; i++)
    {
      struct hwtracing_dev_s *htdev = srcdev->outconns[i].destdev;
      if (htdev && hwtracing_build_path(htdev, destdev, path) == 0)
        {
          found = true;
          break;
        }
    }

  if (!found)
    {
      return -ENODEV;
    }

out:
  node = kmm_malloc(sizeof(struct hwtracing_node_s));
  if (node == NULL)
    {
      return -ENOMEM;
    }

  node->htdev = srcdev;
  list_add_head(path, &node->link);
  return 0;
}

/****************************************************************************
 * Name: hwtracing_release_path
 *
 * Description:
 *   Release memory malloced through the path.
 *
 ****************************************************************************/

static void hwtracing_release_path(FAR struct list_node *path)
{
  FAR struct hwtracing_node_s *node;
  FAR struct hwtracing_node_s *next;

  list_for_every_entry_safe(path, node, next, struct hwtracing_node_s, link)
    {
      list_delete(&node->link);
      kmm_free(node);
    }
}

/****************************************************************************
 * Name: hwtracing_disable_dev
 ****************************************************************************/

static void hwtracing_disable_dev(FAR struct hwtracing_node_s *node)
{
  switch (node->htdev->type)
    {
      case HWTRACING_DEV_TYPE_SINK:
        hwtracing_disable_sink(node->htdev);
        break;

      case HWTRACING_DEV_TYPE_LINK:
        {
          FAR struct hwtracing_node_s *prev =
            list_prev_entry(node, struct hwtracing_node_s, link);
          FAR struct hwtracing_node_s *next =
            list_next_entry(node, struct hwtracing_node_s, link);
          hwtracing_disable_link(node->htdev, prev->htdev, next->htdev);
        }
        break;

      /* We skip the first node in the path assuming that it is the source，
       * and it will be disabled in hwtracing_disable. So we don't expect a
       * source device in the middle of a path.
       */

      default:
        DEBUGASSERT(0);
        break;
    }
}

/****************************************************************************
 * Name: hwtracing_disable_path_from
 *
 * Description:
 *   Disable hwtracing devices from specific node.
 *
 * Input Parameters:
 *   path  - Head of the path.
 *   node  - Start position to search, it begins from next of this node to
 *           disable.
 *
 ****************************************************************************/

static void hwtracing_disable_path_from(FAR struct list_node *path,
                                        FAR struct hwtracing_node_s *node)
{
  list_for_every_entry_continue(node, path, struct hwtracing_node_s, link)
    {
      hwtracing_disable_dev(node);
    }
}

/****************************************************************************
 * Name: hwtracing_disable_path
 *
 * Description:
 *   Disable all the hwtracing devices through the path except source device.
 *   Source device will be disabled by hwtracing_disable or perf end
 *   function.
 *
 ****************************************************************************/

static void hwtracing_disable_path(FAR struct list_node *path)
{
  hwtracing_disable_path_from(path,
    container_of(path->next, struct hwtracing_node_s, link));
}

/****************************************************************************
 * Name: hwtracing_init_path
 *
 * Description:
 *   Enable all hwtracing devices through the path in reverse order.
 *
 * Input Parameters:
 *   path  - path from source device to sink device.
 *
 * Returned Value:
 *   Zero on success; a negative value on failure.
 *
 ****************************************************************************/

static int hwtracing_init_path(FAR struct list_node *path)
{
  FAR struct hwtracing_node_s *node;
  int ret = 0;

  list_for_every_entry(path, node, struct hwtracing_node_s, link)
    {
      switch (node->htdev->type)
        {
          /* Sink device is the first device to be enable. No need to disable
           * other hwtracing device in the path if it enabled failed.
           */

          case HWTRACING_DEV_TYPE_SINK:
            if (node->htdev->ops->sink_ops->init != NULL)
              {
                ret = node->htdev->ops->sink_ops->init(node->htdev);
                if (ret < 0)
                  {
                    hterr("enalbe sink: %s failed ret: %d\n",
                    node->htdev->name, ret);
                    return ret;
                  }
              }
              break;
          case HWTRACING_DEV_TYPE_LINK:
            if (node->htdev->ops->link_ops->init != NULL)
            {
              ret = node->htdev->ops->link_ops->init(node->htdev);
              if (ret < 0)
                {
                  hterr("enalbe link: %s failed ret: %d\n",
                        node->htdev->name, ret);
                  goto err;
                }
            }
            break;
          /* Source device will be enabled in hwtracing_enable or
           * perf start function.
           */

          case HWTRACING_DEV_TYPE_SOURCE:
            if (node->htdev->ops->source_ops->init != NULL)
              {
                ret = node->htdev->ops->source_ops->init(node->htdev);
                if (ret < 0)
                  {
                    hterr("enalbe link: %s failed ret: %d\n",
                    node->htdev->name, ret);
                    goto err;
                  }
              }
            break;
          default:
            hterr("invalid hwtracing device type through the path\n");
            DEBUGASSERT(0);
            goto err;
      }
    }

  return ret;

err:
  hwtracing_disable_path_from(path, node);
  return ret;
}

/****************************************************************************
 * Name: hwtracing_enable_path
 *
 * Description:
 *   Enable all hwtracing devices through the path in reverse order.
 *
 * Input Parameters:
 *   path  - path from source device to sink device.
 *
 * Returned Value:
 *   Zero on success; a negative value on failure.
 *
 ****************************************************************************/

static int hwtracing_enable_path(FAR struct list_node *path)
{
  FAR struct hwtracing_node_s *node;
  int ret = 0;

  list_for_every_entry(path, node, struct hwtracing_node_s, link)
    {
      switch (node->htdev->type)
        {
          /* Sink device is the first device to be enable. No need to disable
           * other hwtracing device in the path if it enabled failed.
           */

          case HWTRACING_DEV_TYPE_SINK:
            ret = hwtracing_enable_sink(node->htdev);
            if (ret < 0)
              {
                hterr("enalbe sink: %s failed ret: %d\n",
                      node->htdev->name, ret);
                return ret;
              }
            break;

          case HWTRACING_DEV_TYPE_LINK:
            {
              FAR struct hwtracing_node_s *prev =
                list_prev_entry(node, struct hwtracing_node_s, link);
              FAR struct hwtracing_node_s *next =
                list_next_entry(node, struct hwtracing_node_s, link);
              ret = hwtracing_enable_link(node->htdev, prev->htdev,
                                          next->htdev);
              if (ret < 0)
                {
                  hterr("enalbe link: %s failed ret: %d\n",
                        node->htdev->name, ret);
                  goto err;
                }
            }
            break;

          /* Source device will be enabled in hwtracing_enable or
           * perf start function.
           */

          case HWTRACING_DEV_TYPE_SOURCE:
            break;

          default:
            hterr("invalid hwtracing device type through the path\n");
            DEBUGASSERT(0);
            goto err;
      }
    }

  return ret;

err:
  hwtracing_disable_path_from(path, node);
  return ret;
}

/****************************************************************************
 * Name: hwtracing_find_dev
 ****************************************************************************/

FAR struct hwtracing_dev_s *hwtracing_find_dev(FAR const char *name)
{
  FAR struct hwtracing_dev_s *tempdev;
  irqstate_t flags;

  flags = spin_lock_irqsave(&g_htdev_list_lock);
  list_for_every_entry(&g_htdev_list, tempdev, struct hwtracing_dev_s, node)
    {
      if (strcmp(tempdev->name, name) == 0)
        {
          spin_unlock_irqrestore(&g_htdev_list_lock, flags);
          return tempdev;
        }
    }

  spin_unlock_irqrestore(&g_htdev_list_lock, flags);
  return NULL;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: hwtracing_register
 *
 * Description:
 *   Register a hwtracing device to the hwtracing bus.
 *
 * Input Parameters:
 *   htdev  - Pointer to the hwtracing device that needs to be registered.
 *   desc   - Pointer to the attribute description of this hwtracing device.
 *
 * Returned Value:
 *   Zero on success; a negative value on failure.
 *
 ****************************************************************************/

int hwtracing_register(FAR struct hwtracing_dev_s *htdev,
                       FAR const struct hwtracing_desc_s *desc)
{
  FAR struct hwtracing_dev_s *tempdev;
  irqstate_t flags;
  int i;

  if (hwtracing_find_dev(desc->name) != NULL)
    {
      hterr("device has been registered!\n");
      return -EEXIST;
    }

  htdev->name = desc->name;
  htdev->addr = desc->addr;
  htdev->type = desc->type;
  htdev->subtype = desc->subtype;
  htdev->outport_num = desc->outport_num;
  list_initialize(&htdev->path);
  spin_lock_init(&htdev->lock);

#ifdef CONFIG_CLK
  if (desc->clkname != NULL)
    {
      htdev->clk = clk_get(desc->clkname);
      if (htdev->clk == NULL)
        {
          hterr("get device clk failed\n");
          return -ENODEV;
        }
    }
#endif

  if (htdev->outport_num > 0)
    {
      htdev->outconns =
        kmm_zalloc(sizeof(struct hwtracing_connect_s) * desc->outport_num);
      if (htdev->outconns == NULL)
        {
          return -ENOMEM;
        }

      for (i = 0; i < htdev->outport_num; i++)
        {
          FAR struct hwtracing_connect_s *conn = &htdev->outconns[i];
          FAR const struct hwtracing_portdesc_s *portdesc =
            &desc->outports[i];

          conn->srcport = i;
          conn->destport = portdesc->port;
          conn->destname = portdesc->remote;
          conn->srcdev = htdev;
        }
    }

  flags = spin_lock_irqsave(&g_htdev_list_lock);
  list_for_every_entry(&g_htdev_list, tempdev, struct hwtracing_dev_s, node)
    {
      for (i = 0; i < tempdev->outport_num; i++)
        {
          FAR struct hwtracing_connect_s *conn = &tempdev->outconns[i];
          if (strcmp(conn->destname, htdev->name) == 0)
            {
              conn->destdev = htdev;
            }
        }

      for (i = 0; i < htdev->outport_num; i++)
        {
          FAR struct hwtracing_connect_s *conn = &htdev->outconns[i];
          if (strcmp(conn->destname, tempdev->name) == 0)
            {
              conn->destdev = tempdev;
            }
        }
    }

  list_add_tail(&g_htdev_list, &htdev->node);
  spin_unlock_irqrestore(&g_htdev_list_lock, flags);

  return 0;
}

/****************************************************************************
 * Name: hwtracing_unregister
 *
 * Description:
 *   Unregister a hwtracing device from hwtracing bus.
 *
 * Input Parameters:
 *   htdev  - Pointer to the hwtracing device that needs to be unregistered.
 *
 ****************************************************************************/

void hwtracing_unregister(FAR struct hwtracing_dev_s *htdev)
{
  FAR struct hwtracing_dev_s *tempdev;
  irqstate_t flags;
  int i;

  flags = spin_lock_irqsave(&g_htdev_list_lock);
  list_for_every_entry(&g_htdev_list, tempdev, struct hwtracing_dev_s, node)
    {
      if (htdev == tempdev)
        {
          continue;
        }

      for (i = 0; i < tempdev->outport_num; i++)
        {
          FAR struct hwtracing_connect_s *conn = &tempdev->outconns[i];
          if (conn->destdev == htdev)
            {
              conn->destdev = NULL;
            }
        }
    }

  if (htdev->refcnt > 0)
    {
      switch (htdev->type)
        {
          case HWTRACING_DEV_TYPE_SINK:
            if (htdev->ops->sink_ops->disable != NULL)
              {
                htdev->ops->sink_ops->disable(htdev);
              }
            break;

          case HWTRACING_DEV_TYPE_SOURCE:
            if (htdev->ops->source_ops->disable != NULL)
              {
                htdev->ops->source_ops->disable(htdev);
              }
            break;

          /* Link devices may have multiple inport or outport, it can
           * not be distinguished here which one of them has been enabled.
           * so disable inport/outports in its own unregister function.
           */

          default:
            break;
        }

      hwtracing_disable_clk(htdev);
    }

  list_delete(&htdev->node);
  spin_unlock_irqrestore(&g_htdev_list_lock, flags);

  if (htdev->outport_num > 0)
    {
      kmm_free(htdev->outconns);
      htdev->outconns = NULL;
    }
}

/****************************************************************************
 * Name: hwtracing_enable
 *
 * Description:
 *   Enable trace start from srcdev to destdev.
 *
 * Input Parameters:
 *   srcdev  - Source device that generates trace data.
 *   destdev - Sink device that finally accepts the trace data.
 *
 * Returned Value:
 *   Zero on success; a negative value on failure.
 *
 ****************************************************************************/

int hwtracing_enable(FAR struct hwtracing_dev_s *srcdev,
                     FAR struct hwtracing_dev_s *destdev)
{
  FAR struct hwtracing_node_s *node;
  irqstate_t flags;
  int ret;

  ret = hwtracing_validate_source(srcdev);
  if (ret < 0)
    {
      return ret;
    }

  flags = spin_lock_irqsave(&g_htdev_list_lock);

  if (list_is_empty(&srcdev->path))
    {
      ret = hwtracing_build_path(srcdev, destdev, &srcdev->path);
      if (ret < 0)
        {
          hterr("build path failed from %s ret: %d\n", srcdev->name, ret);
          goto err_path;
        }
    }

  ret = hwtracing_init_path(&srcdev->path);
  if (ret < 0)
    {
      hterr("init path failed from %s ret: %d\n", srcdev->name, ret);
      goto err_path;
    }

  ret = hwtracing_enable_source(srcdev);
  if (ret < 0)
    {
      hterr("enable source failed %s ret: %d\n", srcdev->name, ret);
      goto err_source;
    }

  ret = hwtracing_enable_path(&srcdev->path);
  if (ret < 0)
    {
      hterr("enable path failed from %s ret: %d\n", srcdev->name, ret);
      goto err_path;
    }

  htinfo("trace enabled success while devices are:");
  list_for_every_entry(&srcdev->path, node, struct hwtracing_node_s, link)
    {
      htinfo("-> %s", node->htdev->name);
    }

out:
  spin_unlock_irqrestore(&g_htdev_list_lock, flags);
  return ret;

err_source:
  hwtracing_disable_path(&srcdev->path);

err_path:
  hwtracing_release_path(&srcdev->path);
  goto out;
}

/****************************************************************************
 * Name: hwtracing_disable
 *
 * Description:
 *   Disable the trace start from srcdev to destdev.
 *
 * Input Parameters:
 *   srcdev  - Source device that generates trace data.
 *
 ****************************************************************************/

void hwtracing_disable(FAR struct hwtracing_dev_s *srcdev)
{
  irqstate_t flags;

  flags = spin_lock_irqsave(&g_htdev_list_lock);

  hwtracing_disable_source(srcdev);
  hwtracing_disable_path(&srcdev->path);
  hwtracing_release_path(&srcdev->path);

  spin_unlock_irqrestore(&g_htdev_list_lock, flags);
}

/****************************************************************************
 * Name: hwtracing_timeout
 *
 * Description:
 *   Loop until a bitmask of register has changed to a specific value.
 *
 * Input Parameters:
 *   addr    - Base addr of the hwtracing device.
 *   off     - Register offset of the hwtracing device.
 *   bitmask - Bitmask to be checked.
 *   val     - Value to be matched.
 *
 * Returned Value:
 *   Zero on success; a negative value on failure.
 *
 ****************************************************************************/

int hwtracing_timeout(uint32_t val, uint32_t mask, uintptr_t addr)
{
  int i;

  for (i = CONFIG_HWTRACING_TIMEOUT; i > 0; i--)
    {
      uint32_t value = hwtracing_get32(addr);
      if ((value & mask) == val)
        {
          return 0;
        }

      up_udelay(1);
    }

  return -EAGAIN;
}
