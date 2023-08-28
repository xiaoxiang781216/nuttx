/****************************************************************************
 * drivers/pcie/msi.c
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

#include <nuttx/pcie/cap.h>
#include <nuttx/pcie/pcie.h>
#include <nuttx/pcie/controller.h>

/****************************************************************************
 * Name: get_msi_mmc
 *
 * Description:
 *   Get Multiple Message Capable of Message Control Register for MSI.
 *
 * Input Parameters:
 *   dev  - A PCI-E device private data
 *   base - A pointer in Configuration Space to the first entry
 * of a linked list of new capabilities.
 *
 * Returned Value:
 *   Return the Multiple Message Capable value
 ****************************************************************************/

static uint32_t get_msi_mmc(FAR struct pcie_dev_s *dev,
                            uint32_t base)
{
  uint32_t mcr;

  mcr = dev->ops->pci_cfg_read(dev->bdf, base + PCIE_MSI_MCR);

  /* Getting MMC true count: 2^(MMC field) */

  return 1 << ((mcr & PCIE_MSI_MCR_MMC) >> PCIE_MSI_MCR_MMC_SHIFT);
}

/****************************************************************************
 * Name: disable_msi
 *
 * Description:
 *   This function is used to disable the MSI Enable bit in Message Control
 * Register for MSI.
 *
 * Input Parameters:
 *   dev  - A PCI-E device private data
 *   base - A pointer in Configuration Space to the first entry
 * of a linked list of new capabilities.
 *
 ****************************************************************************/

static void disable_msi(FAR struct pcie_dev_s *dev,
                        uint32_t base)
{
  uint32_t mcr;

  mcr = dev->ops->pci_cfg_read(dev->bdf, base + PCIE_MSI_MCR);
  mcr &= ~PCIE_MSI_MCR_EN;
  dev->ops->pci_cfg_write(dev->bdf, base + PCIE_MSI_MCR, mcr);
}

/****************************************************************************
 * Name: enable_msi
 *
 * Description:
 *   This function is used to enable the MSI Enable bit in Message Control
 * Register for MSI.
 *
 * Input Parameters:
 *   dev      - A PCI-E device private data
 *   vectors  - MSI vectors by System-specified
 *   n_vector - The number of MSI vectors
 *   base     - A pointer in Configuration Space to the first entry
 * of a linked list of new capabilities.
 *   irq      - An interrupt request
 *
 ****************************************************************************/

static void enable_msi(FAR struct pcie_dev_s *dev,
           FAR msi_vector_t *vectors,
           uint8_t n_vector,
           uint32_t base,
           unsigned int irq)
{
  uint32_t mcr;
  uint32_t map;
  uint32_t mdr;
  uint32_t mme;

  /* map: System-specified message address */

  map = vectors->arch.address;

  /* write map to MSI Message lower Address */

  dev->ops->pci_cfg_write(dev->bdf, base + PCIE_MSI_MAP0, map);

  mdr = vectors->arch.eventid;
  mcr = dev->ops->pci_cfg_read(dev->bdf, base + PCIE_MSI_MCR);
  if ((mcr & PCIE_MSI_MCR_64) != 0U)
    {
      /* write 0 to MSI Message Upper Address */

      dev->ops->pci_cfg_write(dev->bdf, base + PCIE_MSI_MAP1_64, 0U);

      /* write data to MSI Message Data register */

      dev->ops->pci_cfg_write(dev->bdf, base + PCIE_MSI_MDR_64, mdr);
    }
  else
    {
      /* write data to 32bit MSI Message Data register */

      dev->ops->pci_cfg_write(dev->bdf, base + PCIE_MSI_MDR_32, mdr);
    }

  /* Generating MME field (1 counts as a power of 2) */

  for (mme = 0; n_vector > 1; mme++)
    {
      n_vector >>= 1;
    }

  mcr |= mme << PCIE_MSI_MCR_MME_SHIFT;

  mcr |= PCIE_MSI_MCR_EN;
  dev->ops->pci_cfg_write(dev->bdf, base + PCIE_MSI_MCR, mcr);
}

/****************************************************************************
 * Name: pcie_msi_base
 *
 * Description:
 *   This function is used to get base address of a PCIE device MSI.
 *
 * Input Parameters:
 *   dev - A PCI-E device private data
 *   msi - Support MSI flags(false or true)
 *
 * Returned Value:
 *   Return the base address of MSI
 ****************************************************************************/

static uint32_t pcie_msi_base(FAR struct pcie_dev_s *dev, FAR bool *msi)
{
  uint32_t base;

  if (msi != NULL)
    {
      *msi = true;
    }

  base = pcie_find_cap(dev, PCI_CAP_ID_MSI);

  return base;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: pcie_msi_vector_connect
 *
 * Description:
 *    Connect the MSI vector to the handler.
 *
 * Input Parameters:
 *   dev       - A PCI-E device private data
 *   vector    - the MSI vector to connect
 *   handler   - Interrupt service routine
 *   parameter - ISR parameter
 *   flags     - Arch-specific IRQ configuration flag
 *
 * Returned Value:
 *   True on success, false otherwise
 ****************************************************************************/

bool pcie_msi_vector_connect(FAR struct pcie_dev_s *dev,
           msi_vector_t *vector,
           CODE int (*handler)(int irq, FAR void *context, FAR void *arg),
           FAR const void *parameter,
           uint32_t flags)
{
  uint32_t base;

  base = pcie_msi_base(dev, NULL);
  if (base == 0)
    {
      return false;
    }

  return dev->ops->pcie_msi_vector_connect(vector, handler, parameter,
                                           flags);
}

/****************************************************************************
 * Name: pcie_msi_vectors_allocate
 *
 * Description:
 *   Allocate vector(s) for the endpoint MSI message(s).
 *
 * Input Parameters:
 *   dev      - A PCI-E device private data
 *   priority - The MSI vectors base interrupt priority
 *   vectors  - An array for storing allocated MSI vectors
 *   n_vector - The size of the MSI vectors array
 *
 * Returned Value:
 *   The number of allocated MSI vectors.
 ****************************************************************************/

uint8_t pcie_msi_vectors_allocate(FAR struct pcie_dev_s *dev,
                                  unsigned int priority,
                                  FAR msi_vector_t *vectors,
                                  uint8_t n_vector)
{
  uint32_t req_vectors;
  uint32_t base;
  bool msi;

  /* get Message Signalled Interrupts */

  base = pcie_msi_base(dev, &msi);
  if (msi)
    {
      /* get mmc and get byte value by shifting */

      req_vectors = get_msi_mmc(dev, base);
    }

  if (n_vector > req_vectors)
    {
      n_vector = req_vectors;
    }

  for (req_vectors = 0; req_vectors < n_vector; req_vectors++)
    {
      vectors[req_vectors].bdf = dev->bdf;
    }

  return dev->ops->pcie_msi_vectors_allocate(priority, vectors, n_vector);
}

/****************************************************************************
 * Name: pcie_is_msi
 *
 * Description:
 *   Check if the given PCI endpoint supports MSI(currently not support
 * MSIX).
 *
 * Input Parameters:
 *   dev - A PCI-E device private data
 *
 * Returned Value:
 *   true if the endpoint support MSI
 ****************************************************************************/

bool pcie_is_msi(FAR struct pcie_dev_s *dev)
{
  return (pcie_msi_base(dev, NULL) != 0);
}

/****************************************************************************
 * Name: pcie_msi_enable
 *
 * Description:
 *   Configure the given PCI endpoint to generate MSIs.
 *
 * Input Parameters:
 *   dev      - A PCI-E device private data
 *   vectors  - An array of allocated vector(s)
 *   n_vector - the size of the vector array
 *   irq      - The IRQ we wish to trigger via MSI
 *
 * Returned Value:
 *   true if the endpoint supports MSI, false otherwise.
 ****************************************************************************/

bool pcie_msi_enable(FAR struct pcie_dev_s *dev, FAR msi_vector_t *vectors,
                     uint8_t n_vector,
                     unsigned int irq)
{
  uint32_t base;
  bool msi;

  base = pcie_msi_base(dev, &msi);
  if (base == 0)
    {
      return false;
    }

  enable_msi(dev, vectors, n_vector, base, irq);

  pcie_set_cmd(dev->ops, dev->bdf, PCIE_CONF_CMDSTAT_MASTER, true);

  return true;
}
