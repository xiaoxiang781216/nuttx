/****************************************************************************
 * libs/libc/machine/arm/arch_unwind_find_exidx.c
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
#include <nuttx/lib/elf.h>
#include <unwind.h>

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: __gnu_Unwind_Find_exidx
 *
 * Description:
 *   Called by the ARM EHABI unwind runtime (libgcc) to retrieve the
 *   .ARM.exidx exception index section for a given PC address.
 *
 *   First checks dynamically loaded modules via libelf_find_unwind.
 *   Falls back to the main firmware's exidx from linker script symbols.
 *
 ****************************************************************************/

_Unwind_Ptr __gnu_Unwind_Find_exidx(_Unwind_Ptr return_address, int *nrecp)
{
#ifdef CONFIG_LIBC_ELF_UNWIND_SECTNAME
  uintptr_t unwind;
  size_t unwindsize;

  if (libelf_find_unwind((uintptr_t)return_address,
                         &unwind, &unwindsize) == 0)
    {
      *nrecp = unwindsize / 8;
      return (_Unwind_Ptr)unwind;
    }
#endif

  /* Fallback: main firmware's .ARM.exidx from linker script */

  *nrecp = __exidx_end - __exidx_start;
  return (_Unwind_Ptr)__exidx_start;
}
