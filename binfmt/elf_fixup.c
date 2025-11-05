/****************************************************************************
 * binfmt/elf_fixup.c
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
#include <string.h>
#include <debug.h>
#include <errno.h>

#include <nuttx/mm/mm.h>
#include <nuttx/addrenv.h>
#include <nuttx/binfmt/binfmt.h>
#include <nuttx/binfmt/elf_fixup.h>
#include <nuttx/nuttx.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define FIXUP_TABLE \
  ((FAR const struct elf_fixup_s *) \
  (CONFIG_ELF_FIXUP_FLASH_START + CONFIG_ELF_FIXUP_FLASH_SIZE) - 1)

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int elf_fixup_loadbinary(FAR struct binary_s *binp,
                                FAR const char *filename,
                                FAR const struct symtab_s *exports,
                                int nexports);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct binfmt_s g_elf_fixup_binfmt =
{
  NULL,                 /* next */
  elf_fixup_loadbinary, /* load */
  NULL,                 /* unload */
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: elf_fixup_loadbinary
 *
 * Description:
 *   Verify that the file is an elf fixup binary.
 *
 ****************************************************************************/

static int elf_fixup_loadbinary(FAR struct binary_s *binp,
                                FAR const char *filename,
                                FAR const struct symtab_s *exports,
                                int nexports)
{
  /* The fixup table is expected to be at the end of a reserved flash
   * region.
   */

  FAR const struct elf_fixup_s *fixup = FIXUP_TABLE;
  FAR const char *name;
  int i;

  binfo("Loading file: %s\n", filename);

  name = strrchr(filename, '/');
  if (name == NULL)
    {
      name = filename;
    }
  else
    {
      name++;
    }

  for (; fixup->entry != 0; fixup--)
    {
      if (strcmp(fixup->name, name) != 0)
        {
          continue;
        }

      binp->entrypt   = (main_t)fixup->entry;
      binp->stacksize = fixup->stacksize ?
                        fixup->stacksize :
                        CONFIG_DEFAULT_TASK_STACKSIZE;
      binp->priority  = fixup->priority ?
                        fixup->priority :
                        SCHED_PRIORITY_DEFAULT;
#ifdef CONFIG_SCHED_USER_IDENTITY
      binp->uid       = fixup->uid;
      binp->gid       = fixup->gid;
      binp->mode      = fixup->mode;
#endif
#ifdef CONFIG_MM_TASK_HEAP
      binp->heapsize  = fixup->heapsize ?
                        fixup->heapsize :
                        CONFIG_MM_TASK_HEAP_DEFAULT_SIZE;
#endif

#ifdef CONFIG_ARCH_ADDRENV
      binp->addrenv = addrenv_allocate();
      for (i = 0; i < CONFIG_ELF_FIXUP_NSEGMENTS; i++)
        {
          if (fixup->phdr[i].p_type != PT_LOAD)
            {
              continue;
            }

          if (fixup->phdr[i].p_flags & PF_W)
            {
              if (binp->addrenv->addrenv.data == 0)
                {
                  binp->addrenv->addrenv.data = fixup->phdr[i].p_vaddr;
                }

              binp->addrenv->addrenv.datasize += fixup->phdr[i].p_memsz;
            }
          else
            {
              if (binp->addrenv->addrenv.text == 0)
                {
                  binp->addrenv->addrenv.text = fixup->phdr[i].p_vaddr;
                }

             binp->addrenv->addrenv.textsize += fixup->phdr[i].p_memsz;
            }
        }

      /* Will access memory for elffixup, so need select addrenv.
       * Data include data/bss/heap, we protect data + datasize
       * will cover heap as well.
       */

      addrenv_select(binp->addrenv, &binp->oldenv);

#  ifdef CONFIG_MM_TASK_HEAP
      binp->addrenv->addrenv.heap =
        (uintptr_t)mm_initialize(fixup->name,
                                 (FAR void *)fixup->heapstart,
                                 binp->heapsize);
      binp->addrenv->addrenv.heapsize = binp->heapsize;
#  endif

#endif
      for (i = 0; i < CONFIG_ELF_FIXUP_NSEGMENTS; i++)
        {
          if (fixup->phdr[i].p_type != PT_LOAD)
            {
              continue;
            }

          if (fixup->phdr[i].p_vaddr != fixup->phdr[i].p_paddr)
            {
              /* Copy segment from load to virtual address */

              memcpy((FAR void *)fixup->phdr[i].p_vaddr,
                     (FAR const void *)fixup->phdr[i].p_paddr,
                     fixup->phdr[i].p_filesz);

              /* And zero out the uninitialized data segment */

              memset((FAR void *)(fixup->phdr[i].p_vaddr +
                      fixup->phdr[i].p_filesz), 0,
                      fixup->phdr[i].p_memsz -
                      fixup->phdr[i].p_filesz
#ifdef CONFIG_MM_TASK_HEAP
                      - ((fixup->phdr[i].p_vaddr <= fixup->heapstart &&
                        fixup->phdr[i].p_vaddr + fixup->phdr[i].p_memsz
                        > fixup->heapstart) ?
                      binp->heapsize : 0)
#endif
                      );
            }
        }

#ifdef CONFIG_ARCH_ADDRENV
      addrenv_restore(binp->oldenv);
#endif
      return OK;
    }

  return -ENOENT;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: elf_fixup_initialize
 *
 * Description:
 *   In order to use the elf fixup format, this function must be called
 *   during system initialize to register the elf fixup binary format.
 *
 * Returned Value:
 *   This is a NuttX internal function so it follows the convention that
 *   0 (OK) is returned on success and a negated errno is returned on
 *   failure.
 *
 ****************************************************************************/

int elf_fixup_initialize(void)
{
  int ret;

  /* Register ourselves as a binfmt loader */

  binfo("Registering Elf fixup Loader\n");

  ret = register_binfmt(&g_elf_fixup_binfmt);
  if (ret != 0)
    {
      berr("Failed to register binfmt: %d\n", ret);
    }

  return ret;
}

/****************************************************************************
 * Name: elf_fixup_uninitialize
 *
 * Description:
 *   Unregister the elf fixup binary loader
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void elf_fixup_uninitialize(void)
{
  unregister_binfmt(&g_elf_fixup_binfmt);
}

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

uintptr_t elf_fixup_ramstart(void)
{
  FAR const struct elf_fixup_s *fixup = FIXUP_TABLE;
  uintptr_t ramstart = UINTPTR_MAX;
  int i;

  for (; fixup->entry != 0; fixup--)
    {
      for (i = 0; i < CONFIG_ELF_FIXUP_NSEGMENTS; i++)
        {
          if (fixup->phdr[i].p_type == PT_LOAD &&
              fixup->phdr[i].p_flags & PF_W &&
              fixup->phdr[i].p_vaddr != 0 &&
              ramstart > fixup->phdr[i].p_vaddr)
            {
              ramstart = fixup->phdr[i].p_vaddr;
            }
        }
    }

  return ramstart;
}
