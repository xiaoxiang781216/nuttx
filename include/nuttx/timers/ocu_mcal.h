/****************************************************************************
 * include/nuttx/timers/ocu_mcal.h
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

#ifndef __INCLUDE_NUTTX_TIMERS_OCU_MCAL_H
#define __INCLUDE_NUTTX_TIMERS_OCU_MCAL_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/irq.h>
#include <nuttx/timers/ocu.h>

#include "Ocu.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Re-arm mode for compare match callback */

enum mcal_ocu_rearm_mode_e
{
  MCAL_OCU_REARM_ABSOLUTE = 0,  /* Ocu_SetAbsoluteThreshold(value) */
  MCAL_OCU_REARM_RELATIVE,      /* Ocu_SetRelativeThreshold(value) */
  MCAL_OCU_REARM_ADVANCE,       /* Ocu_SetAbsoluteThreshold(old + value) */
};

/* Configuration for a single MCAL OCU channel adaptation */

struct mcal_ocu_channel_config_s
{
  int                         bus;           /* bus number for /dev/ocuN */
  Ocu_ChannelType             channel;       /* MCAL OCU channel number */
  uint32_t                    max_counter;   /* Maximum counter value */
  enum mcal_ocu_rearm_mode_e  rearm_mode;    /* How to re-arm on callback
                                              * reload=true */
  bool                        has_pin;       /* Whether this channel has
                                              * an output pin */

  /* Compare-match interrupt wiring.  These are supplied by the
   * chip/board layer to keep this adaptation layer platform-agnostic.
   * The ISR is chip-specific (it drives the vendor interrupt dispatch,
   * e.g. Gtm_AtomIsr() on Infineon AURIX) and eventually results in a
   * call to mcal_ocu_notify().  Set irq to a negative value (and isr to
   * NULL) when the interrupt is routed by other means (e.g. the MCAL Irq
   * module / Irq.xdm), in which case setup()/shutdown() skip the
   * attach/enable step.
   */

  int                         irq;           /* SRC/IRQ number, or <0 if
                                              * not managed by this layer */
  CODE xcpt_t                 isr;           /* Chip-specific ISR handler,
                                              * or NULL if not used */
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: mcal_ocu_initialize
 *
 * Description:
 *   Initialize the MCAL OCU module and register OCU channel devices.
 *   This function calls Ocu_Init() with the per-core configuration,
 *   then registers each specified channel as a NuttX /dev/ocuN device,
 *   where N is taken from each channel's "bus" field.
 *
 *   For multi-core systems, each core calls this function with its own
 *   config and non-overlapping "bus" numbers.
 *
 * Input Parameters:
 *   mcal_ocu_config - Pointer to MCAL OCU configuration for this core.
 *   channels        - Array of channel configurations to register.  Each
 *                     entry's "bus" field selects the /dev/ocu<bus> device
 *                     node and the slot in the internal channel table.
 *   num_channels    - Number of channels to register.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure.
 *
 ****************************************************************************/

int mcal_ocu_initialize(FAR const Ocu_ConfigType *mcal_ocu_config,
                        FAR const struct mcal_ocu_channel_config_s *channels,
                        size_t num_channels);

/****************************************************************************
 * Name: mcal_ocu_notify
 *
 * Description:
 *   MCAL OCU compare match notification dispatch function.  Called from
 *   ISR context by per-channel Ocu_Notification_<Channel> stubs.
 *
 *   Board-level code should implement the MCAL notification callbacks whose
 *   names match the EB Tresos OcuNotification parameter, passing the MCAL
 *   OCU channel number, e.g.:
 *
 *   void Ocu_Ch0_Notification(void) { mcal_ocu_notify(0); }
 *   void Ocu_Ch1_Notification(void) { mcal_ocu_notify(1); }
 *
 * Input Parameters:
 *   channel - MCAL OCU channel number (the OcuChannelId / config->channel),
 *             NOT the /dev/ocu "bus" number.  The dispatcher looks up the
 *             registered channel whose config->channel matches.
 *
 ****************************************************************************/

void mcal_ocu_notify(Ocu_ChannelType channel);

#endif /* __INCLUDE_NUTTX_TIMERS_OCU_MCAL_H */
