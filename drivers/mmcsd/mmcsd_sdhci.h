/****************************************************************************
 * drivers/mmcsd/mmcsd_sdhci.h
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

#ifndef __DRIVERS_MMCSD_MMCSD_SDHCI_H
#define __DRIVERS_MMCSD_MMCSD_SDHCI_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/sdio.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Register Offsets *********************************************************/

/* SDHCI standard register offsets.
 * Reference: SD Host Controller Standard Specification, Section 2.2
 */

#define SDHCI_DMA_ADDRESS_OFFSET            0x0000  /* SDMA System Address / 32-bit Block Count */
#define SDHCI_BLOCK_SIZE_OFFSET             0x0004  /* Block Size */
#define SDHCI_BLOCK_COUNT_OFFSET            0x0006  /* 16-bit Block Count */
#define SDHCI_ARGUMENT_OFFSET               0x0008  /* Argument */
#define SDHCI_TRANSFER_MODE_OFFSET          0x000c  /* Transfer Mode */
#define SDHCI_COMMAND_OFFSET                0x000e  /* Command */
#define SDHCI_RESPONSE0_OFFSET              0x0010  /* Response 0 */
#define SDHCI_RESPONSE1_OFFSET              0x0014  /* Response 1 */
#define SDHCI_RESPONSE2_OFFSET              0x0018  /* Response 2 */
#define SDHCI_RESPONSE3_OFFSET              0x001c  /* Response 3 */
#define SDHCI_BUFFER_DATA_PORT_OFFSET       0x0020  /* Buffer Data Port */
#define SDHCI_PRESENT_STATE_OFFSET          0x0024  /* Present State */
#define SDHCI_HOST_CONTROL_OFFSET           0x0028  /* Host Control 1 */
#define SDHCI_POWER_CONTROL_OFFSET          0x0029  /* Power Control */
#define SDHCI_BLOCK_GAP_CONTROL_OFFSET      0x002a  /* Block Gap Control */
#define SDHCI_WAKEUP_CONTROL_OFFSET         0x002b  /* Wakeup Control */
#define SDHCI_CLOCK_CONTROL_OFFSET          0x002c  /* Clock Control */
#define SDHCI_TIMEOUT_CONTROL_OFFSET        0x002e  /* Timeout Control */
#define SDHCI_SOFTWARE_RESET_OFFSET         0x002f  /* Software Reset */
#define SDHCI_INT_STATUS_OFFSET             0x0030  /* Normal Interrupt Status */
#define SDHCI_ERR_INT_STATUS_OFFSET         0x0032  /* Error Interrupt Status */
#define SDHCI_INT_STATUS_EN_OFFSET          0x0034  /* Normal Interrupt Status Enable */
#define SDHCI_ERR_INT_STATUS_EN_OFFSET      0x0036  /* Error Interrupt Status Enable */
#define SDHCI_INT_SIGNAL_EN_OFFSET          0x0038  /* Normal Interrupt Signal Enable */
#define SDHCI_ERR_INT_SIGNAL_EN_OFFSET      0x003a  /* Error Interrupt Signal Enable */
#define SDHCI_AUTO_CMD_ERR_STATUS_OFFSET    0x003c  /* Auto CMD Error Status */
#define SDHCI_HOST_CONTROL_2_OFFSET         0x003e  /* Host Control 2 */
#define SDHCI_CAPABILITIES_OFFSET           0x0040  /* Capabilities (64-bit) */
#define SDHCI_MAX_CURRENT_CAPS_OFFSET       0x0048  /* Maximum Current Capabilities */
#define SDHCI_FORCE_AUTO_CMD_ERR_OFFSET     0x0050  /* Force Event for Auto CMD Error */
#define SDHCI_FORCE_ERR_INT_OFFSET          0x0052  /* Force Event for Error Interrupt */
#define SDHCI_ADMA_ERR_STATUS_OFFSET        0x0054  /* ADMA Error Status */
#define SDHCI_ADMA_ADDRESS_OFFSET           0x0058  /* ADMA System Address (64-bit) */
#define SDHCI_PRESET_INIT_OFFSET            0x0060  /* Preset Value for Initialization */
#define SDHCI_PRESET_DEFAULT_SPEED_OFFSET   0x0062  /* Preset Value for Default Speed */
#define SDHCI_PRESET_HIGH_SPEED_OFFSET      0x0064  /* Preset Value for High Speed */
#define SDHCI_PRESET_SDR12_OFFSET           0x0066  /* Preset Value for SDR12 */
#define SDHCI_PRESET_SDR25_OFFSET           0x0068  /* Preset Value for SDR25 */
#define SDHCI_PRESET_SDR50_OFFSET           0x006a  /* Preset Value for SDR50 */
#define SDHCI_PRESET_SDR104_OFFSET          0x006c  /* Preset Value for SDR104 */
#define SDHCI_PRESET_DDR50_OFFSET           0x006e  /* Preset Value for DDR50 */
#define SDHCI_PRESET_UHS2_OFFSET            0x0074  /* Preset Value for UHS-II */
#define SDHCI_ADMA3_DESC_ADDRESS_OFFSET     0x0078  /* ADMA3 Descriptor Address (64-bit) */
#define SDHCI_SLOT_INT_STATUS_OFFSET        0x00fc  /* Slot Interrupt Status */
#define SDHCI_HOST_VERSION_OFFSET           0x00fe  /* Host Controller Version */

/* Register Bit Definitions *************************************************/

/* Block Size Register */

/* Bits 15: Reserved */

#define SDHCI_BLOCK_SIZE_SDMA_BOUNDARY_SHIFT  (12)
#define SDHCI_BLOCK_SIZE_SDMA_BOUNDARY_MASK   (7 << SDHCI_BLOCK_SIZE_SDMA_BOUNDARY_SHIFT)
#  define SDHCI_BLOCK_SIZE_SDMA_BOUNDARY_4K   (0 << SDHCI_BLOCK_SIZE_SDMA_BOUNDARY_SHIFT)
#  define SDHCI_BLOCK_SIZE_SDMA_BOUNDARY_8K   (1 << SDHCI_BLOCK_SIZE_SDMA_BOUNDARY_SHIFT)
#  define SDHCI_BLOCK_SIZE_SDMA_BOUNDARY_16K  (2 << SDHCI_BLOCK_SIZE_SDMA_BOUNDARY_SHIFT)
#  define SDHCI_BLOCK_SIZE_SDMA_BOUNDARY_32K  (3 << SDHCI_BLOCK_SIZE_SDMA_BOUNDARY_SHIFT)
#  define SDHCI_BLOCK_SIZE_SDMA_BOUNDARY_64K  (4 << SDHCI_BLOCK_SIZE_SDMA_BOUNDARY_SHIFT)
#  define SDHCI_BLOCK_SIZE_SDMA_BOUNDARY_128K (5 << SDHCI_BLOCK_SIZE_SDMA_BOUNDARY_SHIFT)
#  define SDHCI_BLOCK_SIZE_SDMA_BOUNDARY_256K (6 << SDHCI_BLOCK_SIZE_SDMA_BOUNDARY_SHIFT)
#  define SDHCI_BLOCK_SIZE_SDMA_BOUNDARY_512K (7 << SDHCI_BLOCK_SIZE_SDMA_BOUNDARY_SHIFT)

#define SDHCI_BLOCK_SIZE_TRANSFER_SIZE_SHIFT  (0)
#define SDHCI_BLOCK_SIZE_TRANSFER_SIZE_MASK   (0xfff << SDHCI_BLOCK_SIZE_TRANSFER_SIZE_SHIFT)

/* 16-bit Block Count Register */

#define SDHCI_BLOCK_COUNT_SHIFT               (0)
#define SDHCI_BLOCK_COUNT_MASK                (0xffff << SDHCI_BLOCK_COUNT_SHIFT)

/* Argument Register (32-bit, no sub-fields) */

/* Transfer Mode Register */

/* Bits 15-9: Reserved */

#define SDHCI_TRANSFER_MODE_RESP_INT_DISABLE  (1 << 8)  /* Response Interrupt Disable */
#define SDHCI_TRANSFER_MODE_RESP_ERR_CHECK    (1 << 7)  /* Response Error Check Enable */
#define SDHCI_TRANSFER_MODE_RESP_R1_R5        (1 << 6)  /* Response Type R1/R5 */
#define SDHCI_TRANSFER_MODE_MULTI_BLOCK       (1 << 5)  /* Multi/Single Block Select */
#define SDHCI_TRANSFER_MODE_READ              (1 << 4)  /* Data Transfer Direction */

#define SDHCI_TRANSFER_MODE_AUTO_CMD_SHIFT    (2)       /* Auto CMD Enable */
#define SDHCI_TRANSFER_MODE_AUTO_CMD_MASK     (3 << SDHCI_TRANSFER_MODE_AUTO_CMD_SHIFT)
#  define SDHCI_TRANSFER_MODE_AUTO_CMD_DIS    (0 << SDHCI_TRANSFER_MODE_AUTO_CMD_SHIFT)
#  define SDHCI_TRANSFER_MODE_AUTO_CMD12      (1 << SDHCI_TRANSFER_MODE_AUTO_CMD_SHIFT)
#  define SDHCI_TRANSFER_MODE_AUTO_CMD23      (2 << SDHCI_TRANSFER_MODE_AUTO_CMD_SHIFT)
#  define SDHCI_TRANSFER_MODE_AUTO_CMD_SEL    (3 << SDHCI_TRANSFER_MODE_AUTO_CMD_SHIFT)

#define SDHCI_TRANSFER_MODE_BLOCK_COUNT_EN    (1 << 1)  /* Block Count Enable */
#define SDHCI_TRANSFER_MODE_DMA_EN            (1 << 0)  /* DMA Enable */

/* Command Register */

/* Bits 15-14: Reserved */

#define SDHCI_COMMAND_INDEX_SHIFT             (8)       /* Command Index */
#define SDHCI_COMMAND_INDEX_MASK              (0x3f << SDHCI_COMMAND_INDEX_SHIFT)

#define SDHCI_COMMAND_TYPE_SHIFT              (6)       /* Command Type */
#define SDHCI_COMMAND_TYPE_MASK               (3 << SDHCI_COMMAND_TYPE_SHIFT)
#  define SDHCI_COMMAND_TYPE_NORMAL           (0 << SDHCI_COMMAND_TYPE_SHIFT)
#  define SDHCI_COMMAND_TYPE_SUSPEND          (1 << SDHCI_COMMAND_TYPE_SHIFT)
#  define SDHCI_COMMAND_TYPE_RESUME           (2 << SDHCI_COMMAND_TYPE_SHIFT)
#  define SDHCI_COMMAND_TYPE_ABORT            (3 << SDHCI_COMMAND_TYPE_SHIFT)

#define SDHCI_COMMAND_DATA_PRESENT            (1 << 5)  /* Data Present Select */
#define SDHCI_COMMAND_INDEX_CHECK_EN          (1 << 4)  /* Command Index Check Enable */
#define SDHCI_COMMAND_CRC_CHECK_EN            (1 << 3)  /* Command CRC Check Enable */
#define SDHCI_COMMAND_SUB_CMD_FLAG            (1 << 2)  /* Sub Command Flag (V4.10+) */

#define SDHCI_COMMAND_RESPONSE_SHIFT          (0)       /* Response Type Select */
#define SDHCI_COMMAND_RESPONSE_MASK           (3 << SDHCI_COMMAND_RESPONSE_SHIFT)
#  define SDHCI_COMMAND_RESPONSE_NONE         (0 << SDHCI_COMMAND_RESPONSE_SHIFT)
#  define SDHCI_COMMAND_RESPONSE_R2           (1 << SDHCI_COMMAND_RESPONSE_SHIFT)
#  define SDHCI_COMMAND_RESPONSE_R1_R3_R4_R5  (2 << SDHCI_COMMAND_RESPONSE_SHIFT)
#  define SDHCI_COMMAND_RESPONSE_R1B_R5B      (3 << SDHCI_COMMAND_RESPONSE_SHIFT)

/* Present State Register */

#define SDHCI_PRESENT_STATE_UHS2_IF_DET       (1 << 31) /* UHS-II IF Detection */
#define SDHCI_PRESENT_STATE_LANE_SYNC         (1 << 30) /* Lane Synchronization */
#define SDHCI_PRESENT_STATE_DORMANT           (1 << 29) /* In Dormant State */
#define SDHCI_PRESENT_STATE_SUB_CMD_STATUS    (1 << 28) /* Sub Command Status */
#define SDHCI_PRESENT_STATE_CMD_NOT_ISSUED    (1 << 27) /* Command Not Issued by Error */

/* Bit 26: Reserved */

#define SDHCI_PRESENT_STATE_REGULATOR_STABLE  (1 << 25) /* Regulator Voltage Stable */
#define SDHCI_PRESENT_STATE_CMD_LINE_LEVEL    (1 << 24) /* CMD Line Signal Level */

#define SDHCI_PRESENT_STATE_DAT_LINE_SHIFT    (20)      /* DAT[3:0] Line Signal Level */
#define SDHCI_PRESENT_STATE_DAT_LINE_MASK     (0xf << SDHCI_PRESENT_STATE_DAT_LINE_SHIFT)

#define SDHCI_PRESENT_STATE_WRITE_PROTECT     (1 << 19) /* Write Protect Switch Pin Level */
#define SDHCI_PRESENT_STATE_CARD_DETECT       (1 << 18) /* Card Detect Pin Level */
#define SDHCI_PRESENT_STATE_CARD_STABLE       (1 << 17) /* Card State Stable */
#define SDHCI_PRESENT_STATE_CARD_INSERTED     (1 << 16) /* Card Inserted */

/* Bits 15-12: Reserved */

#define SDHCI_PRESENT_STATE_BUF_READ_ENABLE   (1 << 11) /* Buffer Read Enable */
#define SDHCI_PRESENT_STATE_BUF_WRITE_ENABLE  (1 << 10) /* Buffer Write Enable */
#define SDHCI_PRESENT_STATE_READ_TRANSFER     (1 << 9)  /* Read Transfer Active */
#define SDHCI_PRESENT_STATE_WRITE_TRANSFER    (1 << 8)  /* Write Transfer Active */

#define SDHCI_PRESENT_STATE_DAT7_4_SHIFT      (4)       /* DAT[7:4] Line Signal Level */
#define SDHCI_PRESENT_STATE_DAT7_4_MASK       (0xf << SDHCI_PRESENT_STATE_DAT7_4_SHIFT)

#define SDHCI_PRESENT_STATE_RETUNING_REQUEST  (1 << 3)  /* Re-Tuning Request (UHS-I) */
#define SDHCI_PRESENT_STATE_DAT_LINE_ACTIVE   (1 << 2)  /* DAT Line Active */
#define SDHCI_PRESENT_STATE_CMD_INHIBIT_DAT   (1 << 1)  /* Command Inhibit (DAT) */
#define SDHCI_PRESENT_STATE_CMD_INHIBIT_CMD   (1 << 0)  /* Command Inhibit (CMD) */

/* Host Control 1 Register */

#define SDHCI_HOST_CONTROL_CARD_DETECT_SIGNAL (1 << 7)  /* Card Detect Signal Selection */
#define SDHCI_HOST_CONTROL_CARD_DETECT_TEST   (1 << 6)  /* Card Detect Test Level */
#define SDHCI_HOST_CONTROL_8BIT_BUS           (1 << 5)  /* Extended Data Transfer Width */

#define SDHCI_HOST_CONTROL_DMA_SHIFT          (3)       /* DMA Select */
#define SDHCI_HOST_CONTROL_DMA_MASK           (3 << SDHCI_HOST_CONTROL_DMA_SHIFT)
#  define SDHCI_HOST_CONTROL_DMA_SDMA         (0 << SDHCI_HOST_CONTROL_DMA_SHIFT)
#  define SDHCI_HOST_CONTROL_DMA_RESERVED     (1 << SDHCI_HOST_CONTROL_DMA_SHIFT)
#  define SDHCI_HOST_CONTROL_DMA_ADMA2        (2 << SDHCI_HOST_CONTROL_DMA_SHIFT)
#  define SDHCI_HOST_CONTROL_DMA_ADMA2_64BIT  (3 << SDHCI_HOST_CONTROL_DMA_SHIFT)

#define SDHCI_HOST_CONTROL_HIGH_SPEED         (1 << 2)  /* High Speed Enable */
#define SDHCI_HOST_CONTROL_4BIT_BUS           (1 << 1)  /* Data Transfer Width */
#define SDHCI_HOST_CONTROL_LED                (1 << 0)  /* LED Control */

/* Power Control Register */

#define SDHCI_POWER_CONTROL_VDD2_SHIFT        (5)       /* Voltage Select for VDD2 */
#define SDHCI_POWER_CONTROL_VDD2_MASK         (7 << SDHCI_POWER_CONTROL_VDD2_SHIFT)
#  define SDHCI_POWER_CONTROL_VDD2_1_8V       (5 << SDHCI_POWER_CONTROL_VDD2_SHIFT)

#define SDHCI_POWER_CONTROL_VDD2_POWER        (1 << 4)  /* SD Bus Power for VDD2 */

#define SDHCI_POWER_CONTROL_VDD1_SHIFT        (1)       /* Voltage Select for VDD1 */
#define SDHCI_POWER_CONTROL_VDD1_MASK         (7 << SDHCI_POWER_CONTROL_VDD1_SHIFT)
#  define SDHCI_POWER_CONTROL_VDD1_3_3V       (7 << SDHCI_POWER_CONTROL_VDD1_SHIFT)
#  define SDHCI_POWER_CONTROL_VDD1_3_0V       (6 << SDHCI_POWER_CONTROL_VDD1_SHIFT)
#  define SDHCI_POWER_CONTROL_VDD1_1_8V       (5 << SDHCI_POWER_CONTROL_VDD1_SHIFT)

#define SDHCI_POWER_CONTROL_VDD1_POWER        (1 << 0)  /* SD Bus Power for VDD1 */

/* Block Gap Control Register */

/* Bits 7-4: Reserved */

#define SDHCI_BLOCK_GAP_INT_ENABLE            (1 << 3)  /* Interrupt At Block Gap */
#define SDHCI_BLOCK_GAP_READ_WAIT             (1 << 2)  /* Read Wait Control */
#define SDHCI_BLOCK_GAP_CONTINUE_REQUEST      (1 << 1)  /* Continue Request (RWAC) */
#define SDHCI_BLOCK_GAP_STOP_REQUEST          (1 << 0)  /* Stop At Block Gap Request */

/* Wakeup Control Register */

/* Bits 7-3: Reserved */

#define SDHCI_WAKEUP_CARD_REMOVAL             (1 << 2)  /* Wakeup On Card Removal */
#define SDHCI_WAKEUP_CARD_INSERTION           (1 << 1)  /* Wakeup On Card Insertion */
#define SDHCI_WAKEUP_CARD_INTERRUPT           (1 << 0)  /* Wakeup On Card Interrupt */

/* Clock Control Register */

#define SDHCI_CLOCK_CONTROL_FREQ_SHIFT        (8)       /* SDCLK Frequency Select */
#define SDHCI_CLOCK_CONTROL_FREQ_MASK         (0xff << SDHCI_CLOCK_CONTROL_FREQ_SHIFT)

#define SDHCI_CLOCK_CONTROL_FREQ_UPPER_SHIFT  (6)       /* Upper Bits of SDCLK Freq */
#define SDHCI_CLOCK_CONTROL_FREQ_UPPER_MASK   (3 << SDHCI_CLOCK_CONTROL_FREQ_UPPER_SHIFT)
#define SDHCI_CLOCK_CONTROL_FREQ_10BIT_MASK   (SDHCI_CLOCK_CONTROL_FREQ_MASK | \
                                               SDHCI_CLOCK_CONTROL_FREQ_UPPER_MASK)

#define SDHCI_CLOCK_CONTROL_PROGRAMMABLE      (1 << 5)  /* Clock Generator Select */

/* Bit 4: Reserved */

#define SDHCI_CLOCK_CONTROL_PLL_ENABLE        (1 << 3)  /* PLL Enable (V4.10+) */
#define SDHCI_CLOCK_CONTROL_SD_CLOCK_ENABLE   (1 << 2)  /* SD Clock Enable */
#define SDHCI_CLOCK_CONTROL_INT_STABLE        (1 << 1)  /* Internal Clock Stable */
#define SDHCI_CLOCK_CONTROL_INT_ENABLE        (1 << 0)  /* Internal Clock Enable */

/* Timeout Control Register */

/* Bits 7-4: Reserved */

#define SDHCI_TIMEOUT_CONTROL_SHIFT           (0)       /* Data Timeout Counter Value */
#define SDHCI_TIMEOUT_CONTROL_MASK            (0xf << SDHCI_TIMEOUT_CONTROL_SHIFT)

/* Timeout = TMCLK x 2^(N+13), N=0..14.  1111b: Reserved */

#define SDHCI_TIMEOUT_MAX                     14        /* Maximum valid timeout value */

/* Software Reset Register */

/* Bits 7-3: Reserved */

#define SDHCI_SOFTWARE_RESET_DAT              (1 << 2)  /* Reset DAT Line (RWAC) */
#define SDHCI_SOFTWARE_RESET_CMD              (1 << 1)  /* Reset CMD Line (RWAC) */
#define SDHCI_SOFTWARE_RESET_ALL              (1 << 0)  /* Reset All (RWAC) */

/* Normal Interrupt Status Register.
 * The following bit definitions also apply to:
 *   - Normal Interrupt Status Enable Register (034h)
 *   - Normal Interrupt Signal Enable Register (038h)
 */

#define SDHCI_INT_ERROR                       (1 << 15) /* Error Interrupt (ROC) */

/* Bit 14: Reserved */

#define SDHCI_INT_FX_EVENT                    (1 << 13) /* FX Event (ROC) */
#define SDHCI_INT_RETUNING                    (1 << 12) /* Re-Tuning Event (UHS-I) */
#define SDHCI_INT_EMBED_C                     (1 << 11) /* INT_C (Embedded) */
#define SDHCI_INT_EMBED_B                     (1 << 10) /* INT_B (Embedded) */
#define SDHCI_INT_EMBED_A                     (1 << 9)  /* INT_A (Embedded) */
#define SDHCI_INT_CARD                        (1 << 8)  /* Card Interrupt */
#define SDHCI_INT_CARD_REMOVAL                (1 << 7)  /* Card Removal (RW1C) */
#define SDHCI_INT_CARD_INSERTION              (1 << 6)  /* Card Insertion (RW1C) */
#define SDHCI_INT_READ_READY                  (1 << 5)  /* Buffer Read Ready (RW1C) */
#define SDHCI_INT_WRITE_READY                 (1 << 4)  /* Buffer Write Ready (RW1C) */
#define SDHCI_INT_DMA                         (1 << 3)  /* DMA Interrupt (RW1C) */
#define SDHCI_INT_BLOCK_GAP                   (1 << 2)  /* Block Gap Event (RW1C) */
#define SDHCI_INT_TRANSFER_COMPLETE           (1 << 1)  /* Transfer Complete (RW1C) */
#define SDHCI_INT_CMD_COMPLETE                (1 << 0)  /* Command Complete (RW1C) */

/* Common interrupt mask combinations */

#define SDHCI_INT_CMD_MASK                    (SDHCI_INT_CMD_COMPLETE)
#define SDHCI_INT_DATA_MASK                   (SDHCI_INT_TRANSFER_COMPLETE | \
                                               SDHCI_INT_DMA | SDHCI_INT_READ_READY | \
                                               SDHCI_INT_WRITE_READY)
#define SDHCI_INT_CARD_MASK                   (SDHCI_INT_CARD_REMOVAL | \
                                               SDHCI_INT_CARD_INSERTION)

/* Error Interrupt Status Register.
 * The following bit definitions also apply to:
 *   - Error Interrupt Status Enable Register (036h)
 *   - Error Interrupt Signal Enable Register (03Ah)
 */

#define SDHCI_ERR_INT_VENDOR_SHIFT            (12)      /* Vendor Specific Error Status */
#define SDHCI_ERR_INT_VENDOR_MASK             (0xf << SDHCI_ERR_INT_VENDOR_SHIFT)

#define SDHCI_ERR_INT_RESPONSE                (1 << 11) /* Response Error (RW1C) */
#define SDHCI_ERR_INT_TUNING                  (1 << 10) /* Tuning Error (UHS-I, RW1C) */
#define SDHCI_ERR_INT_ADMA                    (1 << 9)  /* ADMA Error (RW1C) */
#define SDHCI_ERR_INT_AUTO_CMD                (1 << 8)  /* Auto CMD Error (RW1C) */
#define SDHCI_ERR_INT_CURRENT_LIMIT           (1 << 7)  /* Current Limit Error (RW1C) */
#define SDHCI_ERR_INT_DATA_END_BIT            (1 << 6)  /* Data End Bit Error (RW1C) */
#define SDHCI_ERR_INT_DATA_CRC                (1 << 5)  /* Data CRC Error (RW1C) */
#define SDHCI_ERR_INT_DATA_TIMEOUT            (1 << 4)  /* Data Timeout Error (RW1C) */
#define SDHCI_ERR_INT_CMD_INDEX               (1 << 3)  /* Command Index Error (RW1C) */
#define SDHCI_ERR_INT_CMD_END_BIT             (1 << 2)  /* Command End Bit Error (RW1C) */
#define SDHCI_ERR_INT_CMD_CRC                 (1 << 1)  /* Command CRC Error (RW1C) */
#define SDHCI_ERR_INT_CMD_TIMEOUT             (1 << 0)  /* Command Timeout Error (RW1C) */

/* Error interrupt mask combinations */

#define SDHCI_ERR_INT_CMD_MASK                (SDHCI_ERR_INT_CMD_TIMEOUT | \
                                               SDHCI_ERR_INT_CMD_CRC | \
                                               SDHCI_ERR_INT_CMD_END_BIT | \
                                               SDHCI_ERR_INT_CMD_INDEX | \
                                               SDHCI_ERR_INT_RESPONSE)
#define SDHCI_ERR_INT_DATA_MASK               (SDHCI_ERR_INT_DATA_TIMEOUT | \
                                               SDHCI_ERR_INT_DATA_CRC | \
                                               SDHCI_ERR_INT_DATA_END_BIT)
#define SDHCI_ERR_INT_ALL_MASK                (0xffff)

/* Auto CMD Error Status Register */

/* Bits 15-8: Reserved */

#define SDHCI_AUTO_CMD_ERR_CMD_NOT_ISSUED     (1 << 7)  /* CMD Not Issued By Auto CMD12 */

/* Bit 6: Reserved */

#define SDHCI_AUTO_CMD_ERR_RESPONSE           (1 << 5)  /* Auto CMD Response Error */
#define SDHCI_AUTO_CMD_ERR_INDEX              (1 << 4)  /* Auto CMD Index Error */
#define SDHCI_AUTO_CMD_ERR_END_BIT            (1 << 3)  /* Auto CMD End Bit Error */
#define SDHCI_AUTO_CMD_ERR_CRC                (1 << 2)  /* Auto CMD CRC Error */
#define SDHCI_AUTO_CMD_ERR_TIMEOUT            (1 << 1)  /* Auto CMD Timeout Error */
#define SDHCI_AUTO_CMD_ERR_NOT_EXECUTED       (1 << 0)  /* Auto CMD12 Not Executed */

/* Host Control 2 Register */

#define SDHCI_HOST_CONTROL_2_PRESET_ENABLE    (1 << 15) /* Preset Value Enable */
#define SDHCI_HOST_CONTROL_2_ASYNC_INT_EN     (1 << 14) /* Async Interrupt Enable */
#define SDHCI_HOST_CONTROL_2_ADDR_64BIT       (1 << 13) /* 64-bit Addressing (V4) */
#define SDHCI_HOST_CONTROL_2_VERSION_4_EN     (1 << 12) /* Host Version 4 Enable */
#define SDHCI_HOST_CONTROL_2_CMD23_ENABLE     (1 << 11) /* CMD23 Enable */
#define SDHCI_HOST_CONTROL_2_ADMA2_LENGTH     (1 << 10) /* ADMA2 Length Mode */

/* Bit 9: Reserved */

#define SDHCI_HOST_CONTROL_2_UHS2_ENABLE      (1 << 8)  /* UHS-II Interface Enable */
#define SDHCI_HOST_CONTROL_2_SAMPLING_CLOCK   (1 << 7)  /* Sampling Clock Select */
#define SDHCI_HOST_CONTROL_2_EXECUTE_TUNING   (1 << 6)  /* Execute Tuning (RWAC) */

#define SDHCI_HOST_CONTROL_2_DRIVER_SHIFT     (4)       /* Driver Strength Select */
#define SDHCI_HOST_CONTROL_2_DRIVER_MASK      (3 << SDHCI_HOST_CONTROL_2_DRIVER_SHIFT)
#  define SDHCI_HOST_CONTROL_2_DRIVER_TYPE_B  (0 << SDHCI_HOST_CONTROL_2_DRIVER_SHIFT)
#  define SDHCI_HOST_CONTROL_2_DRIVER_TYPE_A  (1 << SDHCI_HOST_CONTROL_2_DRIVER_SHIFT)
#  define SDHCI_HOST_CONTROL_2_DRIVER_TYPE_C  (2 << SDHCI_HOST_CONTROL_2_DRIVER_SHIFT)
#  define SDHCI_HOST_CONTROL_2_DRIVER_TYPE_D  (3 << SDHCI_HOST_CONTROL_2_DRIVER_SHIFT)

#define SDHCI_HOST_CONTROL_2_1_8V_SIGNALING   (1 << 3)  /* 1.8V Signaling Enable */

#define SDHCI_HOST_CONTROL_2_UHS_MODE_SHIFT   (0)       /* UHS Mode Select */
#define SDHCI_HOST_CONTROL_2_UHS_MODE_MASK    (7 << SDHCI_HOST_CONTROL_2_UHS_MODE_SHIFT)
#  define SDHCI_HOST_CONTROL_2_UHS_SDR12      (0 << SDHCI_HOST_CONTROL_2_UHS_MODE_SHIFT)
#  define SDHCI_HOST_CONTROL_2_UHS_SDR25      (1 << SDHCI_HOST_CONTROL_2_UHS_MODE_SHIFT)
#  define SDHCI_HOST_CONTROL_2_UHS_SDR50      (2 << SDHCI_HOST_CONTROL_2_UHS_MODE_SHIFT)
#  define SDHCI_HOST_CONTROL_2_UHS_SDR104     (3 << SDHCI_HOST_CONTROL_2_UHS_MODE_SHIFT)
#  define SDHCI_HOST_CONTROL_2_UHS_DDR50      (4 << SDHCI_HOST_CONTROL_2_UHS_MODE_SHIFT)
#  define SDHCI_HOST_CONTROL_2_UHS_UHS2       (7 << SDHCI_HOST_CONTROL_2_UHS_MODE_SHIFT)

/* Capabilities Register (64-bit) */

/* Bits 63-62: Reserved */

/* Bit 61: Reserved for future VDD2 */

#define SDHCI_CAPABILITIES_VDD2_1_8V          (1ULL << 60) /* 1.8V VDD2 Support */
#define SDHCI_CAPABILITIES_ADMA3              (1ULL << 59) /* ADMA3 Support */

/* Bits 58-56: Reserved */

#define SDHCI_CAPABILITIES_CLOCK_MULT_SHIFT   (48)
#define SDHCI_CAPABILITIES_CLOCK_MULT_MASK    (0xffULL << SDHCI_CAPABILITIES_CLOCK_MULT_SHIFT)

#define SDHCI_CAPABILITIES_RETUNING_MODE_SHIFT (46)
#define SDHCI_CAPABILITIES_RETUNING_MODE_MASK (3ULL << SDHCI_CAPABILITIES_RETUNING_MODE_SHIFT)

#define SDHCI_CAPABILITIES_TUNING_SDR50       (1ULL << 45) /* Use Tuning for SDR50 */

/* Bit 44: Reserved */

#define SDHCI_CAPABILITIES_RETUNING_TIMER_SHIFT (40)
#define SDHCI_CAPABILITIES_RETUNING_TIMER_MASK (0xfULL << SDHCI_CAPABILITIES_RETUNING_TIMER_SHIFT)

/* Bit 39: Reserved */

#define SDHCI_CAPABILITIES_DRIVER_TYPE_D      (1ULL << 38) /* Driver Type D Support */
#define SDHCI_CAPABILITIES_DRIVER_TYPE_C      (1ULL << 37) /* Driver Type C Support */
#define SDHCI_CAPABILITIES_DRIVER_TYPE_A      (1ULL << 36) /* Driver Type A Support */
#define SDHCI_CAPABILITIES_UHS2               (1ULL << 35) /* UHS-II Support */
#define SDHCI_CAPABILITIES_DDR50              (1ULL << 34) /* DDR50 Support */
#define SDHCI_CAPABILITIES_SDR104             (1ULL << 33) /* SDR104 Support */
#define SDHCI_CAPABILITIES_SDR50              (1ULL << 32) /* SDR50 Support */

#define SDHCI_CAPABILITIES_SLOT_TYPE_SHIFT    (30)
#define SDHCI_CAPABILITIES_SLOT_TYPE_MASK     (3ULL << SDHCI_CAPABILITIES_SLOT_TYPE_SHIFT)
#  define SDHCI_CAPABILITIES_SLOT_REMOVABLE   (0ULL << SDHCI_CAPABILITIES_SLOT_TYPE_SHIFT)
#  define SDHCI_CAPABILITIES_SLOT_EMBEDDED    (1ULL << SDHCI_CAPABILITIES_SLOT_TYPE_SHIFT)
#  define SDHCI_CAPABILITIES_SLOT_SHARED      (2ULL << SDHCI_CAPABILITIES_SLOT_TYPE_SHIFT)

#define SDHCI_CAPABILITIES_ASYNC_INT          (1ULL << 29) /* Async Interrupt Support */
#define SDHCI_CAPABILITIES_64BIT_V3           (1ULL << 28) /* 64-bit System Address (V3) */
#define SDHCI_CAPABILITIES_64BIT_V4           (1ULL << 27) /* 64-bit System Address (V4) */
#define SDHCI_CAPABILITIES_VOLTAGE_1_8V       (1ULL << 26) /* Voltage Support 1.8V */
#define SDHCI_CAPABILITIES_VOLTAGE_3_0V       (1ULL << 25) /* Voltage Support 3.0V */
#define SDHCI_CAPABILITIES_VOLTAGE_3_3V       (1ULL << 24) /* Voltage Support 3.3V */
#define SDHCI_CAPABILITIES_SUSPEND_RESUME     (1ULL << 23) /* Suspend/Resume Support */
#define SDHCI_CAPABILITIES_SDMA               (1ULL << 22) /* SDMA Support */
#define SDHCI_CAPABILITIES_HIGH_SPEED         (1ULL << 21) /* High Speed Support */

/* Bit 20: Reserved */

#define SDHCI_CAPABILITIES_ADMA2              (1ULL << 19) /* ADMA2 Support */
#define SDHCI_CAPABILITIES_8BIT               (1ULL << 18) /* 8-bit Support (Embedded) */

#define SDHCI_CAPABILITIES_MAX_BLOCK_LEN_SHIFT (16)
#define SDHCI_CAPABILITIES_MAX_BLOCK_LEN_MASK (3ULL << SDHCI_CAPABILITIES_MAX_BLOCK_LEN_SHIFT)
#  define SDHCI_CAPABILITIES_MAX_BLOCK_512    (0ULL << SDHCI_CAPABILITIES_MAX_BLOCK_LEN_SHIFT)
#  define SDHCI_CAPABILITIES_MAX_BLOCK_1024   (1ULL << SDHCI_CAPABILITIES_MAX_BLOCK_LEN_SHIFT)
#  define SDHCI_CAPABILITIES_MAX_BLOCK_2048   (2ULL << SDHCI_CAPABILITIES_MAX_BLOCK_LEN_SHIFT)

#define SDHCI_CAPABILITIES_BASE_CLOCK_SHIFT   (8)
#define SDHCI_CAPABILITIES_BASE_CLOCK_MASK    (0xffULL << SDHCI_CAPABILITIES_BASE_CLOCK_SHIFT)

#define SDHCI_CAPABILITIES_TIMEOUT_UNIT       (1ULL << 7)  /* Timeout Clock Unit */

/* Bit 6: Reserved */

#define SDHCI_CAPABILITIES_TIMEOUT_FREQ_SHIFT (0)
#define SDHCI_CAPABILITIES_TIMEOUT_FREQ_MASK  (0x3fULL << SDHCI_CAPABILITIES_TIMEOUT_FREQ_SHIFT)

/* Maximum Current Capabilities Register (64-bit) */

/* Bits 63-40: Reserved */

#define SDHCI_MAX_CURRENT_VDD2_SHIFT          (32)
#define SDHCI_MAX_CURRENT_VDD2_MASK           (0xffULL << SDHCI_MAX_CURRENT_VDD2_SHIFT)

/* Bits 31-24: Reserved */

#define SDHCI_MAX_CURRENT_VDD1_1_8V_SHIFT     (16)
#define SDHCI_MAX_CURRENT_VDD1_1_8V_MASK      (0xffULL << SDHCI_MAX_CURRENT_VDD1_1_8V_SHIFT)

#define SDHCI_MAX_CURRENT_VDD1_3_0V_SHIFT     (8)
#define SDHCI_MAX_CURRENT_VDD1_3_0V_MASK      (0xffULL << SDHCI_MAX_CURRENT_VDD1_3_0V_SHIFT)

#define SDHCI_MAX_CURRENT_VDD1_3_3V_SHIFT     (0)
#define SDHCI_MAX_CURRENT_VDD1_3_3V_MASK      (0xffULL << SDHCI_MAX_CURRENT_VDD1_3_3V_SHIFT)

/* Force Event Register for Auto CMD Error Status */

/* Bits 15-8: Reserved */

#define SDHCI_FORCE_AUTO_CMD_CMD_NOT_ISSUED   (1 << 7)  /* Force CMD Not Issued */

/* Bit 6: Reserved */

#define SDHCI_FORCE_AUTO_CMD_RESPONSE         (1 << 5)  /* Force Auto CMD Response Error */
#define SDHCI_FORCE_AUTO_CMD_INDEX            (1 << 4)  /* Force Auto CMD Index Error */
#define SDHCI_FORCE_AUTO_CMD_END_BIT          (1 << 3)  /* Force Auto CMD End Bit Error */
#define SDHCI_FORCE_AUTO_CMD_CRC              (1 << 2)  /* Force Auto CMD CRC Error */
#define SDHCI_FORCE_AUTO_CMD_TIMEOUT          (1 << 1)  /* Force Auto CMD Timeout Error */
#define SDHCI_FORCE_AUTO_CMD_NOT_EXECUTED     (1 << 0)  /* Force Auto CMD12 Not Executed */

/* Force Event Register for Error Interrupt Status */

#define SDHCI_FORCE_ERR_INT_VENDOR_SHIFT      (12)      /* Force Vendor Specific Error */
#define SDHCI_FORCE_ERR_INT_VENDOR_MASK       (0xf << SDHCI_FORCE_ERR_INT_VENDOR_SHIFT)

#define SDHCI_FORCE_ERR_INT_RESPONSE          (1 << 11) /* Force Response Error */
#define SDHCI_FORCE_ERR_INT_TUNING            (1 << 10) /* Force Tuning Error */
#define SDHCI_FORCE_ERR_INT_ADMA              (1 << 9)  /* Force ADMA Error */
#define SDHCI_FORCE_ERR_INT_AUTO_CMD          (1 << 8)  /* Force Auto CMD Error */
#define SDHCI_FORCE_ERR_INT_CURRENT_LIMIT     (1 << 7)  /* Force Current Limit Error */
#define SDHCI_FORCE_ERR_INT_DATA_END_BIT      (1 << 6)  /* Force Data End Bit Error */
#define SDHCI_FORCE_ERR_INT_DATA_CRC          (1 << 5)  /* Force Data CRC Error */
#define SDHCI_FORCE_ERR_INT_DATA_TIMEOUT      (1 << 4)  /* Force Data Timeout Error */
#define SDHCI_FORCE_ERR_INT_CMD_INDEX         (1 << 3)  /* Force Command Index Error */
#define SDHCI_FORCE_ERR_INT_CMD_END_BIT       (1 << 2)  /* Force Command End Bit Error */
#define SDHCI_FORCE_ERR_INT_CMD_CRC           (1 << 1)  /* Force Command CRC Error */
#define SDHCI_FORCE_ERR_INT_CMD_TIMEOUT       (1 << 0)  /* Force Command Timeout Error */

/* ADMA Error Status Register */

/* Bits 7-3: Reserved */

#define SDHCI_ADMA_ERR_LENGTH_MISMATCH        (1 << 2)  /* ADMA Length Mismatch Error */

#define SDHCI_ADMA_ERR_STATE_SHIFT            (0)       /* ADMA Error State */
#define SDHCI_ADMA_ERR_STATE_MASK             (3 << SDHCI_ADMA_ERR_STATE_SHIFT)
#  define SDHCI_ADMA_ERR_STATE_STOP           (0 << SDHCI_ADMA_ERR_STATE_SHIFT)
#  define SDHCI_ADMA_ERR_STATE_FDS            (1 << SDHCI_ADMA_ERR_STATE_SHIFT)
#  define SDHCI_ADMA_ERR_STATE_TFR            (3 << SDHCI_ADMA_ERR_STATE_SHIFT)

/* Preset Value Registers.
 * Common bit field definitions for all Preset Value registers.
 */

#define SDHCI_PRESET_DRIVER_SHIFT             (14)      /* Driver Strength Select */
#define SDHCI_PRESET_DRIVER_MASK              (3 << SDHCI_PRESET_DRIVER_SHIFT)
#  define SDHCI_PRESET_DRIVER_TYPE_B          (0 << SDHCI_PRESET_DRIVER_SHIFT)
#  define SDHCI_PRESET_DRIVER_TYPE_A          (1 << SDHCI_PRESET_DRIVER_SHIFT)
#  define SDHCI_PRESET_DRIVER_TYPE_C          (2 << SDHCI_PRESET_DRIVER_SHIFT)
#  define SDHCI_PRESET_DRIVER_TYPE_D          (3 << SDHCI_PRESET_DRIVER_SHIFT)

/* Bits 13-11: Reserved */

#define SDHCI_PRESET_CLOCK_GENERATOR          (1 << 10) /* Clock Generator Select */

#define SDHCI_PRESET_FREQ_SHIFT               (0)       /* SDCLK Frequency Select */
#define SDHCI_PRESET_FREQ_MASK                (0x3ff << SDHCI_PRESET_FREQ_SHIFT)

/* Host Controller Version Register */

#define SDHCI_HOST_VERSION_VENDOR_SHIFT       (8)       /* Vendor Version */
#define SDHCI_HOST_VERSION_VENDOR_MASK        (0xff << SDHCI_HOST_VERSION_VENDOR_SHIFT)

#define SDHCI_HOST_VERSION_SPEC_SHIFT         (0)       /* Specification Version Number */
#define SDHCI_HOST_VERSION_SPEC_MASK          (0xff << SDHCI_HOST_VERSION_SPEC_SHIFT)
#  define SDHCI_HOST_VERSION_SPEC_1           (0 << SDHCI_HOST_VERSION_SPEC_SHIFT)
#  define SDHCI_HOST_VERSION_SPEC_2           (1 << SDHCI_HOST_VERSION_SPEC_SHIFT)
#  define SDHCI_HOST_VERSION_SPEC_3           (2 << SDHCI_HOST_VERSION_SPEC_SHIFT)
#  define SDHCI_HOST_VERSION_SPEC_4           (3 << SDHCI_HOST_VERSION_SPEC_SHIFT)

/* Interrupt mask combinations.
 * Normal interrupt bits (030h) and error interrupt bits (032h) are in
 * separate 16-bit registers. We keep them separate to avoid incorrect
 * masking in the ISR.
 */

/* Error bits for command response (032h bits 0-3) */

#define SDHCI_RESPERR_INTS                    (SDHCI_ERR_INT_CMD_TIMEOUT  | \
                                               SDHCI_ERR_INT_CMD_CRC      | \
                                               SDHCI_ERR_INT_CMD_END_BIT  | \
                                               SDHCI_ERR_INT_CMD_INDEX)

/* Normal bits for command response (030h bit 0 = CC) */

#define SDHCI_RESPDONE_INTS                   (SDHCI_INT_CMD_COMPLETE)

/* Error bits for data transfer (032h bits 4-6) */

#define SDHCI_XFRERR_INTS                     (SDHCI_ERR_INT_DATA_TIMEOUT | \
                                               SDHCI_ERR_INT_DATA_CRC     | \
                                               SDHCI_ERR_INT_DATA_END_BIT)

/* Normal bits for receive: BRR + TC (030h bits 5,1) */

#define SDHCI_RCV_XFR_INTS                    (SDHCI_INT_READ_READY | \
                                               SDHCI_INT_TRANSFER_COMPLETE)

/* Normal bits for send: BWR + TC (030h bits 4,1) */

#define SDHCI_SND_XFR_INTS                    (SDHCI_INT_WRITE_READY | \
                                               SDHCI_INT_TRANSFER_COMPLETE)

/* Normal bits for transfer done (030h bit 1 = TC) */

#define SDHCI_XFRDONE_INTS                    (SDHCI_INT_TRANSFER_COMPLETE)

#define SDHCI_WAITALL_INTS                    (SDHCI_RESPDONE_INTS | SDHCI_XFRDONE_INTS)

/****************************************************************************
 * Public Types
 ****************************************************************************/

/****************************************************************************
 * Public Data
 ****************************************************************************/

/****************************************************************************
 * Public Functions Prototypes
 ****************************************************************************/

#endif /* __DRIVERS_MMCSD_MMCSD_SDHCI_H */
