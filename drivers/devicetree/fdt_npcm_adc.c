/****************************************************************************
 * drivers/devicetree/fdt_npcm_adc.c
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

#include <nuttx/fdt.h>
#include <nuttx/analog/adc.h>
#include <nuttx/analog/npcm_adc.h>

#include <libfdt.h>

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: fdt_npcm_adc_devices_register
 *
 * Description:
 *   Scan the device tree for "nuvoton,npcm750-adc" nodes and register each
 *   as an ADC character device (/dev/adc0, /dev/adc1, ...).
 *
 * Input Parameters:
 *   fdt      - Device tree handle
 *   irqbase  - Interrupt base number (added to FDT-supplied SPI index)
 *
 * Returned Value:
 *   Return OK on success; a negated errno on failure.
 *
 ****************************************************************************/

int fdt_npcm_adc_devices_register(FAR const void *fdt, int irqbase)
{
  FAR struct adc_dev_s *lower;
  uintptr_t addr;
  char devpath[32];
  int offset = -1;
  int irqnum;
  int minor = 0;
  int ret;

  for (; ; )
    {
      offset = fdt_node_offset_by_compatible(fdt, offset,
                                             "nuvoton,npcm750-adc");
      if (offset == -FDT_ERR_NOTFOUND)
        {
          break;
        }

      addr   = fdt_get_reg_base(fdt, offset, 0);
      irqnum = fdt_get_irq(fdt, offset, 1, irqbase);
      if (addr == 0 || irqnum < 0)
        {
          return -EINVAL;
        }

      lower = npcm_adc_initialize(addr, irqnum);
      if (lower == NULL)
        {
          return -ENODEV;
        }

      snprintf(devpath, sizeof(devpath), "/dev/adc%d", minor++);
      ret = adc_register(devpath, lower);
      if (ret < 0)
        {
          return ret;
        }
    }

  return OK;
}
