/****************************************************************************
 * include/nuttx/pcie/cap.h
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

#ifndef __INCLUDE_NUTTX_PCIE_CAP_H
#define __INCLUDE_NUTTX_PCIE_CAP_H

/* PCI & PCI Express Capabilities
 * from PCI Code and ID Assignment Specification Revision 1.11
 */

#define PCI_CAP_ID_NULL   0x00U  /* Null Capability */
#define PCI_CAP_ID_MSI    0x05U  /* Message Signalled Interrupts */
#define PCI_CAP_ID_PCIX   0x07U  /* PCI-X */
#define PCI_CAP_ID_EXP    0x10U  /* PCI Express */
#define PCI_CAP_ID_MSIX   0x11U  /* MSI-X */

#endif /* __INCLUDE_NUTTX_PCIE_CAP_H */
