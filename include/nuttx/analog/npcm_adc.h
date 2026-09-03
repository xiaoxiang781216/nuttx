/****************************************************************************
 * include/nuttx/analog/npcm_adc.h
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

#ifndef __INCLUDE_NUTTX_ANALOG_NPCM_ADC_H
#define __INCLUDE_NUTTX_ANALOG_NPCM_ADC_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>

#include <nuttx/analog/adc.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Number of analog input channels multiplexed onto the single converter. */

#define NPCM_ADC_NCHANNELS  8

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Name: npcm_adc_initialize
 *
 * Description:
 *   Initialise a Nuvoton NPCM7xx ADC lower-half bound to a memory-mapped
 *   register window (as modelled by QEMU's "nuvoton,npcm750-adc" device on
 *   the virt platform).  The returned device is passed to adc_register().
 *
 * Input Parameters:
 *   base - Physical (kernel-visible) base address of the ADC register
 *          window, taken from the device tree "reg" property.
 *   irq  - Data-ready interrupt number (from the device tree "interrupts"
 *          property).  Retained for future interrupt-driven operation; the
 *          current implementation drives conversions by polling and does
 *          not attach a handler.
 *
 * Returned Value:
 *   Valid ADC device structure reference on success; NULL on failure.
 *
 ****************************************************************************/

FAR struct adc_dev_s *npcm_adc_initialize(uintptr_t base, int irq);

#ifdef __cplusplus
}
#endif

#endif /* __INCLUDE_NUTTX_ANALOG_NPCM_ADC_H */
