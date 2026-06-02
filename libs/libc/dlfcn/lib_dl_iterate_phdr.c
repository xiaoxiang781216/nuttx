/****************************************************************************
 * libs/libc/dlfcn/lib_dl_iterate_phdr.c
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
#include <link.h>
#include <nuttx/lib/elf.h>

/****************************************************************************
 * External References
 ****************************************************************************/

extern char _stext[];
extern char _etext[];

#ifndef CONFIG_ARCH_ARM
extern char __eh_frame_hdr_start[] __attribute__((weak));
extern char __eh_frame_hdr_end[] __attribute__((weak));
extern char __eh_frame_start[] __attribute__((weak));
extern char __eh_frame_end[] __attribute__((weak));
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

#ifdef CONFIG_LIBC_ELF_UNWIND_SECTNAME
struct phdr_iterate_s
{
  CODE int (*callback)(FAR struct dl_phdr_info *info,
                       size_t size, FAR void *data);
  FAR void *data;
  int       ret;
};
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: dl_report_object
 *
 * Description:
 *   Report a single loaded object to the callback by constructing a
 *   dl_phdr_info with appropriate program headers.
 *
 * Input Parameters:
 *   cb           - User callback function
 *   data         - Opaque data passed to callback
 *   text_addr    - Start address of the text segment
 *   text_size    - Size of the text segment
 *   unwind_addr  - Address of unwind data, or 0 if not available
 *   unwind_size  - Size of unwind data
 *   unwind_type  - Program header type for unwind data:
 *                  PT_ARM_EXIDX: ARM32 exception index table
 *                  PT_GNU_EH_FRAME: DWARF .eh_frame_hdr index
 *                  0: raw .eh_frame (reported via dlpi_eh_frame extension)
 *
 * Returned Value:
 *   Return value from callback (0 to continue, non-zero to stop)
 *
 ****************************************************************************/

static int dl_report_object(CODE int (*cb)(FAR struct dl_phdr_info *info,
                                           size_t size, FAR void *data),
                            FAR void *data, uintptr_t text_addr,
                            size_t text_size, uintptr_t unwind_addr,
                            size_t unwind_size, uint32_t unwind_type)
{
  struct dl_phdr_info info;
  Elf_Phdr phdrs[2];
  int phnum = 1;

  /* PT_LOAD: text segment */

  phdrs[0].p_type   = PT_LOAD;
  phdrs[0].p_flags  = PF_X | PF_R;
  phdrs[0].p_vaddr  = text_addr;
  phdrs[0].p_memsz  = text_size;
  phdrs[0].p_offset = 0;

  /* Unwind segment: report via phdr (.ARM.exidx or .eh_frame_hdr) */

  if (unwind_addr != 0 && unwind_type != 0)
    {
      phdrs[1].p_type        = unwind_type;
      phdrs[1].p_flags       = PF_R;
      phdrs[1].p_vaddr       = unwind_addr;
      phdrs[1].p_memsz       = unwind_size;
      phdrs[1].p_offset      = 0;
      phnum                  = 2;
      info.dlpi_eh_frame     = 0;
      info.dlpi_eh_frame_len = 0;
    }
  else
    {
      /* Provide raw .eh_frame when .eh_frame_hdr is not supported,
       * e.g. riscv-gcc
       */

      info.dlpi_eh_frame     = unwind_addr;
      info.dlpi_eh_frame_len = unwind_size;
    }

  info.dlpi_addr  = 0;
  info.dlpi_name  = "";
  info.dlpi_phdr  = phdrs;
  info.dlpi_phnum = phnum;

  return cb(&info, sizeof(info), data);
}

/****************************************************************************
 * Name: dl_iterate_module_cb
 *
 * Description:
 *   libelf_registry_foreach callback. Reports a dynamically loaded ELF
 *   module to the user callback via dl_report_object.
 *
 * Input Parameters:
 *   modp - The registered module to report
 *   arg  - Pointer to struct phdr_iterate_s containing the user callback
 *
 * Returned Value:
 *   0 to continue iteration, non-zero to stop (propagates callback result)
 *
 ****************************************************************************/

#ifdef CONFIG_LIBC_ELF_UNWIND_SECTNAME
static int dl_iterate_module_cb(FAR struct module_s *modp, FAR void *arg)
{
  FAR struct phdr_iterate_s *ctx = (FAR struct phdr_iterate_s *)arg;

  if (modp->textalloc == NULL || modp->unwind == 0 || modp->unwindsize == 0)
    {
      return 0;
    }

#  ifdef CONFIG_ARCH_ARM
  ctx->ret = dl_report_object(ctx->callback, ctx->data,
                              (uintptr_t)modp->textalloc, modp->textsize,
                              modp->unwind, modp->unwindsize, PT_ARM_EXIDX);
#  else
  ctx->ret = dl_report_object(ctx->callback, ctx->data,
                              (uintptr_t)modp->textalloc, modp->textsize,
                              modp->unwind, modp->unwindsize, 0);
#  endif

  return ctx->ret;
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: dl_iterate_phdr
 *
 * Description:
 *   Iterate over loaded objects and invoke the callback for each one,
 *   providing program header information that describes the object's
 *   memory layout and unwind data.
 *
 *   This is the primary interface used by libunwind to discover unwind
 *   information at runtime. The implementation reports the current image
 *   (main firmware or ELF process itself):
 *     - ARM32: PT_LOAD + PT_ARM_EXIDX
 *     - DWARF with .eh_frame_hdr: PT_LOAD + PT_GNU_EH_FRAME
 *     - DWARF without .eh_frame_hdr: PT_LOAD + dlpi_eh_frame extension
 *
 * Input Parameters:
 *   callback - Function to call for each loaded object. Iteration stops
 *              when callback returns non-zero.
 *   data     - Opaque pointer passed through to the callback.
 *
 * Returned Value:
 *   The non-zero return value from callback if iteration was stopped
 *   early, or 0 if all objects were visited.
 *
 ****************************************************************************/

int dl_iterate_phdr(CODE int (*callback)(FAR struct dl_phdr_info *info,
                                         size_t size, FAR void *data),
                    FAR void *data)
{
#ifdef CONFIG_LIBC_ELF_UNWIND_SECTNAME
  struct phdr_iterate_s ctx;
#endif
  uintptr_t unwind_addr = 0;
  size_t unwind_size = 0;
  uint32_t unwind_type = 0;
  int ret;

  /* Search nuttx image */

#ifdef CONFIG_ARCH_ARM
  if ((uintptr_t)__exidx_start != (uintptr_t)__exidx_end)
    {
      unwind_addr = (uintptr_t)__exidx_start;
      unwind_size = (uintptr_t)__exidx_end - (uintptr_t)__exidx_start;
      unwind_type = PT_ARM_EXIDX;
    }
#else
  if ((uintptr_t)__eh_frame_hdr_start != (uintptr_t)__eh_frame_hdr_end)
    {
      unwind_addr = (uintptr_t)__eh_frame_hdr_start;
      unwind_size = (uintptr_t)__eh_frame_hdr_end -
                    (uintptr_t)__eh_frame_hdr_start;
      unwind_type = PT_GNU_EH_FRAME;
    }
  else if ((uintptr_t)__eh_frame_start != (uintptr_t)__eh_frame_end)
    {
      unwind_addr = (uintptr_t)__eh_frame_start;
      unwind_size = (size_t)(__eh_frame_end - __eh_frame_start);
      unwind_type = 0;
    }
#endif

  ret = dl_report_object(callback, data, (uintptr_t)_stext,
                         (uintptr_t)_etext - (uintptr_t)_stext,
                         unwind_addr, unwind_size, unwind_type);
  if (ret != 0)
    {
      return ret;
    }

  /* Search dynamically loaded ELFs */

#ifdef CONFIG_LIBC_ELF_UNWIND_SECTNAME
  ctx.callback = callback;
  ctx.data     = data;
  ctx.ret      = 0;
  libelf_registry_foreach(dl_iterate_module_cb, &ctx);
  if (ctx.ret != 0)
    {
      return ctx.ret;
    }
#endif

  return 0;
}
