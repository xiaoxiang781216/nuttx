/****************************************************************************
 * include/nuttx/pcie/controller.h
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

#ifndef __INCLUDE_NUTTX_PCIE_CONTROLLER_H
#define __INCLUDE_NUTTX_PCIE_CONTROLLER_H

/****************************************************************************
 * Included Files
 ****************************************************************************/
#include <nuttx/pcie/pcie.h>

/****************************************************************************
 * Name: pcie_ctrl_region_allocate
 *
 * Description:
 *  Allocate a memory region subset for an endpoint Base Address Register.
 *
 *  When enumerating PCIe Endpoints, Type0 endpoints can require up to 6
 * memory zones via the Base Address Registers from I/O or Memory types.
 *
 * Input Parameters:
 *   ctrl_dev     - Address of PCIE controller private data
 *   bdf          - PCI(e) endpoint
 *   mem          - True if the BAR is of memory type
 *   mem64        - True if the BAR is of 64bit memory type
 *   bar_size     - Size in bytes of the Base Address Register as returned
 * by HW
 *   bar_bus_addr - bus-centric address allocated to be written in the BAR
 * register
 *
 * Returned Value:
 * True if allocation was possible, False if allocation failed
 ****************************************************************************/

static inline bool pcie_ctrl_region_allocate(
                                      FAR struct pcie_ctrl_dev *ctrl_dev,
                                      pcie_bdf_t bdf,
                                      bool mem, bool mem64, size_t bar_size,
                                      FAR uintptr_t *bar_bus_addr)
{
  if (!ctrl_dev->ops->region_allocate)
    {
      return false;
    }

  return ctrl_dev->ops->region_allocate(ctrl_dev->ops, bdf, mem, mem64,
                                        bar_size, bar_bus_addr);
}

/****************************************************************************
 * Name: pci_map_bar
 *
 * Description:
 *  Map address in the memory address space
 *
 * Input Parameters:
 *   ctrl_dev     - Address of PCIE controller private data
 *   bdf          - PCI(e) endpoint
 *   mem          - True if the BAR is of memory type
 *   mem64        - True if the BAR is of 64bit memory type
 *   bar_size     - Size in bytes of the Base Address Register as returned
 * by HW
 *   bar_bus_addr - Bus-centric address allocated to be written in the BAR
 * register
 *
 * Returned Value:
 *   0: success, <0: A negated errno
 *
 ****************************************************************************/

static inline bool pcie_ctrl_region_translate(
                              FAR struct pcie_ctrl_dev *ctrl_dev,
                              pcie_bdf_t bdf,
                              bool mem, bool mem64,
                              uintptr_t bar_bus_addr,
                              FAR uintptr_t *bar_addr)
{
  if (!ctrl_dev->ops->region_translate)
    {
      *bar_addr = bar_bus_addr;
      return true;
    }
  else
    {
      return ctrl_dev->ops->region_translate(ctrl_dev, bdf, mem, mem64,
                                   bar_bus_addr, bar_addr);
    }
}

/****************************************************************************
 * Name: pcie_ctrl_region_get_allocate_base
 *
 * Description:
 *   Function called to get the current allocation base of a memory region
 * subset for an endpoint Base Address Register.
 *
 *   When enumerating PCIe Endpoints, Type1 bridge endpoints requires a range
 * of memory allocated by all endpoints in the bridged bus.
 *
 * Input Parameters:
 *   ctrl_dev     - Address of PCIE controller private data
 *   bdf           - PCI(e) endpoint
 *   mem           - True if the BAR is of memory type
 *   mem64         - True if the BAR is of 64bit memory type
 *   align         - Size to take in account for alignment
 *   bar_base_addr - Bus-centric address allocation base
 *
 * Returned Value:
 *   True if allocation was possible, False if allocation failed
 ****************************************************************************/

static inline bool pcie_ctrl_region_get_allocate_base(
                       FAR struct pcie_ctrl_dev *ctrl_dev,
                       pcie_bdf_t bdf,
                       bool mem, bool mem64, size_t align,
                       FAR uintptr_t *bar_base_addr)
{
  if (!ctrl_dev->ops->region_get_allocate_base)
    {
      return false;
    }
  return ctrl_dev->ops->region_get_allocate_base(ctrl_dev, bdf, mem, mem64,
                                       align, bar_base_addr);
}

#endif /* __INCLUDE_NUTTX_PCIE_CONTROLLER_H */