/****************************************************************************
 * arch/arm64/src/common/arm64_idle.c
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

#include <nuttx/arch.h>
#include "arm64_internal.h"
#include "arm64_arch.h"

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

void up_idle(void)
{
#if defined(CONFIG_SUPPRESS_INTERRUPTS) || defined(CONFIG_SUPPRESS_TIMER_INTS)
  /* If the system is idle and there are no timer interrupts, then process
   * "fake" timer interrupts. Hopefully, something will wake up.
   */

  nxsched_process_timer();
#elif defined(CONFIG_ARCH_TRUSTZONE_SECURE)

  /* Secure OS idle: no tasks ready, yield CPU to REE unconditionally.
   * REE will WFI. When a G1S fires, EL3 preempts REE and returns here.
   * When a NS interrupt fires, EL3 switches to REE and REE_IDLE will
   * resume us later.
   *
   * Every entry calls ARM_SMCCC_NUTTX_TEE_IDLE:
   *   - First call: EL3 registers interrupt handlers and boots REE.
   *   - Subsequent calls: EL3 unconditionally switches to REE (TEE has
   *     no ready tasks and does not sleep itself; REE is the sleeper).
   *
   * TEE runs again when:
   *   1. A G1S fires while REE runs -> EL3 does world switch -> TEE
   *      resumes here -> pending G1S IRQ fires through normal S-EL1
   *      vector table -> threads run -> all done -> back here ->
   *      ARM_SMCCC_NUTTX_TEE_IDLE -> yield to REE again
   *   2. TEE was preempted by a NS interrupt -> REE handles it, then
   *      idles -> ARM_SMCCC_NUTTX_REE_IDLE -> EL3 sees s_preempted and
   *      switches back to TEE -> TEE continues from its interruption
   *      point
   */

  struct arm_smccc_res res;

  arm_smccc_smc(ARM_SMCCC_NUTTX_TEE_IDLE, 0, 0, 0, 0, 0, 0, 0, &res);

#elif defined(CONFIG_ARCH_TRUSTZONE_NONSECURE)

  /* REE idle.
   *
   * Every entry calls ARM_SMCCC_NUTTX_REE_IDLE:
   *   - EL3 checks if TEE was preempted by a NS interrupt.  If so,
   *     resumes TEE.  Otherwise returns 0 and REE does WFI.
   *
   * Disable IRQs to prevent a lost-wakeup race: if a NS IRQ were taken
   * between SMC return and WFI, its handler could wake tasks but WFI
   * would still sleep.  With IRQs masked, the pending interrupt keeps
   * WFI from blocking.
   */

  struct arm_smccc_res res;

  irqstate_t flags = irq_save();

  arm_smccc_smc(ARM_SMCCC_NUTTX_REE_IDLE, 0, 0, 0, 0, 0, 0, 0, &res);

  asm("dsb sy");
  asm("wfi");

  irq_restore(flags);
#else

  /* Sleep until an interrupt occurs to save power */

  asm("dsb sy");
  asm("wfi");
#endif
}
