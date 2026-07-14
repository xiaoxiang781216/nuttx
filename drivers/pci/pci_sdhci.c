/****************************************************************************
 * drivers/pci/pci_sdhci.c
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

#include <stdint.h>
#include <inttypes.h>
#include <debug.h>
#include <errno.h>

#include <nuttx/mmcsd.h>
#include <nuttx/pci/pci.h>
#include <nuttx/sdio.h>

#include "pci_drivers.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* BAR0 holds the SDHCI register map */

#define PCI_SDHCI_BAR             0

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int  pci_sdhci_probe(FAR struct pci_device_s *dev);
static void pci_sdhci_remove(FAR struct pci_device_s *dev);

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Match SDHCI host controller by PCI class code.
 * dev->class is 24-bit: base_class<<16 | subclass<<8 | prog-if.
 * PCI_CLASS_SYSTEM_SDHCI (0x0805) is 16-bit (base+subclass only),
 * so shift left 8 to align with dev->class, and use mask 0xffff00
 * to match class+subclass while ignoring prog-if.
 */

static const struct pci_device_id_s g_pci_sdhci_ids[] =
{
  { PCI_DEVICE_CLASS(PCI_CLASS_SYSTEM_SDHCI << 8, 0xffff00) },
  { }
};

static struct pci_driver_s g_pci_sdhci_driver =
{
  .id_table = g_pci_sdhci_ids,
  .probe    = pci_sdhci_probe,
  .remove   = pci_sdhci_remove,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: pci_sdhci_probe
 *
 * Description:
 *   Called by the PCI core when an SDHCI host controller is discovered on
 *   the PCI bus.  Enables the device, maps BAR0, initializes the SDHCI
 *   lower-half driver, and registers with the MMC/SD subsystem.
 *
 ****************************************************************************/

static int pci_sdhci_probe(FAR struct pci_device_s *dev)
{
  FAR struct sdio_dev_s *sdio;
  uintptr_t base;
  int irq;
  int ret;

  /* Enable the PCI device and bus mastering */

  ret = pci_enable_device(dev);
  if (ret < 0)
    {
      pcierr("pci_enable_device failed: %d\n", ret);
      return ret;
    }

  pci_set_master(dev);

  /* Map BAR0 to get the CPU-accessible base address */

  base = (uintptr_t)pci_map_bar(dev, PCI_SDHCI_BAR);
  if (base == 0)
    {
      pcierr("pci_map_bar failed\n");
      ret = -ENOMEM;
      goto err_disable;
    }

  /* Get the interrupt number from PCI config space */

  irq = pci_get_irq(dev);
  if (irq < 0)
    {
      pcierr("pci_get_irq failed: %d\n", irq);
      ret = irq;
      goto err_disable;
    }

  /* Initialize the SDHCI lower-half driver */

  sdio = sdhci_initialize(base, irq);
  if (sdio == NULL)
    {
      pcierr("sdhci_initialize failed\n");
      ret = -ENOMEM;
      goto err_disable;
    }

  /* Register with the MMC/SD upper-half subsystem */

  ret = mmcsd_slotinitialize(0, sdio);
  if (ret < 0)
    {
      pcierr("mmcsd_slotinitialize failed: %d\n", ret);
      goto err_sdio;
    }

  /* Keep the instance for teardown in pci_sdhci_remove() */

  dev->priv = sdio;

  pciinfo("SDHCI controller at %" PRIxPTR " irq=%d\n", base, irq);
  return OK;

err_sdio:
  sdhci_uninitialize(sdio);
err_disable:
  pci_clear_master(dev);
  pci_disable_device(dev);
  return ret;
}

/****************************************************************************
 * Name: pci_sdhci_remove
 *
 * Description:
 *   Called by the PCI core when the device is removed.
 *
 ****************************************************************************/

static void pci_sdhci_remove(FAR struct pci_device_s *dev)
{
  /* Release the SDHCI lower-half instance */

  if (dev->priv != NULL)
    {
      sdhci_uninitialize(dev->priv);
      dev->priv = NULL;
    }

  pci_clear_master(dev);
  pci_disable_device(dev);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: pci_register_sdhci_driver
 *
 * Description:
 *   Register the SDHCI PCI glue driver with the PCI bus core.
 *
 ****************************************************************************/

int pci_register_sdhci_driver(void)
{
  return pci_register_driver(&g_pci_sdhci_driver);
}
