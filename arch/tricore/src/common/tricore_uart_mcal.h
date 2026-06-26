/****************************************************************************
 * arch/tricore/src/common/tricore_uart_mcal.h
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

#ifndef __ARCH_TRICORE_SRC_COMMON_TRICORE_UART_MCAL_H
#define __ARCH_TRICORE_SRC_COMMON_TRICORE_UART_MCAL_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/serial/serial.h>

#include "Uart.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Maximum UART channels supported */

#define MCAL_UART_MAX_CHANNELS CONFIG_MCAL_UART_MAX_CHANNELS

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Per-channel board-level configuration for MCAL UART */

struct mcal_uart_info_s
{
  uint8_t  channel_id;  /* MCAL Uart channel ID (logical), same as
                         * the channelid configured in EB
                         */
  uint8_t  hw_module;   /* HW module index for ISR routing */
  uint16_t rx_irq;      /* RX interrupt number */
  uint16_t tx_irq;      /* TX interrupt number */
  uint16_t err_irq;     /* Error interrupt number */
  uint16_t recv_len;    /* RX ring buffer size */
  uint16_t xmit_len;    /* TX buffer size */
  int      bus;         /* UART bus number for /dev/ttySx */
  bool     isconsole;   /* true if this is the console UART */

  /* DMA configuration (-1 = DMA disabled for this direction) */

  int16_t  dma_tx_channel; /* DMA channel ID for TX, -1 if unused */
  int16_t  dma_rx_channel; /* DMA channel ID for RX, -1 if unused */
  uint16_t dma_tx_irq;    /* DMA TX channel completion IRQ number */
  uint16_t dma_rx_irq;    /* DMA RX channel completion IRQ number */
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: mcal_uart_initialize
 *
 * Description:
 *   Initialize MCAL UART channels and register them with NuttX serial
 *   framework. This function:
 *   1. Calls Uart_Init() with mcal_config for this core's partition
 *   2. For each channel in config[] creates a uart_dev_s and registers
 *      it as /dev/ttySx (and /dev/console if configured)
 *   3. Sets up ISR routing from NuttX IRQ to MCAL ISR handlers
 *
 * Input Parameters:
 *   config      - MCAL config pointer for this core (from SchM_Uart.c),
 *                 NULL if this core has no UART partition
 *   infos       - Array of per-core board-level configurations
 *   num         - Number of entries in infos array
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure.
 *
 ****************************************************************************/

int mcal_uart_initialize(const Uart_ConfigType *config,
                         const struct mcal_uart_info_s *infos,
                         size_t num);

/****************************************************************************
 * Name: mcal_uart_streaming_notify
 *
 * Description:
 *   Streaming RX notification handler. Called from board-level
 *   notification callback wrappers when MCAL streaming RX completes.
 *
 * Input Parameters:
 *   channel_id - MCAL Uart channel ID
 *   error      - MCAL error code (UART_E_NO_ERR on success)
 *   size       - Number of bytes received in streaming buffer
 *
 ****************************************************************************/

void mcal_uart_streaming_notify(uint8_t channel_id,
                                Uart_ErrorIdType error,
                                uint16_t size);

/****************************************************************************
 * Name: mcal_uart_tx_notify
 *
 * Description:
 *   TX completion notification handler. Called from board-level
 *   notification callback wrappers when MCAL TX completes.
 *
 * Input Parameters:
 *   channel_id - MCAL Uart channel ID
 *   error      - MCAL error code (UART_E_NO_ERR on success)
 *
 ****************************************************************************/

void mcal_uart_tx_notify(uint8_t channel_id,
                         Uart_ErrorIdType error);

#endif /* __ARCH_TRICORE_SRC_COMMON_TRICORE_UART_MCAL_H */
