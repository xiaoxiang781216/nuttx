# ##############################################################################
# arch/arm64/src/imx9/CMakeLists.txt
#
# SPDX-License-Identifier: Apache-2.0
#
# Licensed to the Apache Software Foundation (ASF) under one or more contributor
# license agreements.  See the NOTICE file distributed with this work for
# additional information regarding copyright ownership.  The ASF licenses this
# file to you under the Apache License, Version 2.0 (the "License"); you may not
# use this file except in compliance with the License.  You may obtain a copy of
# the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
# License for the specific language governing permissions and limitations under
# the License.
#
# ##############################################################################

set(SRCS imx9_clockconfig.c imx9_iomuxc.c imx9_lowputc.c)

# Boot / MMU region table selection:
#
# * On the ACRN guest board (imx95-a55-acrn) NuttX runs inside a VM carved out
#   by the hypervisor. The native chip bring-up path (DDR training, TRDC, MIX
#   power-up, LPUART pin muxing) is not applicable, and the MMU region table has
#   to describe the HPA window + emulated device range the guest actually sees
#   rather than the raw EVK memory map.
#
# * For any other board we use imx9_boot.c, which owns the real hardware
#   bring-up sequence and the chip-default RAMBANK1 layout from
#   arch/arm64/include/imx9/chip.h.
if(CONFIG_IMX95_ACRN_GUEST)
  list(APPEND SRCS imx9_acrn_boot.c)
else()
  list(APPEND SRCS imx9_boot.c)
endif()

# Serial lower-half selection:
#
# * If any CONFIG_IMX9_LPUARTn is selected, build the native i.MX9 LPUART driver
#   (imx9_lpuart.c). It provides its own arm64_{early,}serialinit() against the
#   common upper-half. (Every CONFIG_IMX9_LPUARTn 'select's the umbrella
#   CONFIG_IMX9_LPUART in Kconfig, so the single symbol covers all eight
#   instances.)
#
# * Otherwise, if CONFIG_UART_PL011 is selected (e.g. NuttX running as an ACRN
#   VM where the console is a PL011 vuart), build the thin PL011 shim
#   (imx9_serial_pl011.c) which forwards to drivers/serial/uart_pl011.c.
#
# The two are mutually exclusive; at most one is compiled to keep the
# arm64_{early,}serialinit() symbols unique.
if(CONFIG_IMX9_LPUART)
  list(APPEND SRCS imx9_lpuart.c)
elseif(CONFIG_UART_PL011)
  list(APPEND SRCS imx9_serial_pl011.c)
endif()

if(CONFIG_ARCH_CHIP_IMX93)
  list(APPEND SRCS imx9_ccm.c imx9_gpio.c imx9_gpiobase.c)
elseif(CONFIG_ARCH_CHIP_IMX95)
  list(APPEND SRCS imx9_gpio.c imx9_gpiobase.c)
endif()

if(CONFIG_IMX9_GPIO_IRQ)
  list(APPEND SRCS imx9_gpioirq.c)
endif()

if(CONFIG_IMX9_MU)
  list(APPEND SRCS imx9_mu.c)
endif()

if(CONFIG_IMX9_SCMI)
  list(APPEND SRCS imx9_scmi.c)
endif()

if(CONFIG_IMX9_FLEXIO_PWM)
  list(APPEND SRCS imx9_flexio_pwm.c)
endif()

if(CONFIG_IMX9_TPM_PWM)
  list(APPEND SRCS imx9_tpm_pwm.c)
endif()

if(CONFIG_IMX9_USBDEV)
  list(APPEND SRCS imx9_usbdev.c)
endif()

if(CONFIG_IMX9_DWC3)
  list(APPEND SRCS imx9_dwc3dev.c)
endif()

if(CONFIG_IMX9_USBHOST)
  list(APPEND SRCS imx9_ehci.c)
endif()

if(CONFIG_IMX9_LPI2C)
  list(APPEND SRCS imx9_lpi2c.c)
endif()

if(CONFIG_IMX9_LPSPI)
  list(APPEND SRCS imx9_lpspi.c)
endif()

if(CONFIG_IMX9_EDMA)
  list(APPEND SRCS imx9_edma.c)
endif()

if(CONFIG_IMX9_DMA_ALLOC)
  list(APPEND SRCS imx9_dma_alloc.c)
endif()

if(CONFIG_IMX9_ENET)
  list(APPEND SRCS imx9_enet.c)
endif()

if(CONFIG_IMX9_USDHC)
  list(APPEND SRCS imx9_usdhc.c)
endif()

if(CONFIG_IMX9_FLEXSPI)
  list(APPEND SRCS imx9_flexspi.c)
endif()

if(CONFIG_IMX9_FLEXSPI_NOR)
  list(APPEND SRCS imx9_flexspi_nor.c)
endif()

if(CONFIG_IMX9_BOOTLOADER)
  list(APPEND SRCS imx9_system_ctl.c imx9_trdc.c imx9_ele.c)
endif()

if(CONFIG_IMX9_ROMAPI)
  list(APPEND SRCS imx9_romapi.c)
endif()

if(CONFIG_IMX9_AHAB_BOOT)
  list(APPEND SRCS imx9_ahab.c)
endif()

if(CONFIG_IMX9_FLEXCAN)
  list(APPEND SRCS imx9_flexcan.c)
endif()

target_sources(arch PRIVATE ${SRCS})
