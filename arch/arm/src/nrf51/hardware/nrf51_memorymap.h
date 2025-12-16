/****************************************************************************
 * arch/arm/src/nrf51/hardware/nrf51_memorymap.h
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

#ifndef __ARCH_ARM_SRC_NRF51_HARDWARE_NRF51_MEMORYMAP_H
#define __ARCH_ARM_SRC_NRF51_HARDWARE_NRF51_MEMORYMAP_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Memory Map */

#define NRF51_FLASH_BASE          0x00000000 /* Flash memory Start Address */
#define NRF51_SRAM_BASE           0x20000000 /* SRAM Start Address */
#define NRF51_FICR_BASE           0x10000000 /* FICR */
#define NRF51_UICR_BASE           0x10001000 /* UICR */
#define NRF51_APB0_BASE           0x40000000 /* APB  */

#define NRF51_CORTEXM0_BASE       0xe0000000 /* Cortex-M0 Private Peripheral Bus */

/* APB Peripherals */

#define NRF51_CLOCK_BASE          0x40000000
#define NRF51_POWER_BASE          0x40000000
#define NRF51_RADIO_BASE          0x40001000
#define NRF51_UART0_BASE          0x40002000
#define NRF51_SPI0_BASE           0x40003000
#define NRF51_TWI0_BASE           0x40003000
#define NRF51_SPI1_BASE           0x40004000
#define NRF51_TWI1_BASE           0x40004000
#define NRF51_GPIOTE_BASE         0x40006000
#define NRF51_ADC_BASE            0x40007000
#define NRF51_TIMER0_BASE         0x40008000
#define NRF51_TIMER1_BASE         0x40009000
#define NRF51_TIMER2_BASE         0x4000a000
#define NRF51_RTC0_BASE           0x4000b000
#define NRF51_TEMP_BASE           0x4000c000
#define NRF51_RNG_BASE            0x4000d000
#define NRF51_ECB_BASE            0x4000e000
#define NRF51_CCM_BASE            0x4000f000
#define NRF51_AAR_BASE            0x4000f000
#define NRF51_WDT_BASE            0x40010000
#define NRF51_RTC1_BASE           0x40011000
#define NRF51_QDEC_BASE           0x40012000
#define NRF51_LPCOMP_BASE         0x40013000
#define NRF51_SWI0_BASE           0x40014000
#define NRF51_SWI1_BASE           0x40015000
#define NRF51_SWI2_BASE           0x40016000
#define NRF51_SWI3_BASE           0x40017000
#define NRF51_SWI4_BASE           0x40018000
#define NRF51_SWI5_BASE           0x40019000
#define NRF51_NVMC_BASE           0x4001e000
#define NRF51_PPI_BASE            0x4001f000

/* GPIO */

#define NRF51_GPIO_P0_BASE        0x50000000

#endif /* __ARCH_ARM_SRC_NRF51_HARDWARE_NRF51_MEMORYMAP_H */
