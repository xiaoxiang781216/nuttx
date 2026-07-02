/****************************************************************************
 * arch/arm/src/qemu/qemu_idle.c
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
#include <nuttx/power/pm.h>
#include <nuttx/arch.h>
#include "arm_internal.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_idle
 *
 * Description:
 *   up_idle() is the logic that will be executed when there is no other
 *   ready-to-run task.  This is processor idle time and will continue until
 *   some interrupt occurs to cause a context switch from the idle task.
 *
 *   Processing in this state may be processor-specific. e.g., this is where
 *   power management operations might be performed.
 *
 ****************************************************************************/

#ifdef CONFIG_PM

#ifdef CONFIG_SMP
static bool pm_idle_handler(int cpu,
                            enum pm_state_e cpu_state,
                            enum pm_state_e system_state)
{
  bool first = false;
  switch (cpu_state)
    {
      case PM_NORMAL:
      case PM_IDLE:
      case PM_STANDBY:
      case PM_SLEEP:

        /* do cpu domain pm enter operations */

        asm("NOP");

        if (system_state >= PM_NORMAL)
          {
            switch (system_state)
              {
                case PM_NORMAL:
                case PM_IDLE:
                case PM_STANDBY:
                case PM_SLEEP:

                  /* do system domain pm enter operations */

                  asm("NOP");

                  break;
                default:
                  break;
              }
          }

        pm_idle_unlock();

        /* do no cross-core relative operations */

        asm("WFI");

        first = pm_idle_lock(cpu);
        if (first)
          {
            /* do system domain pm leave operations */

            asm("NOP");
          }

        /* do cpu domain pm leave operations */

        asm("NOP");

        break;
      default:
        break;
    }

  return first;
}
#else

static void pm_idle_handler(enum pm_state_e state)
{
  switch (state)
    {
      default:
        asm("WFI");
        break;
    }
}
#endif

#endif

void up_idle(void)
{
#ifdef CONFIG_PM
  pm_idle(pm_idle_handler);
#endif

#ifdef CONFIG_ARCH_TRUSTZONE_SECURE
  struct arm_smccc_res res;

  irqstate_t flags = irq_save();

  arm_smccc_smc(ARM_SMCCC_NUTTX_TEE_IDLE, 0, 0, 0, 0, 0, 0, 0, &res);

  asm("dsb sy");
  asm("wfi");
  irq_restore(flags);
#elif defined(CONFIG_ARCH_TRUSTZONE_NONSECURE)

  /* REE idle: no tasks ready, yield CPU to TEE unconditionally.
   * The TEE will WFI.  When a foreign (non-secure) interrupt fires while
   * the TEE runs, the monitor (or EL3) preempts the TEE and returns here.
   * When a secure interrupt fires while the REE runs, the monitor switches
   * to the TEE, which resumes us later via the TEE_IDLE handshake.
   *
   * This path is shared by the in-image ARMv7-A monitor (GICv2) and the
   * external AArch64 EL3 monitor (GICv3): both use the same REE_IDLE SMC.
   */

  struct arm_smccc_res res;

  arm_smccc_smc(ARM_SMCCC_NUTTX_REE_IDLE, 0, 0, 0, 0, 0, 0, 0, &res);
#elif defined(CONFIG_SUPPRESS_INTERRUPTS) || defined(CONFIG_SUPPRESS_TIMER_INTS)
  /* If the system is idle and there are no timer interrupts, then process
   * "fake" timer interrupts. Hopefully, something will wake up.
   */

  nxsched_process_timer();
#else

  /* Sleep until an interrupt occurs to save power */

  asm("WFI");
#endif
}
