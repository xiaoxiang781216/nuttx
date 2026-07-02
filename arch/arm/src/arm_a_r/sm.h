/****************************************************************************
 * arch/arm/src/arm_a_r/sm.h
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
 * Copyright (c) 2015-2020, Linaro Limited
 * Copyright (c) 2021-2023, Arm Limited
 *
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_ARM_A_R_SM_H
#define __ARCH_ARM_SRC_ARM_A_R_SM_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>
#include <stdbool.h>

#include "arm_cpu_psci.h"
#include "sctlr.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define SM_EXIT_TO_NON_SECURE       0
#define SM_EXIT_TO_SECURE           1

/****************************************************************************
 * Public Type Declarations
 ****************************************************************************/

/* Per-world CPU context saved/restored by the secure monitor.  The secure
 * (TEE) and non-secure (REE) worlds use an identical layout: r8-r12 are
 * preserved so that a context preempted by a foreign interrupt can be fully
 * saved/restored and resumed at its exact interruption point.
 *
 * The banked-mode registers are the first members so that a pointer to the
 * whole context doubles as the base the monitor assembly walks when it
 * saves/restores the banked set.  Under CONFIG_ARCH_FPU the VFP register
 * file follows the banked set directly, so arm_sm_save_ctx/restore_ctx can
 * stream it out with the same advancing pointer the banked loop leaves
 * behind (no separate offset computation).  The general-purpose registers
 * (r8-r12, r0-r7) and the monitor return state then follow.
 */

struct arm_sm_ctx
{
  /* Banked-mode registers.  Note that fiq_r{8-12} are not saved here;
   * instead the FIQ handler preserves r{8-12} explicitly.
   */

  uint32_t usr_sp;
  uint32_t usr_lr;
  uint32_t irq_spsr;
  uint32_t irq_sp;
  uint32_t irq_lr;
  uint32_t fiq_spsr;
  uint32_t fiq_sp;
  uint32_t fiq_lr;
  uint32_t svc_spsr;
  uint32_t svc_sp;
  uint32_t svc_lr;
  uint32_t abt_spsr;
  uint32_t abt_sp;
  uint32_t abt_lr;
  uint32_t und_spsr;
  uint32_t und_sp;
  uint32_t und_lr;

#ifdef CONFIG_ARCH_FPU
  /* VFP/NEON register file, placed immediately after the banked set so the
   * monitor assembly reaches it with the pointer the banked save/restore
   * loop already advanced to (no SM_CPU_CTX_VFP offset, no add).  The file
   * is a shared hardware resource NOT banked by security state, so it must
   * be swapped on every world switch alongside the banked integer registers
   * (mirroring what arm64 TFA does via simd_ctx_save/restore under
   * CTX_INCLUDE_FPREGS), letting a context be resumed at its exact
   * interruption point.  66 * uint32_t covers d0-d31 (64 words) + FPSCR
   * (1 word, +1 pad); it is stored/loaded with VSTMIA/VLDMIA of the d
   * registers, which only require 4-byte alignment, so it can sit at the
   * banked set's (word-aligned) end.  The struct size is mirrored as
   * SM_CTX_SEC_SIZE in arm_sm_vector.S and guarded by static_assert in
   * arm_sm_runtime.c.
   */

  uint32_t vfp[66];
#endif

  uint32_t r8;
  uint32_t r9;
  uint32_t r10;
  uint32_t r11;
  uint32_t r12;

  uint32_t r0;
  uint32_t r1;
  uint32_t r2;
  uint32_t r3;
  uint32_t r4;
  uint32_t r5;
  uint32_t r6;
  uint32_t r7;

  /* Return state */

  uint32_t mon_lr;
  uint32_t mon_spsr;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: arm_sm_init
 *
 * Description:
 *   Initializes secure monitor
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void arm_sm_init(void);

/****************************************************************************
 * Name: arm_sm_init_stack
 *
 * Description:
 *   Initializes secure monitor
 *
 * Input Parameters:
 *   stack - the address of the stack that allocated for secure monitor
 *           runtime
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void arm_sm_init_stack(int stack);

/****************************************************************************
 * Name: arm_sm_boot_nsec
 *
 * Description:
 *   Set up the non-secure startup context, the entry will set to the
 *   non-secure context lr register, thus when we boot ap from tee, the
 *   non-secure will start running from the address that defined in lr
 *   register, of which contains the entry
 *
 * Input Parameters:
 *   entry - the entry address of the ap core
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void arm_sm_boot_nsec(uintptr_t entry);

/****************************************************************************
 * Name: arm_sm_get_nsec_ctx
 *
 * Description:
 *   Get the non-secure context that used by secure monitor mode
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   The arm_sm_ctx of which contains the non-secure context
 *
 ****************************************************************************/

struct arm_sm_ctx *arm_sm_get_nsec_ctx(void);

/****************************************************************************
 * Name: arm_sm_save_ctx
 *
 * Description:
 *   Save the per-world CPU context (banked-mode registers, and the VFP
 *   register file under CONFIG_ARCH_FPU) into the supplied context.
 *
 * Input Parameters:
 *   ctx - the per-world CPU context to save into
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void arm_sm_save_ctx(struct arm_sm_ctx *ctx);

/****************************************************************************
 * Name: arm_sm_restore_ctx
 *
 * Description:
 *   Restore a per-world CPU context (banked-mode registers, and the VFP
 *   register file under CONFIG_ARCH_FPU) saved by arm_sm_save_ctx.
 *
 * Input Parameters:
 *   ctx - the per-world CPU context to restore from
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void arm_sm_restore_ctx(struct arm_sm_ctx *ctx);

/****************************************************************************
 * Name: arm_vectorsmc
 *
 * Description:
 *   The arm_vectorsmc internal will make "smc" call to enter into the
 *   monitor mode
 *
 * Input Parameters:
 *   a0 - the r0, stored in register
 *   a1 - the r1, stored in register
 *   a2 - the r2, stored in register
 *   a3 - the r3, stored in register
 *   a4 - the r4, stored in stack
 *   a5 - the r5, stored in stack
 *   a6 - the r6, stored in stack
 *   a7 - the r7, stored in stack
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void arm_vectorsmc(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,
                   uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7);

/****************************************************************************
 * Name: arm_vectorfiq_entry
 *
 * Description:
 *   This function is called when non-secure world trigger FIQ, and trapped
 *   to monitor world, and then this FIQ is handled in secure world
 *
 * Input Parameters:
 *   a0 - the r0, stored in register
 *   a1 - the r1, stored in register
 *   a2 - the r2, stored in register
 *   a3 - the r3, stored in register
 *   a4 - the r4, stored in stack
 *   a5 - the r5, stored in stack
 *   a6 - the r6, stored in stack
 *   a7 - the r7, stored in stack
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void arm_vectorfiq_entry(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,
                         uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7);

/****************************************************************************
 * Name: arm_sm_from_sec
 *
 * Description:
 *   Called from monitor mode when an SMC arrives from the secure (TEE)
 *   world.  Decodes ctx->sec.r0 (the SMC function ID) to service the
 *   TEE-side world-switch protocol:
 *
 *   - ARM_SMCCC_NUTTX_TEE_IDLE: TEE has no ready tasks.  If a non-secure
 *     context was preempted by a secure interrupt (ns_preempted set),
 *     resume it (return SM_EXIT_TO_NON_SECURE).  Otherwise return 0 in
 *     ctx->sec.r0 and stay in the secure world (SM_EXIT_TO_SECURE) so the
 *     TEE can WFI.
 *
 *   - ARM_SMCCC_NUTTX_FOREIGN_INTR: TEE detected a foreign (non-secure)
 *     interrupt pending while running and cooperatively yields the CPU to
 *     the non-secure world (SM_EXIT_TO_NON_SECURE).  The interrupt is not
 *     acked by the TEE; it will be handled by REE's own vector once the
 *     non-secure world resumes.
 *
 * Input Parameters:
 *   None.  The per-CPU secure/non-secure contexts are the typed globals
 *   g_monitor.sec / g_monitor.nsec.
 *
 * Returned Value:
 *   SM_EXIT_TO_NON_SECURE: switch to and resume the non-secure world
 *   SM_EXIT_TO_SECURE:     stay in the secure world (return to the TEE)
 *
 ****************************************************************************/

uint32_t arm_sm_from_sec(void);

/****************************************************************************
 * Name: arm_sm_get_ns_preempted / arm_sm_set_ns_preempted
 *
 * Description:
 *   Accessors for the ns_preempted flag.  The flag records that a
 *   non-secure (REE) context was preempted by a secure interrupt while it
 *   was executing, so the monitor must resume that exact context (rather
 *   than leaving the REE parked) once the TEE goes idle.
 *
 ****************************************************************************/

bool arm_sm_get_ns_preempted(void);
void arm_sm_set_ns_preempted(bool preempted);

#endif /* __ARCH_ARM_SRC_ARM_A_R_SM_H */
