/****************************************************************************
 * libs/libc/builtin/lib_builtin_getheapsize.c
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

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: builtin_getheapsize
 *
 * Description:
 *   Returns the heap size of the application at 'index' in the table
 *   of built-in applications.
 *
 * Input Parameters:
 *   index - From 0 and on ...
 *
 * Returned Value:
 *   Returns the valid heap size for the app if index is valid.
 *   Otherwise 0 is returned.
 *
 ****************************************************************************/

size_t builtin_getheapsize(int index)
{
  FAR const struct builtin_s *builtin;

  builtin = builtin_for_index(index);

  if (builtin != NULL)
    {
      return builtin->heapsize;
    }

  return CONFIG_MM_TASK_HEAP_DEFAULT_SIZE;
}
