/****************************************************************************
 * include/nuttx/timers/wdg_mcal.h
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

#ifndef __INCLUDE_NUTTX_TIMERS_WDG_MCAL_H
#define __INCLUDE_NUTTX_TIMERS_WDG_MCAL_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_MCAL_WDG

#include <stddef.h>
#include <stdint.h>

#include <nuttx/fs/ioctl.h>

#include "Wdg.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define WDIOC_MCAL_SETMODE  _WDIOC(0x100)

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct mcal_wdg_config_s
{
  WdgIf_ModeType default_mode;  /* Initial mode after Wdg_Init */
  uint32_t timeout_ms;          /* Initial trigger period (ms) */
  uint32_t slow_max_timeout_ms; /* Max timeout in SLOW mode */
  uint32_t fast_max_timeout_ms; /* Max timeout in FAST mode */
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
 * Name: mcal_wdg_initialize
 *
 * Description:
 *   Initialize the AUTOSAR MCAL Wdg module for the CURRENT core, then
 *   register NuttX watchdog devices as /dev/watchdog<devno> through
 *   /dev/watchdog<devno + num - 1>.
 *
 * Input Parameters:
 *   mcal_config - AUTOSAR Wdg_ConfigType for THIS core. Passed verbatim
 *                 to Wdg_Init(). Must not be NULL.
 *   config      - Adapter configuration extracted from vendor config.
 *                 Must not be NULL.
 *   devno       - Base device number for /dev/watchdogN naming.
 *   num         - Number of watchdog devices to register (>= 1).
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno on failure.
 *
 ****************************************************************************/

int mcal_wdg_initialize(FAR const Wdg_ConfigType *mcal_config,
                        FAR const struct mcal_wdg_config_s *config,
                        int devno, int num);

#undef EXTERN
#ifdef __cplusplus
}
#endif

#endif /* CONFIG_MCAL_WDG */
#endif /* __INCLUDE_NUTTX_TIMERS_WDG_MCAL_H */
