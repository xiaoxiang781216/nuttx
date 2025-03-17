/****************************************************************************
 * drivers/coresight/coresight_itm.c
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
#include <nuttx/bits.h>
#include <nuttx/coresight/coresight_itm.h>
#include <nuttx/kmalloc.h>
#include <nuttx/spinlock.h>
#include <sys/param.h>

#include "coresight_common.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define ITM_STIMPORT(d)  (0x000 + 4*(d)) /* ITM stimulus port */
#define ITM_TER          0xE00           /* ITM Trace Enable Register */
#define ITM_TTR          0xE20           /* ITM Trace Trigger Register */
#define ITM_TPR          0xE40           /* ITM Trace Privilege Register */
#define ITM_TCR          0xE80           /* ITM control register  */
#define ITM_SYNCCTRL     0xE90           /* ITM Sychronisation register */

#define ITM_CTRL_EN      0x01            /* ITM enable */
#define ITM_CTRL_BUSY    0x800000        /* ITM busy */

/****************************************************************************
 * Private Types
 ****************************************************************************/

/****************************************************************************
 * Private Functions Prototypes
 ****************************************************************************/

static int itm_enable(FAR struct coresight_dev_s *csdev);
static void itm_disable(FAR struct coresight_dev_s *csdev);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct coresight_source_ops_s g_itm_source_ops =
{
  .enable  = itm_enable,
  .disable = itm_disable,
};

static const struct coresight_ops_s g_itm_ops =
{
  .source_ops = &g_itm_source_ops,
};

static FAR struct coresight_itm_dev_s *g_itmdev;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: itm_hw_disable
 ****************************************************************************/

static void itm_hw_disable(FAR struct coresight_itm_dev_s *itmdev)
{
  coresight_unlock(itmdev->csdev.addr);
  coresight_modify32(0x0, ITM_CTRL_EN, itmdev->csdev.addr + ITM_TCR);
  coresight_put32(0x0, itmdev->csdev.addr + ITM_TER);
  coresight_lock(itmdev->csdev.addr);
}

/****************************************************************************
 * Name: itm_hw_enable
 ****************************************************************************/

static void itm_hw_enable(FAR struct coresight_itm_dev_s *itmdev)
{
  coresight_unlock(itmdev->csdev.addr);
  coresight_put32(0xffffffff, itmdev->csdev.addr + ITM_TER);
  coresight_modify32(ITM_CTRL_EN, ITM_CTRL_EN,
                     itmdev->csdev.addr + ITM_TCR);
  coresight_lock(itmdev->csdev.addr);
}

/****************************************************************************
 * Name: itm_hw_disable
 ****************************************************************************/

static void itm_disable(FAR struct coresight_dev_s *csdev)
{
  FAR struct coresight_itm_dev_s *itmdev =
    (FAR struct coresight_itm_dev_s *)csdev;

  itm_hw_disable(itmdev);
  coresight_disclaim_device(itmdev->csdev.addr);
}

/****************************************************************************
 * Name: itm_enable
 ****************************************************************************/

static int itm_enable(FAR struct coresight_dev_s *csdev)
{
  FAR struct coresight_itm_dev_s *itmdev =
    (FAR struct coresight_itm_dev_s *)csdev;
  int ret;

  ret = coresight_claim_device(itmdev->csdev.addr);
  if (ret < 0)
    {
      return ret;
    }

  itm_hw_enable(itmdev);
  return ret;
}

/****************************************************************************
 * Name: itm_get_stimulus_port_num
 ****************************************************************************/

static uint32_t itm_get_stimulus_port_num(uintptr_t addr)
{
  uint32_t numsp;

  coresight_unlock(addr);
  coresight_put32(0xffffffff, addr + ITM_TPR);
  numsp = coresight_get32(addr + ITM_TPR);
  coresight_put32(0x0, addr + ITM_TPR);
  coresight_lock(addr);

  return LOG2_CEIL(numsp) * 8;
}

/****************************************************************************
 * Name: itm_stimulus_send
 *
 * Description:
 *   Send data to ITM stimulus port.
 *
 * Input Parameters:
 *   stimulus - Pointer to the ITM stimulus port.
 *   value    - Pointer to the data to send.
 *   size     - The size of the data to send.
 *
 * Returned Value:
 *   OK on success; a negated errno value on failure.
 *
 * Note:
 *   This function is not thread-safe. data must be aligned to 32-bit.
 *
 ****************************************************************************/

static ssize_t itm_stimulus_send(uintptr_t stimulus, FAR const void *data,
                                 size_t size)
{
  int i;

  for (i = 0; i < CONFIG_CORESIGHT_TIMEOUT; i++)
    {
      if (coresight_get32(stimulus))
        {
          switch (size)
            {
              case 4:
                coresight_put32(*(uint32_t *)data, stimulus);
                return OK;

              case 2:
                coresight_put16(*(uint16_t *)data, stimulus);
                return OK;

              case 3:
                coresight_put16(*(uint16_t *)data, stimulus);
                coresight_put8(*(uint8_t *)(data + 2), stimulus);
                return OK;

              case 1:
                coresight_put8(*(uint8_t *)data, stimulus);
                return OK;
            }
        }
    }

  return -ETIMEDOUT;
}

/****************************************************************************
 * Name: itm_stream_putc
 ****************************************************************************/

static void itm_stream_putc(FAR struct lib_outstream_s *stream, int ch)
{
  FAR struct itm_stream_s *port = (FAR struct itm_stream_s *)stream;
  itm_stimulus_send(port->port->stimulus, &ch, 1);
}

/****************************************************************************
 * Name: itm_stream_puts
 ****************************************************************************/

static ssize_t itm_stream_puts(FAR struct lib_outstream_s *stream,
                              FAR const void *buf, size_t len)
{
  FAR struct itm_stream_s *port = (FAR struct itm_stream_s *)stream;
  return itm_send(port->port, buf, len);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: itm_alloc_port
 *
 * Description:
 *   Allocate a ITM stimulus port.
 *
 * Returned Value:
 *   Pointer to the ITM stimulus port on success; NULL on failure.
 *
 ****************************************************************************/

FAR struct itm_port_s *itm_alloc_port(void)
{
  FAR struct itm_port_s *port;
  irqstate_t flags;

  if (g_itmdev == NULL)
    {
      return NULL;
    }

  port = kmm_zalloc(sizeof(struct itm_port_s));
  if (port == NULL)
    {
      return NULL;
    }

  flags = spin_lock_irqsave(&g_itmdev->lock);
  port->num = ffs(g_itmdev->map);
  set_bit(port->num, &g_itmdev->map);
  spin_unlock_irqrestore(&g_itmdev->lock, flags);

  port->stimulus = g_itmdev->csdev.addr + ITM_STIMPORT(port->num);
  spin_lock_init(&port->lock);
  return port;
}

/****************************************************************************
 * Name: itm_free_port
 *
 * Description:
 *   Free a ITM stimulus port.
 *
 * Input Parameters:
 *   port - Pointer to the ITM stimulus port.
 *
 * Returned Value:
 *   OK on success; a negated errno value on failure.
 *
 ****************************************************************************/

int itm_free_port(FAR struct itm_port_s *port)
{
  irqstate_t flags;

  if (port == NULL)
    {
      return -ENODEV;
    }

  flags = spin_lock_irqsave(&g_itmdev->lock);
  clear_bit(port->num, &g_itmdev->map);
  spin_unlock_irqrestore(&g_itmdev->lock, flags);

  kmm_free(port);
  return OK;
}

/****************************************************************************
 * Name: itm_send
 *
 * Description:
 *   Send data to ITM stimulus port.
 *
 * Input Parameters:
 *   port     - Pointer to the ITM stimulus port.
 *   buffer   - Pointer to the data to send.
 *   size     - The size of the data to send.
 *
 * Returned Value:
 *   The number of bytes sent on success; a negated errno value on failure.
 *
 ****************************************************************************/

ssize_t itm_send(FAR struct itm_port_s *port, FAR const void *buffer,
                 size_t size)
{
  FAR const uint8_t *data = buffer;
  size_t remaining = size;
  irqstate_t flags;
  size_t unaligned;
  uint32_t value;

  if (port == NULL || buffer == NULL)
    {
      return -ENODEV;
    }

  flags = spin_lock_irqsave(&port->lock);
  unaligned = (uintptr_t)data % sizeof(uint32_t);
  if (unaligned > 0)
    {
      size_t min = MIN(size, sizeof(uint32_t) - unaligned);
      memcpy(&value, data, min);
      itm_stimulus_send(port->stimulus, &value, min);
      remaining -= min;
      data += min;
    }

  while (remaining >= sizeof(uint32_t))
    {
      itm_stimulus_send(port->stimulus, data, sizeof(uint32_t));
      remaining -= sizeof(uint32_t);
      data += sizeof(uint32_t);
    }

  if (remaining > 0)
    {
      itm_stimulus_send(port->stimulus, data, remaining);
    }

  spin_unlock_irqrestore(&port->lock, flags);
  return size;
}

/****************************************************************************
 * Name: itm_stream_open
 *
 * Description:
 *   Open a ITM stream.
 *
 ****************************************************************************/

void itm_stream_open(FAR struct itm_port_s *port,
                     FAR struct itm_stream_s *stream)
{
  stream->port = port;
  stream->common.putc = itm_stream_putc;
  stream->common.puts = itm_stream_puts;
  stream->common.flush = lib_noflush;
}

/****************************************************************************
 * Name: itm_register
 *
 * Description:
 *   Register a ITM devices.
 *
 * Input Parameters:
 *   desc  - A description of this coresight device.
 *
 * Returned Value:
 *   Pointer to a ITM device on success; NULL on failure.
 *
 ****************************************************************************/

FAR struct coresight_itm_dev_s *
itm_register(FAR const struct coresight_desc_s *desc)
{
  FAR struct coresight_itm_dev_s *itmdev;
  FAR struct coresight_dev_s *csdev;
  uint32_t numsp;
  int ret;

  itmdev = kmm_zalloc(sizeof(struct coresight_itm_dev_s));
  if (itmdev == NULL)
    {
      cserr("%s:malloc failed!\n", desc->name);
      return NULL;
    }

  numsp = itm_get_stimulus_port_num(desc->addr);
  itmdev->numsp = numsp;
  itmdev->traceid = coresight_get_system_trace_id();
  if (itmdev->traceid < 0)
    {
      kmm_free(itmdev);
      cserr("%s:get unique traceid failed!\n", desc->name);
      return NULL;
    }

  csdev = &itmdev->csdev;
  csdev->ops = &g_itm_ops;
  ret = coresight_register(csdev, desc);
  if (ret < 0)
    {
      coresight_put_system_trace_id(itmdev->traceid);
      kmm_free(itmdev);
      cserr("%s:register failed\n", desc->name);
      return NULL;
    }

  g_itmdev = itmdev;
  return itmdev;
}

/****************************************************************************
 * Name: itm_unregister
 *
 * Description:
 *   Unregister a ITM devices.
 *
 * Input Parameters:
 *   itmdev  - Pointer to the ITM device.
 *
 ****************************************************************************/

void itm_unregister(FAR struct coresight_itm_dev_s *itmdev)
{
  if (itmdev != g_itmdev)
    {
      return;
    }

  coresight_unregister(&itmdev->csdev);
  coresight_put_system_trace_id(itmdev->traceid);
  kmm_free(itmdev);
  g_itmdev = NULL;
}
