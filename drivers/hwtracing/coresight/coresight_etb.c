/****************************************************************************
 * drivers/hwtracing/coresight/coresight_etb.c
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
#include <stdio.h>
#include <string.h>
#include <nuttx/bits.h>
#include <nuttx/kmalloc.h>
#include <nuttx/fs/fs.h>
#include <nuttx/irq.h>

#include <nuttx/hwtracing/coresight/coresight_etb.h>

#include "coresight_common.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* ETB registers */

#define ETB_RAM_DEPTH_REG     0x004
#define ETB_STATUS_REG        0x00c
#define ETB_RAM_READ_DATA_REG 0x010
#define ETB_RAM_READ_POINTER  0x014
#define ETB_RAM_WRITE_POINTER 0x018
#define ETB_TRG               0x01c
#define ETB_CTL_REG           0x020
#define ETB_RWD_REG           0x024
#define ETB_FFSR              0x300
#define ETB_FFCR              0x304
#define ETB_ITMISCOP0         0xee0
#define ETB_ITTRFLINACK       0xee4
#define ETB_ITTRFLIN          0xee8
#define ETB_ITATBDATA0        0xeeC
#define ETB_ITATBCTR2         0xef0
#define ETB_ITATBCTR1         0xef4
#define ETB_ITATBCTR0         0xef8

/* STS - 0x00C */

#define ETB_STATUS_RAM_FULL   BIT(0)

/* CTL - 0x020 */

#define ETB_CTL_CAPT_EN       BIT(0)

/* FFCR - 0x304 */

#define ETB_FFCR_EN_FTC       BIT(0)
#define ETB_FFCR_FON_MAN      BIT(6)
#define ETB_FFCR_STOP_FI      BIT(12)
#define ETB_FFCR_STOP_TRIGGER BIT(13)

#define ETB_FFSR_FT_STOPPED   BIT(1)

#define ETB_FRAME_SIZE_WORDS  4

#define ETB_MAX_NAME_LEN      32

/****************************************************************************
 * Private Functions Prototypes
 ****************************************************************************/

static int etb_enable(FAR struct hwtracing_dev_s *htdev);
static void etb_disable(FAR struct hwtracing_dev_s *htdev);
static int etb_open(FAR struct file *filep);
static int etb_close(FAR struct file *filep);
static ssize_t etb_read(FAR struct file *filep, FAR char *buffer,
                        size_t buflen);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct hwtracing_sink_ops_s g_etb_sink_ops =
{
  .enable  = etb_enable,
  .disable = etb_disable,
};

static const struct hwtracing_ops_s g_etb_ops =
{
  .sink_ops = &g_etb_sink_ops,
};

static const struct file_operations g_etb_fops =
{
  etb_open,               /* open */
  etb_close,              /* close */
  etb_read,               /* read */
  NULL,                   /* write */
  NULL,                   /* seek */
  NULL,                   /* ioctl */
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: etb_hw_enable
 ****************************************************************************/

static void etb_hw_enable(FAR struct coresight_etb_dev_s *etbdev)
{
  uint32_t i;

  coresight_unlock(etbdev->htdev.addr);

  /* Clear entire RAM buffer. */

  hwtracing_put32(0x0, etbdev->htdev.addr + ETB_RAM_WRITE_POINTER);
  for (i = 0; i < etbdev->buffer_depth; i += ETB_FRAME_SIZE_WORDS)
    {
      hwtracing_put32(0x0, etbdev->htdev.addr + ETB_RWD_REG);
    }

  /* Reset read/write RAM pointer. */

  hwtracing_put32(0x0, etbdev->htdev.addr + ETB_RAM_WRITE_POINTER);
  hwtracing_put32(0x0, etbdev->htdev.addr + ETB_RAM_READ_POINTER);

  /* Set trigger event. */

  hwtracing_put32(etbdev->trigger_cntr, etbdev->htdev.addr + ETB_TRG);
  hwtracing_put32(ETB_FFCR_EN_FTC | ETB_FFCR_STOP_TRIGGER,
                  etbdev->htdev.addr + ETB_FFCR);

  /* Enable trace capture. */

  hwtracing_put32(ETB_CTL_CAPT_EN, etbdev->htdev.addr + ETB_CTL_REG);
  coresight_lock(etbdev->htdev.addr);
}

/****************************************************************************
 * Name: etb_hw_disable
 ****************************************************************************/

static void etb_hw_disable(FAR struct coresight_etb_dev_s *etbdev)
{
  coresight_unlock(etbdev->htdev.addr);

  /* Trigger a formatter stop event. */

  hwtracing_modify32(ETB_FFCR_STOP_FI, ETB_FFCR_STOP_FI,
                     etbdev->htdev.addr + ETB_FFCR);
  hwtracing_modify32(ETB_FFCR_FON_MAN, ETB_FFCR_FON_MAN,
                     etbdev->htdev.addr + ETB_FFCR);
  if (hwtracing_timeout(0x0, ETB_FFCR_FON_MAN,
                        etbdev->htdev.addr + ETB_FFCR) < 0)
    {
      hterr("timeout while waiting for completion of Manual Flush\n");
    }

  /* Disable trace capture. */

  hwtracing_put32(0x0, etbdev->htdev.addr + ETB_CTL_REG);
  if (hwtracing_timeout(ETB_FFSR_FT_STOPPED, ETB_FFSR_FT_STOPPED,
                        etbdev->htdev.addr + ETB_FFSR) < 0)
    {
      hterr("timeout while waiting for Formatter to Stop\n");
    }

  coresight_lock(etbdev->htdev.addr);
}

/****************************************************************************
 * Name: etb_hw_read
 *
 * Description:
 *   Dump ETB RAM buffer to device's buffer for usrspace's read.
 *
 * Input Parameters:
 *   etbdev  - Pointer to the ETB device.
 *
 ****************************************************************************/

static void etb_hw_read(FAR struct coresight_etb_dev_s *etbdev)
{
  uint32_t readptr;
  uint32_t writeptr;
  uint32_t frameoff;
  FAR uint32_t *bufptr;
  bool lost = false;
  uint32_t i;

  coresight_unlock(etbdev->htdev.addr);

  /* ARM recommends that addresses are 128-bit aligned. Read from 0 when ETB
   * buffer is not full, otherwise, read from writepointer and there are some
   * trace data has been overwriten and lost.
   */

  readptr = hwtracing_get32(etbdev->htdev.addr + ETB_RAM_READ_POINTER);
  writeptr = hwtracing_get32(etbdev->htdev.addr + ETB_RAM_WRITE_POINTER);

  frameoff = writeptr % ETB_FRAME_SIZE_WORDS;
  if (frameoff != 0)
    {
      hterr("writeptr: 0x%" PRIx32 " not aligned to formatter frame size\n",
            writeptr);
      writeptr += ETB_FRAME_SIZE_WORDS - frameoff;
    }

  etbdev->available = writeptr > readptr ? writeptr - readptr :
                      etbdev->buffer_depth - readptr + writeptr;
  htinfo("read: %" PRId32 ", write: %" PRId32 ", available: %" PRId32 "\n",
         readptr, writeptr, etbdev->available);
  if ((hwtracing_get32(etbdev->htdev.addr + ETB_STATUS_REG) &
       ETB_STATUS_RAM_FULL) == 0)
    {
      hwtracing_put32(0x0, etbdev->htdev.addr + ETB_RAM_READ_POINTER);
    }
  else
    {
      hwtracing_put32(writeptr, etbdev->htdev.addr + ETB_RAM_READ_POINTER);
      lost = true;
    }

  bufptr = etbdev->bufptr;
  for (i = 0; i < etbdev->available; i += ETB_FRAME_SIZE_WORDS)
    {
      *bufptr = hwtracing_get32(etbdev->htdev.addr + ETB_RAM_READ_DATA_REG);
      bufptr += 1;
    }

  if (lost == true)
    {
      coresight_insert_barrier_packet(etbdev->bufptr);
    }

  /* Clear content between frameoff and ETB_FRAME_SIZE_WORDS - frameoff. */

  if (frameoff)
    {
      bufptr -= ETB_FRAME_SIZE_WORDS - frameoff;
      for (i = 0; i < ETB_FRAME_SIZE_WORDS - frameoff; i++)
        {
          *bufptr = 0x0;
          bufptr += 1;
        }
    }

  hwtracing_put32(readptr, etbdev->htdev.addr + ETB_RAM_READ_POINTER);
  coresight_lock(etbdev->htdev.addr);
}

/****************************************************************************
 * Name: etb_enable
 ****************************************************************************/

static int etb_enable(FAR struct hwtracing_dev_s *htdev)
{
  FAR struct coresight_etb_dev_s *etbdev =
    (FAR struct coresight_etb_dev_s *)htdev;
  int ret;

  ret = coresight_claim_device(etbdev->htdev.addr);
  if (ret < 0)
    {
      return ret;
    }

  etb_hw_enable(etbdev);
  return ret;
}

/****************************************************************************
 * Name: etb_disable
 ****************************************************************************/

static void etb_disable(FAR struct hwtracing_dev_s *htdev)
{
  FAR struct coresight_etb_dev_s *etbdev =
    (FAR struct coresight_etb_dev_s *)htdev;

  etb_hw_disable(etbdev);
  coresight_disclaim_device(etbdev->htdev.addr);
}

/****************************************************************************
 * Name: etb_open
 ****************************************************************************/

static int etb_open(FAR struct file *filep)
{
  FAR struct inode *inode = filep->f_inode;
  FAR struct coresight_etb_dev_s *etbdev;
  int ret;

  DEBUGASSERT(inode->i_private);
  etbdev = (FAR struct coresight_etb_dev_s *)inode->i_private;

  ret = nxmutex_lock(&etbdev->lock);
  if (ret < 0)
    {
      return ret;
    }

  if (etbdev->opencnt++ == 0)
    {
      /* Each buffer line is 32bit size. */

      etbdev->bufptr = kmm_zalloc(etbdev->buffer_depth);
      if (etbdev->bufptr == NULL)
        {
          hterr("malloc buffer failed\n");
          etbdev->opencnt--;
          ret = -ENOMEM;
        }
      else
        {
          irqstate_t flags;

          flags = spin_lock_irqsave(&etbdev->htdev.lock);
          if (etbdev->htdev.refcnt > 0)
            {
              etb_hw_disable(etbdev);
            }

          etb_hw_read(etbdev);
          if (etbdev->htdev.refcnt > 0)
            {
              etb_hw_enable(etbdev);
            }

          spin_unlock_irqrestore(&etbdev->htdev.lock, flags);
        }
    }

  nxmutex_unlock(&etbdev->lock);
  return ret;
}

/****************************************************************************
 * Name: etb_close
 ****************************************************************************/

static int etb_close(FAR struct file *filep)
{
  FAR struct inode *inode = filep->f_inode;
  FAR struct coresight_etb_dev_s *etbdev;
  int ret;

  DEBUGASSERT(inode->i_private);
  etbdev = (FAR struct coresight_etb_dev_s *)inode->i_private;

  ret = nxmutex_lock(&etbdev->lock);
  if (ret < 0)
    {
      return ret;
    }

  if (--etbdev->opencnt == 0)
    {
      kmm_free(etbdev->bufptr);
    }

  nxmutex_unlock(&etbdev->lock);
  return ret;
}

/****************************************************************************
 * Name: etb_read
 ****************************************************************************/

static ssize_t etb_read(FAR struct file *filep, FAR char *buffer,
                        size_t buflen)
{
  FAR struct inode *inode = filep->f_inode;
  FAR struct coresight_etb_dev_s *etbdev;

  DEBUGASSERT(inode->i_private);
  etbdev = (FAR struct coresight_etb_dev_s *)inode->i_private;

  if (filep->f_pos > etbdev->available)
    {
      return 0;
    }

  if (filep->f_pos + buflen > etbdev->available)
    {
      buflen = etbdev->available - filep->f_pos;
    }

  memcpy(buffer, (char *)etbdev->bufptr + filep->f_pos, buflen);
  filep->f_pos += buflen;

  return buflen;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: etb_register
 *
 * Description:
 *   Register an etb devices.
 *
 * Input Parameters:
 *   desc  - A description of this coresight device.
 *
 * Returned Value:
 *   Pointer to an etb device on success; NULL on failure.
 *
 ****************************************************************************/

FAR struct coresight_etb_dev_s *
etb_register(FAR const struct hwtracing_desc_s *desc)
{
  FAR struct coresight_etb_dev_s *etbdev;
  FAR struct hwtracing_dev_s *htdev;
  char pathname[ETB_MAX_NAME_LEN];
  int ret;

  etbdev = kmm_zalloc(sizeof(struct coresight_etb_dev_s));
  if (etbdev == NULL)
    {
      hterr("%s:malloc failed!\n", desc->name);
      return NULL;
    }

  coresight_unlock(desc->addr);
  etbdev->buffer_depth = hwtracing_get32(desc->addr + ETB_RAM_DEPTH_REG);
  coresight_lock(desc->addr);
  if (etbdev->buffer_depth & 0x80000000)
    {
      hterr("%s:etb buffer depth is invalid\n", desc->name);
      goto buf_err;
    }

  etbdev->buffer_depth *= ETB_FRAME_SIZE_WORDS;
  htdev = &etbdev->htdev;
  htdev->ops = &g_etb_ops;
  ret = hwtracing_register(htdev, desc);
  if (ret < 0)
    {
      hterr("%s:coresight register failed\n", desc->name);
      goto buf_err;
    }

  snprintf(pathname, sizeof(pathname), "/dev/%s", desc->name);
  ret = register_driver(pathname, &g_etb_fops, 0444, etbdev);
  if (ret < 0)
    {
      hterr("%s:driver register failed\n", desc->name);
      goto drv_err;
    }

  nxmutex_init(&etbdev->lock);
  return etbdev;

drv_err:
  hwtracing_unregister(htdev);
buf_err:
  kmm_free(etbdev);
  return NULL;
}

/****************************************************************************
 * Name: etb_unregister
 *
 * Description:
 *   Unregister an etb devices.
 *
 * Input Parameters:
 *   etbdev  - Pointer to the etb device.
 *
 ****************************************************************************/

void etb_unregister(FAR struct coresight_etb_dev_s *etbdev)
{
  char pathname[ETB_MAX_NAME_LEN];

  snprintf(pathname, sizeof(pathname), "/dev/%s", etbdev->htdev.name);
  unregister_driver(pathname);
  hwtracing_unregister(&etbdev->htdev);
  nxmutex_destroy(&etbdev->lock);
  kmm_free(etbdev);
}
