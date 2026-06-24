/****************************************************************************
 * drivers/fault/fault_dev.c
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

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <debug.h>

#include <nuttx/fs/fs.h>
#include <nuttx/fs/ioctl.h>
#include <nuttx/crc16.h>
#include <nuttx/kmalloc.h>
#include <nuttx/list.h>
#include <nuttx/spinlock.h>

#include <nuttx/fault_inject.h>

/****************************************************************************
 * External References (fault_core.c)
 ****************************************************************************/

typedef void (*fault_inject_iter_t)(FAR struct fault_point_s *fp,
                                    FAR void *priv);

int      fault_inject_validate_action(FAR const struct fault_point_s *fp,
                                      FAR const struct fault_config_s *cfg);
int      fault_inject_active_trigger(FAR struct fault_point_s *fp);
void     fault_inject_for_each(fault_inject_iter_t cb, FAR void *priv);
uint32_t fault_inject_point_id(FAR const struct fault_point_s *fp);
void     fault_inject_link_reaction(uint32_t seq, uint32_t reaction_event);

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* Attached to every i_private pointer.  A NULL fp marks the singleton
 * control node; a non-NULL fp marks a regular per-point node.
 */

struct fault_dev_priv_s
{
  FAR struct fault_point_s *fp;     /* NULL == control, non-NULL == point */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static ssize_t fault_dev_read(FAR struct file *filep, FAR char *buffer,
                              size_t buflen);
static int     fault_dev_ioctl(FAR struct file *filep, int cmd,
                               unsigned long arg);
static int     fault_dev_poll(FAR struct file *filep,
                              FAR struct pollfd *fds, bool setup);
static void    fault_dev_point_reset(FAR struct fault_point_s *fp,
                                     FAR void *priv);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct file_operations g_fault_dev_fops =
{
  NULL,                                  /* open  */
  NULL,                                  /* close */
  fault_dev_read,
  NULL,                                  /* write */
  NULL,                                  /* seek  */
  fault_dev_ioctl,
  NULL,                                  /* truncate */
  fault_dev_poll
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: fault_dev_path
 *
 * Description:
 *   Format the on-device path used to register a fault point.  Format:
 *   /dev/fault/<group>/<leaf>, where <leaf> is whatever follows
 *   the first '.' separator inside the registered name.
 *
 ****************************************************************************/

static void fault_dev_path(FAR const struct fault_point_meta_s *meta,
                           FAR char *out, size_t outlen)
{
  FAR const char *dot;

  dot = strchr(meta->name, '.');
  if (dot == NULL || *(dot + 1) == '\0')
    {
      snprintf(out, outlen, "%s/%s",
               FAULT_INJECT_DEV_DIR, meta->name);
    }
  else
    {
      snprintf(out, outlen, "%s/%s/%s",
               FAULT_INJECT_DEV_DIR, meta->group, dot + 1);
    }
}

/****************************************************************************
 * Name: fault_dev_read
 *
 * Description:
 *   Minimal status read.  Returns a single line that summarises the
 *   bound point or, for the control node, a one-shot acknowledgement.
 *   Designed for `cat /dev/fault/...` ergonomics, not for the
 *   audit log (which goes through sched_note).
 *
 ****************************************************************************/

static ssize_t fault_dev_read(FAR struct file *filep, FAR char *buffer,
                              size_t buflen)
{
  FAR struct inode             *inode = filep->f_inode;
  FAR struct fault_dev_priv_s  *priv  = inode->i_private;
  char line[96];
  int  len;

  if (filep->f_pos > 0)
    {
      return 0;
    }

  if (priv->fp == NULL)
    {
      len = snprintf(line, sizeof(line), "fault_inject control\n");
    }
  else
    {
      FAR struct fault_point_s *fp = priv->fp;

      len = snprintf(line, sizeof(line),
                     "%s enabled=%d trig=%d action=%d call=%llu inj=%llu\n",
                     fp->meta != NULL ? fp->meta->name : "?",
                     fp->enabled,
                     fp->config.trig_mode,
                     fp->config.action,
                     fp->stats.call_count,
                     fp->stats.inject_count);
    }

  if (len > buflen)
    {
      len = buflen;
    }

  memcpy(buffer, line, len);
  filep->f_pos += len;
  return len;
}

/****************************************************************************
 * Name: fault_dev_ioctl_point
 *
 * Description:
 *   ioctl handler for per-point devices.
 *
 ****************************************************************************/

static int fault_dev_ioctl_point(FAR struct fault_point_s *fp, int cmd,
                                 unsigned long arg)
{
  irqstate_t flags;
  int        ret = OK;

  switch (cmd)
    {
      case FAULTIOC_GET_CONFIG:
        {
          FAR struct fault_config_s *out =
            (FAR struct fault_config_s *)(uintptr_t)arg;

          if (out == NULL)
            {
              return -EINVAL;
            }

          flags = spin_lock_irqsave(&fp->lock);
          *out = fp->config;
          spin_unlock_irqrestore(&fp->lock, flags);
          break;
        }

      case FAULTIOC_SET_CONFIG:
        {
          FAR const struct fault_config_s *in =
            (FAR const struct fault_config_s *)(uintptr_t)arg;

          if (in == NULL)
            {
              return -EINVAL;
            }

          ret = fault_inject_validate_action(fp, in);
          if (ret < 0)
            {
              return ret;
            }

          flags = spin_lock_irqsave(&fp->lock);
          fp->config = *in;
          fp->magic  = FAULT_POINT_MAGIC;
          fp->crc    = crc16((FAR const uint8_t *)&fp->config,
                             sizeof(fp->config));
          spin_unlock_irqrestore(&fp->lock, flags);
          break;
        }

      case FAULTIOC_ENABLE:
        fp->enabled = (arg != 0) ? 1 : 0;
        break;

      case FAULTIOC_TRIGGER:
        ret = fault_inject_active_trigger(fp);
        break;

      case FAULTIOC_GET_STATS:
        {
          FAR struct fault_stats_s *out =
            (FAR struct fault_stats_s *)(uintptr_t)arg;

          if (out == NULL)
            {
              return -EINVAL;
            }

          flags = spin_lock_irqsave(&fp->lock);
          *out = fp->stats;
          spin_unlock_irqrestore(&fp->lock, flags);
          break;
        }

      case FAULTIOC_CLEAR_STATS:
        flags = spin_lock_irqsave(&fp->lock);
        memset(&fp->stats, 0, sizeof(fp->stats));
        spin_unlock_irqrestore(&fp->lock, flags);
        break;

      case FAULTIOC_RESET:
        flags = spin_lock_irqsave(&fp->lock);
        memset(&fp->config, 0, sizeof(fp->config));
        fp->enabled = 0;
        fp->magic   = 0;
        fp->crc     = 0;
        spin_unlock_irqrestore(&fp->lock, flags);
        break;

      case FAULTIOC_GET_META:
        {
          FAR struct fault_meta_query_s *q =
            (FAR struct fault_meta_query_s *)(uintptr_t)arg;

          if (q == NULL || fp->meta == NULL)
            {
              return -EINVAL;
            }

          q->meta = *fp->meta;
          strlcpy(q->name_storage,
                  fp->meta->name != NULL ? fp->meta->name : "",
                  sizeof(q->name_storage));
          strlcpy(q->group_storage,
                  fp->meta->group != NULL ? fp->meta->group : "",
                  sizeof(q->group_storage));
          strlcpy(q->file_storage,
                  fp->meta->file != NULL ? fp->meta->file : "",
                  sizeof(q->file_storage));
          break;
        }

      default:
        ret = -ENOTTY;
        break;
    }

  return ret;
}

/****************************************************************************
 * Name: fault_dev_list_cb
 *
 * Description:
 *   Iterator callback used by FAULTIOC_LIST_ALL to populate the caller's
 *   variable-length output buffer.
 *
 ****************************************************************************/

struct fault_list_ctx_s
{
  FAR struct fault_array_s *out;
  uint32_t                 written;
};

static void fault_dev_list_cb(FAR struct fault_point_s *fp, FAR void *priv)
{
  FAR struct fault_list_ctx_s *ctx = priv;

  if (fp->meta == NULL)
    {
      return;
    }

  if (ctx->written < ctx->out->count)
    {
      FAR struct fault_entry_s *e =
        &ctx->out->entries[ctx->written];

      strlcpy(e->name,
              fp->meta->name != NULL ? fp->meta->name : "",
              sizeof(e->name));
      strlcpy(e->group,
              fp->meta->group != NULL ? fp->meta->group : "",
              sizeof(e->group));
      e->supported = fp->meta->supported;
      e->enabled   = fp->enabled;
      e->trig_mode = fp->config.trig_mode;
      e->action    = fp->config.action;
      e->reserved  = 0;
      ctx->written++;
    }

  ctx->out->total++;
}

/****************************************************************************
 * Name: fault_dev_ioctl_control
 *
 * Description:
 *   ioctl handler for the singleton /dev/fault/control node.
 *
 ****************************************************************************/

static int fault_dev_ioctl_control(int cmd, unsigned long arg)
{
  switch (cmd)
    {
      case FAULTIOC_LIST_ALL:
        {
          FAR struct fault_array_s *out =
            (FAR struct fault_array_s *)(uintptr_t)arg;
          struct fault_list_ctx_s ctx;

          if (out == NULL)
            {
              return -EINVAL;
            }

          out->total      = 0;
          ctx.out         = out;
          ctx.written     = 0;
          fault_inject_for_each(fault_dev_list_cb, &ctx);
          return OK;
        }

      case FAULTIOC_CLEAR_ALL:

        /* Iterate and reset each point. */

        fault_inject_for_each(fault_dev_point_reset, NULL);
        return OK;

      case FAULTIOC_AUDIT_LINK:
        {
          uint32_t seq = (uint32_t)arg;

          fault_inject_link_reaction(seq, 0);
          return OK;
        }

      default:
        return -ENOTTY;
    }
}

/****************************************************************************
 * Name: fault_dev_point_reset
 *
 * Description:
 *   Iterator callback used by FAULTIOC_CLEAR_ALL to wipe every registered
 *   point's config + stats.
 *
 ****************************************************************************/

static void fault_dev_point_reset(FAR struct fault_point_s *fp,
                                  FAR void *priv)
{
  irqstate_t flags;

  UNUSED(priv);

  flags = spin_lock_irqsave(&fp->lock);
  memset(&fp->config, 0, sizeof(fp->config));
  memset(&fp->stats, 0, sizeof(fp->stats));
  fp->enabled = 0;
  fp->magic   = 0;
  fp->crc     = 0;
  spin_unlock_irqrestore(&fp->lock, flags);
}

/****************************************************************************
 * Name: fault_dev_ioctl
 ****************************************************************************/

static int fault_dev_ioctl(FAR struct file *filep, int cmd,
                           unsigned long arg)
{
  FAR struct inode            *inode = filep->f_inode;
  FAR struct fault_dev_priv_s *priv  = inode->i_private;

  if (priv->fp == NULL)
    {
      return fault_dev_ioctl_control(cmd, arg);
    }

  return fault_dev_ioctl_point(priv->fp, cmd, arg);
}

/****************************************************************************
 * Name: fault_dev_poll
 *
 * Description:
 *   Anchor a pending POLLIN waiter on a per-point node so user code can
 *   block until the next injection trip.  Control-node poll is rejected.
 *
 ****************************************************************************/

static int fault_dev_poll(FAR struct file *filep,
                          FAR struct pollfd *fds, bool setup)
{
  FAR struct inode            *inode = filep->f_inode;
  FAR struct fault_dev_priv_s *priv  = inode->i_private;
  FAR struct fault_point_s    *fp;
  irqstate_t flags;
  int        ret = OK;

  if (priv->fp == NULL)
    {
      return -ENOSYS;
    }

  fp = priv->fp;
  flags = spin_lock_irqsave(&fp->lock);

  if (setup)
    {
      if (fp->fds != NULL)
        {
          ret = -EBUSY;
        }
      else
        {
          fp->fds   = fds;
          fds->priv = fp;
        }
    }
  else if (fds->priv == fp)
    {
      fp->fds   = NULL;
      fds->priv = NULL;
    }

  spin_unlock_irqrestore(&fp->lock, flags);
  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: fault_dev_register
 *
 * Description:
 *   Register the character-device entry for a newly attached fault point.
 *   Idempotent: failure to register (path collision / OOM) leaves the
 *   point usable from in-kernel call sites but invisible to ioctl.
 *
 ****************************************************************************/

void fault_dev_register(FAR struct fault_point_s *fp)
{
  FAR struct fault_dev_priv_s *priv;
  char path[FAULT_INJECT_DEV_PATH_MAX];

  if (fp == NULL || fp->meta == NULL || fp->meta->name == NULL)
    {
      return;
    }

  priv = kmm_zalloc(sizeof(*priv));
  if (priv == NULL)
    {
      return;
    }

  priv->fp = fp;

  fault_dev_path(fp->meta, path, sizeof(path));

  /* register_driver returning -EEXIST is tolerated: every BMP CPU
   * runs through fault_inject_populate() and may attempt to
   * register the same name into its own VFS instance; the first
   * caller wins, subsequent ones drop their priv silently.
   */

  if (register_driver(path, &g_fault_dev_fops, 0666, priv) < 0)
    {
      kmm_free(priv);
    }
}

/****************************************************************************
 * Name: fault_dev_init_control
 *
 * Description:
 *   Register the /dev/fault/control node into the calling CPU's
 *   VFS tree.  Called once per CPU from fault_inject_init()
 *   because VFS inode trees are per-CPU in BMP mode; the runtime state
 *   that the node operates on (g_fault_list, the per-site
 *   fault_point_s) is the single shared image-wide instance, so the
 *   "control" semantics remain image-singleton even though the file
 *   node itself is materialised on every core.
 *
 ****************************************************************************/

int fault_dev_init_control(void)
{
  FAR struct fault_dev_priv_s *priv;
  int ret;

  priv = kmm_zalloc(sizeof(*priv));
  if (priv == NULL)
    {
      return -ENOMEM;
    }

  priv->fp = NULL;

  ret = register_driver(FAULT_INJECT_CTRL_PATH,
                        &g_fault_dev_fops, 0666, priv);
  if (ret < 0)
    {
      kmm_free(priv);
      return ret;
    }

  return OK;
}
