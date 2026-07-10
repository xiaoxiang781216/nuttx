/****************************************************************************
 * arch/tricore/src/common/tricore_fpu.c
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

#include <stdbool.h>
#include <string.h>

#include <arch/arch.h>
#include <arch/irq.h>

/****************************************************************************
 * Private Types
 ****************************************************************************/

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: tricore_fpuinit
 *
 * Description:
 *   Initializes the Floating-Point Unit (FPU) for TriCore architecture
 *   microcontrollers. Enables FPU hardware, configures floating-point
 *   exception handling (including FZ_TRAP_EN_SHIFT-related trap control),
 *   and sets up FPU control registers to ensure reliable floating-point
 *   arithmetic operations.
 *
 ****************************************************************************/

void tricore_fpuinit(void)
{
  /* FPU zero-divide trap enable */

  tricore_mtcr(FPU_SYNC_TRAP_REG, tricore_mfcr(FPU_SYNC_TRAP_REG) |
                                  (1U << FPU_TRAP_FZE_SHIFT));
  UP_ISB();
}

/****************************************************************************
 * Name: up_fpucmp
 *
 * Description:
 *   Compare FPU areas from thread context.
 *
 * Input Parameters:
 *   saveregs1 - Pointer to the saved FPU registers.
 *   saveregs2 - Pointer to the saved FPU registers.
 *
 * Returned Value:
 *   True if FPU areas compare equal, False otherwise.
 *
 ****************************************************************************/

bool up_fpucmp(const void *saveregs1, const void *saveregs2)
{
  /* The FPU uses the general-purpose data registers, so
   * only the upper half needs to be compared.
   */

  return memcmp(saveregs1, saveregs2, TC_CONTEXT_SIZE) == 0;
}
