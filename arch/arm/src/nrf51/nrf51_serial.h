/****************************************************************************
 * arch/arm/src/nrf51/nrf51_serial.h
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

#ifndef __ARCH_ARM_SRC_NRF51_NRF51_SERIAL_H
#define __ARCH_ARM_SRC_NRF51_NRF51_SERIAL_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* nRF51 UART Register Offsets (Task and Event registers) */

#define NRF51_UART_STARTRX_OFFSET       0x000  /* Start UART receiver */
#define NRF51_UART_STOPRX_OFFSET        0x004  /* Stop UART receiver */
#define NRF51_UART_STARTTX_OFFSET       0x008  /* Start UART transmitter */
#define NRF51_UART_STOPTX_OFFSET        0x00C  /* Stop UART transmitter */
#define NRF51_UART_SUSPEND_OFFSET       0x01C  /* Suspend UART */

#define NRF51_UART_CTS_OFFSET           0x100  /* CTS event */
#define NRF51_UART_NCTS_OFFSET          0x104  /* NCTS event */
#define NRF51_UART_RXDRDY_OFFSET        0x108  /* Data received in RXD */
#define NRF51_UART_TXDRDY_OFFSET        0x11C  /* Data sent from TXD */
#define NRF51_UART_ERROR_OFFSET         0x124  /* Error detected */
#define NRF51_UART_RXTO_OFFSET          0x144  /* Receiver timeout */

#define NRF51_UART_INTEN_OFFSET         0x300  /* Interrupt enable register */
#define NRF51_UART_INTENSET_OFFSET      0x304  /* Interrupt enable set */
#define NRF51_UART_INTENCLR_OFFSET      0x308  /* Interrupt enable clear */
#define NRF51_UART_ERRORSRC_OFFSET      0x480  /* Error source */
#define NRF51_UART_ENABLE_OFFSET        0x500  /* Enable UART */
#define NRF51_UART_PSELRTS_OFFSET       0x508  /* Pin select for RTS */
#define NRF51_UART_PSELTXD_OFFSET       0x50C  /* Pin select for TXD */
#define NRF51_UART_PSELCTS_OFFSET       0x510  /* Pin select for CTS */
#define NRF51_UART_PSELRXD_OFFSET       0x514  /* Pin select for RXD */
#define NRF51_UART_RXD_OFFSET           0x518  /* RXD register */
#define NRF51_UART_TXD_OFFSET           0x51C  /* TXD register */
#define NRF51_UART_BAUDRATE_OFFSET      0x524  /* Baud rate */
#define NRF51_UART_CONFIG_OFFSET        0x56C  /* Configuration */

/* UART Interrupt Enable bits */

#define NRF51_UART_INT_CTS              (1 << 0)
#define NRF51_UART_INT_NCTS             (1 << 1)
#define NRF51_UART_INT_RXDRDY           (1 << 2)
#define NRF51_UART_INT_TXDRDY           (1 << 7)
#define NRF51_UART_INT_ERROR            (1 << 9)
#define NRF51_UART_INT_RXTO             (1 << 17)

/* UART Enable values */

#define NRF51_UART_ENABLE_DISABLED      0x00
#define NRF51_UART_ENABLE_ENABLED       0x04

/* UART Baudrate values */

#define NRF51_UART_BAUDRATE_1200        0x0004F000
#define NRF51_UART_BAUDRATE_2400        0x0009D000
#define NRF51_UART_BAUDRATE_4800        0x0013B000
#define NRF51_UART_BAUDRATE_9600        0x00275000
#define NRF51_UART_BAUDRATE_14400       0x003B0000
#define NRF51_UART_BAUDRATE_19200       0x004EA000
#define NRF51_UART_BAUDRATE_28800       0x0075F000
#define NRF51_UART_BAUDRATE_31250       0x00800000
#define NRF51_UART_BAUDRATE_38400       0x009D5000
#define NRF51_UART_BAUDRATE_56000       0x00E50000
#define NRF51_UART_BAUDRATE_57600       0x00EBF000
#define NRF51_UART_BAUDRATE_76800       0x013A9000
#define NRF51_UART_BAUDRATE_115200      0x01D7E000
#define NRF51_UART_BAUDRATE_230400      0x03AFB000
#define NRF51_UART_BAUDRATE_250000      0x04000000
#define NRF51_UART_BAUDRATE_460800      0x075F7000
#define NRF51_UART_BAUDRATE_921600      0x0EBEDFA4
#define NRF51_UART_BAUDRATE_1M          0x10000000

/* UART Error Source bits */

#define NRF51_UART_ERRORSRC_OVERRUN     (1 << 0)
#define NRF51_UART_ERRORSRC_PARITY      (1 << 1)
#define NRF51_UART_ERRORSRC_FRAMING     (1 << 2)
#define NRF51_UART_ERRORSRC_BREAK       (1 << 3)

/* UART Config bits */

#define NRF51_UART_CONFIG_HWFC          (1 << 0)  /* Hardware flow control */
#define NRF51_UART_CONFIG_PARITY_NONE   (0 << 1)
#define NRF51_UART_CONFIG_PARITY_EVEN   (7 << 1)

/* Console configuration */

#if defined(CONFIG_NRF51_UART0_SERIAL_CONSOLE) && defined(CONFIG_NRF51_UART0)
#  undef CONFIG_NRF51_UART1_SERIAL_CONSOLE
#  define HAVE_NRF51_CONSOLE             1
#elif defined(CONFIG_NRF51_UART1_SERIAL_CONSOLE) && defined(CONFIG_NRF51_UART1)
#  undef CONFIG_NRF51_UART0_SERIAL_CONSOLE
#  define HAVE_NRF51_CONSOLE             1
#else
#  undef CONFIG_NRF51_UART0_SERIAL_CONSOLE
#  undef CONFIG_NRF51_UART1_SERIAL_CONSOLE
#  undef HAVE_NRF51_CONSOLE
#endif

/****************************************************************************
 * Public Functions Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: nrf51_earlyserialinit
 *
 * Description:
 *   Performs the low level UART initialization early in debug so that the
 *   serial console will be available during bootup.  This must be called
 *   before uart_serialinit.
 *
 ****************************************************************************/

void nrf51_earlyserialinit(void);

/****************************************************************************
 * Name: nrf51_serialinit
 *
 * Description:
 *   Register serial console and serial ports.  This assumes that
 *   nrf51_earlyserialinit was called previously.
 *
 ****************************************************************************/

void nrf51_serialinit(void);

#endif /* __ARCH_ARM_SRC_NRF51_NRF51_SERIAL_H */
