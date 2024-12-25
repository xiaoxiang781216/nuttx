/****************************************************************************
 * arch/arm64/src/goldfish/goldfish_idle.c
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

#ifdef CONFIG_ARCH_TRUSTZONE_SECURE
#include "sm.h"
#endif

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

#ifdef CONFIG_ARCH_TRUSTZONE_SECURE
extern volatile uint32_t g_ap_entry;
#endif

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
  if (g_ap_entry != 0)
    {
      up_irq_disable();
      arm_sm_boot_nsec(g_ap_entry);
      arm_sm_switch_nsec();
    }
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
