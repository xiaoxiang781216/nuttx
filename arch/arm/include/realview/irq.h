/****************************************************************************
 * arch/arm/include/realview/irq.h
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

/* This file should never be included directly but, rather,
 * only indirectly through nuttx/irq.h
 */

#ifndef __ARCH_ARM_INCLUDE_REALVIEW_IRQ_H
#define __ARCH_ARM_INCLUDE_REALVIEW_IRQ_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

/****************************************************************************
 * Pre-processor Prototypes
 ****************************************************************************/

/* RealView-EB Interrupts (GIC SPI, offset by 32) */

#define REALVIEW_IRQ_WATCHDOG  32  /* Watchdog timer (SPI 0) */
#define REALVIEW_IRQ_SOFTWARE  33  /* Software interrupt (SPI 1) */
#define REALVIEW_IRQ_COMMSRX   34  /* Debug comms receive (SPI 2) */
#define REALVIEW_IRQ_COMMSTX   35  /* Debug comms transmit (SPI 3) */
#define REALVIEW_IRQ_TIMER01   36  /* Timer 0/1 SP804 (SPI 4) */
#define REALVIEW_IRQ_TIMER23   37  /* Timer 2/3 SP804 (SPI 5) */
#define REALVIEW_IRQ_GPIO0     38  /* GPIO 0 (SPI 6) */
#define REALVIEW_IRQ_GPIO1     39  /* GPIO 1 (SPI 7) */
#define REALVIEW_IRQ_GPIO2     40  /* GPIO 2 (SPI 8) */
#define REALVIEW_IRQ_RTC       42  /* Real Time Clock (SPI 10) */
#define REALVIEW_IRQ_SSP       43  /* Synchronous Serial Port (SPI 11) */
#define REALVIEW_IRQ_UART0     44  /* UART 0 (SPI 12) */
#define REALVIEW_IRQ_UART1     45  /* UART 1 (SPI 13) */
#define REALVIEW_IRQ_UART2     46  /* UART 2 (SPI 14) */
#define REALVIEW_IRQ_UART3     47  /* UART 3 (SPI 15) */
#define REALVIEW_IRQ_SCI       48  /* Smart Card Interface (SPI 16) */
#define REALVIEW_IRQ_MCI0      49  /* MCI 0 PL181 MMC (SPI 17) */
#define REALVIEW_IRQ_MCI1      50  /* MCI 1 PL181 MMC (SPI 18) */
#define REALVIEW_IRQ_AACI      51  /* Audio PL041 (SPI 19) */
#define REALVIEW_IRQ_KMI0      52  /* Keyboard PL050 (SPI 20) */
#define REALVIEW_IRQ_KMI1      53  /* Mouse PL050 (SPI 21) */
#define REALVIEW_IRQ_CLCD      55  /* CLCD controller (SPI 23) */
#define REALVIEW_IRQ_DMA       56  /* DMA controller (SPI 24) */
#define REALVIEW_IRQ_ETH       60  /* Ethernet SMC91c111 (SPI 28) */

#define NR_IRQS                96  /* SGI(16)+PPI(16)+SPI(64) */

/****************************************************************************
 * Public Types
 ****************************************************************************/

/****************************************************************************
 * Inline functions
 ****************************************************************************/

/****************************************************************************
 * Public Data
 ****************************************************************************/

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifndef __ASSEMBLY__
#ifdef __cplusplus
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

#undef EXTERN
#ifdef __cplusplus
}
#endif
#endif

#endif /* __ARCH_ARM_INCLUDE_REALVIEW_IRQ_H */
