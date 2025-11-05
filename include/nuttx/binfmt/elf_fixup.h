/****************************************************************************
 * include/nuttx/binfmt/elf_fixup.h
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

#ifndef __INCLUDE_NUTTX_BINFMT_ELF_FIXUP_H
#define __INCLUDE_NUTTX_BINFMT_ELF_FIXUP_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <stddef.h>
#include <elf.h>

/****************************************************************************
 * Public Types
 ****************************************************************************/

#ifdef CONFIG_ELF_FIXUP
struct elf_fixup_s
{
  const char      name[NAME_MAX + 1];
  uintptr_t       entry;
  size_t          stacksize;
  int             priority;
#ifdef CONFIG_SCHED_USER_IDENTITY
  uid_t           uid;
  gid_t           gid;
  int             mode;
#endif
#ifdef CONFIG_MM_TASK_HEAP
  size_t          heapstart;
  size_t          heapsize;
#endif
  Elf_Phdr        phdr[CONFIG_ELF_FIXUP_NSEGMENTS];
};
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: elf_fixup_ramstart
 *
 * Description:
 *   Return the start address of RAM segment from elf fixup table
 *
 * Returned Value:
 *   ram start address
 *
 ****************************************************************************/

uintptr_t elf_fixup_ramstart(void);

#endif /* __INCLUDE_NUTTX_BINFMT_ELF_FIXUP_H */
