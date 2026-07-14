/****************************************************************************
 * include/nuttx/timers/gpt_mcal.h
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

#ifndef __INCLUDE_NUTTX_TIMERS_GPT_MCAL_H
#define __INCLUDE_NUTTX_TIMERS_GPT_MCAL_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_MCAL_GPT

#include <stddef.h>
#include <stdint.h>

#include <nuttx/irq.h>

#include "Gpt.h"

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Per-device configuration for each GPT timer instance.
 * frequency_hz / max_ticks must match EB tresos configuration.
 */

struct mcal_gpt_config_s
{
  uint16_t channel;       /* MCAL Gpt channel ID (Gpt_ChannelType) */
  uint32_t frequency_hz;  /* Channel tick frequency in Hz */
  uint32_t max_ticks;     /* Maximum tick count (ATOM=0xFFFFFF) */
  int timer_index;        /* Device path: /dev/timer{timer_index} */
  int irq;                /* SRC/IRQ number, or <0 if externally managed */
  CODE xcpt_t isr;        /* Chip-specific ISR, or NULL to skip attach */
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef __cplusplus
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

/****************************************************************************
 * Name: mcal_gpt_initialize
 *
 * Description:
 *   Initialize the AUTOSAR MCAL GPT module for the CURRENT core, then
 *   register NuttX timer devices as /dev/timer<N>.
 *
 *   Core-affinity is the caller's responsibility: pick the per-core
 *   Gpt_ConfigType and the per-core device array before invoking.
 *   Typical pattern at board level:
 *
 *     int cpu = up_cpu_index();
 *     mcal_gpt_initialize(g_gpt_mcal_config[cpu],
 *                         g_gpt_devs[cpu],
 *                         g_gpt_devs_count[cpu]);
 *
 * Input Parameters:
 *   config    - AUTOSAR Gpt_ConfigType for THIS core. Passed verbatim
 *               to Gpt_Init(). NULL skips Gpt_Init (already done).
 *   devinfos  - Array of timer device descriptors that belong to
 *               THIS core. Must not be NULL.
 *   ndevinfos - Number of entries in devinfos[]. Must be >= 1.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno on failure.
 *
 ****************************************************************************/

int mcal_gpt_initialize(FAR const Gpt_ConfigType *config,
                        FAR const struct mcal_gpt_config_s *devinfos,
                        size_t ndevinfos);

#undef EXTERN
#ifdef __cplusplus
}
#endif

#endif /* CONFIG_MCAL_GPT */
#endif /* __INCLUDE_NUTTX_TIMERS_GPT_MCAL_H */
