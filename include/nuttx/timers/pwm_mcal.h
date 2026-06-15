/****************************************************************************
 * include/nuttx/timers/pwm_mcal.h
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

#ifndef __INCLUDE_NUTTX_TIMERS_PWM_MCAL_H
#define __INCLUDE_NUTTX_TIMERS_PWM_MCAL_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_MCAL_PWM_DRIVER

#include <stddef.h>
#include <stdint.h>

#include "Pwm.h"

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Per-channel MCAL information */

struct mcal_pwm_chan_s
{
  Pwm_ChannelType channel_id;          /* MCAL logical channel number */
  Pwm_ChannelClassType channel_class;  /* AUTOSAR channel classification */
};

/* Board-level device-info structure for one /dev/pwmN device.
 * Core-affinity is handled by the caller; this struct describes a
 * device that is already known to belong to the current core.
 * No vendor-specific symbols - all HW details come from board config.
 */

struct mcal_pwm_devinfo_s
{
  FAR const char *devpath;                    /* Device path "/dev/pwm0" */
  uint32_t clock_freq;                        /* Timer clock freq in Hz */
  uint8_t nchan;                              /* Number of channels (>= 1) */
  FAR const struct mcal_pwm_chan_s *chan;     /* Per-channel descriptor */
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
 * Name: mcal_pwm_initialize
 *
 * Description:
 *   Initialize the AUTOSAR MCAL PWM module for the CURRENT core, then
 *   register every NuttX /dev/pwmN device described in 'devinfos[]'.
 *
 *   Core-affinity is the caller's responsibility: pick the per-core
 *   Pwm_ConfigType and the per-core device array before invoking.
 *   Typical pattern at board level:
 *
 *     int cpu = up_cpu_index();
 *     mcal_pwm_initialize(g_mcal_cfg_per_core[cpu],
 *                         g_pwm_devs_per_core[cpu],
 *                         g_pwm_devs_count[cpu]);
 *
 * Input Parameters:
 *   config      - AUTOSAR Pwm_ConfigType for THIS core. Passed verbatim
 *                 to Pwm_Init(). Must not be NULL.
 *   devinfos    - Array of /dev/pwmN device descriptors that belong to
 *                 THIS core. Must not be NULL.
 *   ndevinfos   - Number of entries in 'devinfos[]'. Must be >= 1.
 *
 * Returned Value:
 *   OK on full success. On partial failure, returns the first error
 *   encountered while continuing to register remaining devices.
 *   -EINVAL  invalid arguments (NULL pointer or ndevinfos == 0)
 *   -ENOMEM  kmm_zalloc failed for at least one device
 *   -E*      whatever pwm_register() returned for the first failing dev
 *
 ****************************************************************************/

int mcal_pwm_initialize(FAR const Pwm_ConfigType *config,
                        FAR const struct mcal_pwm_devinfo_s *devinfos,
                        size_t ndevinfos);

#undef EXTERN
#ifdef __cplusplus
}
#endif

#endif /* CONFIG_MCAL_PWM_DRIVER */
#endif /* __INCLUDE_NUTTX_TIMERS_PWM_MCAL_H */
