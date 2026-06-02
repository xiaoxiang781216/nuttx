/****************************************************************************
 * libs/libc/elf/elf_find_unwind.c
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
#include <errno.h>

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct unwind_search_s
{
  uintptr_t address;
  uintptr_t unwind;
  size_t    unwindsize;
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: find_unwind_callback
 *
 * Description:
 *   libelf_registry_foreach callback.  Check if the given module's text
 *   segment covers the target return address.  If so, store the module's
 *   unwind section info and return 1 to stop iteration.
 *
 * Returned Value:
 *   0 - Module does not match, continue iterating
 *   1 - Module matched, stop iterating (result stored in search struct)
 *
 ****************************************************************************/

static int find_unwind_callback(FAR struct module_s *modp, FAR void *arg)
{
  FAR struct unwind_search_s *search = (FAR struct unwind_search_s *)arg;
  uintptr_t text_start = (uintptr_t)modp->textalloc;
  uintptr_t text_end = text_start + modp->textsize;

  if (modp->unwind != 0 && modp->unwindsize != 0 &&
      search->address >= text_start &&
      search->address < text_end)
    {
      search->unwind     = modp->unwind;
      search->unwindsize = modp->unwindsize;
      return 1;
    }

  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: libelf_find_unwind
 *
 * Description:
 *   Search all loaded modules to find the unwind section that covers
 *   a given return address.
 *
 * Input Parameters:
 *   address        - The PC value to find exception info
 *   unwind_start   - Pointer to store the start address of unwind section
 *   unwindsize     - Pointer to store the size of unwind section (bytes)
 *
 * Returned Value:
 *   0 on success, -ENOENT if not found
 *
 ****************************************************************************/

int libelf_find_unwind(uintptr_t address, FAR uintptr_t *unwind,
                       FAR size_t *unwindsize)
{
  struct unwind_search_s search;

  search.address    = address;
  search.unwind     = 0;
  search.unwindsize = 0;

  libelf_registry_foreach(find_unwind_callback, &search);

  if (search.unwind != 0)
    {
      *unwind  = search.unwind;
      *unwindsize = search.unwindsize;
      return 0;
    }

  return -ENOENT;
}
