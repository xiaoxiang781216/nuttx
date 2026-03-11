/****************************************************************************
 * libs/libc/builtin/lib_builtin_forindex.c
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

#include <nuttx/lib/builtin.h>
#include <nuttx/userspace.h>

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: builtin_for_index
 *
 * Description:
 *   Returns the builtin_s structure for the selected built-in.
 *   If support for built-in functions is enabled in the NuttX
 *   configuration, then this function must be provided by the application
 *   code.
 *
 * Input Parameters:
 *   index, from 0 and on...
 *
 * Returned Value:
 *   Returns valid pointer pointing to the builtin_s structure if index is
 *   valid.
 *   Otherwise, NULL is returned.
 *
 ****************************************************************************/

FAR const struct builtin_s *builtin_for_index(int index)
{
#if defined(CONFIG_BUILD_PROTECTED) && defined(__KERNEL__)
  /* The kernel-side table has a trailing {} sentinel entry; skip it so that
   * the user-side table is reached for indices past the real kernel apps.
   */

  int kernel_count = g_builtin_count - 1;
  if (index >= kernel_count)
    {
      index -= kernel_count;
      if (index < *USERSPACE->builtin_count)
        {
          return &USERSPACE->builtins[index];
        }

      return NULL;
    }
#endif

  if (index < g_builtin_count)
    {
      return &g_builtins[index];
    }

  return NULL;
}
