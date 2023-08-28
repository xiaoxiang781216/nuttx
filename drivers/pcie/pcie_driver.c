/****************************************************************************
 * drivers/pcie/pcie_driver.c
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
#include <debug.h>
#include <assert.h>
#include <nuttx/mm/mm.h>
#include <nuttx/kmalloc.h>
#include <nuttx/pcie/pcie.h>
#include <nuttx/pcie/controller.h>

int pcie_scan_bus(FAR const struct pcie_bus_ops_s *ops, uint8_t bus);
extern struct pcie_ctrl_dev g_nuttx_init_data;

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct list_node
g_pcie_dev_list = LIST_INITIAL_VALUE(g_pcie_dev_list);
static mutex_t g_pcie_dev_list_mutex = NXMUTEX_INITIALIZER;

static struct list_node
g_pcie_driver_list = LIST_INITIAL_VALUE(g_pcie_driver_list);
static mutex_t g_pcie_driver_mutex = NXMUTEX_INITIALIZER;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: pcie_scan_dev
 *
 * Description:
 *   This function is used to scanning all device endpoints in pcie bus.
 *
 * Input Parameters:
 *   ops - Operation callback of the pcie contoller.
 *   bus - bus number
 *   dev - device number(include 8 function)
 *
 * Return Value:
 *   Return 1 if sunccess, otherwise 0 or negative number.
 ****************************************************************************/

static int pcie_scan_dev(FAR const struct pcie_bus_ops_s *ops, uint8_t bus,
                         uint8_t dev)
{
  uint32_t secondary = 0;
  uint32_t id;
  uint32_t type;
  pcie_bdf_t bdf = PCIE_BDF(bus, dev, 0);
  FAR struct pcie_dev_s *pcie_dev;
  uint32_t num;

  if (ops == NULL)
    {
      pcieerr("(%s) ops paramter Error\n", __func__);
      return -EINVAL;
    }

  id = ops->pci_cfg_read(bdf, PCIE_CONF_ID);

  if (!PCIE_ID_IS_VALID(id))
    {
      pcieerr("PCI-E ID is valid\n");
      return 0;
    }

  type = ops->pci_cfg_read(bdf, PCIE_CONF_TYPE);
  switch (PCIE_CONF_TYPE_GET(type))
  {
    case PCIE_CONF_TYPE_STANDARD:
      pcie_dev = kmm_zalloc(sizeof(struct pcie_dev_s));
      if (pcie_dev == NULL)
        {
          return -ENOMEM;
        }

      pcie_dev->type = kmm_zalloc(sizeof(struct pcie_dev_type_s));
      if (pcie_dev->type == NULL)
        {
          kmm_free(pcie_dev);
          return -ENOMEM;
        }

      pcie_dev->type->vendor = PCIE_ID_TO_VEND(id);
      pcie_dev->type->device = PCIE_ID_TO_DEV(id);
      pcie_dev->bdf = bdf;
      pcie_dev->ops = ops;

      list_add_after(&g_pcie_dev_list, &pcie_dev->node);

      break;
    case PCIE_CONF_TYPE_PCI_BRIDGE:
      num = ops->pci_cfg_read(bdf, PCIE_BUS_NUMBER);
      secondary = PCIE_BUS_SECONDARY_NUMBER(num);

      if (pcie_scan_bus(ops, secondary) < 0)
        {
          pcieerr("PCI-E scan bus error\n");
          return -EINVAL;
        }

      break;
    default:
      break;
  }

  return 1;
}

/****************************************************************************
 * Name: pci_get_bar
 *
 * Description:
 *  This function is used to get a 32 bits bar.
 *
 * Input Parameters:
 *   dev       - Device private data
 *   bar_index - Bar number
 *   bar       - Bar Content
 *
 * Returned Value:
 *   0: success, <0: A negated errno
 *
 ****************************************************************************/

static bool pci_get_bar(FAR struct pcie_dev_s *dev,
                        unsigned int bar_index,
                        FAR struct pcie_bar *bar)
{
  size_t size;
  uintptr_t phys_addr;
  uint32_t reg = bar_index + PCIE_CONF_BAR0;

  if (dev == NULL)
    {
      pcieerr("(%s) invalid dev paramter\n", __func__);
      return false;
    }

  if (reg > PCIE_CONF_BAR5)
    {
      pcieerr("Invalid reg size\n");
      return false;
    }

  phys_addr = dev->ops->pci_cfg_read(dev->bdf, reg);

  dev->ops->pci_cfg_write(dev->bdf, reg, 0xffffffff);
  size = dev->ops->pci_cfg_read(dev->bdf, reg);
  dev->ops->pci_cfg_write(dev->bdf, reg, (uint32_t)phys_addr);

  if (PCIE_CONF_BAR_64(phys_addr))
    {
      reg++;
      phys_addr |= ((uint64_t)dev->ops->pci_cfg_read(dev->bdf, reg)) << 32;

      if (PCIE_CONF_BAR_ADDR(phys_addr) == PCIE_CONF_BAR_INVAL64 ||
          PCIE_CONF_BAR_ADDR(phys_addr) == PCIE_CONF_BAR_NONE)
        {
          /* Discard on invalid address */

          pcieerr("Invalid 64bits config BAR MEM address\n");
          return false;
        }

      dev->ops->pci_cfg_write(dev->bdf, reg, 0xffffffff);
      size |= ((uint64_t)dev->ops->pci_cfg_read(dev->bdf, reg)) << 32;
      dev->ops->pci_cfg_write(dev->bdf, reg,
                              (uint32_t)((uint64_t)phys_addr >> 32));
    }
  else if (PCIE_CONF_BAR_ADDR(phys_addr) == PCIE_CONF_BAR_INVAL ||
           PCIE_CONF_BAR_ADDR(phys_addr) == PCIE_CONF_BAR_NONE)
    {
      /* Discard on invalid address */

      pcieerr("Invalid 32bits config BAR MEM address\n");
      return false;
    }

  if (PCIE_CONF_BAR_IO(phys_addr))
    {
      size = PCIE_CONF_BAR_IO_ADDR(size);
      if (size == 0)
        {
          /* Discard on invalid size */

          pcieerr("Invalid config BAR IO address\n");
          return false;
        }
    }
  else
    {
      size = PCIE_CONF_BAR_ADDR(size);
      if (size == 0)
        {
          /* Discard on invalid size */

          pcieerr("Invalid config BAR address size\n");
          return false;
        }
    }

  /* Translate to physical memory address from bus address */

  if (!pcie_ctrl_region_translate(&g_nuttx_init_data, dev->bdf,
                  PCIE_CONF_BAR_MEM(phys_addr),
                  PCIE_CONF_BAR_64(phys_addr),
                  PCIE_CONF_BAR_MEM(phys_addr) ?
                  PCIE_CONF_BAR_ADDR(phys_addr)
                  : PCIE_CONF_BAR_IO_ADDR(phys_addr), &bar->phys_addr))
    {
      return false;
    }

  bar->size = size & ~(size - 1);

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: pcie_scan_bus
 *
 * Description:
 *   This function is used to scan pcie device and add pcie endpoint device
 * into list.
 *
 * Input Parameters:
 *   ops - Operation callback of the pcie contoller
 *   bus - Bus number
 *
 * Return Value:
 *   Return positive number if success, otherwise negative number.
 ****************************************************************************/

int pcie_scan_bus(FAR const struct pcie_bus_ops_s *ops, uint8_t bus)
{
  for (uint8_t dev = 0; dev <= PCIE_MAX_DEV; dev++)
    {
      if (pcie_scan_dev(ops, bus, dev) < 0)
        {
          pcieerr("pcie scan dev failed\n");
          return -EINVAL;
        }
    }

  return 1;
}

/****************************************************************************
 * Name: pci_enumerate
 *
 * Description:
 *   Scan the PCI bus and enumerate the devices.
 *   Initialize any recognized devices, given in types.
 *
 * Input Parameters:
 *   dev    - PCI-E Device private data
 *   types  - List of pointers to devices types recognized, NULL terminated
 *
 * Returned Value:
 *   0: success, <0: A negated errno
 *
 ****************************************************************************/

bool pci_lookup_dev(FAR struct pcie_dev_s *dev,
                    FAR struct pcie_dev_type_s **types)
{
  FAR struct pcie_dev_s *device;

  if (dev == NULL)
    {
      pcieerr("Invalid dev paramter\n");
      return false;
    }

  if (!types)
    {
      pcieerr("Invalid types paramter\n");
      return false;
    }

  list_for_every_entry(&g_pcie_dev_list, device, struct pcie_dev_s, node)
    {
      if (device->type->vendor == PCI_ID_ANY)
        {
          continue;
        }

      for (int i = 0; types[i] != NULL; i++)
        {
          if (types[i]->vendor == device->type->vendor)
            {
              if (types[i]->device == device->type->device)
                {
                  if (types[i]->class_rev == device->type->class_rev)
                    {
                      pcieinfo("The PCI-E device found\n");
                      return true;
                    }
                }
            }
        }
    }

  return false;
}

/****************************************************************************
 * Name: pcie_initialize
 *
 * Description:
 *  Initialize the PCI-E bus and enumerate the devices with give devices
 *  type array
 *
 * Input Parameters:
 *   dev    - PCI-E Device private data
 *   types  - A array of PCIE device types
 *
 * Returned Value:
 *   OK if the driver was successfully register; A negated errno value is
 *   returned on any failure.
 *
 ****************************************************************************/

int pcie_initialize(FAR struct pcie_dev_s *dev,
                    FAR struct pcie_dev_type_s **types)
{
  return pci_lookup_dev(dev, types);
}

/****************************************************************************
 * Name: pcie_set_cmd
 *
 * Description:
 *   This function performs the command/status regster settings,
 *   sunch as I/O Space Enable, Mem Space Enable, Bus Master Enable and so
 *   on.
 *
 * Input Parameters:
 *   ops  - Bus related operations that's related to controller
 *   bdf  - The PCI(e) endpoint
 *   bits - The bit operated in commond register
 *   on   - The bit value, 0 clear, 1 set
 *
 ****************************************************************************/

void pcie_set_cmd(FAR const struct pcie_bus_ops_s *ops, pcie_bdf_t bdf,
                  uint32_t bits, bool on)
{
  uint32_t cmdstat;

  if (ops == NULL)
    {
      pcieerr("(%s) ops paramter Error\n", __func__);
      return;
    }

  cmdstat = ops->pci_cfg_read(bdf, PCIE_CONF_CMDSTAT);

  if (on)
    {
      cmdstat |= bits;
    }
  else
    {
      cmdstat &= ~bits;
    }

  ops->pci_cfg_write(bdf, PCIE_CONF_CMDSTAT, cmdstat);
}

/****************************************************************************
 * Name: pci_find_cap
 *
 * Description:
 *   Search reg address through the PCI-E device capability list to find
 *  given capability.
 *
 * Input Parameters:
 *   dev    - PCI-E Device private data
 *   cap_id - Bitmask of capability
 *
 * Returned Value:
 *   < 0: Capability not supported
 *   other: the offset in PCI configuration space to the capability structure
 *
 ****************************************************************************/

int pcie_find_cap(FAR struct pcie_dev_s *dev, uint16_t cap_id)
{
  uint32_t data;
  uint32_t reg;

  if (dev == NULL)
    {
      pcieerr("(%s) invalid dev paramter\n", __func__);
      return -EINVAL;
    }

  /* Read type0/type1 status register */

  data = dev->ops->pci_cfg_read(dev->bdf, PCIE_CONF_CMDSTAT);

  /* Capabilities list bit in status register must be hardwired to 1b */

  if (!(data & PCIE_CONF_CMDSTAT_CAPS))
      return -EINVAL;

  /* Using this register as a pointer in Configuration Space to the first
   * entry of a linked list of new capabilities
   */

  data = dev->ops->pci_cfg_read(dev->bdf, PCIE_CONF_CAPPTR);

  /* The bottom two bits are Reserved and must be set to 00b */

  reg = PCIE_CONF_CAPPTR_FIRST(data);

  while (reg != 0)
    {
      data = dev->ops->pci_cfg_read(dev->bdf, reg);

      /* PCI Express Capability List Register, see 7.5.3.1.
       * Capability ID - Indicates the MSI Capability structure
       */

      if (PCIE_CONF_CAP_ID(data) == cap_id)
        {
          break;
        }

      /* iteration next cap pointer */

      reg = PCIE_CONF_CAP_NEXT(data);
    }

  return reg;
}

/****************************************************************************
 * Name: pcie_get_bar
 *
 * Description:
 *   Get value from a bar register.
 *
 * Input Parameters:
 *   dev       - PCI-E Device private data
 *   bar_index - 0-based BAR index
 *   mbar      - Pointer to struct pcie_bar
 *
 * Return Value:
 *   Return true if the BAR was found and is valid, false otherwise.
 *
 ****************************************************************************/

bool pcie_get_bar(FAR struct pcie_dev_s *dev, unsigned int bar_index,
                  FAR struct pcie_bar *mbar)
{
  return pci_get_bar(dev, bar_index, mbar);
}

/****************************************************************************
 * Name: pcie_probe_bar
 *
 * Description:
 *   Probe the nth BAR assigned to an endpoint.
 *   A PCI(e) endpoint has 0 or more BARs. This function allows the caller
 * with index=0..n.
 *   Value of n has to be below 6, as there is a maximum of 6 BARs. The
 * indices are order-preserving with respect to the endpoint BARs: e.g.,
 * index 0 will return the lowest-numbered BAR on the endpoint.
 *
 * Input Parameters:
 *   dev   - A endpoint device
 *   index - (0-based) index
 *   bar   - Pointer to struct pcie_bar
 *
 * Return Value:
 *   Return true if the BAR was found and is valid, false otherwise.
 *
 ****************************************************************************/

bool pcie_probe_bar(FAR struct pcie_dev_s *dev, unsigned int index,
                    FAR struct pcie_bar *bar)
{
  uint32_t reg;

  if (dev == NULL)
    {
      pcieerr("(%s) invalid dev paramter\n", __func__);
      return false;
    }

  for (reg = PCIE_CONF_BAR0; index > 0 && reg <= PCIE_CONF_BAR5;
       reg++, index--)
    {
      uintptr_t addr = dev->ops->pci_cfg_read(dev->bdf, reg);

      if (PCIE_CONF_BAR_MEM(addr) && PCIE_CONF_BAR_64(addr))
        {
          reg++;
        }
    }

  if (index != 0)
    {
      return false;
    }

  return pcie_get_bar(dev, reg - PCIE_CONF_BAR0, bar);
}

/****************************************************************************
 * Name: pcie_alloc_irq
 *
 * Description:
 *   To set a irq to a endpoint device.
 *
 * Input Parameters:
 *   dev - A endpoint device
 *   irq - Interrupt request number
 *
 * Returned Value:
 *   Return true if the BAR was found and is valid, false otherwise.
 *
 ****************************************************************************/

bool pcie_alloc_irq(FAR struct pcie_dev_s *dev, unsigned int irq)
{
  uint32_t data;

  if (dev == NULL)
    {
      pcieerr("(%s) invalid dev paramter\n", __func__);
      return false;
    }

  data = dev->ops->pci_cfg_read(dev->bdf, PCIE_CONF_INTR);
  irq = PCIE_CONF_INTR_IRQ(data);

  if (irq == PCIE_CONF_INTR_IRQ_NONE || irq >= CONFIG_MAX_IRQ_LINES)
    {
      return false;
    }

  data &= ~0xff;
  data |= irq;
  dev->ops->pci_cfg_write(dev->bdf, PCIE_CONF_INTR, data);

  return true;
}

/****************************************************************************
 * Name: pcie_get_irq
 *
 * Description:
 *   To get a endpoint device irq.
 *
 * Input Parameters:
 *   dev - An endpoint device private data
 *
 * Returned Value:
 *   Return a irq number if success
 *
 ****************************************************************************/

int pcie_get_irq(FAR struct pcie_dev_s *dev)
{
  uint32_t data;

  if (dev == NULL)
    {
      pcieerr("(%s) invalid dev paramter\n", __func__);
      return -EINVAL;
    }

  data = dev->ops->pci_cfg_read(dev->bdf, PCIE_CONF_INTR);

  return PCIE_CONF_INTR_IRQ(data);
}

/****************************************************************************
 * Name: pcie_connect_irq
 *
 * Description:
 *   Attach the interrupt handler, msi interrupt is not surrported.
 *
 * Input Parameters:
 *   dev       - A endpoint device
 *   irq       - Interruption request number
 *   priority  - The MSI vectors base interrupt priority
 *   handler   - Interrupt handler
 *   parameter - ISR parameter
 *   flags     - Arch-specific IRQ configuration flag
 *
 * Returned Value:
 *   Return true if the BAR was found and is valid, false otherwise.
 ****************************************************************************/

bool pcie_connect_irq(FAR struct pcie_dev_s *dev, unsigned int irq,
              unsigned int priority,
              xcpt_t handler,
              FAR void *parameter,
              uint32_t flags)
{
#if defined(CONFIG_PCIE_MSI)
  if (pcie_is_msi(dev))
    {
      msi_vector_t vector;

      if ((pcie_msi_vectors_allocate(dev, priority, &vector, 1) == 0) ||
           !pcie_msi_vector_connect(bdf, &vector, handler, parameter, flags))
        {
          return false;
        }
    }
  else
#endif
    {
      if (irq_attach(irq, handler, parameter) < 0)
        {
          return false;
        }
    }

  return true;
}

/****************************************************************************
 * Name: pcie_irq_enable
 *
 * Description:
 *   Enable the pcie endpoint device interruption.
 *
 * Input Parameters:
 *   dev - A endpoint device
 *   irq - interruption request number
 *
 ****************************************************************************/

void pcie_irq_enable(FAR struct pcie_dev_s *dev, unsigned int irq)
{
#if defined(CONFIG_PCIE_MSI)
  if (pcie_msi_enable(dev->bdf, NULL, 1, irq))
    {
      return;
    }
#endif

  up_enable_irq(irq);
}

/****************************************************************************
 * Name: pcie_register_driver
 ****************************************************************************/

int pcie_register_driver(FAR struct pcie_drv_s *driver)
{
  FAR struct pcie_dev_s *device;
  int ret;

  DEBUGASSERT(driver != NULL && driver->probe != NULL &&
              driver->remove != NULL);

  ret = nxmutex_lock(&g_pcie_driver_mutex);
  if (ret < 0)
    {
      return ret;
    }

  list_for_every_entry(&g_pcie_dev_list, device,
                       struct pcie_dev_s, node)
    {
      if (driver->device->type->vendor == device->type->vendor &&
          driver->device->type->device == device->type->device)
        {
          ret = driver->probe(device);
          if (ret > 0)
            {
              list_add_after(&g_pcie_driver_list, &driver->node);
            }
        }
    }

  nxmutex_unlock(&g_pcie_driver_mutex);
  return ret;
}

/****************************************************************************
 * Name: virtio_unregister_driver
 ****************************************************************************/

int pcie_unregister_driver(FAR struct pcie_drv_s *driver)
{
  FAR struct pcie_dev_s *device;
  int ret;

  DEBUGASSERT(driver != NULL && driver->probe != NULL &&
              driver->remove != NULL);

  ret = nxmutex_lock(&g_pcie_driver_mutex);
  if (ret < 0)
    {
      return ret;
    }

  list_for_every_entry(&g_pcie_dev_list, device,
                       struct pcie_dev_s, node)
    {
      if (driver->device->type->vendor == device->type->vendor &&
          driver->device->type->device == device->type->device)
        {
          driver->remove(device);
          list_delete(&driver->node);
          ret = 0;
        }
    }

  nxmutex_unlock(&g_pcie_driver_mutex);
  return ret;
}
