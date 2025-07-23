/****************************************************************************
 * drivers/hwtracing/tricoreht/tricoreht_trif_tfif.c
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
#include <stdio.h>
#include <string.h>
#include <nuttx/fs/fs.h>
#include <nuttx/kmalloc.h>
#include <nuttx/irq.h>

#include <nuttx/hwtracing/tricoreht/tricoreht_trif.h>

#include "tricoreht_trif_core.h"
#include "tricoreht_mcds_core.h"
#include "IfxTrif_reg.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define TFIF_MAX_NAME_LEN       32

/****************************************************************************
 * Private Functions Prototypes
 ****************************************************************************/

static int trif_tfif_sink_enable(FAR struct hwtracing_dev_s *htdev);
static void trif_tfif_sink_disable(FAR struct hwtracing_dev_s *htdev);
static int trif_tfif_sink_init(FAR struct hwtracing_dev_s *htdev);
static int trif_tfif_open(FAR struct file *filep);
static int trif_tfif_close(FAR struct file *filep);
static ssize_t trif_tfif_read(FAR struct file *filep, FAR char *buffer,
                              size_t buflen);

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* tfif-ETB and tfif-ETF sink device operations. */

static const struct hwtracing_sink_ops_s g_trif_tfif_sink_ops =
{
  .enable  = trif_tfif_sink_enable,
  .disable = trif_tfif_sink_disable,
  .init    = trif_tfif_sink_init,
};

static const struct hwtracing_ops_s g_trif_tfif_ops =
{
  .sink_ops = &g_trif_tfif_sink_ops,
};

static const struct file_operations g_trif_tfif_fops =
{
  trif_tfif_open,  /* open */
  trif_tfif_close, /* close */
  trif_tfif_read,  /* read */
  NULL,            /* write */
  NULL,            /* seek */
  NULL,            /* ioctl */
};

/****************************************************************************
 * Name: trif_tfif_hw_read
 *
 * Description:
 *   Dump ETB RAM buffer to device's buffer for usrspace's read. It just need
 *   to performing successive reads to the RRD Register, until the value
 *   0xFFFFFFFF is returned whick is kind different from tricoreht ETB
 *   device's reading process. refers to TRM.
 *
 ****************************************************************************/

static void trif_tfif_hw_read(FAR struct tricoreht_trif_dev_s *tfifdev)
{
  FAR uint32_t *bufptr;
  uint32_t read_data;
  uint16_t tla;

  bufptr = tfifdev->buf;
  tfifdev->len = 0;
  for (; ; )
    {
      read_data = hwtracing_get32(tfifdev->htdev.addr);
      tla = (uint16_t)(TRIF_TBTMESR.U & TRIF_TBTMESR_TLA_MASK);
      if (tla == 0 || tfifdev->size <= tfifdev->len + 4)
        {
          break;
        }

      memcpy(bufptr, &read_data, 4);
      bufptr += 1;
      tfifdev->len += 4;
    }
}

/****************************************************************************
 * Name: trif_tfif_hw_stop_and_read
 *
 * Description:
 *   Used for ETF sink devices to dump trace buffer. Do not dump trace buffer
 *   in trif_tfif_hw_disable to avoid trace buffer's data confusion when a
 *   process is reading trace buffer and anther process calles tfif_disable.
 *
 ****************************************************************************/

static void
trif_tfif_stop_and_read(FAR struct tricoreht_trif_dev_s *tfifdev)
{
  mcds_tbuf_stop();

  /* This is kind different from ETB tricoreht device, it should read data
   * from ram buffer in stopped state rather then disabled state.
   */

  trif_tfif_hw_read(tfifdev);
}

/****************************************************************************
 * Name: trif_tfif_hw_stop_and_read
 *
 * Description:
 *   Used for ETF sink devices to dump trace buffer. Do not dump trace buffer
 *   in trif_tfif_hw_disable to avoid trace buffer's data confusion when a
 *   process is reading trace buffer and anther process calles tfif_disable.
 *
 ****************************************************************************/

static void
trif_tfif_start(FAR struct tricoreht_trif_dev_s *tfifdev)
{
  mcds_tbuf_start();
}

/****************************************************************************
 * Name: trif_tfif_sink_init
 ****************************************************************************/

static int trif_tfif_sink_init(FAR struct hwtracing_dev_s *htdev)
{
  return trif_init(htdev);
}

/****************************************************************************
 * Name: trif_tfif_sink_enable
 ****************************************************************************/

static int trif_tfif_sink_enable(FAR struct hwtracing_dev_s *htdev)
{
  hwtracing_modify32(TRIF_CTRL_OPMODE_P | TRIF_OPMODE_TRACE_TBUF_TFIF,
                     TRIF_CTRL_OPMODE_P | TRIF_CTRL_OPMODE_MASK,
                     &TRIF_CTRL.U);

  return trif_enable(htdev);
}

/****************************************************************************
 * Name: trif_tfif_sink_disable
 ****************************************************************************/

static void trif_tfif_sink_disable(FAR struct hwtracing_dev_s *htdev)
{
  trif_disable(htdev);
}

/****************************************************************************
 * Name: trif_tfif_open
 ****************************************************************************/

static int trif_tfif_open(FAR struct file *filep)
{
  FAR struct inode *inode = filep->f_inode;
  FAR struct tricoreht_trif_dev_s *tfifdev;
  int ret;

  DEBUGASSERT(inode->i_private);
  tfifdev = (FAR struct tricoreht_trif_dev_s *)inode->i_private;

  ret = nxmutex_lock(&tfifdev->lock);
  if (ret < 0)
    {
      return ret;
    }

  if (tfifdev->htdev.refcnt <= 0)
    {
      /* ETF devices can not read buffer directly when it is not
      * enabled like etb device does. Its buffer needs to be
      * captured in stopped state which is transferred after it
      * has been enabled.
      */

      return -EACCES;
    }

  if (tfifdev->opencnt++ == 0)
    {
      tfifdev->buf = kmm_zalloc(tfifdev->size);
      if (tfifdev->buf == NULL)
        {
          hterr("malloc buffer failed\n");
          tfifdev->opencnt--;
          return -ENOMEM;
        }

      /* Only do the stop/start sequence under the lock */

      irqstate_t flags = spin_lock_irqsave(&tfifdev->htdev.lock);
      trif_tfif_stop_and_read(tfifdev);
      trif_tfif_start(tfifdev);
      spin_unlock_irqrestore(&tfifdev->htdev.lock, flags);
    }

  nxmutex_unlock(&tfifdev->lock);
  return ret;
}

/****************************************************************************
 * Name: trif_tfif_close
 ****************************************************************************/

static int trif_tfif_close(FAR struct file *filep)
{
  FAR struct inode *inode = filep->f_inode;
  FAR struct tricoreht_trif_dev_s *tfifdev;
  int ret;

  DEBUGASSERT(inode->i_private);
  tfifdev = (FAR struct tricoreht_trif_dev_s *)inode->i_private;

  ret = nxmutex_lock(&tfifdev->lock);
  if (ret < 0)
    {
      return ret;
    }

  if (--tfifdev->opencnt == 0)
    {
      kmm_free(tfifdev->buf);
    }

  nxmutex_unlock(&tfifdev->lock);
  return ret;
}

/****************************************************************************
 * Name: trif_tfif_read
 ****************************************************************************/

static ssize_t trif_tfif_read(FAR struct file *filep, FAR char *buffer,
                              size_t buflen)
{
  FAR struct inode *inode = filep->f_inode;
  FAR struct tricoreht_trif_dev_s *tfifdev;

  DEBUGASSERT(inode->i_private);
  tfifdev = (FAR struct tricoreht_trif_dev_s *)inode->i_private;

  if (filep->f_pos > tfifdev->len)
    {
      return 0;
    }

  if (filep->f_pos + buflen > tfifdev->len)
    {
      buflen = tfifdev->len - filep->f_pos;
    }

  memcpy(buffer, (FAR char *)tfifdev->buf + filep->f_pos, buflen);
  filep->f_pos += buflen;

  return buflen;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: trif_tfif_register
 ****************************************************************************/

int trif_tfif_register(FAR struct tricoreht_trif_dev_s * tfifdev,
                       FAR const struct hwtracing_desc_s *desc)
{
  int ret;

  if (desc->type == HWTRACING_DEV_TYPE_SINK)
    {
      char pathname[TFIF_MAX_NAME_LEN];

      tfifdev->htdev.ops = &g_trif_tfif_ops;
      tfifdev->size = MCDS_TBUF_SIZE;
      ret = hwtracing_register(&tfifdev->htdev, desc);
      if (ret < 0)
        {
          hterr("%s:tricoreht register failed\n", desc->name);
          return ret;
        }

      snprintf(pathname, sizeof(pathname), "/dev/%s", desc->name);
      ret = register_driver(pathname, &g_trif_tfif_fops, 0444, tfifdev);
      if (ret < 0)
        {
          hterr("%s:driver register failed\n", desc->name);
          hwtracing_unregister(&tfifdev->htdev);
          return ret;
        }
    }
  else
    {
      hterr("unsupported tfif device type\n");
      return -EPERM;
    }

  return 0;
}

/****************************************************************************
 * Name: trif_tfif_unregister
 ****************************************************************************/

void trif_tfif_unregister(FAR struct tricoreht_trif_dev_s * tfifdev)
{
  if (tfifdev->htdev.type == HWTRACING_DEV_TYPE_SINK)
    {
      char pathname[TFIF_MAX_NAME_LEN];

      snprintf(pathname, sizeof(pathname), "/dev/%s", tfifdev->htdev.name);
      unregister_driver(pathname);
    }

  hwtracing_unregister(&tfifdev->htdev);
}
