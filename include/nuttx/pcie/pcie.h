/****************************************************************************
 * include/nuttx/pcie/pcie.h
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

#ifndef __INCLUDE_NUTTX_PCIE_PCIE_H
#define __INCLUDE_NUTTX_PCIE_PCIE_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>
#include <stdint.h>

#include <nuttx/fs/ioctl.h>
#include <nuttx/list.h>
#include <nuttx/mutex.h>
#include <nuttx/pcie/msi.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Invalid PCIe BDF */

#define PCIE_BDF_NONE 0xFFFFFFFFU

/* PCIe ID Operation Macro **************************************************/

#define PCI_ID_ANY    0xffff

/* Helper macro to exclude invalid PCIe identifiers. We should really only
 * need to look for PCIE_ID_NONE, but because of some broken PCI host
 * controllers we have try cases where both VID & DID are zero or just one
 * of them is zero (0x0000) and the other is all ones (0xFFFF).
 */

#define PCIE_ID_IS_VALID(id) ((id != PCIE_ID_NONE) && \
   (id != PCIE_ID(0x0000, 0x0000)) &&        \
   (id != PCIE_ID(0xFFFF, 0x0000)) &&        \
   (id != PCIE_ID(0x0000, 0xFFFF)))

#define PCIE_ID_TO_VEND(id) (((id) >> PCIE_ID_VEND_SHIFT) & PCIE_ID_VEND_MASK)
#define PCIE_ID_TO_DEV(id)  (((id) >> PCIE_ID_DEV_SHIFT) & PCIE_ID_DEV_MASK)

#define PCIE_ID_NONE PCIE_ID(0xFFFF, 0xFFFF)

/* We represent a PCI device ID as [31:16] device ID, [15:0] vendor ID. Not
 * coincidentally, this is same representation used in PCI configuration
 * space.
 */

#define PCIE_ID_VEND_SHIFT  0U
#define PCIE_ID_VEND_MASK   0xFFFFU
#define PCIE_ID_DEV_SHIFT   16U
#define PCIE_ID_DEV_MASK    0xFFFFU

#define PCIE_ID(vend, dev) \
   ((((vend) & PCIE_ID_VEND_MASK) << PCIE_ID_VEND_SHIFT) | \
   (((dev) & PCIE_ID_DEV_MASK) << PCIE_ID_DEV_SHIFT))

/* PCIe capabilities Macro **************************************************/

/* Configuration word 13 contains the head of the capabilities list.
 *
 * capabilities pointer
 */

#define PCIE_CONF_CAPPTR          13U

/* The bottom two bits are Reserved and must be set to 00b */

#define PCIE_CONF_CAPPTR_FIRST(w) (((w) >> 2) & 0x3FU)

/* The first word of every capability contains a capability identifier,
 * and a link to the next capability (or 0) in configuration space.
 */

#define PCIE_CONF_CAP_ID(w)       ((w) & 0xFFU)
#define PCIE_CONF_CAP_NEXT(w)     (((w) >> 10) & 0x3FU)

/* PCIe Common Configuration Macro ******************************************/

/* Configuration word 0 aligns directly with pcie_id_t. */

#define PCIE_CONF_ID    0U

/* Configuration word 1 contains command and status bits. */

/* command/status register */
#define PCIE_CONF_CMDSTAT  1U

/* I/O access enable */

#define PCIE_CONF_CMDSTAT_IO        0x00000001U

/* mem access enable */

#define PCIE_CONF_CMDSTAT_MEM       0x00000002U

/* bus master enable */

#define PCIE_CONF_CMDSTAT_MASTER    0x00000004U

/* interrupt status */

#define PCIE_CONF_CMDSTAT_INTERRUPT 0x00080000U

/* capabilities list */

#define PCIE_CONF_CMDSTAT_CAPS      0x00100000U

/* Configuration word 2 has additional function identification that
 * we only care about for debug output (PCIe shell commands).
 */

/* class/revision register */

#define PCIE_CONF_CLASSREV             2U
#define PCIE_CONF_CLASSREV_CLASS(w)    (((w) >> 24) & 0xFFU)
#define PCIE_CONF_CLASSREV_SUBCLASS(w) (((w) >> 16) & 0xFFU)
#define PCIE_CONF_CLASSREV_PROGIF(w)   (((w) >> 8) & 0xFFU)
#define PCIE_CONF_CLASSREV_REV(w)      ((w) & 0xFFU)

/* The only part of configuration word 3 that is of interest to us is
 * the header type, as we use it to distinguish functional endpoints
 * from bridges (which are, for our purposes, transparent).
 */

#define PCIE_CONF_TYPE    3U
#define PCIE_CONF_TYPE_GET(w)          (((w) >> 16) & 0x7F)
#define PCIE_CONF_MULTIFUNCTION(w)     (((w) & 0x00800000U) != 0U)
#define PCIE_CONF_TYPE_BRIDGE(w)       (((w) & 0x007F0000U) != 0U)

#define PCIE_CONF_TYPE_STANDARD        0x0U
#define PCIE_CONF_TYPE_PCI_BRIDGE      0x1U
#define PCIE_CONF_TYPE_CARDBUS_BRIDGE  0x2U

/* Words 4-9 are BARs are I/O or memory decoders. Memory decoders may
 * be 64-bit decoders, in which case the next configuration word holds
 * the high-order bits (and is, thus, not a BAR itself).
 */
#define PCIE_CONF_BAR0    4U
#define PCIE_CONF_BAR1    5U
#define PCIE_CONF_BAR2    6U
#define PCIE_CONF_BAR3    7U
#define PCIE_CONF_BAR4    8U
#define PCIE_CONF_BAR5    9U

/* Base Address registers that map to I/O Space must return a 1b in bit 0
 * (see Figure 7-12 )
 * 00 - 32bit
 * 10 - 64bit
 */
#define PCIE_CONF_BAR_IO(w)      (((w) & 0x00000001U) == 0x00000001U)
#define PCIE_CONF_BAR_MEM(w)     (((w) & 0x00000001U) != 0x00000001U)
#define PCIE_CONF_BAR_64(w)      (((w) & 0x00000006U) == 0x00000004U)
#define PCIE_CONF_BAR_ADDR(w)    ((w) & ~0xfUL)
#define PCIE_CONF_BAR_IO_ADDR(w) ((w) & ~0x3UL)
#define PCIE_CONF_BAR_FLAGS(w)   ((w) & 0xfUL)
#define PCIE_CONF_BAR_NONE    0U

#define PCIE_CONF_BAR_INVAL      0xFFFFFFF0U
#define PCIE_CONF_BAR_INVAL64    0xFFFFFFFFFFFFFFF0UL

#define PCIE_CONF_BAR_INVAL_FLAGS(w)         \
   ((((w) & 0x00000006U) == 0x00000006U) ||  \
   (((w) & 0x00000006U) == 0x00000002U))

/* PCIe Type 1  Header Macro ************************************************/

/* Type 1 Header has files related to bus management
 */
#define PCIE_BUS_NUMBER                 6U
#define PCIE_BUS_PRIMARY_NUMBER(w)      ((w) & 0xffUL)
#define PCIE_BUS_SECONDARY_NUMBER(w)    (((w) >> 8) & 0xffUL)
#define PCIE_BUS_SUBORDINATE_NUMBER(w)  (((w) >> 16) & 0xffUL)
#define PCIE_SECONDARY_LATENCY_TIMER(w) (((w) >> 24) & 0xffUL)

#define PCIE_BUS_NUMBER_VAL(prim, sec, sub, lat) \
   (((prim) & 0xffUL) |          \
   (((sec) & 0xffUL) << 8) |     \
   (((sub) & 0xffUL) << 16) |    \
   (((lat) & 0xffUL) << 24))

/* PCIe Type 1  Memory base and limits Macro ********************************/

/* Type 1 words 7 to 12 setups Bridge Memory base and limits
 */
#define PCIE_IO_SEC_STATUS      7U

#define PCIE_IO_BASE(w)         ((w) & 0xffUL)
#define PCIE_IO_LIMIT(w)        (((w) >> 8) & 0xffUL)
#define PCIE_SEC_STATUS(w)      (((w) >> 16) & 0xffffUL)

#define PCIE_IO_SEC_STATUS_VAL(iob, iol, sec_status) \
   (((iob) & 0xffUL) |           \
   (((iol) & 0xffUL) << 8) |     \
   (((sec_status) & 0xffffUL) << 16))

#define PCIE_MEM_BASE_LIMIT     8U

#define PCIE_MEM_BASE(w)        ((w) & 0xffffUL)
#define PCIE_MEM_LIMIT(w)       (((w) >> 16) & 0xffffUL)

#define PCIE_MEM_BASE_LIMIT_VAL(memb, meml) \
   (((memb) & 0xffffUL) |        \
   (((meml) & 0xffffUL) << 16))

#define PCIE_IO_BASE_LIMIT_UPPER 12U

#define PCIE_IO_BASE_UPPER(w)    ((w) & 0xffffUL)
#define PCIE_IO_LIMIT_UPPER(w)   (((w) >> 16) & 0xffffUL)

#define PCIE_IO_BASE_LIMIT_UPPER_VAL(iobu, iolu) \
   (((iobu) & 0xffffUL) |       \
   (((iolu) & 0xffffUL) << 16))

/* PCIe interrupt Macro *****************************************************/

/* Word 15 contains information related to interrupts.
 *
 * We're only interested in the low byte, which is [supposed to be] set by
 * the firmware to indicate which wire IRQ the device interrupt is routed to.
 */

#define PCIE_CONF_INTR    15U

/* no interrupt routed */

#define PCIE_CONF_INTR_IRQ_NONE  0xFFU
#define PCIE_CONF_INTR_IRQ(w)  ((w) & 0xFFU)

/* PCIe BDF(bus, device, function) Macro ************************************/

#define PCIE_MAX_BUS  (0xFFFFFFFFU & PCIE_BDF_BUS_MASK)
#define PCIE_MAX_DEV  (0xFFFFFFFFU & PCIE_BDF_DEV_MASK)
#define PCIE_MAX_FUNC (0xFFFFFFFFU & PCIE_BDF_FUNC_MASK)

/* typedef pcie_bdf_t
 * brief A unique PCI(e) endpoint (bus, device, function).
 *
 * A PCI(e) endpoint is uniquely identified topologically using a
 * (bus, device, function) tuple. The internal structure is documented
 * in include/dt-bindings/pcie/pcie.h: see PCIE_BDF() and friends, since
 * these tuples are referenced from devicetree.
 */

#define PCIE_BDF_BUS_SHIFT  16U
#define PCIE_BDF_BUS_MASK   0xFFU
#define PCIE_BDF_DEV_SHIFT  11U
#define PCIE_BDF_DEV_MASK   0x1FU
#define PCIE_BDF_FUNC_SHIFT 8U
#define PCIE_BDF_FUNC_MASK  0x7U

#define PCIE_BDF(bus, dev, func) \
   ((((bus) & PCIE_BDF_BUS_MASK) << PCIE_BDF_BUS_SHIFT) | \
   (((dev) & PCIE_BDF_DEV_MASK) << PCIE_BDF_DEV_SHIFT) | \
   (((func) & PCIE_BDF_FUNC_MASK) << PCIE_BDF_FUNC_SHIFT))

#define PCIE_BDF_TO_BUS(bdf) \
   (((bdf) >> PCIE_BDF_BUS_SHIFT) & PCIE_BDF_BUS_MASK)

#define PCIE_BDF_TO_DEV(bdf) \
   (((bdf) >> PCIE_BDF_DEV_SHIFT) & PCIE_BDF_DEV_MASK)

#define PCIE_BDF_TO_FUNC(bdf) \
   (((bdf) >> PCIE_BDF_FUNC_SHIFT) & PCIE_BDF_FUNC_MASK)

/* Class code encodings and corresponding Sub-class code encodings */

#define PCI_CLASS_CODE_BRIDGE_DEV    0x06U
#define PCI_SUBCLASS_CODE_BRIDGE_DEV 0x04U

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Structure describing a device that supports the PCI Express Controller API
 * cfg_addr: Configuration space address
 * cfg_size: Configuration space size
 * bus_start: bus-centric offset from the start of the region
 * size: region size
 */

struct pcie_cfg_data
{
  uintptr_t cfg_addr;
  size_t cfg_size;
  uintptr_t bus_start;
  size_t size;
};

struct pcie_ctrl_dev
{
  FAR struct pcie_cfg_data *data;
  FAR const struct pcie_bus_ops_s *ops
};

struct pcie_bar
{
  uintptr_t phys_addr;
  size_t size;
};

/* struct pcie_bus_state - pcie bus state when scanning
 * bus_bdf: Current scanned bus BDF, always valid
 * bridge_bdf: Current bridge endpoint BDF, either
 *    valid or PCIE_BDF_NONE
 * next_bdf: Next BDF to scan on bus, either valid
 *    or PCIE_BDF_NONE when all EP scanned
 */

struct pcie_bus_state
{
  unsigned int bus_bdf;
  unsigned int bridge_bdf;
  unsigned int next_bdf;
};

/* The PCIE driver interface */

struct pcie_bus_s;
struct pcie_dev_type_s;
struct pcie_dev_s;

/* Bus related operations */

struct pcie_bus_ops_s
{
    CODE int (*pci_cfg_write)(pcie_bdf_t bdf, unsigned int reg,
                              uint32_t data);

    CODE int (*pci_cfg_read)(pcie_bdf_t bdf, unsigned int reg);

    CODE int (*pci_map_bar)(FAR struct pcie_dev_s *dev, uint32_t addr,
                            unsigned long length);

    CODE int (*pci_map_bar64)(FAR struct pcie_dev_s *dev, uint64_t addr,
                            unsigned long length);

    CODE int (*pci_msi_register)(FAR struct pcie_dev_s *dev,
                                 uint16_t vector);
    CODE bool (*region_allocate)(FAR struct pcie_ctrl_dev *ctrl_dev,
                                 pcie_bdf_t bdf,
                                 bool mem, bool mem64, size_t bar_size,
                                 FAR uintptr_t *bar_bus_addr);
    CODE bool (*region_get_allocate_base)(
                                FAR struct pcie_ctrl_dev *ctrl_dev,
                                pcie_bdf_t bdf,
                                bool mem, bool mem64, size_t align,
                                FAR uintptr_t *bar_base_addr);
    CODE bool (*region_translate)(FAR struct pcie_ctrl_dev *ctrl_dev,
                                pcie_bdf_t bdf,
                                bool mem, bool mem64, uintptr_t bar_bus_addr,
                                FAR uintptr_t *bar_addr);
    CODE bool(*pcie_msi_vectors_allocate)(unsigned int priority,
                                          FAR msi_vector_t *vectors,
                                          FAR uint8_t n_vector);
    CODE bool (*pcie_msi_vector_connect)(FAR msi_vector_t *vector,
            CODE int (*handler)(int irq, FAR void *context, FAR void *arg),
            FAR const void *parameter,
            uint32_t flags)
};

/* PCIE bus private data. */

struct pcie_bus_s
{
  FAR const struct pcie_bus_ops_s *ops; /* operations */
};

/* PCIE device type, defines by vendor ID and device ID
 * vendor: Device vendor ID
 * device: Device ID
 * class_rev: Device reversion
 * nam: Human readable name
 */

struct pcie_dev_type_s
{
  uint16_t      vendor;
  uint16_t      device;
  uint32_t      class_rev;
  FAR const char    *name;
};

/* PCIE device private data. */

struct pcie_dev_s
{
  struct list_node node;

  /* operations */

  FAR const struct pcie_bus_ops_s *ops;
  FAR struct pcie_dev_type_s *type;

  /* FAR struct pcie_cfg_data *data; */

  uint16_t bdf;
};

struct pcie_drv_s
{
  struct list_node node;
  FAR struct pcie_dev_s *device;

  /* Call back function when a device is probed */

  CODE int (*probe)(FAR struct pcie_dev_s *device);
  CODE void (*remove)(FAR struct pcie_dev_s *device);
};

/****************************************************************************
 * Public Functions Prototypes
 ****************************************************************************/

#undef EXTERN
#if defined(__cplusplus)
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

/****************************************************************************
 * Name: pcie_scan_bus
 *
 * Description:
 *  pcie device add into list.
 * Input Parameters:
 *
 * Return Value:
 *
 ****************************************************************************/

int pcie_scan_bus(FAR const struct pcie_bus_ops_s *ops, uint8_t bus);

/****************************************************************************
 * Name:
 *
 * Description:
 *
 *
 * Input Parameters:
 *
 *
 * Returned Value:
 *
 ****************************************************************************/

void pcie_set_cmd(FAR const struct pcie_bus_ops_s *ops,
                  pcie_bdf_t bdf,
                  uint32_t bits, bool on);

/****************************************************************************
 * Name: pci_find_cap
 *
 * Description:
 *  Search through the PCI-e device capability list to find given capability.
 *
 * Input Parameters:
 *   dev - Device
 *   cap - Bitmask of capability
 *
 * Returned Value:
 *   -1: Capability not supported
 *   other: the offset in PCI configuration space to the capability structure
 *
 ****************************************************************************/

int pcie_find_cap(FAR struct pcie_dev_s *dev, uint16_t cap_id);

/****************************************************************************
 * Name: pcie_initialize
 *
 * Description:
 *  Initialize the PCI-E bus and enumerate the devices with give devices
 *  type array
 *
 * Input Parameters:
 *   bus    - An PCIE bus
 *   types  - A array of PCIE device types
 *
 * Returned Value:
 *   OK if the driver was successfully register; A negated errno value is
 *   returned on any failure.
 *
 ****************************************************************************/

int pcie_initialize(FAR struct pcie_dev_s *dev,
                    FAR struct pcie_dev_type_s **types);

/****************************************************************************
 * Name: pci_find_cap
 *
 * Description:
 *  Search through the PCI-e device capability list to find given capability.
 *
 * Input Parameters:
 *   dev - Device
 *   cap - Bitmask of capability
 *
 * Returned Value:
 *   -1: Capability not supported
 *   other: the offset in PCI configuration space to the capability structure
 *
 ****************************************************************************/

int pci_find_cap(FAR struct pcie_dev_s *dev, uint16_t cap);

/****************************************************************************
 * Name: pcie_get_bar
 *
 * Description:
 *   Get value from a bar register.
 *
 * Input Parameters:
 *   dev   - A endpoint device
 *   index - 0-based BAR index
 *   mbar  - Pointer to struct pcie_bar
 *
 * Return Value:
 *   Return true if the BAR was found and is valid, false otherwise.
 *
 ****************************************************************************/

bool pcie_get_bar(FAR struct pcie_dev_s *dev,
       unsigned int bar_index,
       FAR struct pcie_bar *mbar);

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

bool pcie_probe_bar(FAR struct pcie_dev_s *dev,
                    unsigned int index,
                    FAR struct pcie_bar *bar);

/****************************************************************************
 * Name: pcie_alloc_irq
 *
 * Description:
 *   To set a irq to a endpoint device.
 *
 * Input Parameters:
 *   dev - A endpoint device
 *   irq - interrupt request number
 *
 * Returned Value:
 *   Return true if the BAR was found and is valid, false otherwise.
 *
 ****************************************************************************/

bool pcie_alloc_irq(FAR struct pcie_dev_s *dev, unsigned int irq);

/****************************************************************************
 * Name: pcie_get_irq
 *
 * Description:
 *   To get a endpoint device irq.
 *
 * Input Parameters:
 *   dev - A endpoint device
 *
 * Returned Value:
 *   Return a irq number if success
 *
 ****************************************************************************/

int pcie_get_irq(FAR struct pcie_dev_s *dev);

/****************************************************************************
 * Name: pcie_connect_irq
 *
 * Description:
 *   Attach the interrupt handler, msi interrupt is not surrported.
 *
 * Input Parameters:
 *   dev - A endpoint device
 *   irq - interruption request number
 *
 * Returned Value:
 *   Return true if the BAR was found and is valid, false otherwise.
 *
 ****************************************************************************/

bool pcie_connect_irq(FAR struct pcie_dev_s *dev,
              unsigned int irq,
              unsigned int priority,
              CODE int (*handler)(int irq, FAR void *context, FAR void *arg),
              FAR void *parameter,
              uint32_t flags);

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

void pcie_irq_enable(FAR struct pcie_dev_s *dev, unsigned int irq);

/****************************************************************************
 * Name: pcie_register_driver
 ****************************************************************************/

int pcie_register_driver(FAR struct pcie_drv_s *driver);

/****************************************************************************
 * Name: virtio_unregister_driver
 ****************************************************************************/

int pcie_unregister_driver(FAR struct pcie_drv_s *driver);

#undef EXTERN
#if defined(__cplusplus)
}
#endif
#endif /* __INCLUDE_NUTTX_PCIE_PCIE_H */
