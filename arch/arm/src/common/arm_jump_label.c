/****************************************************************************
 * arch/arm/src/common/arm_jump_label.c
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

#include <nuttx/cache.h>
#include <nuttx/jump_label.h>
#include <nuttx/spinlock.h>

#include <arch/barriers.h>
#include <stdint.h>
#include <assert.h>

#ifdef CONFIG_ARM_MPU
#  include "arm_internal.h"
#  include "mpu.h"
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifdef CONFIG_ARCH_ARMV6M

/* Thumb-1: single 16-bit NOP (nop = 0xBF00) */

#  define ARM_THUMB_NOP 0xbf00u
typedef uint16_t insn_t;

#else

/* Thumb-2: single 32-bit NOP (nop.w = 0xf3af 0x8000) */

#  define ARM_THUMB_NOP 0x8000f3afu
typedef uint32_t insn_t;

#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

static spinlock_t g_patch_lock = SP_UNLOCKED;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#ifdef CONFIG_ARCH_ARMV6M

/****************************************************************************
 * Name: arm_gen_branch
 *
 * Description:
 *   Thumb-1: 16-bit branch (encoding T2).
 *   Encoding T2: 1110 0iii iiii iiii  (imm11, ±2 KB range)
 *
 ****************************************************************************/

static insn_t arm_gen_branch(uint32_t pc, uint32_t target)
{
  int32_t offset = (int32_t)(target - (pc + 4));

  DEBUGASSERT(offset >= -2048 && offset <= 2046);

  return 0xe000 | ((offset >> 1) & 0x7ff);
}

#else

/****************************************************************************
 * Name: arm_gen_branch
 *
 * Description:
 *   Thumb-2: 32-bit branch (b.w, encoding T4, ±16 MB range).
 *   Format: [15:0] = 0xf000 | S | imm10, [31:16] = 0x9000 | J1 | J2 | imm11
 *
 ****************************************************************************/

static insn_t arm_gen_branch(uint32_t pc, uint32_t target)
{
  int32_t offset = (int32_t)(target - (pc + 4));
  uint32_t s;
  uint32_t j1;
  uint32_t j2;
  uint32_t imm10;
  uint32_t imm11;
  uint16_t hw0;
  uint16_t hw1;

  DEBUGASSERT(offset >= -16777216 && offset <= 16777214);

  s     = (offset >> 24) & 1;
  imm10 = (offset >> 12) & 0x3ff;
  imm11 = (offset >>  1) & 0x7ff;
  j1    = ((~(offset >> 23)) ^ s) & 1;
  j2    = ((~(offset >> 22)) ^ s) & 1;

  hw0 = 0xf000 | (s << 10) | imm10;
  hw1 = 0x9000 | (j1 << 13) | (j2 << 11) | imm11;

  return ((uint32_t)hw1 << 16) | hw0;
}

#endif

/****************************************************************************
 * Name: arm_patch_text
 *
 * Description:
 *   Patch a single instruction in the text section.  This is used to
 *   implement static branch patching for jump labels.
 *
 ****************************************************************************/

static void arm_patch_text(volatile insn_t *addr, insn_t insn)
{
  irqstate_t flags;
#ifdef CONFIG_ARM_MPU
  uint32_t mpu_ctrl;
#endif

  flags = spin_lock_irqsave(&g_patch_lock);

#ifdef CONFIG_ARM_MPU
  mpu_ctrl = getreg32(MPU_CTRL);
  putreg32(mpu_ctrl & ~MPU_CTRL_ENABLE, MPU_CTRL);
  UP_DSB();
  UP_ISB();
#endif

  *addr = insn;

  up_coherent_dcache((uintptr_t)addr, (uintptr_t)addr + sizeof(insn_t));

#ifdef CONFIG_ARM_MPU
  putreg32(mpu_ctrl, MPU_CTRL);
  UP_DSB();
  UP_ISB();
#endif

  spin_unlock_irqrestore(&g_patch_lock, flags);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_static_branch_patch
 *
 * Description:
 *   Patch a single jump site: replace NOP with branch or vice versa.
 *
 ****************************************************************************/

void up_static_branch_patch(struct jump_entry_s *entry, bool enable)
{
  volatile insn_t *addr = (void *)(entry->code & ~1u);

  if (enable)
    {
      arm_patch_text(addr, arm_gen_branch(entry->code & ~1u,
                                          entry->target & ~1u));
    }
  else
    {
      arm_patch_text(addr, ARM_THUMB_NOP);
    }
}
