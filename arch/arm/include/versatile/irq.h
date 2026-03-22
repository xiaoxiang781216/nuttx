/****************************************************************************
 * arch/arm/include/versatile/irq.h
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

#ifndef __ARCH_ARM_INCLUDE_VERSATILE_IRQ_H
#define __ARCH_ARM_INCLUDE_VERSATILE_IRQ_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

/****************************************************************************
 * Pre-processor Prototypes
 ****************************************************************************/

/* VersatilePB Interrupts (Primary VIC, PL190) */

#define VERSATILE_IRQ_WATCHDOG  0  /* Watchdog timer */
#define VERSATILE_IRQ_SOFTWARE  1  /* Software interrupt */
#define VERSATILE_IRQ_COMMSRX   2  /* Debug comms receive */
#define VERSATILE_IRQ_COMMSTX   3  /* Debug comms transmit */
#define VERSATILE_IRQ_TIMER01   4  /* Timer 0/1 (SP804) */
#define VERSATILE_IRQ_TIMER23   5  /* Timer 2/3 (SP804) */
#define VERSATILE_IRQ_GPIO0     6  /* GPIO 0 */
#define VERSATILE_IRQ_GPIO1     7  /* GPIO 1 */
#define VERSATILE_IRQ_GPIO2     8  /* GPIO 2 */
#define VERSATILE_IRQ_GPIO3     9  /* GPIO 3 */
#define VERSATILE_IRQ_RTC      10  /* Real Time Clock */
#define VERSATILE_IRQ_SSP      11  /* Synchronous Serial Port */
#define VERSATILE_IRQ_UART0    12  /* UART 0 */
#define VERSATILE_IRQ_UART1    13  /* UART 1 */
#define VERSATILE_IRQ_UART2    14  /* UART 2 */
#define VERSATILE_IRQ_SCI      15  /* Smart Card Interface */
#define VERSATILE_IRQ_CLCD     16  /* CLCD controller */
#define VERSATILE_IRQ_DMA      17  /* DMA controller */
#define VERSATILE_IRQ_PWRFAIL  18  /* Power failure */
#define VERSATILE_IRQ_MBX      19  /* Graphics processor */
#define VERSATILE_IRQ_GND      20  /* Reserved */
#define VERSATILE_IRQ_SIC      31  /* Secondary interrupt controller */

#define NR_IRQS                32  /* Primary VIC: 32 interrupt lines */

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

#endif /* __ARCH_ARM_INCLUDE_VERSATILE_IRQ_H */
