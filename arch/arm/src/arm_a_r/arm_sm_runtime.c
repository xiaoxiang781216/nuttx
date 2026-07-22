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
#define MONITOR_STACK_SIZE      ALIGN_UP(CONFIG_ARM_MONITOR_STACK_SIZE, \
                                         MONITOR_STACK_ALIGNMENT)

/* Per-CPU secure-monitor storage.  The monitor-mode C dispatcher stack
 * (the stack[] member) sits at the base, immediately below the secure and
 * non-secure per-world contexts (the sec/nsec members); the per-CPU
 * preemption flag follows the contexts.  The C code reaches every member
 * directly as a typed global (g_monitor.sec / g_monitor.nsec /
 * g_monitor.s_preempted).
 *
 * Flag g_monitor.s_preempted: a secure (TEE) context was preempted by a
 * foreign interrupt (IRQ, Group 1) while it was running, or the TEE
 * cooperatively yielded via ARM_SMCCC_NUTTX_FOREIGN_INTR.  The monitor
 * must resume that exact context once the REE goes idle, instead of
 * leaving the TEE parked.  Set by arm_sm_vectorirq and the FOREIGN_INTR
 * handler, cleared by the REE_IDLE handler.
 *
 * The monitor SP is initialised to the top of g_monitor.sec (see
 * arm_sm_init), so the srsdb/push in the .S entry path lands directly
 * inside sec/nsec and the C dispatcher's stack grows down from there into
 * stack[].  The .S path is purely SP-relative (it never references the
 * g_monitor symbol) and only requires that stack[] sits immediately below
 * sec and that nsec immediately follows sec, which the layout below
 * guarantees; s_preempted follows nsec, clear of that machinery.
 */

struct arm_sm_monitor
{
  uint8_t               stack[MONITOR_STACK_SIZE]
                        aligned_data(MONITOR_STACK_ALIGNMENT);
  struct arm_sm_ctx     sec;
  struct arm_sm_ctx     nsec;
  bool                  s_preempted;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static DEFINE_PER_CPU_BSS(struct arm_sm_monitor, g_monitor)
locate_data(".monitor_stack") aligned_data(MONITOR_STACK_ALIGNMENT);
#define g_monitor this_cpu_var(g_monitor)

/****************************************************************************
 * Public Data
 ****************************************************************************/

/* Non-secure (REE) entry point, set by board_boot_image() once the AP image
 * has been loaded by the TEE.  Zero until then.  The first TEE_IDLE that
 * sees a non-zero value seeds the NS context and launches the REE.
 *
 * This is the single canonical definition (default 0).  Boards that load an
 * AP image reference it via `extern` and assign the entry point; secure
 * boards without an AP loader leave it at 0, so the first-boot REE launch
 * branch in arm_sm_from_sec() is never taken.
 */

volatile uint32_t g_ap_entry;

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
  /* Point the monitor SP one arm_sm_ctx above the sec context base, which
   * is the secure-world resting SP the .S entry path expects.  The FIRST
   * monitor entry is a SECURE one (the TEE issues ARM_SMCCC_NUTTX_TEE_IDLE),
   * and the secure entry path derives the context base as
   * (mon_sp - sizeof(arm_sm_ctx)) via srsdb/push{r0-r7}/sub.  Pointing at
   * &sec + sizeof(arm_sm_ctx) lands the derivation exactly on &sec; mon_sp
   * then bounces to &nsec + sizeof after the switch to the non-secure
   * world, where the nsec entry path derives the &nsec base the same way.
   * The C dispatcher stack grows down from the context region into stack[].
   */

  arm_sm_init_stack((uintptr_t)&g_monitor.nsec);
}

/****************************************************************************
 * Name: arm_sm_get_nsec_ctx
 *
 * Description:
 *   Get the non-secure (REE) context used by the secure monitor.  The
 *   per-CPU non-secure context is a typed global (g_monitor.nsec), so it
 *   is returned directly rather than by reading the monitor-mode SP.
 *
 * Returned Value:
 *   Pointer to the per-CPU non-secure context (g_monitor.nsec).
 *
 ****************************************************************************/

struct arm_sm_ctx *arm_sm_get_nsec_ctx(void)
{
  return &g_monitor.nsec;
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
  FAR struct arm_sm_ctx *nsec_ctx;

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
 *   None.  The per-CPU secure/non-secure contexts are the typed globals
 *   g_monitor.sec / g_monitor.nsec.
 *
 * Returned Value:
 *   SM_EXIT_TO_NON_SECURE: return to the non-secure world after this
 *   procedure
 *   SM_EXIT_TO_SECURE: continue running inside the secure world
 *
 ****************************************************************************/

uint32_t arm_sm_from_nsec(void)
{
  FAR struct arm_sm_ctx *sec = &g_monitor.sec;
  FAR struct arm_sm_ctx *nsec = &g_monitor.nsec;
  FAR uint32_t *nsec_r0 = (FAR uint32_t *)(&nsec->r0);
  FAR struct arm_smccc_res *args = (FAR struct arm_smccc_res *)nsec_r0;

  /* Handle ARM_SMCCC_NUTTX_REE_IDLE: the non-secure world (REE) has no
   * ready tasks.  If the secure world (TEE) was preempted by a foreign
   * interrupt (or cooperatively yielded via FOREIGN_INTR), resume it at
   * its real interruption point.  Otherwise return 0 and stay in the REE
   * so it can WFI (the REE is the sleeper).  Do NOT overwrite
   * sec->mon_lr: it still holds the PC where the TEE last parked.
   */

  if (args->a0 == ARM_SMCCC_NUTTX_REE_IDLE)
    {
      /* No preempted TEE context: return 0 to the REE so it can WFI. */

      if (!g_monitor.s_preempted)
        {
          nsec->r0 = 0;
          return SM_EXIT_TO_NON_SECURE;
        }

      /* A secure context was preempted while it was running.  Now that
       * the REE is idle, resume it.
       *
       * arm_sm_save/restore_ctx also swap the VFP register file
       * (*->vfp) under CONFIG_ARCH_FPU: save the REE's live VFP, restore
       * the TEE's saved VFP.
       */

      g_monitor.s_preempted = false;
      arm_sm_save_ctx(nsec);
      arm_sm_restore_ctx(sec);
      return SM_EXIT_TO_SECURE;
    }

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
      nsec->r0 = 0;
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
        nsec->r0 = (1 << PSCI_VERSION_MAJOR_SHIFT) | 0;
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
        nsec->r0 = PSCI_RET_NOT_SUPPORTED;
        return SM_EXIT_TO_NON_SECURE;

      case PSCI_0_2_FN_CPU_ON:
        nsec->r0 = PSCI_RET_NOT_SUPPORTED;
        return SM_EXIT_TO_NON_SECURE;

      default:
        break;
    }
#endif

  /* arm_sm_save_ctx also saves the REE's live VFP into nsec->vfp
   * (under CONFIG_ARCH_FPU), while it is still resident in hardware.
   */

  arm_sm_save_ctx(nsec);

  /* Set up the secure-world return payload BEFORE restoring the secure
   * banked/VFP state.  memcpy() may be lowered to NEON instructions that
   * clobber the VFP register file, so it must run while the VFP hardware is
   * still scratch: the REE's VFP has already been saved by the call above,
   * and the TEE's VFP is only restored by arm_sm_restore_ctx below.
   */

  memcpy(&sec->r0, args, sizeof(*args));

  sec->mon_lr = (uint32_t)arm_vectorsmc;

  /* Restore the secure banked registers and VFP register file last, so the
   * freshly restored secure VFP cannot be corrupted by the memcpy above.
   */

  arm_sm_restore_ctx(sec);

  /* the following procedure return "SM_EXIT_TO_SECURE" which indicate
   * that we should still stay at secure world, until the
   * "arm_vectorsmc" using "smc #0" to return to the
   * non-secure world
   */

  return SM_EXIT_TO_SECURE;
}

/****************************************************************************
 * Name: arm_sm_from_sec
 *
 * Description:
 *   Called from monitor mode when an SMC arrives from the secure (TEE)
 *   world.  Services the TEE-side world-switch protocol.  See sm.h for the
 *   full contract.
 *
 ****************************************************************************/

uint32_t arm_sm_from_sec(void)
{
  FAR struct arm_sm_ctx *sec = &g_monitor.sec;
  FAR struct arm_sm_ctx *nsec = &g_monitor.nsec;
  FAR uint32_t *sec_r0 = (FAR uint32_t *)(&sec->r0);
  FAR struct arm_smccc_res *args = (FAR struct arm_smccc_res *)sec_r0;

  if (args->a0 == ARM_SMCCC_NUTTX_TEE_IDLE)
    {
      /* First TEE idle after the AP image has been loaded: the non-secure
       * (REE) context has never run, so nsec->mon_lr is still zero.
       * Seed it with the AP entry point and launch the REE for the first
       * time.  This is the one-shot boot switch (formerly done in up_idle
       * via a separate secure-to-non-secure launch path); folding it here
       * removes the separate boot path and the g_ree_booted flag.
       *
       * nsec here is the same per-CPU storage arm_sm_get_nsec_ctx()
       * returns; seed it directly.
       */

      if (nsec->mon_lr == 0)
        {
          /* Wait for CPU0 to load the AP image and set g_ap_entry.
           *
           * CPU0 itself must NOT block here: it is the core that runs
           * board_boot_image() which sets g_ap_entry.  If CPU0 reaches
           * TEE_IDLE before that task runs (idle task preempts briefly),
           * just return to the secure world so TEE tasks keep executing.
           *
           * Secondary cores use wfe to save power; CPU0's sev after
           * writing g_ap_entry wakes them promptly.
           */

          if (g_ap_entry == 0)
            {
              if (up_cpu_index() == 0)
                {
                  sec->r0 = 0;
                  return SM_EXIT_TO_SECURE;
                }

              while (g_ap_entry == 0)
                {
                  __asm__ volatile("wfe");
                }
            }

          nsec->mon_lr = g_ap_entry;
          nsec->mon_spsr = PSR_MODE_SVC | PSR_I_BIT;
          if (g_ap_entry & 1)
            {
              nsec->mon_spsr |= PSR_T_BIT;
            }

          /* arm_sm_save/restore_ctx also swap the VFP register file
           * under CONFIG_ARCH_FPU: save the TEE's VFP, restore the
           * (zero-initialised) REE VFP for its first run.
           */

          arm_sm_save_ctx(sec);
          arm_sm_restore_ctx(nsec);
          return SM_EXIT_TO_NON_SECURE;
        }

      /* The TEE has no ready tasks.  It does not sleep itself — fall
       * through to switch to the REE (the REE is the sleeper).  This
       * returns the REE to its interruption point.
       */
    }
  else if (args->a0 == ARM_SMCCC_NUTTX_FOREIGN_INTR)
    {
      /* The TEE detected a foreign (non-secure) interrupt pending while
       * it was running and cooperatively yields the CPU to the REE.
       * Mark it preempted so REE_IDLE resumes it later.
       */

      g_monitor.s_preempted = true;
    }

  /* ARM_SMCCC_NUTTX_TEE_IDLE or ARM_SMCCC_NUTTX_FOREIGN_INTR: switch to
   * the non-secure world and resume the saved REE context at its real
   * interruption point.  Do NOT overwrite nsec->mon_lr.
   */

  /* arm_sm_save/restore_ctx also swap the VFP register file under
   * CONFIG_ARCH_FPU: save the TEE's live VFP, restore the preempted REE's
   * saved VFP.
   */

  arm_sm_save_ctx(sec);
  arm_sm_restore_ctx(nsec);

  return SM_EXIT_TO_NON_SECURE;
}

/****************************************************************************
 * Name: arm_sm_get_s_preempted / arm_sm_set_s_preempted
 *
 * Description:
 *   Accessors for the s_preempted flag.
 *
 ****************************************************************************/

bool arm_sm_get_s_preempted(void)
{
  return g_monitor.s_preempted;
}

void arm_sm_set_s_preempted(bool preempted)
{
  g_monitor.s_preempted = preempted;
}
