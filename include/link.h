/****************************************************************************
 * include/link.h
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

#ifndef __INCLUDE_LINK_H
#define __INCLUDE_LINK_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <elf.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Define ElfW macro for both 32-bit and 64-bit architectures.
 * This macro is used by libcamera and other code that needs to work
 * with ELF dynamic sections in a portable way.
 */

#ifdef CONFIG_ARCH_64BIT
#  define ElfW(type) Elf64_##type
#else
#  define ElfW(type) Elf32_##type
#endif

/****************************************************************************
 * Public Data
 ****************************************************************************/

#ifdef __cplusplus
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

EXTERN Elf_Dyn _DYNAMIC[];

#undef EXTERN
#ifdef __cplusplus
}
#endif

#endif /* __INCLUDE_LINK_H */
