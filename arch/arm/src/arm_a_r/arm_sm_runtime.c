/****************************************************************************
 * arch/arm/src/arm_a_r/arm_sm_runtime.c
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
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2015-2023, Linaro Limited
 * Copyright (c) 2023, Arm Limited
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/nuttx.h>
#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <nuttx/reboot_notifier.h>
#include <string.h>

#include "arm.h"
#include "sm.h"
#include "gic.h"
#include "arm_cpu_psci.h"

#ifdef CONFIG_ARM_PSCI_SERVER
#  include <arch/syscall.h>
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* QEMU virt secure GPIO (PL061) at 0x090b0000 for system reset/poweroff.
 * gpio-poweroff: pin 0 active-high triggers poweroff.
 * gpio-restart:  pin 1 active-high triggers system reset.
 */

#ifdef CONFIG_ARM_PSCI_SERVER
#  define SECURE_GPIO_BASE        0x090b0000
#  define SECURE_GPIO_POWEROFF    0
#  define SECURE_GPIO_RESET       1
#  define PL061_GPIODIR           0x400
#endif

#define MONITOR_STACK_ALIGNMENT 64 /* (sizeof(long) * U(2)) */
#define MONITOR_STACK_NSEC_SIZE ALIGN_UP(sizeof(struct arm_sm_nsec_ctx), \
                                         MONITOR_STACK_ALIGNMENT)
#define MONITOR_STACK_SIZE      ALIGN_UP(CONFIG_ARM_MONITOR_STACK_SIZE, \
                                         MONITOR_STACK_ALIGNMENT)

typedef uint8_t monitor_stack_t[MONITOR_STACK_SIZE];

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* the g_monitor_stack are allocated using to manage the secure context and
 * non-secure context.
 * the "g_monitor_stack" cannot be declared as "const", as the assembly
 * code need to access "g_monitor_stack", if we declared as "const",
 * the arm_sm_init procedure will not work
 */

static DEFINE_PER_CPU_BSS(monitor_stack_t, g_monitor_stack)
locate_data(".monitor_stack") aligned_data(MONITOR_STACK_ALIGNMENT);
#define g_monitor_stack this_cpu_var(g_monitor_stack)

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

#ifdef CONFIG_ARM_PSCI_SERVER

/****************************************************************************
 * Name: arm_psci_server_reset
 *
 * Description:
 *   Trigger system reset via QEMU virt secure GPIO (PL061).
 *   Pin 1 active-high triggers hardware reset.
 *
 ****************************************************************************/

static void arm_psci_server_reset(void)
{
  volatile uint8_t *dir = (volatile uint8_t *)
                          (SECURE_GPIO_BASE + PL061_GPIODIR);
  volatile uint8_t *data = (volatile uint8_t *)
                           (SECURE_GPIO_BASE +
                            (1 << (SECURE_GPIO_RESET + 2)));

  /* Set pin 1 as output */

  *dir |= (1 << SECURE_GPIO_RESET);

  /* Drive pin 1 high to trigger reset (gpio-restart is active-high) */

  *data = (1 << SECURE_GPIO_RESET);
}

/****************************************************************************
 * Name: arm_psci_server_poweroff
 *
 * Description:
 *   Trigger system poweroff via QEMU virt secure GPIO (PL061).
 *   Pin 0 active-high triggers poweroff.
 *
 ****************************************************************************/

static void arm_psci_server_poweroff(void)
{
  volatile uint8_t *dir = (volatile uint8_t *)
                          (SECURE_GPIO_BASE + PL061_GPIODIR);
  volatile uint8_t *data = (volatile uint8_t *)
                           (SECURE_GPIO_BASE +
                            (1 << (SECURE_GPIO_POWEROFF + 2)));

  /* Set pin 0 as output */

  *dir |= (1 << SECURE_GPIO_POWEROFF);

  /* Drive pin 0 high to trigger poweroff (gpio-poweroff is active-high) */

  *data = (1 << SECURE_GPIO_POWEROFF);
}
#endif

/****************************************************************************
 * Name: arm_secure_panic_notifier
 *
 * Description:
 *   Panic notifier callback to switch secure world to nonsecure world.
 *
 ****************************************************************************/

static int arm_secure_panic_notifier(FAR struct notifier_block *nb,
                                     unsigned long action,
                                     FAR void *data)
{
  UNUSED(nb);
  UNUSED(data);

  if (action == SYS_HALT)
    {
      arm_sm_switch_nsec();
    }

  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: arm_sm_init
 *
 * Description:
 *   This method is called at the boot stage, the arm_head.S will call this
 *   method to perform init the secure monitor runtime
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void arm_sm_init(void)
{
  arm_sm_init_stack(((uintptr_t)&g_monitor_stack
                    + MONITOR_STACK_SIZE - MONITOR_STACK_NSEC_SIZE));
}

/****************************************************************************
 * Name: arm_sm_boot_nsec
 *
 * Description:
 *   As the ap core is loaded by tee core, when the tee core is boot
 *   finished, it need to setup the nsec_ctc mon_lr as the ap core entry
 *   address, then using the smc command to switch to the monitor mode and
 *   switch to the ap core
 *
 * Input Parameters:
 *   entry  - The ap core entry address
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void arm_sm_boot_nsec(uintptr_t entry)
{
  FAR struct arm_sm_nsec_ctx *nsec_ctx;

  nsec_ctx = arm_sm_get_nsec_ctx();
  if (nsec_ctx->mon_lr == 0)
    {
      nsec_ctx->mon_lr = entry;
      nsec_ctx->mon_spsr = PSR_MODE_SVC | PSR_I_BIT;
      if (entry & 1)
        {
          nsec_ctx->mon_spsr |= PSR_T_BIT;
        }
    }
}

/****************************************************************************
 * Name: arm_sm_from_nsec
 *
 * Description:
 *   Returns one of SM_EXIT_TO_* exit monitor in secure or non-secure world
 *
 * Input Parameters:
 *   ctx - The secure monitor context
 *
 * Returned Value:
 *   SM_EXIT_TO_NON_SECURE: return to the non-secure world after this
 *   procedure
 *   SM_EXIT_TO_SECURE: continue running inside the secure world
 *
 ****************************************************************************/

uint32_t arm_sm_from_nsec(FAR struct arm_sm_ctx *ctx)
{
  FAR uint32_t *nsec_r0 = (FAR uint32_t *)(&ctx->nsec.r0);
  FAR struct arm_smccc_res *args = (FAR struct arm_smccc_res *)nsec_r0;

  /* Handle fast SMC: ARM_SMCCC_NUTTX_SEND_SECURE_SGI
   * REE requests monitor to send a secure (Group 0) SGI to TEE.
   * r0 = function ID, r1 = SGI ID (0-15).
   *
   * In monitor mode (secure state), write GICD_SGIR with NSATT=0
   * to generate a Group 0 SGI targeting self. The SGI will pend in
   * GIC and be delivered to TEE next time it runs.
   * Return directly to non-secure world without entering TEE.
   */

#ifdef CONFIG_ARCH_HAVE_IRQTRIGGER
  if (args->a0 == ARM_SMCCC_NUTTX_SEND_SECURE_SGI)
    {
      uint32_t sgi_id = (uint32_t)args->a1;
      uint32_t cpuset = (uint32_t)args->a2;

      up_trigger_irq(sgi_id, cpuset);
      ctx->nsec.r0 = 0;
      return SM_EXIT_TO_NON_SECURE;
    }
#endif

  /* Handle PSCI calls from non-secure world.
   * TEE acts as PSCI server: intercept standard PSCI function IDs,
   * perform the requested action, and return directly to NS world.
   */

#ifdef CONFIG_ARM_PSCI_SERVER
  switch (args->a0)
    {
      case PSCI_0_2_FN_PSCI_VERSION:
        ctx->nsec.r0 = (1 << PSCI_VERSION_MAJOR_SHIFT) | 0;
        return SM_EXIT_TO_NON_SECURE;

      case PSCI_0_2_FN_SYSTEM_OFF:
        arm_psci_server_poweroff();

        /* Should not reach here */

        for (; ; );
        break;

      case PSCI_0_2_FN_SYSTEM_RESET:
        arm_psci_server_reset();

        /* Should not reach here */

        for (; ; );
        break;

      case PSCI_0_2_FN_CPU_OFF:
      case PSCI_0_2_FN_CPU_SUSPEND:
      case PSCI_0_2_FN_AFFINITY_INFO:
      case PSCI_0_2_FN_MIGRATE:
      case PSCI_0_2_FN_MIGRATE_INFO_TYPE:
      case PSCI_0_2_FN_MIGRATE_INFO_UP_CPU:
        ctx->nsec.r0 = PSCI_RET_NOT_SUPPORTED;
        return SM_EXIT_TO_NON_SECURE;

      case PSCI_0_2_FN_CPU_ON:
        ctx->nsec.r0 = PSCI_RET_NOT_SUPPORTED;
        return SM_EXIT_TO_NON_SECURE;

      default:
        break;
    }
#endif

  arm_sm_save_banked_regs(&ctx->nsec.regs);
  arm_sm_restore_banked_regs(&ctx->sec.regs);

  memcpy(&ctx->sec.r0, args, sizeof(*args));

  ctx->sec.mon_lr = (uint32_t)arm_vectorsmc;

  /* the following procedure return "SM_EXIT_TO_SECURE" which indicate
   * that we should still stay at secure world, until the
   * "arm_vectorsmc" using "smc #0" to return to the
   * non-secure world
   */

  return SM_EXIT_TO_SECURE;
}

/****************************************************************************
 * Name: arm_panic_switch_nsec_register
 *
 * Description:
 *   This function is used to register a panic notifier to handle system
 *   crash events. When triggered, it switches the system to the non-secure
 *   world by executing an SMC (Secure Monitor Call) instruction.
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
