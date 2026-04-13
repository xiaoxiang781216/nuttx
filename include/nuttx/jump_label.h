/****************************************************************************
 * include/nuttx/jump_label.h
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

#ifndef __INCLUDE_NUTTX_JUMP_LABEL_H
#define __INCLUDE_NUTTX_JUMP_LABEL_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <assert.h>
#include <nuttx/atomic.h>
#include <nuttx/compiler.h>
#include <nuttx/config.h>
#include <nuttx/kconfig.h>
#include <nuttx/macro.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef CONFIG_ARCH_JUMP_LABEL
#  include <nuttx/arch.h>
#endif

/* Jump label / static key model:
 *
 * Two key types: static_key_true (initially enabled)
 *                static_key_false (initially disabled)
 *
 * Combined with branch hints to select optimal initial instruction:
 *
 *   type\branch  | likely              | unlikely
 *   -------------+---------------------+--------------------
 *   true  (1)    | NOP  (fast path)    | JMP
 *   false (0)    | JMP                 | NOP  (fast path)
 *
 * Best practice: pair DEFINE_STATIC_KEY_TRUE  with static_branch_likely
 *                pair DEFINE_STATIC_KEY_FALSE with static_branch_unlikely
 *
 * At runtime, static_key_enable/disable patches NOP<->JMP via
 * up_static_branch_patch.
 *
 * Fallback (!CONFIG_ARCH_JUMP_LABEL): volatile bool + branch prediction
 * hints.
 */

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Static key — reference-counted runtime toggle.
 *
 * static_key_true / static_key_false are distinct types so that
 * static_branch_likely/unlikely can dispatch at compile time via
 * is_types_compatible, selecting NOP vs JMP as the initial
 * instruction for zero-cost hot paths.
 */

struct static_key_s
{
  atomic_t enabled;                     /* Reference count, >0 means enabled */
};

struct static_key_true
{
  struct static_key_s key;
};

struct static_key_false
{
  struct static_key_s key;
};

/****************************************************************************
 * Pre-processor Definitions (shared)
 ****************************************************************************/

#define STATIC_KEY_TRUE_INIT  \
  {                           \
    .key = {.enabled = 1}     \
  }

#define STATIC_KEY_FALSE_INIT \
  {                           \
    .key = {.enabled = 0}     \
  }

#define DEFINE_STATIC_KEY_TRUE(name) \
  struct static_key_true name = STATIC_KEY_TRUE_INIT

#define DEFINE_STATIC_KEY_FALSE(name) \
  struct static_key_false name = STATIC_KEY_FALSE_INIT

#define DECLARE_STATIC_KEY_TRUE(name) extern struct static_key_true name

#define DECLARE_STATIC_KEY_FALSE(name) extern struct static_key_false name

/* Check if a static key is currently enabled */

#define static_key_enabled(key) (atomic_read(&(key)->enabled) > 0)

#ifdef CONFIG_ARCH_JUMP_LABEL

/****************************************************************************
 * Public Types (JUMP_LABEL path)
 ****************************************************************************/

/* Jump table entry — generated at compile time by asm goto,
 * stored in the jump_table section.
 */

struct jump_entry_s
{
  uintptr_t code;   /* Address of instruction to patch */
  uintptr_t target; /* Branch target address */
  uintptr_t key;    /* static_key pointer (low bit encodes branch type) */
};

/****************************************************************************
 * Pre-processor Definitions (JUMP_LABEL path)
 ****************************************************************************/

/* Branch check API - dispatches based on key type at compile time.
 *
 * Logic:
 *
 *   static_branch_likely(x):
 *     static_key_true  -> up_static_branch(key, true)
 *                         NOP -> true  (zero-cost true)
 *     static_key_false -> up_static_branch_jump(key, true)
 *                         JMP -> false (zero-cost false)
 *
 *   static_branch_unlikely(x):
 *     static_key_true  -> !up_static_branch_jump(key, false)
 *                         JMP -> false
 *     static_key_false -> !up_static_branch(key, false)
 *                         NOP -> true -> !true = false
 */

#define static_branch_likely(x)                              \
  ({                                                         \
    bool __branch;                                           \
    if (is_types_compatible(*(x), struct static_key_true))   \
      {                                                      \
        __branch = up_static_branch(&(x)->key, true);        \
      }                                                      \
    else                                                     \
      {                                                      \
        __branch = up_static_branch_jump(&(x)->key, true);   \
      }                                                      \
    predict_true(__branch);                                  \
  })

#define static_branch_unlikely(x)                            \
  ({                                                         \
    bool __branch;                                           \
    if (is_types_compatible(*(x), struct static_key_true))   \
      {                                                      \
        __branch = !up_static_branch_jump(&(x)->key, false); \
      }                                                      \
    else                                                     \
      {                                                      \
        __branch = !up_static_branch(&(x)->key, false);      \
      }                                                      \
    predict_false(__branch);                                 \
  })

/****************************************************************************
 * Public Function Prototypes (JUMP_LABEL path)
 ****************************************************************************/

#ifndef __ASSEMBLY__

/* Runtime toggle — patches all jump sites for this key */

void static_key_enable(FAR struct static_key_s *key);
void static_key_disable(FAR struct static_key_s *key);

#endif /* !__ASSEMBLY__ */

#else /* !CONFIG_ARCH_JUMP_LABEL */

/****************************************************************************
 * Pre-processor Definitions (fallback path)
 ****************************************************************************/

/* Branch check API — volatile bool + branch prediction */

#define static_branch_unlikely(x) \
  predict_false(static_key_enabled(&(x)->key))

#define static_branch_likely(x) \
  predict_true(static_key_enabled(&(x)->key))

/****************************************************************************
 * Public Function Prototypes (fallback path)
 ****************************************************************************/

#ifndef __ASSEMBLY__

static inline_function void static_key_enable(FAR struct static_key_s *key)
{
  atomic_add(&key->enabled, 1);
}

static inline_function void static_key_disable(FAR struct static_key_s *key)
{
  DEBUGASSERT(atomic_read(&key->enabled) > 0);
  atomic_sub(&key->enabled, 1);
}

#endif /* !__ASSEMBLY__ */

#endif /* CONFIG_ARCH_JUMP_LABEL */

/* MAYBE variants — key type follows a boolean Kconfig option.
 *
 * Usage:
 *   DEFINE_STATIC_KEY_MAYBE(CONFIG_FOO, my_key);
 *   DECLARE_STATIC_KEY_MAYBE(CONFIG_FOO, my_key);
 *   if (static_branch_maybe(CONFIG_FOO, &my_key)) { ... }
 *
 * When CONFIG_FOO=y: equivalent to STATIC_KEY_TRUE  + static_branch_likely
 * When CONFIG_FOO=n: equivalent to STATIC_KEY_FALSE + static_branch_unlikely
 */

#define DEFINE_STATIC_KEY_1(name)  DEFINE_STATIC_KEY_TRUE(name)
#define DEFINE_STATIC_KEY_0(name)  DEFINE_STATIC_KEY_FALSE(name)
#define DEFINE_STATIC_KEY_MAYBE(cfg, name) \
  CONCATENATE(DEFINE_STATIC_KEY_, NX_IS_ENABLED(cfg))(name)

#define DECLARE_STATIC_KEY_1(name) DECLARE_STATIC_KEY_TRUE(name)
#define DECLARE_STATIC_KEY_0(name) DECLARE_STATIC_KEY_FALSE(name)
#define DECLARE_STATIC_KEY_MAYBE(cfg, name) \
  CONCATENATE(DECLARE_STATIC_KEY_, NX_IS_ENABLED(cfg))(name)

#define static_branch_maybe(config, x) \
  (NX_IS_ENABLED(config) ? static_branch_likely(x) \
                      : static_branch_unlikely(x))

#endif /* __INCLUDE_NUTTX_JUMP_LABEL_H */
