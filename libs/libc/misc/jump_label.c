/****************************************************************************
 * libs/libc/misc/jump_label.c
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
#include <nuttx/jump_label.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Extract the key pointer from a jump_entry_s (mask off low 2 bits) */

#define jump_entry_key(entry) \
  ((FAR struct static_key_s *)((entry)->key & ~(uintptr_t)3))

/* Extract the branch type bit from a jump_entry_s */

#define jump_entry_branch(entry) ((bool)((entry)->key & 1u))

/****************************************************************************
 * Extern Declarations
 ****************************************************************************/

extern struct jump_entry_s __start_jump_table[];
extern struct jump_entry_s __stop_jump_table[];

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void jump_label_update(FAR struct static_key_s *key, bool enable)
{
  FAR struct jump_entry_s *entry;

  for (entry = __start_jump_table; entry < __stop_jump_table; entry++)
    {
      if (jump_entry_key(entry) == key)
        {
          bool branch = jump_entry_branch(entry);
          up_static_branch_patch(entry, enable ^ branch);
        }
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void static_key_enable(FAR struct static_key_s *key)
{
  if (atomic_add(&key->enabled, 1) == 0)
    {
      jump_label_update(key, true);
    }
}

void static_key_disable(FAR struct static_key_s *key)
{
  DEBUGASSERT(atomic_read(&key->enabled) > 0);

  if (atomic_sub(&key->enabled, 1) == 1)
    {
      jump_label_update(key, false);
    }
}
