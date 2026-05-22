/****************************************************************************
 * arch/arm/src/arm_m/arm_gen_nonsecfault.c
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
#include <nuttx/syslog/syslog.h>
#include <nuttx/reboot_notifier.h>
#include <nuttx/sched.h>

#include <stdint.h>
#include <arch/irq.h>

#include "nvic.h"
#include "sau.h"
#include "arm_internal.h"
#include "exc_return.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define OFFSET_R0              (0 * 4) /* R0 */
#define OFFSET_R1              (1 * 4) /* R1 */
#define OFFSET_R2              (2 * 4) /* R2 */
#define OFFSET_R3              (3 * 4) /* R3 */
#define OFFSET_R12             (4 * 4) /* R12 */
#define OFFSET_R14             (5 * 4) /* R14 = LR */
#define OFFSET_R15             (6 * 4) /* R15 = PC */
#define OFFSET_XPSR            (7 * 4) /* xPSR */

/****************************************************************************
 * Private Data
 ****************************************************************************/

static int arm_secure_panic_notifier(FAR struct notifier_block *nb,
                                     unsigned long action,
                                     FAR void *data);

static DEFINE_PER_CPU_BMP(struct notifier_block, g_switch_nse_panic_nb) =
{
  arm_secure_panic_notifier,  /* notifier_call */
};
#define g_switch_nse_panic_nb this_cpu_var_bmp(g_switch_nse_panic_nb)

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: arm_secure_panic_notifier
 *
 * Description:
 *   Panic notifier callback. Restore NS R4-R11 and EXC_RETURN from
 *   the saved xcptcontext (exception_common already saved them),
 *   then return to NS.
 *
 ****************************************************************************/

static int arm_secure_panic_notifier(FAR struct notifier_block *nb,
                                     unsigned long action,
                                     FAR void *data)
{
  uint32_t *regs = (uint32_t *)g_running_task->xcp.regs;

  UNUSED(nb);
  UNUSED(data);

  if (action != SYS_HALT || regs == NULL)
    {
      return 0;
    }

  if (regs[REG_EXC_RETURN] & EXC_RETURN_SECURE_STACK)
    {
      /* TEE internal panic. Construct fake exception frame on
       * MSP_NS with PC=0 to trigger NS fault for crash dump.
       * (8 registers x 4 bytes: R0, R1, R2, R3, R12, LR, PC, xPSR)
       */

      uint32_t msp_ns;

      __asm__ volatile ("mrs %0, msp_ns" : "=r" (msp_ns));

      putreg32(0,          msp_ns - 32);
      putreg32(0,          msp_ns - 28);
      putreg32(0,          msp_ns - 24);
      putreg32(0,          msp_ns - 20);
      putreg32(0,          msp_ns - 16);
      putreg32(0,          msp_ns - 12);
      putreg32(0,          msp_ns - 8);
      putreg32(0x01000000, msp_ns - 4);

      __asm__ volatile (
        "msr msp_ns, %0" : : "r" (msp_ns - 32) : "memory"
      );

      /* EXC_RETURN = 0xFFFFFFB1:
       *   bit[6]  S     = 0  (NS stack frame)
       *   bit[5]  DCRS  = 1  (default callee regs)
       *   bit[4]  FType = 1  (no FP)
       *   bit[3]  Mode  = 0  (Handler Mode)
       *   bit[2]  SPSEL = 0  (MSP)
       *   bit[0]  ES    = 1  (Secure)
       */

      __asm__ volatile (
        "ldr lr, =0xFFFFFFB1\n"
        "bx  lr             \n"
        :
        :
        : "lr"
      );
    }
  else
    {
      /* Secure interrupt preempted NS. With DCRS=1, hardware did not
       * push R4-R11 to Secure stack on entry, so they must hold NS
       * values when we BX EXC_RETURN. The panic path corrupts them,
       * so restore from xcptcontext saved by exception_common.
       * Then point MSP_NS to the HW exception frame (starting at
       * REG_R0) so hardware unstacks R0-R3, R12, LR, PC, xPSR.
       */

      __asm__ volatile (
        "ldr   r4, [%0, %1]    \n"
        "ldr   r5, [%0, %2]    \n"
        "ldr   r6, [%0, %3]    \n"
        "ldr   r7, [%0, %4]    \n"
        "ldr   r8, [%0, %5]    \n"
        "ldr   r9, [%0, %6]    \n"
        "ldr   r10, [%0, %7]   \n"
        "ldr   r11, [%0, %8]   \n"
        "mov   lr, %9          \n"
        "bx    lr              \n"
        :
        : "r" (regs),
          "i" (REG_R4 * 4), "i" (REG_R5 * 4),
          "i" (REG_R6 * 4), "i" (REG_R7 * 4),
          "i" (REG_R8 * 4), "i" (REG_R9 * 4),
          "i" (REG_R10 * 4), "i" (REG_R11 * 4),
          "r" (regs[REG_EXC_RETURN])
        : "r4", "r5", "r6", "r7", "r8", "r9",
          "r10", "r11", "lr", "memory"
      );
    }

  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: arm_should_gen_nonsecurefault
 *
 * Description:
 *   Check whether should generate non-secure IRQ from securefault
 *
 ****************************************************************************/

bool weak_function arm_should_gen_nonsecurefault(void)
{
  return true;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: arm_gen_nonsecurefault
 *
 * Description:
 *   For TEE & REE, securefault & busfault are not banked, so the faults can
 *   only forword to TEE/REE.
 *   But how to crash dump the other core which not handled faults ?
 *
 *   Here we provide a way to resolve this problem:
 *   1. Set the securefault & busfault to TEE
 *   2. busfault happened from TEE, then directly dump TEE
 *   3. busfault happened from REE, then generate nonsecurefault
 *   4. Back to REE, and dump
 *
 * Return values:
 *   1 means generated done
 *   0 means don't need generated
 *
 ****************************************************************************/

int arm_gen_nonsecurefault(int irq, uint32_t *regs)
{
  uint32_t sp_ns;

  if (!arm_should_gen_nonsecurefault())
    {
      return 0;
    }

  /* Check whether come from REE */

  if (regs[REG_EXC_RETURN] & EXC_RETURN_SECURE_STACK)
    {
      return 0;
    }

  if (getreg32(SAU_SFSR) == 0)
    {
      /* busfault are forward to REE ? */

      if (getreg32(NVIC_AIRCR) & NVIC_AIRCR_BFHFNMINS)
        {
          return 0;
        }

      /* Redirect busfault to REE */

      up_secure_irq(NVIC_IRQ_BUSFAULT, false);
    }

  /* Get non-secure SP */

  if (regs[REG_EXC_RETURN] & EXC_RETURN_THREAD_MODE)
    {
      __asm__ __volatile__ ("mrs %0, psp_ns" : "=r" (sp_ns));
    }
  else
    {
      __asm__ __volatile__ ("mrs %0, msp_ns" : "=r" (sp_ns));
    }

  _alert("Dump REE registers:\n");
  _alert("R0: %08" PRIx32 " R1: %08" PRIx32
         " R2: %08" PRIx32 "  R3: %08" PRIx32 "\n",
         getreg32(sp_ns + OFFSET_R0), getreg32(sp_ns + OFFSET_R1),
         getreg32(sp_ns + OFFSET_R2), getreg32(sp_ns + OFFSET_R3));
  _alert("IP: %08" PRIx32 " SP: %08" PRIx32
          " LR: %08" PRIx32 "  PC: %08" PRIx32 "\n",
         getreg32(sp_ns + OFFSET_R12), sp_ns,
         getreg32(sp_ns + OFFSET_R14), getreg32(sp_ns + OFFSET_R15));

  syslog_flush();

  /* Don't back to REE when its PC is 0 */

  if (getreg32(sp_ns + OFFSET_R15) == 0)
    {
      return 0;
    }

  /* Force set return ReturnAddress to 0, then non-secure cpu will crash.
   * Also, the ReturnAddress is very important, so move it to R12.
   */

  putreg32(getreg32(sp_ns + OFFSET_R15), sp_ns + OFFSET_R12);
  putreg32(0, sp_ns + OFFSET_R15);

  return 1;
}

/****************************************************************************
 * Name: arm_panic_switch_nsec_register
 *
 * Description:
 *   This function is used to register a panic notifier to handle system
 *   crash events. When triggered, it switches the system to the non-secure
 *   world by constructing fake stack frames and "bx" instruction.
 *
 * Input Parameters:
 *   None
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void arm_panic_switch_nsec_register(void)
{
  register_reboot_notifier(&g_switch_nse_panic_nb);
}
