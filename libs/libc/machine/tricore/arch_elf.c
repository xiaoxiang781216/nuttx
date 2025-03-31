/****************************************************************************
 * libs/libc/machine/tricore/arch_elf.c
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

#include <inttypes.h>
#include <errno.h>
#include <debug.h>

#include <nuttx/elf.h>

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_checkarch
 *
 * Description:
 *   Given the ELF header in 'hdr', verify that the ELF file is appropriate
 *   for the current, configured architecture.  Every architecture that uses
 *   the ELF loader must provide this function.
 *
 * Input Parameters:
 *   hdr - The ELF header read from the ELF file.
 *
 * Returned Value:
 *   True if the architecture supports this ELF file.
 *
 ****************************************************************************/

bool up_checkarch(const Elf32_Ehdr *ehdr)
{
  /* Make sure it's an TriCore executable */

  if (ehdr->e_machine != EM_TRICORE)
    {
      berr("ERROR: Not for TRICORE: e_machine=%04x\n", ehdr->e_machine);
      return false;
    }

  /* Make sure that 32-bit objects are supported */

  if (ehdr->e_ident[EI_CLASS] != ELFCLASS32)
    {
      berr("ERROR: Need 32-bit objects: e_ident[EI_CLASS]=%02x\n",
           ehdr->e_ident[EI_CLASS]);
      return false;
    }

  /* Verify endian-ness */

#ifdef CONFIG_ENDIAN_BIG
  if (ehdr->e_ident[EI_DATA] != ELFDATA2MSB)
#else
  if (ehdr->e_ident[EI_DATA] != ELFDATA2LSB)
#endif
    {
      berr("ERROR: Wrong endian-ness: e_ident[EI_DATA]=%02x\n",
           ehdr->e_ident[EI_DATA]);
      return false;
    }

  /* TODO:  Check ABI here. */

  return true;
}

/****************************************************************************
 * Name: up_relocate
 *
 * Description:
 *   Perform a standard ELF relocation.
 *   TriCore-specific handling for R_TRICORE_* relocation types.
 *
 ****************************************************************************/

int up_relocate(const Elf32_Rel *rel, const Elf32_Sym *sym, uintptr_t addr,
                void *arch_data)
{
  berr("ERROR:up_relocate not implemented.\n");
  return -ENOSYS;
}

/****************************************************************************
 * Name: up_relocateadd
 *
 * Description:
 *   Perform an ELF relocation with explicit addend (Rela).
 *   Similar to up_relocate, but uses rel->r_addend.
 *
 ****************************************************************************/

int up_relocateadd(const Elf_Rela *rel, const Elf_Sym *sym,
                   uintptr_t addr, void *arch_data)
{
  berr("ERROR: up_relocateadd not implemented\n");
  return -ENOSYS;
}
