/****************************************************************************
 * arch/arm/src/arm_a_r/arm_panic_switch_nsec.c
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

#include <nuttx/reboot_notifier.h>
#include <nuttx/nuttx.h>
#include <arch/syscall.h>

#include "arm_internal.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

static DEFINE_PER_CPU_BSS(struct notifier_block, g_panic_switch_nsec_nb);
#define g_panic_switch_nsec_nb this_cpu_var(g_panic_switch_nsec_nb)

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: arm_panic_switch_nsec_callback
 *
 * Description:
 *   Reboot notifier callback for TrustZone Secure world with an external
 *   EL3 monitor (TF-A). On SYS_HALT (panic), issue an SMC to yield the
 *   CPU back to the non-secure world so the REE can finish its coredump
 *   or recovery sequence.
 *
 ****************************************************************************/

static int arm_panic_switch_nsec_callback(struct notifier_block *block,
                                          unsigned long action, void *data)
{
  if (action == SYS_HALT)
    {
      struct arm_smccc_res res;

      /* Switch to non-secure world so the REE can finish coredump.
       * FOREIGN_INTR unconditionally resumes the saved NS context.
       */

      arm_smccc_smc(ARM_SMCCC_NUTTX_FOREIGN_INTR,
                    0, 0, 0, 0, 0, 0, 0, &res);
    }

  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: arm_panic_switch_nsec_register
 *
 * Description:
 *   Register a reboot notifier that switches to the non-secure world
 *   when the secure world panics, so the REE can finish its coredump
 *   or recovery sequence.
 *
 ****************************************************************************/

void arm_panic_switch_nsec_register(void)
{
  g_panic_switch_nsec_nb.notifier_call = arm_panic_switch_nsec_callback;
  g_panic_switch_nsec_nb.priority = INT_MIN;
  register_reboot_notifier(&g_panic_switch_nsec_nb);
}
