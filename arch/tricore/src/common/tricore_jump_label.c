/****************************************************************************
 * arch/tricore/src/common/tricore_jump_label.c
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

#include <nuttx/arch.h>
#include <nuttx/atomic.h>
#include <nuttx/jump_label.h>

#include <stdint.h>
#include <assert.h>

#include "tricore_internal.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* TriCore instruction encodings:
 *
 * NOP (16-bit): 0x0000
 *   Two consecutive NOPs = 0x00000000 (4 bytes)
 *
 * J disp24 (32-bit, B-format): unconditional PC-relative jump
 *   Encoding (little-endian uint32_t):
 *     bits[7:0]   = opcode (0x1D)
 *     bits[15:8]  = disp24[23:16]
 *     bits[31:16] = disp24[15:0]
 *   Target = PC + sign_extend(disp24) * 2
 *   Range: +/-16 MB
 */

#define TRICORE_INSN_NOP2     0x00000000u  /* Two 16-bit NOPs */
#define TRICORE_OPCODE_J      0x1du        /* J (unconditional jump) opcode */

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: tricore_gen_j
 *
 * Description:
 *   Generate a 32-bit J (unconditional jump) instruction encoding.
 *   TriCore B-format (little-endian uint32_t):
 *     bits[7:0]   = opcode (0x1D)
 *     bits[15:8]  = disp24[23:16]
 *     bits[31:16] = disp24[15:0]
 *   Target address = PC + sign_extend(disp24) * 2
 *
 ****************************************************************************/

static uint32_t tricore_gen_j(uint32_t pc, uint32_t target)
{
  int32_t offset = (int32_t)(target - pc);
  int32_t disp24;

  /* disp24 = offset / 2 (must be expressible in 24 signed bits) */

  DEBUGASSERT((offset & 1) == 0);
  disp24 = offset >> 1;
  DEBUGASSERT(disp24 >= -8388608 && disp24 <= 8388607);

  /* TriCore B-format encoding for J disp24:
   *   bits[7:0]   = opcode (0x1D)
   *   bits[15:8]  = disp24[23:16]  (high byte of displacement)
   *   bits[31:16] = disp24[15:0]   (low 16 bits of displacement)
   */

  return ((uint32_t)(disp24 & 0x0000ffff) << 16) |
         ((uint32_t)((disp24 >> 16) & 0xff) << 8) |
         TRICORE_OPCODE_J;
}

/****************************************************************************
 * Name: tricore_patch_text
 *
 * Description:
 *   Atomically patch a 4-byte instruction site in the text section.
 *   The address must be 4-byte aligned and located in writable program
 *   memory (PSPR).  Uses atomic_set for explicit store semantics,
 *   followed by UP_DSB (data synchronization barrier to ensure store
 *   completion) and UP_ISB (instruction synchronization barrier to
 *   flush pipeline) guaranteeing subsequent fetches see the new
 *   instruction.
 *
 ****************************************************************************/

static void tricore_patch_text(FAR atomic_t *addr, uint32_t insn)
{
  atomic_set(addr, insn);
  UP_DSB();
  UP_ISB();
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_static_branch_patch
 *
 * Description:
 *   Patch a single jump site on the local CPU: replace two NOPs with
 *   J instruction or vice versa.
 *
 *   enable=true  -> write J <target> (branch to l_jmp)
 *   enable=false -> write NOP; NOP   (fall through)
 *
 ****************************************************************************/

void up_static_branch_patch(FAR struct jump_entry_s *entry, bool enable)
{
  uint32_t insn;

  insn = enable ? tricore_gen_j(entry->code, entry->target) :
                  TRICORE_INSN_NOP2;

  tricore_patch_text((FAR atomic_t *)entry->code, insn);
}
