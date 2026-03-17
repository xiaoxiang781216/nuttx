/****************************************************************************
 * arch/arm/include/jump_label.h
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

/* Thumb jump label implementation using asm goto.
 *
 * Thumb-2 (ARMv7-M / ARMv8-M):
 *   NOP state:  nop.w            (0xf3af8000, 4 bytes)
 *   JMP state:  b.w <target>     (4 bytes, +/-16 MB range)
 *
 * Thumb-1 (ARMv6-M):
 *   NOP state:  nop              (0xbf00, 2 bytes)
 *   JMP state:  b <target>       (0xe000+off, 2 bytes, +/-2 KB)
 */

#ifndef __ARCH_ARM_INCLUDE_JUMP_LABEL_H
#define __ARCH_ARM_INCLUDE_JUMP_LABEL_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/compiler.h>
#include <stdbool.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Use macros instead of inline functions so that the "i" constraint
 * receives a genuine compile-time constant even at -O0.
 */

#ifdef CONFIG_ARCH_ARMV6M

/* Thumb-1: single 16-bit NOP / 16-bit branch */

#define up_static_branch(key, branch)              \
  ({                                               \
    __label__ l_jmp, l_end;                        \
    bool __jl_res;                                 \
    asm goto (                                     \
      "1:\n\t"                                     \
      "nop\n\t"                                    \
      ".pushsection jump_table, \"aw\"\n\t"        \
      ".align 2\n\t"                               \
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

#define up_static_branch_jump(key, branch)         \
  ({                                               \
    __label__ l_jmp, l_end;                        \
    bool __jl_res;                                 \
    asm goto (                                     \
      "1:\n\t"                                     \
      "b %l[l_jmp]\n\t"                            \
      ".pushsection jump_table, \"aw\"\n\t"        \
      ".align 2\n\t"                               \
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

#else /* Thumb-2: ARMv7-M / ARMv8-M */

/* Thumb-2: 32-bit nop.w / 32-bit b.w */

#define up_static_branch(key, branch)              \
  ({                                               \
    __label__ l_jmp, l_end;                        \
    bool __jl_res;                                 \
    asm goto (                                     \
      "1:\n\t"                                     \
      "nop.w\n\t"                                  \
      ".pushsection jump_table, \"aw\"\n\t"        \
      ".align 2\n\t"                               \
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

#define up_static_branch_jump(key, branch)         \
  ({                                               \
    __label__ l_jmp, l_end;                        \
    bool __jl_res;                                 \
    asm goto (                                     \
      "1:\n\t"                                     \
      "b.w %l[l_jmp]\n\t"                          \
      ".pushsection jump_table, \"aw\"\n\t"        \
      ".align 2\n\t"                               \
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

#endif /* CONFIG_ARCH_ARMV6M */

#endif /* __ARCH_ARM_INCLUDE_JUMP_LABEL_H */
