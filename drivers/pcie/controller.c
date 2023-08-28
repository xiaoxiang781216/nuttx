/****************************************************************************
 * drivers/pcie/controller.c
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

#include <nuttx/pcie/pcie.h>
#include <nuttx/pcie/controller.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MAX_TRAVERSE_STACK 256

struct pcie_ctrl_dev g_nuttx_init_data;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: pcie_generic_ctrl_enumerate_bars
 *
 * Description:
 *   This function is used to enumerate the Base Address Registers (BARs) of
 * a PCIe device.
 *
 * Input Parameters:
 *   ctrl_dev  - Address of PCIE controller private data
 *   bdf       - PCI(e) endpoint
 *   nbars     - Number of enumerating the Base Address Registers
 *
 ****************************************************************************/

static void pcie_generic_ctrl_enumerate_bars(
                                    FAR struct pcie_ctrl_dev *ctrl_dev,
                                    pcie_bdf_t bdf,
                                    unsigned int nbars)
{
  unsigned int bar;
  unsigned int reg;
  unsigned int data;
  uintptr_t scratch;
  uintptr_t bar_bus_addr;
  size_t size;
  size_t bar_size;
  bool found_mem64 = false;
  bool found_mem = false;
  uintptr_t bar_phys_addr;

  if (ctrl_dev == NULL || ctrl_dev->ops == NULL || ctrl_dev->data == NULL)
    {
      pcieerr("(%s) paramters Error\n", __func__);
      return;
    }

  for (bar = 0, reg = PCIE_CONF_BAR0; bar < nbars && reg <= PCIE_CONF_BAR5;
       reg ++, bar++)
    {
      scratch = ctrl_dev->ops->pci_cfg_read(bdf, reg);
      data = scratch;

      /* reserved bit 010 or bit 110 */

      if (PCIE_CONF_BAR_INVAL_FLAGS(data))
        {
          continue;
        }

      if (PCIE_CONF_BAR_MEM(data))
        {
          found_mem = true;
          if (PCIE_CONF_BAR_64(data))
            {
              found_mem64 = true;
              scratch |= ((uint64_t)ctrl_dev->ops->pci_cfg_read(bdf, \
                                                         reg + 1)) << 32;
              if (PCIE_CONF_BAR_ADDR(scratch) == PCIE_CONF_BAR_INVAL64)
                {
                  continue;
                }
            }
          else
            {
              if (PCIE_CONF_BAR_ADDR(scratch) == PCIE_CONF_BAR_INVAL)
                {
                  continue;
                }
            }
        }

      ctrl_dev->ops->pci_cfg_write(bdf, reg, 0xffffffff);
      size = ctrl_dev->ops->pci_cfg_read(bdf, reg);
      ctrl_dev->ops->pci_cfg_write(bdf, reg, scratch & 0xffffffff);

      if (found_mem64)
        {
          ctrl_dev->ops->pci_cfg_write(bdf, reg + 1, 0xffffffff);
          size |= ((uint64_t)ctrl_dev->ops->pci_cfg_read(bdf, \
                                                    reg + 1)) << 32;
          ctrl_dev->ops->pci_cfg_write(bdf, reg + 1, scratch >> 32);
        }

      if (!PCIE_CONF_BAR_ADDR(size))
        {
          if (found_mem64)
            {
              reg++;
            }

          continue;
        }

      if (found_mem)
        {
          if (found_mem64)
            {
              bar_size = (uint64_t)~PCIE_CONF_BAR_ADDR(size) + 1;
            }
          else
            {
              bar_size = (uint32_t)~PCIE_CONF_BAR_ADDR(size) + 1;
            }
        }
      else
        {
          bar_size = (uint32_t)~PCIE_CONF_BAR_IO_ADDR(size) + 1;
        }

      if (pcie_ctrl_region_allocate(ctrl_dev, bdf, found_mem,
                found_mem64, bar_size, &bar_bus_addr))
        {
          pcie_ctrl_region_translate(ctrl_dev, bdf, found_mem,
                 found_mem64, bar_bus_addr, &bar_phys_addr);

          ctrl_dev->ops->pci_cfg_write(bdf, reg, bar_bus_addr & 0xffffffff);
          if (found_mem64)
            {
              ctrl_dev->ops->pci_cfg_write(bdf, reg + 1, bar_bus_addr >> 32);
            }
        }

      if (found_mem64)
        {
          reg++;
        }
    }
}

/****************************************************************************
 * Name: pcie_generic_ctrl_enumerate_type1
 *
 * Description:
 *   This function is used to enumerate configuration space of a type1 PCIe
 * device.
 *
 * Input Parameters:
 *   ctrl_dev   - Address of PCIE controller private data
 *   bdf        - PCI(e) endpoint
 *   bus_number - secondary bus number
 *
 * Returned Value:
 *   True if enumerate configuration space success, False if failed
 ****************************************************************************/

static bool
pcie_generic_ctrl_enumerate_type1(FAR struct pcie_ctrl_dev *ctrl_dev,
                                  pcie_bdf_t bdf,
                                  unsigned int bus_number)
{
  uintptr_t bar_base_addr;
  uint32_t io;
  uint32_t io_upper;
  uint32_t mem;
  uint32_t class;
  uint32_t number;

  if (ctrl_dev == NULL || ctrl_dev->ops == NULL || ctrl_dev->data == NULL)
    {
      pcieerr("(%s) ops paramter Error\n", __func__);
      return false;
    }

  class = ctrl_dev->ops->pci_cfg_read(bdf, PCIE_CONF_CLASSREV);
  number = ctrl_dev->ops->pci_cfg_read(bdf, PCIE_BUS_NUMBER);

  /* Handle only PCI-to-PCI bridge for now */

  if (PCIE_CONF_CLASSREV_CLASS(class) == PCI_CLASS_CODE_BRIDGE_DEV &&
      PCIE_CONF_CLASSREV_SUBCLASS(class) == PCI_SUBCLASS_CODE_BRIDGE_DEV)
    {
      pcie_generic_ctrl_enumerate_bars(ctrl_dev, bdf, 2);

      /* Configure bus number registers */

      ctrl_dev->ops->pci_cfg_write(bdf, PCIE_BUS_NUMBER,
                    PCIE_BUS_NUMBER_VAL(PCIE_BDF_TO_BUS(bdf),
                    bus_number,
                    0xff, /* set max until we finished scanning */
                    PCIE_SECONDARY_LATENCY_TIMER(number)));

      /* I/O align on 4k boundary */

      if (pcie_ctrl_region_get_allocate_base(ctrl_dev, bdf,
                                           false, false, 1024 * 4,
                                           &bar_base_addr))
        {
          io = ctrl_dev->ops->pci_cfg_read(bdf, PCIE_IO_SEC_STATUS);
          io_upper = ctrl_dev->ops->pci_cfg_read(bdf, \
                                                 PCIE_IO_BASE_LIMIT_UPPER);

          ctrl_dev->ops->pci_cfg_write(bdf, PCIE_IO_SEC_STATUS,
                        PCIE_IO_SEC_STATUS_VAL(PCIE_IO_BASE(io),
                        PCIE_IO_LIMIT(io),
                        PCIE_SEC_STATUS(io)));

          ctrl_dev->ops->pci_cfg_write(bdf, PCIE_IO_BASE_LIMIT_UPPER,
               PCIE_IO_BASE_LIMIT_UPPER_VAL(PCIE_IO_BASE_UPPER(io_upper),
                  PCIE_IO_LIMIT_UPPER(io_upper)));

          pcie_set_cmd(ctrl_dev->ops, bdf, PCIE_CONF_CMDSTAT_IO, true);
        }

      /* MEM align on 1MiB boundary */

      if (pcie_ctrl_region_get_allocate_base(ctrl_dev, bdf, true, false,
                                           1024 * 1024, &bar_base_addr))
        {
          mem = ctrl_dev->ops->pci_cfg_read(bdf, PCIE_MEM_BASE_LIMIT);

          ctrl_dev->ops->pci_cfg_write(bdf, PCIE_MEM_BASE_LIMIT,
                PCIE_MEM_BASE_LIMIT_VAL((bar_base_addr & 0xfff00000) >> 16,
                  PCIE_MEM_LIMIT(mem)));

          pcie_set_cmd(ctrl_dev->ops, bdf, PCIE_CONF_CMDSTAT_MEM, true);
        }

      pcie_set_cmd(ctrl_dev->ops, bdf, PCIE_CONF_CMDSTAT_MASTER, true);

      return true;
    }

  return false;
}

/****************************************************************************
 * Name: pcie_generic_ctrl_enumerate_type0
 *
 * Description:
 *   This function is used to enumerate configuration space of a type0 PCIe
 * device.
 *
 * Input Parameters:
 *   ctrl_dev  - Address of PCIE controller private data
 *   bdf       - PCI(e) endpoint
 *
 ****************************************************************************/

static void
pcie_generic_ctrl_enumerate_type0(FAR struct pcie_ctrl_dev *ctrl_dev,
                                  pcie_bdf_t bdf)
{
  /* Setup Type0 BARs */

  pcie_generic_ctrl_enumerate_bars(ctrl_dev, bdf, 6);
}

/****************************************************************************
 * Name: pcie_generic_ctrl_post_enumerate_type1
 ****************************************************************************/

static void
pcie_generic_ctrl_post_enumerate_type1(FAR struct pcie_ctrl_dev *ctrl_dev,
                                       pcie_bdf_t bdf,
                                       unsigned int bus_number)
{
  uint32_t io;
  uint32_t mem;
  uint32_t io_upper;
  uintptr_t bar_base_addr;

  if (ctrl_dev == NULL || ctrl_dev->ops == NULL || ctrl_dev->data == NULL)
    {
      pcieerr("(%s) ops paramter Error\n", __func__);
      return;
    }

  /* Type 1 Header has files related to bus management */

  uint32_t number = ctrl_dev->ops->pci_cfg_read(bdf, PCIE_BUS_NUMBER);

  /* Configure bus subordinate */

  ctrl_dev->ops->pci_cfg_write(bdf, PCIE_BUS_NUMBER,
                  PCIE_BUS_NUMBER_VAL(PCIE_BUS_PRIMARY_NUMBER(number),
                                      PCIE_BUS_SECONDARY_NUMBER(number),
                                      bus_number - 1,
                                      PCIE_SECONDARY_LATENCY_TIMER(number)));

  /* I/O align on 4k boundary */

  if (pcie_ctrl_region_get_allocate_base(ctrl_dev, bdf, false, false,
                                         1024 * 4, &bar_base_addr))
    {
      io = ctrl_dev->ops->pci_cfg_read(bdf, PCIE_IO_SEC_STATUS);
      io_upper = ctrl_dev->ops->pci_cfg_read(bdf, PCIE_IO_BASE_LIMIT_UPPER);

      ctrl_dev->ops->pci_cfg_write(bdf, PCIE_IO_SEC_STATUS,
                      PCIE_IO_SEC_STATUS_VAL(PCIE_IO_BASE(io),
                                ((bar_base_addr - 1) & 0x0000f000) >> 16,
                                   PCIE_SEC_STATUS(io)));

      ctrl_dev->ops->pci_cfg_write(bdf, PCIE_IO_BASE_LIMIT_UPPER,
                PCIE_IO_BASE_LIMIT_UPPER_VAL(PCIE_IO_BASE_UPPER(io_upper),
                            ((bar_base_addr - 1) & 0xffff0000) >> 16));
    }

  /* MEM align on 1MiB boundary */

  if (pcie_ctrl_region_get_allocate_base(ctrl_dev, bdf, true, false,
                                         1024 * 1024, &bar_base_addr))
    {
      mem = ctrl_dev->ops->pci_cfg_read(bdf, PCIE_MEM_BASE_LIMIT);

      ctrl_dev->ops->pci_cfg_write(bdf, PCIE_MEM_BASE_LIMIT,
                         PCIE_MEM_BASE_LIMIT_VAL(PCIE_MEM_BASE(mem),
                                            (bar_base_addr - 1) >> 16));
    }
}

/****************************************************************************
 * Name: pcie_generic_ctrl_enumerate_endpoint
 *
 * Description:
 *   This function is used to enumerate all PCI-E endpoint in PCI-E tree.
 *
 * Input Parameters:
 *   ctrl_dev        - Address of PCIE controller private data
 *   bus_number      - Secondary bus number
 *   bdf             - The current bridge PCIE device bdf
 *   skip_next_func  - Don't enumerate sub-functions if not a multifunction
 * device
 *
 * Returned Value:
 *   True if success, False if failed
 ****************************************************************************/

static bool
pcie_generic_ctrl_enumerate_endpoint(FAR struct pcie_ctrl_dev *ctrl_dev,
                                     unsigned int bus_number,
                                     pcie_bdf_t bdf,
                                     FAR bool *skip_next_func)
{
  bool multifunction_device = false;
  bool layout_type_1 = false;
  uint32_t data;
  uint32_t class;
  uint32_t id;
  bool is_bridge = false;

  if (ctrl_dev == NULL || ctrl_dev->ops == NULL || ctrl_dev->data == NULL)
    {
      pcieerr("(%s) ops paramter Error\n", __func__);
      return false;
    }

  *skip_next_func = false;

  id = ctrl_dev->ops->pci_cfg_read(bdf, PCIE_CONF_ID);
  if (id == PCIE_ID_NONE)
    {
      return false;
    }

  class = ctrl_dev->ops->pci_cfg_read(bdf, PCIE_CONF_CLASSREV);
  data = ctrl_dev->ops->pci_cfg_read(bdf, PCIE_CONF_TYPE);

  multifunction_device = PCIE_CONF_MULTIFUNCTION(data);
  layout_type_1 = PCIE_CONF_TYPE_BRIDGE(data);

  /* Do not enumerate sub-functions if not a multifunction device */

  if (PCIE_BDF_TO_FUNC(bdf) == 0 && !multifunction_device)
    {
      *skip_next_func = true;
    }

  if (layout_type_1)
    {
      is_bridge = pcie_generic_ctrl_enumerate_type1(ctrl_dev, bdf,
                                                    bus_number);
    }
  else
    {
      pcie_generic_ctrl_enumerate_type0(ctrl_dev, bdf);
    }

  return is_bridge;
}

/****************************************************************************
 * Name: pcie_bdf_bus_next
 *
 * Description:
 *   Return the next BDF or PCIE_BDF_NONE without changing bus number.
 *
 * Input Parameters:
 *   bdf            - Secondary bus number
 *   skip_next_func - Don't enumerate sub-functions if not a multifunction
 * device
 *
 * Returned Value:
 *   Return the next BDF or PCIE_BDF_NONE without changing bus number.
 ****************************************************************************/

static inline unsigned int pcie_bdf_bus_next(unsigned int bdf,
                                             bool skip_next_func)
{
  if (skip_next_func)
    {
      if (PCIE_BDF_TO_DEV(bdf) == PCIE_BDF_DEV_MASK)
        {
          return PCIE_BDF_NONE;
        }

      return PCIE_BDF(PCIE_BDF_TO_BUS(bdf), PCIE_BDF_TO_DEV(bdf) + 1, 0);
    }

  if (PCIE_BDF_TO_DEV(bdf) == PCIE_BDF_DEV_MASK &&
      PCIE_BDF_TO_FUNC(bdf) == PCIE_BDF_FUNC_MASK)
    {
      return PCIE_BDF_NONE;
    }

  return PCIE_BDF(PCIE_BDF_TO_BUS(bdf),
      (PCIE_BDF_TO_DEV(bdf) +
      ((PCIE_BDF_TO_FUNC(bdf) + 1) / (PCIE_BDF_FUNC_MASK + 1))),
      ((PCIE_BDF_TO_FUNC(bdf) + 1) & PCIE_BDF_FUNC_MASK));
}

/****************************************************************************
 * Name: pcie_generic_ctrl_enumerate
 *
 * Description:
 *   Non-recursive stack based PCIe bus & bridge enumeration.
 *
 * Input Parameters:
 *   ops       - Operation callback of the pcie contoller
 *   ctrl_dev  - Address of PCIE controller private data
 *   bdf_start - PCI(e) start endpoint (only bus & dev are used to start
 * enumeration)
 *
 ****************************************************************************/

static void pcie_generic_ctrl_enumerate(FAR struct pcie_ctrl_dev *ctrl_dev,
                                        pcie_bdf_t bdf_start)
{
  int stack_top = 0;
  FAR struct pcie_bus_state *state;
  struct pcie_bus_state stack[MAX_TRAVERSE_STACK];
  unsigned int bus_number = PCIE_BDF_TO_BUS(bdf_start) + 1;

  pcieinfo("Begin the pcie generic enumeration\n");

  if (ctrl_dev == NULL || ctrl_dev->ops == NULL || ctrl_dev->data == NULL)
    {
      pcieerr("(%s) paramters invalid\n", __func__);
      return;
    }

  /* Start with first endpoint of immediate Root Controller bus */

  stack[stack_top].bus_bdf = PCIE_BDF(PCIE_BDF_TO_BUS(bdf_start), 0, 0);
  stack[stack_top].bridge_bdf = PCIE_BDF_NONE;
  stack[stack_top].next_bdf = bdf_start;

  while (stack_top >= 0)
    {
      /* Top of stack contains the current PCIe bus to traverse */

      state = &stack[stack_top];

      /* Finish current bridge configuration before scanning other
       * endpoints
       */

      if (state->bridge_bdf != PCIE_BDF_NONE)
        {
          pcie_generic_ctrl_post_enumerate_type1(ctrl_dev,
                                                 state->bridge_bdf,
                                                 bus_number);

          state->bridge_bdf = PCIE_BDF_NONE;
        }

      /* We still have more endpoints to scan */

      if (state->next_bdf != PCIE_BDF_NONE)
        {
          while (state->next_bdf != PCIE_BDF_NONE)
            {
              bool is_bridge = false;
              bool skip_next_func = false;
              is_bridge = pcie_generic_ctrl_enumerate_endpoint(ctrl_dev,
                                                state->next_bdf,
                                                bus_number,
                                                &skip_next_func);
              if (is_bridge)
                {
                  state->bridge_bdf = state->next_bdf;
                  state->next_bdf = pcie_bdf_bus_next(state->next_bdf,
                                                  skip_next_func);

                  /* If we can't handle more bridges, don't go further */

                  if (stack_top == (MAX_TRAVERSE_STACK - 1) ||
                        bus_number == PCIE_BDF_BUS_MASK)
                    {
                      break;
                    }

                  /* Push to stack to scan this bus */

                  stack_top++;
                  stack[stack_top].bus_bdf = PCIE_BDF(bus_number, 0, 0);
                  stack[stack_top].bridge_bdf = PCIE_BDF_NONE;
                  stack[stack_top].next_bdf = PCIE_BDF(bus_number, 0, 0);

                  /* Increase bus number */

                  bus_number++;

                  break;
                }

              state->next_bdf = pcie_bdf_bus_next(state->next_bdf,
                                              skip_next_func);
            }
        }
      else
        {
          /* We finished scanning this bus, go back and scan next endpoints */

          stack_top--;
        }
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: pcie_boot_init
 *
 * Description:
 *   Firstly enumerate all endponits and bridge in system boot stage, and
 *   then scan the pcie tree to add all endpoint device to list.
 *
 * Input Parameters:
 *   ctrl_dev - Address of PCIE controller private data
 *
 ****************************************************************************/

void pcie_boot_init(FAR struct pcie_ctrl_dev *ctrl_dev)
{
  if (ctrl_dev == NULL ||  ctrl_dev->ops == NULL || ctrl_dev->data == NULL)
    {
      return;
    }

  /* storage PCIE configuration space data to local global var */

  g_nuttx_init_data = *ctrl_dev;

  /* begin to enumerate PCIE bus tree */

  pcie_generic_ctrl_enumerate(ctrl_dev, PCIE_BDF(0, 0, 0));
  pcie_scan_bus(ctrl_dev->ops, 0);
}
