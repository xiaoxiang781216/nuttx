/****************************************************************************
 * include/nuttx/pcie/msi.h
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

#ifndef __INCLUDE_NUTTX_PCIE_MSI_H
#define __INCLUDE_NUTTX_PCIE_MSI_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/pcie/pcie.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

typedef uint32_t pcie_bdf_t;
typedef struct msi_vector msi_vector_t;

/* The first word of the MSI capability is shared with the
 * capability ID and list link.  The high 16 bits are the MCR.
 */

#define PCIE_MSI_MCR            0U

#define PCIE_MSI_MCR_EN         0x00010000U  /* enable MSI */
#define PCIE_MSI_MCR_MMC        0x000E0000U  /* Multi Messages Capable mask */
#define PCIE_MSI_MCR_MMC_SHIFT  17
#define PCIE_MSI_MCR_MME        0x00700000U  /* mask of # of enabled IRQs */
#define PCIE_MSI_MCR_MME_SHIFT  20
#define PCIE_MSI_MCR_64         0x00800000U  /* 64-bit MSI */

/* The MAP follows the MCR. If PCIE_MSI_MCR_64, then the MAP
 * is two words long. The MDR follows immediately after the MAP.
 */

#define PCIE_MSI_MAP0           1U
#define PCIE_MSI_MAP1_64        2U
#define PCIE_MSI_MDR_32         2U
#define PCIE_MSI_MDR_64         3U

/* As for MSI, he first word of the MSI-X capability is shared
 * with the capability ID and list link.  The high 16 bits are the MCR.
 */

#define PCIE_MSIX_MCR           0U

#define PCIE_MSIX_MCR_EN        0x80000000U /* Enable MSI-X */
#define PCIE_MSIX_MCR_FMASK     0x40000000U /* Function Mask */
#define PCIE_MSIX_MCR_TSIZE     0x07FF0000U /* Table size mask */
#define PCIE_MSIX_MCR_TSIZE_SHIFT   16
#define PCIE_MSIR_TABLE_ENTRY_SIZE  16

#define PCIE_MSIX_TR           1U
#define PCIE_MSIX_TR_BIR       0x00000007U /* Table BIR mask */
#define PCIE_MSIX_TR_OFFSET    0xFFFFFFF8U /* Offset mask */

#define PCIE_MSIX_PBA          2U
#define PCIE_MSIX_PBA_BIR      0x00000007U /* PBA BIR mask */
#define PCIE_MSIX_PBA_OFFSET   0xFFFFFFF8U /* Offset mask */

#define PCIE_VTBL_MA           0U /* Msg Address offset */
#define PCIE_VTBL_MUA          4U /* Msg Upper Address offset */
#define PCIE_VTBL_MD           8U /* Msg Data offset */

/* Vector control offset */

#define PCIE_VTBL_VCTRL        12U

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct msi_vector_generic
{
  unsigned int irq;
  uint32_t address;
  uint16_t eventid;
  unsigned int priority;
};

struct msi_vector
{
  pcie_bdf_t bdf;
  struct msi_vector_generic arch;
};

#endif /* __INCLUDE_NUTTX_PCIE_MSI_H */`
