/****************************************************************************
 * arch/arm64/src/imx9/imx9_serial_pl011.c
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

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <nuttx/serial/uart_pl011.h>

#include "arm64_internal.h"

#ifdef USE_SERIALDRIVER

/****************************************************************************
 * Description
 ****************************************************************************/

/* Thin arm64-arch shim that redirects the generic lower-half serial init
 * hooks to the common PL011 driver (drivers/serial/uart_pl011.c). This
 * file is compiled ONLY when the i.MX9 chip's native LPUART driver
 * (imx9_lpuart.c, gated on CONFIG_IMX9_LPUARTn) is NOT in use, i.e. when
 * the board uses the PL011 driver instead.
 *
 * Typical consumer: boards that run NuttX as an ACRN VM, where the UART
 * the guest sees is a PL011 vuart exposed by the hypervisor rather than
 * the real i.MX LPUART hardware. In that case CONFIG_UART_PL011=y and
 * CONFIG_UARTn_PL011=y; no CONFIG_IMX9_LPUARTn is selected.
 *
 * If in the future a board enables both a real passthrough LPUART and
 * an additional PL011 vuart, the existing imx9_lpuart.c already defines
 * arm64_{early,}serialinit(), so this shim is excluded by CMakeLists.
 */

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void arm64_earlyserialinit(void)
{
#ifdef CONFIG_UART_PL011
  pl011_earlyserialinit();
#endif
}

void arm64_serialinit(void)
{
#ifdef CONFIG_UART_PL011
  pl011_serialinit();
#endif
}

#endif /* USE_SERIALDRIVER */
