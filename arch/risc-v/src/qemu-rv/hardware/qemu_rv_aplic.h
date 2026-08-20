/****************************************************************************
 * arch/risc-v/src/qemu-rv/hardware/qemu_rv_aplic.h
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

#ifndef __ARCH_RISCV_SRC_QEMU_RV_HARDWARE_QEMU_RV_APLIC_H
#define __ARCH_RISCV_SRC_QEMU_RV_HARDWARE_QEMU_RV_APLIC_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define QEMU_RV_APLIC_NR_IRQ      0x60

/* IMSIC identity space domain boundaries:
 *   [1, APLIC_IRQ_END]               wire devices via APLIC
 *   [APLIC_IRQ_END+1, NR_PLIC_IRQS)  PCI MSI direct to IMSIC
 */

#define QEMU_RV_APLIC_IRQ_END     QEMU_RV_APLIC_NR_IRQ

/* PCIe legacy INTx uses APLIC sources 32-35.  Defined by QEMU virt
 * machine (PCIE_IRQ = 0x20), reflected in DTB interrupt-map as
 * sources 0x20-0x23.
 */

#define QEMU_RV_PCIE_INTX_BASE    32

#endif /* __ARCH_RISCV_SRC_QEMU_RV_HARDWARE_QEMU_RV_APLIC_H */
