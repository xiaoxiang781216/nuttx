/****************************************************************************
 * arch/risc-v/src/common/riscv_jump_label.c
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

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* RISC-V instruction encodings:
 *
 * NOP: addi x0, x0, 0 = 0x00000013
 *
 * JAL x0, offset (J-type, unconditional jump, discard return address):
 *   bits[31:12] = imm[20|10:1|11|19:12]
 *   bits[11:7]  = rd = x0 (0)
 *   bits[6:0]   = opcode = 0x6f (JAL)
 *   Range: +/-1 MB (21-bit signed offset, 2-byte aligned)
 */

#define RISCV_INSN_NOP        0x00000013u
#define RISCV_OPCODE_JAL      0x6fu

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: riscv_gen_jal
 *
 * Description:
 *   Generate a JAL x0, offset instruction (unconditional jump).
 *   J-type encoding (little-endian):
 *     imm[20|10:1|11|19:12] | rd(x0) | opcode(0x6f)
 *
 ****************************************************************************/

static uint32_t riscv_gen_jal(uintptr_t pc, uintptr_t target)
{
  int32_t offset = (int32_t)(target - pc);
  uint32_t imm20;
  uint32_t imm10_1;
  uint32_t imm11;
  uint32_t imm19_12;

  /* JAL range: +/-1 MB, must be 2-byte aligned */

  DEBUGASSERT((offset & 1) == 0);
  DEBUGASSERT(offset >= -1048576 && offset <= 1048574);

  /* J-type immediate encoding:
   *   bit 31    = imm[20]
   *   bits 30:21 = imm[10:1]
   *   bit 20    = imm[11]
   *   bits 19:12 = imm[19:12]
   */

  imm20    = (offset >> 20) & 0x1;
  imm10_1  = (offset >> 1)  & 0x3ff;
  imm11    = (offset >> 11) & 0x1;
  imm19_12 = (offset >> 12) & 0xff;

  return (imm20 << 31) | (imm10_1 << 21) | (imm11 << 20) |
         (imm19_12 << 12) | RISCV_OPCODE_JAL;
}

/****************************************************************************
 * Name: riscv_patch_text
 *
 * Description:
 *   Atomically patch a 4-byte instruction in the text section.
 *   Uses atomic store for single-word write, followed by fence rw,rw
 *   and fence.i to ensure subsequent instruction fetches see the new
 *   instruction.
 *
 ****************************************************************************/

static void riscv_patch_text(FAR atomic_t *addr, uint32_t insn)
{
  atomic_set(addr, insn);
  __asm__ volatile ("fence rw, rw" ::: "memory");
  __asm__ volatile ("fence.i" ::: "memory");
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_static_branch_patch
 *
 * Description:
 *   Patch a single jump site: replace NOP with JAL or vice versa.
 *
 *   enable=true  -> write JAL x0, <target>
 *   enable=false -> write NOP
 *
 ****************************************************************************/

void up_static_branch_patch(FAR struct jump_entry_s *entry, bool enable)
{
  uint32_t insn;

  insn = enable ? riscv_gen_jal(entry->code, entry->target) :
                  RISCV_INSN_NOP;

  riscv_patch_text((FAR atomic_t *)entry->code, insn);
}
