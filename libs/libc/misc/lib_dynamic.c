/****************************************************************************
 * libs/libc/misc/lib_dynamic.c
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

#include <elf.h>

/****************************************************************************
 * Public Data
 ****************************************************************************/

/* Empty ELF dynamic section - provides the _DYNAMIC symbol declared in
 * include/link.h. Since NuttX uses static linking, the dynamic section
 * is empty (terminated by DT_NULL).
 */

Elf_Dyn _DYNAMIC[] =
{
    {
        DT_NULL,
        {
          0
        }
    }
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/
