/****************************************************************************
 * arch/tricore/include/jump_label.h
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

/* TriCore jump label implementation using asm goto (GCC only).
 *
 * NOP state:  nop; nop         (2x 16-bit = 4 bytes, 0x00000000)
 * JMP state:  j <target>       (32-bit B-format, opcode 0x1D, +/-16 MB)
 *
 * Both states occupy exactly 4 bytes at a 4-byte aligned address,
 * enabling atomic patching via a single 32-bit store.
 */

#ifndef __ARCH_TRICORE_INCLUDE_JUMP_LABEL_H
#define __ARCH_TRICORE_INCLUDE_JUMP_LABEL_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/compiler.h>
#include <stdbool.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* NOP initial state: two 16-bit NOPs (4 bytes total).
 * The .balign 4 ensures the pair starts at a 4-byte boundary so that
 * a single 32-bit store can atomically replace them with a J instruction.
 */

#define up_static_branch(key, branch)              \
  ({                                               \
    __label__ l_jmp, l_end;                        \
    bool __jl_res;                                 \
    asm goto (                                     \
      ".balign 4\n\t"                              \
      "1:\n\t"                                     \
      "nop\n\t"                                    \
      "nop\n\t"                                    \
      ".pushsection jump_table, \"a\"\n\t"         \
      ".balign 4\n\t"                              \
      ".word 1b, %l[l_jmp], %c0\n\t"               \
      ".popsection\n\t"                            \
      : : "i" (&((char *)(key))[branch]) : : l_jmp \
    );                                             \
    __jl_res = true;                               \
    goto l_end;                                    \
  l_jmp:                                           \
    __jl_res = false;                              \
  l_end:                                           \
    __jl_res;                                      \
  })

/* JMP initial state: 32-bit unconditional jump (J instruction). */

#define up_static_branch_jump(key, branch)         \
  ({                                               \
    __label__ l_jmp, l_end;                        \
    bool __jl_res;                                 \
    asm goto (                                     \
      ".balign 4\n\t"                              \
      "1:\n\t"                                     \
      "j %l[l_jmp]\n\t"                            \
      ".pushsection jump_table, \"a\"\n\t"         \
      ".balign 4\n\t"                              \
      ".word 1b, %l[l_jmp], %c0\n\t"               \
      ".popsection\n\t"                            \
      : : "i" (&((char *)(key))[branch]) : : l_jmp \
    );                                             \
    __jl_res = true;                               \
    goto l_end;                                    \
  l_jmp:                                           \
    __jl_res = false;                              \
  l_end:                                           \
    __jl_res;                                      \
  })

#endif /* __ARCH_TRICORE_INCLUDE_JUMP_LABEL_H */
