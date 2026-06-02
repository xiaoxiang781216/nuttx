/****************************************************************************
 * libs/libc/elf/elf_merge_exidx.c
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

#include <stdint.h>
#include <debug.h>
#include <inttypes.h>

#include <nuttx/lib/elf.h>

#include "libc.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define ELF_MAX_UNWIND_ENTRIES 4096

/* ARM EHABI prel31 offset range: 31-bit signed, +-2^30 (~1GB) */

#define PREL31_MAX   ((int32_t)0x3fffffff)
#define PREL31_MIN   ((int32_t)(-0x40000000))
#define PREL31_SIGN  0x40000000u  /* bit30: sign bit in prel31 */
#define PREL31_EXT   0x80000000u  /* bit31: sign extension fill */
#define PREL31_MASK  0x7fffffffu  /* bits 0-30: offset value */

/* Sign-extend a prel31 value to int32_t */

#define PREL31_SIGNEXT(v) \
   (((v) & PREL31_SIGN) ? (int32_t)((v) | PREL31_EXT) : (int32_t)(v))

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: libelf_merge_exidx
 *
 * Description:
 *   Merge all SHT_ARM_EXIDX sections into a single sorted table after
 *   relocation.  This handles COMDAT .ARM.exidx sections produced by
 *   partial linking with -r.
 *
 *   If only one exidx section exists, loadinfo->unwind is set to point
 *   directly at it (no allocation).  If multiple exist, a merged table
 *   is heap-allocated and loadinfo->unwind points to it.
 *
 * Input Parameters:
 *   loadinfo - The load information with section headers available
 *
 * Returned Value:
 *   0 (OK) is returned on success and a negated errno is returned on
 *   failure.
 ****************************************************************************/

int libelf_merge_exidx(FAR struct mod_loadinfo_s *loadinfo)
{
  int total_entries = 0;
  int exidx_count = 0;
  int exidx_idx = -1;
  int idx;

  /* Count all SHT_ARM_EXIDX sections */

  for (idx = 0; idx < loadinfo->ehdr.e_shnum; idx++)
    {
      if (loadinfo->shdr[idx].sh_type == SHT_ARM_EXIDX &&
          loadinfo->shdr[idx].sh_size > 0 &&
          (loadinfo->shdr[idx].sh_flags & SHF_ALLOC) != 0)
        {
          total_entries += loadinfo->shdr[idx].sh_size / 8;
          exidx_count++;
          exidx_idx = idx;
        }
    }

  if (exidx_count == 0)
    {
      loadinfo->unwind     = 0;
      loadinfo->unwindsize = 0;
    }
  else if (exidx_count == 1)
    {
      loadinfo->unwind     = loadinfo->shdr[exidx_idx].sh_addr;
      loadinfo->unwindsize = loadinfo->shdr[exidx_idx].sh_size;
    }
  else if (total_entries <= ELF_MAX_UNWIND_ENTRIES)
    {
      FAR uint32_t *merged;
      int pos = 0;

      merged = (FAR uint32_t *)lib_malloc(total_entries * 8);
      if (merged == NULL)
        {
          return -ENOMEM;
        }

      for (idx = 0; idx < loadinfo->ehdr.e_shnum; idx++)
        {
          int nent;
          int j;

          if (loadinfo->shdr[idx].sh_type != SHT_ARM_EXIDX ||
              loadinfo->shdr[idx].sh_size == 0 ||
              (loadinfo->shdr[idx].sh_flags & SHF_ALLOC) == 0)
            {
              continue;
            }

          nent = loadinfo->shdr[idx].sh_size / 8;
          for (j = 0; j < nent; j++, pos++)
            {
              uintptr_t entryaddr = loadinfo->shdr[idx].sh_addr + j * 8;
              uint32_t word0 = *(FAR uint32_t *)(entryaddr);
              uint32_t word1 = *(FAR uint32_t *)(entryaddr + 4);

              /* Store absolute function address */

              merged[pos * 2] = (uint32_t)(entryaddr +
                                           PREL31_SIGNEXT(word0));

              if (word1 & PREL31_EXT)
                {
                  merged[pos * 2 + 1] = word1;
                }
              else
                {
                  int32_t se1 = PREL31_SIGNEXT(word1);
                  merged[pos * 2 + 1] = (uint32_t)(entryaddr + 4 + se1);
                }
            }
        }

      /* Sort by absolute function address */

      for (idx = 1; idx < total_entries; idx++)
        {
          uint32_t k0 = merged[idx * 2];
          uint32_t k1 = merged[idx * 2 + 1];
          int j = idx - 1;

          while (j >= 0 && merged[j * 2] > k0)
            {
              merged[(j + 1) * 2]     = merged[j * 2];
              merged[(j + 1) * 2 + 1] = merged[j * 2 + 1];
              j--;
            }

          merged[(j + 1) * 2]     = k0;
          merged[(j + 1) * 2 + 1] = k1;
        }

      /* Convert back to prel31 */

      for (idx = 0; idx < total_entries; idx++)
        {
          uintptr_t addr = (uintptr_t)&merged[idx * 2];
          int32_t off0 = merged[idx * 2] - addr;

          /* prel31 can only encode +-1GB offset. If the merged table
           * is too far from the function addresses, abort the merge.
           */

          if (off0 > PREL31_MAX || off0 < PREL31_MIN)
            {
              berr("ERROR: exidx merge: prel31 overflow "
                   "off=%" PRId32 " addr=0x%" PRIxPTR " target=%08" PRIx32
                   "\n", off0, addr, merged[idx * 2]);
              lib_free(merged);
              return -E2BIG;
            }

          merged[idx * 2] = off0 & PREL31_MASK;

          if (!(merged[idx * 2 + 1] & PREL31_EXT))
            {
              int32_t off1 = merged[idx * 2 + 1] - addr - 4;
              if (off1 > PREL31_MAX || off1 < PREL31_MIN)
                {
                  berr("ERROR: exidx merge: prel31 overflow "
                       "off=%" PRId32 "\n", off1);
                  lib_free(merged);
                  return -E2BIG;
                }

              merged[idx * 2 + 1] = off1 & PREL31_MASK;
            }
        }

      loadinfo->unwind     = (uintptr_t)merged;
      loadinfo->unwindsize = total_entries * 8;
    }
  else
    {
      bwarn("WARNING: exidx merge skipped: %d entries exceeds %d limit",
            total_entries, ELF_MAX_UNWIND_ENTRIES);
    }

  return 0;
}

/****************************************************************************
 * Name: libelf_free_exidx
 *
 * Description:
 *   Free the merged exidx table if it was heap-allocated, and remove
 *   the module from the unwind registry.
 *
 * Input Parameters:
 *   modp - The module whose unwind resources should be released
 *
 ****************************************************************************/

void libelf_free_exidx(FAR uintptr_t *unwind, uintptr_t textalloc,
                       size_t textsize)
{
  /* If unwind points outside the text segment,
   * it was heap-allocated by libelf_merge_exidx.
   */

  if (*unwind != 0 &&
      (*unwind < textalloc || *unwind >= textalloc + textsize))
    {
      lib_free((FAR void *)*unwind);
    }

  *unwind = 0;
}
