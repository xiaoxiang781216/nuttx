/****************************************************************************
 * sched/misc/coredump.c
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

#include <sys/stat.h>
#include <sys/param.h>

#include <string.h>
#include <syslog.h>
#include <debug.h>
#include <fcntl.h>

#include <nuttx/arch.h>
#include <nuttx/coredump.h>
#include <nuttx/elf.h>
#include <nuttx/mutex.h>
#include <nuttx/sched.h>
#include <nuttx/syslog/syslog.h>
#include <nuttx/percpu.h>
#include <nuttx/fs/procfs.h>

#include "sched/sched.h"
#include "coredump.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifdef PAGESIZE
#  define ELF_PAGESIZE    PAGESIZE
#else
#  define ELF_PAGESIZE    1024
#endif

#if defined(CONFIG_BOARD_COREDUMP_BLKDEV) || \
    defined(CONFIG_BOARD_COREDUMP_MTDDEV) || \
    defined(CONFIG_BOARD_COREDUMP_MEMDEV)
#  define CONFIG_BOARD_COREDUMP_DEV
#endif

#define PROGRAM_ALIGNMENT 64

/* Architecture can overwrite the default XCPTCONTEXT alignment */

#ifndef XCPTCONTEXT_ALIGN
#  define XCPTCONTEXT_ALIGN 16
#endif

#define PID0_REPLACE  INT32_MAX

/* Number of regions the coredump facility always emits by itself. */

#if CONFIG_ARCH_INTERRUPTSTACK > 0
#  define COREDUMP_BUILTIN_REGIONS  2
#else
#  define COREDUMP_BUILTIN_REGIONS  1
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

#if defined(CONFIG_FS_PROCFS_REGISTER) && defined(CONFIG_BOARD_COREDUMP_DEVPATH)
struct coredump_procfs_s
{
  struct coredump_info_s info;
  struct file            file;
};
#endif

/* This struct provides a description of the dump information of
 * memory regions.
 */

struct elf_dumpinfo_s
{
  FAR const struct memory_region_s *regions;
  struct memory_region_s builtin_regions[COREDUMP_BUILTIN_REGIONS];
  FAR struct lib_outstream_s *stream;
  FAR struct tcb_s           *tcb;
  int                         stksegs;
  int                         memsegs;
};

/* Per-CPU coredump context — all mutable per-cpu state in one struct */

struct coredump_context_s
{
#ifdef CONFIG_BOARD_COREDUMP_COMPRESSION
  struct lib_lzfoutstream_s lzfstream;
#endif

#ifdef CONFIG_BOARD_COREDUMP_SYSLOG
  struct lib_syslogstream_s syslogstream;
#  ifdef CONFIG_BOARD_COREDUMP_BASE64STREAM
  struct lib_base64outstream_s base64stream;
#  else
  struct lib_hexdumpstream_s hexstream;
#  endif
#endif

#ifdef CONFIG_BOARD_COREDUMP_BLKDEV
  struct lib_blkoutstream_s devstream;
#elif defined(CONFIG_BOARD_COREDUMP_MTDDEV)
  struct lib_mtdoutstream_s devstream;
#elif defined(CONFIG_BOARD_COREDUMP_MEMDEV)
  struct lib_fileoutstream_s devstream;
#elif defined(CONFIG_BOARD_COREDUMP_NETPOLL)
  struct lib_netpolloutstream_s netpollstream;
#endif

  FAR const struct memory_region_s *regions;
  mutex_t regions_lock;
  bool stream_initialized;
  bool enabled;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static DEFINE_PER_CPU_BSS_BMP(struct coredump_context_s, g_coredump);
#define g_coredump this_cpu_var_bmp(g_coredump)

#ifdef CONFIG_BOARD_MEMORY_RANGE
static const struct memory_region_s g_memory_region[] =
  {
    CONFIG_BOARD_MEMORY_RANGE
  };
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: elf_flush
 *
 * Description:
 *   Flush the out stream
 *
 ****************************************************************************/

static int elf_flush(FAR struct elf_dumpinfo_s *cinfo)
{
  return lib_stream_flush(cinfo->stream);
}

/****************************************************************************
 * Name: elf_emit
 *
 * Description:
 *   Send the dump data to binfmt_outstream_s
 *
 ****************************************************************************/

noinline_function
static int elf_emit(FAR struct elf_dumpinfo_s *cinfo,
                    FAR const void *buf, size_t len)
{
  FAR const uint8_t *ptr = buf;
  size_t total = len;
  int ret = 0;

  while (total > 0)
    {
      ret = lib_stream_puts(cinfo->stream, ptr, total);
      if (ret < 0)
        {
          break;
        }

      total -= ret;
      ptr   += ret;
    }

  return ret < 0 ? ret : len - total;
}

/****************************************************************************
 * Name: elf_emit_align
 *
 * Description:
 *   Align the filled data according to the current offset
 *
 ****************************************************************************/

noinline_function
static int elf_emit_align(FAR struct elf_dumpinfo_s *cinfo)
{
  off_t align = ALIGN_UP(cinfo->stream->nput,
                         ELF_PAGESIZE) - cinfo->stream->nput;
  unsigned char null[256];
  off_t total = align;
  off_t ret = 0;

  memset(null, 0, sizeof(null));

  while (total > 0)
    {
      ret = elf_emit(cinfo, null, total > sizeof(null) ?
                                 sizeof(null) : total);
      if (ret <= 0)
        {
          break;
        }

      total -= ret;
    }

  return ret < 0 ? ret : align;
}

/****************************************************************************
 * Name: elf_emit_hdr
 *
 * Description:
 *   Fill the elf header
 *
 ****************************************************************************/

static int elf_emit_hdr(FAR struct elf_dumpinfo_s *cinfo)
{
  Elf_Ehdr ehdr;

  memset(&ehdr, 0, sizeof(ehdr));
  memcpy(ehdr.e_ident, ELFMAG, EI_MAGIC_SIZE);

  ehdr.e_ident[EI_CLASS]   = ELF_CLASS;
  ehdr.e_ident[EI_DATA]    = ELF_DATA;
  ehdr.e_ident[EI_VERSION] = EV_CURRENT;
  ehdr.e_ident[EI_OSABI]   = ELF_OSABI;

  ehdr.e_type              = ET_CORE;
  ehdr.e_machine           = EM_ARCH;
  ehdr.e_version           = EV_CURRENT;
  ehdr.e_phoff             = sizeof(Elf_Ehdr);
  ehdr.e_flags             = EF_FLAG;
  ehdr.e_ehsize            = sizeof(Elf_Ehdr);
  ehdr.e_phentsize         = sizeof(Elf_Phdr);
  ehdr.e_phnum             = cinfo->stksegs + cinfo->memsegs + 1 + 1;

  return elf_emit(cinfo, &ehdr, sizeof(ehdr));
}

/****************************************************************************
 * Name: elf_get_ntcb
 *
 * Description:
 *   Calculate the note segment size
 *
 ****************************************************************************/

static int elf_get_ntcb(void)
{
  FAR struct tcb_s *tcb;
  int count = 0;
  int i;

  for (i = 0; i < g_npidhash; i++)
    {
      tcb = nxsched_get_tcb_by_index(i);
      if (tcb)
        {
          nxsched_put_tcb(tcb);
          count++;
        }
    }

  return count;
}

/****************************************************************************
 * Name: elf_get_note_size
 *
 * Description:
 *   Calculate the note segment size
 *
 ****************************************************************************/

static int elf_get_note_size(FAR struct elf_dumpinfo_s *cinfo)
{
  int total;

  total  = cinfo->stksegs * (sizeof(Elf_Nhdr) + COREDUMP_INFONAME_SIZE +
                             sizeof(elf_prstatus_t));
  total += cinfo->stksegs * (sizeof(Elf_Nhdr) + COREDUMP_INFONAME_SIZE +
                             sizeof(elf_prpsinfo_t));
  return total;
}

static int elf_get_info_note_size(void)
{
  return sizeof(Elf_Nhdr) + COREDUMP_INFONAME_SIZE +
         sizeof(struct coredump_info_s);
}

/****************************************************************************
 * Name: elf_emit_tcb_note
 *
 * Description:
 *   Fill the note segment information from tcb
 *
 ****************************************************************************/

noinline_function
static void elf_emit_tcb_note(FAR struct elf_dumpinfo_s *cinfo,
                              FAR struct tcb_s *tcb)
{
  char name[COREDUMP_INFONAME_SIZE];
  elf_prstatus_t status;
  elf_prpsinfo_t info;
  FAR uintptr_t *regs;
  Elf_Nhdr nhdr;
  int i;

  memset(&info,   0x0, sizeof(info));
  memset(&status, 0x0, sizeof(status));

  /* Fill Process info */

  nhdr.n_namesz = sizeof(name);
  nhdr.n_descsz = sizeof(info);
  nhdr.n_type   = NT_PRPSINFO;

  elf_emit(cinfo, &nhdr, sizeof(nhdr));

  strlcpy(name, tcb->name, sizeof(name));
  elf_emit(cinfo, name, sizeof(name));

  info.pr_pid   = tcb->pid;
  strlcpy(info.pr_fname, tcb->name, sizeof(info.pr_fname));
  elf_emit(cinfo, &info, sizeof(info));

  /* Fill Process status */

  nhdr.n_descsz = sizeof(status);
  nhdr.n_type   = NT_PRSTATUS;

  elf_emit(cinfo, &nhdr, sizeof(nhdr));
  elf_emit(cinfo, name, sizeof(name));

  /* PID 0 means special to GDB, in both GDB stub and core file, replace 0
   * with a special value.
   * For core file, if process ID is zero, GDB assign a not used id to it.
   * For GDB stub, 0 is used to indicate ANY thread, thus also not available.
   *
   * Unify the value to PID0_REPLACE to make python script post analysis
   * easier.
   */

  status.pr_pid = tcb->pid == 0 ? PID0_REPLACE : tcb->pid;

  if (running_task() == tcb)
    {
      if (up_interrupt_context())
        {
          regs = (FAR uintptr_t *)running_regs();
        }
      else
        {
          up_saveusercontext(this_cpu_var(g_last_regs));
          regs = (FAR uintptr_t *)this_cpu_var(g_last_regs);
        }
    }
  else
    {
      regs = (FAR uintptr_t *)tcb->xcp.regs;
    }

  if (regs != NULL)
    {
      ssize_t offset = 0;

      memset(status.pr_regs, 0, sizeof(status.pr_regs));
      for (i = 0; i < g_tcbinfo.regs_num; i++)
        {
          FAR const struct reginfo_s *reg = &g_tcbinfo.u.reginfo[i];

          if (reg->coffset != REGINFO_OFFSET_AUTO)
            {
              offset = reg->coffset;
            }

          if (reg->toffset != REGINFO_OFFSET_INVALID)
            {
              memcpy((FAR char *)status.pr_regs + offset,
                     (uint8_t *)regs + reg->toffset, reg->size);
            }

          offset += reg->size;
        }
    }

  elf_emit(cinfo, &status, sizeof(status));
}

/****************************************************************************
 * Name: elf_emit_note
 *
 * Description:
 *   Fill the note segment information
 *
 ****************************************************************************/

static void elf_emit_note(FAR struct elf_dumpinfo_s *cinfo)
{
  FAR struct tcb_s *tcb;
  int i;

  if (cinfo->tcb == NULL)
    {
     FAR struct tcb_s *rtcb = running_task();

      /* Emit the current (typically crashing) task first so that GDB's
       * default thread selection shows the crashing backtrace on the initial
       * `bt`.
       */

      if (rtcb != NULL)
        {
          elf_emit_tcb_note(cinfo, rtcb);
        }

      for (i = 0; i < g_npidhash; i++)
        {
          tcb = nxsched_get_tcb_by_index(i);
          if (tcb)
            {
              if (tcb != rtcb)
                {
                  elf_emit_tcb_note(cinfo, tcb);
                }

              nxsched_put_tcb(tcb);
            }
        }
    }
  else
    {
      elf_emit_tcb_note(cinfo, cinfo->tcb);
    }
}

/****************************************************************************
 * Name: elf_emit_tcb_stack
 *
 * Description:
 *   Fill the task stack information from tcb
 *
 ****************************************************************************/

noinline_function
static void elf_emit_tcb_stack(FAR struct elf_dumpinfo_s *cinfo,
                               FAR struct tcb_s *tcb)
{
  uintptr_t buf = 0;
  uintptr_t sp;
  size_t len;

  if (running_task() != tcb)
    {
      sp = up_getusrsp(tcb->xcp.regs);

      if (sp > (uintptr_t)tcb->stack_base_ptr &&
          sp < (uintptr_t)tcb->stack_base_ptr + tcb->adj_stack_size)
        {
          len = ((uintptr_t)tcb->stack_base_ptr +
                            tcb->adj_stack_size) - sp;
          buf = sp;
        }
#ifdef CONFIG_STACK_COLORATION
      else
        {
          len = up_check_tcbstack(tcb, tcb->adj_stack_size);
          buf = (uintptr_t)tcb->stack_base_ptr +
                           (tcb->adj_stack_size - len);
        }
#endif
    }

  if (buf == 0)
    {
      buf = (uintptr_t)tcb->stack_alloc_ptr;
      len = tcb->adj_stack_size +
            (tcb->stack_base_ptr - tcb->stack_alloc_ptr);
    }

  sp  = ALIGN_DOWN(buf, PROGRAM_ALIGNMENT);
  len = ALIGN_UP(len + (buf - sp), PROGRAM_ALIGNMENT);
  buf = sp;

  elf_emit(cinfo, (FAR void *)buf, len);

  /* Align to page */

  elf_emit_align(cinfo);
}

/****************************************************************************
 * Name: elf_emit_stack
 *
 * Description:
 *   Fill the task stack information
 *
 ****************************************************************************/

static void elf_emit_stack(FAR struct elf_dumpinfo_s *cinfo)
{
  FAR struct tcb_s *tcb;
  int i;

  if (cinfo->tcb == NULL)
    {
      FAR struct tcb_s *rtcb = running_task();

      if (rtcb != NULL)
        {
          elf_emit_tcb_stack(cinfo, rtcb);
        }

      for (i = 0; i < g_npidhash; i++)
        {
          tcb = nxsched_get_tcb_by_index(i);
          if (tcb)
            {
              if (tcb != rtcb)
                {
                  elf_emit_tcb_stack(cinfo, tcb);
                }

              nxsched_put_tcb(tcb);
            }
        }
    }
  else
    {
      elf_emit_tcb_stack(cinfo, cinfo->tcb);
    }
}

/****************************************************************************
 * Name: elf_emit_memory
 *
 * Description:
 *   Fill the note segment information
 *
 ****************************************************************************/

static void elf_emit_memory(FAR struct elf_dumpinfo_s *cinfo)
{
  FAR const struct memory_region_s *region;
  int i;

  for (i = 0; i < cinfo->memsegs; i++)
    {
      region = i < COREDUMP_BUILTIN_REGIONS ?
               &cinfo->builtin_regions[i] :
               &cinfo->regions[i - COREDUMP_BUILTIN_REGIONS];

      if (region->flags & PF_REGISTER)
        {
          FAR uintptr_t *start = (FAR uintptr_t *)region->start;
          FAR uintptr_t *end = (FAR uintptr_t *)region->end;
          uintptr_t buf[64];
          size_t offset = 0;

          while (start < end)
            {
              buf[offset++] = *start++;

              if (offset % (sizeof(buf) / sizeof(uintptr_t)) == 0)
                {
                  elf_emit(cinfo, buf, sizeof(buf));
                  offset = 0;
                }
            }

          if (offset != 0)
            {
              elf_emit(cinfo, buf, offset * sizeof(uintptr_t));
            }
        }
      else
        {
          elf_emit(cinfo, (FAR void *)region->start,
                   region->end - region->start);
        }

      /* Align to page */

      elf_emit_align(cinfo);
    }
}

/****************************************************************************
 * Name: elf_emit_info_note
 *
 * Description:
 *   Fill the core info note
 *
 ****************************************************************************/

static void elf_emit_info_note(FAR struct elf_dumpinfo_s *cinfo)
{
  struct coredump_info_s info;
  Elf_Nhdr nhdr;
  char name[COREDUMP_INFONAME_SIZE];

  memset(&info, 0x0, sizeof(info));
  memset(name, 0x0, sizeof(name));

  nhdr.n_namesz = sizeof(name);
  nhdr.n_descsz = sizeof(info);
  nhdr.n_type   = COREDUMP_MAGIC;

  elf_emit(cinfo, &nhdr, sizeof(nhdr));

  strlcpy(name, "NuttX", sizeof(name));
  elf_emit(cinfo, name, sizeof(name));

  info.size = cinfo->stream->nput + sizeof(info);
  clock_gettime(CLOCK_REALTIME, &info.time);
  uname(&info.name);

  elf_emit(cinfo, &info, sizeof(info));
}

/****************************************************************************
 * Name: elf_emit_tcb_phdr
 *
 * Description:
 *   Fill the program segment header from tcb
 *
 ****************************************************************************/

noinline_function
static void elf_emit_tcb_phdr(FAR struct elf_dumpinfo_s *cinfo,
                              FAR struct tcb_s *tcb,
                              FAR Elf_Phdr *phdr, off_t *offset)
{
  uintptr_t sp;

  phdr->p_vaddr = 0;

  if (running_task() != tcb)
    {
      sp = up_getusrsp(tcb->xcp.regs);

      if (sp > (uintptr_t)tcb->stack_base_ptr &&
          sp < (uintptr_t)tcb->stack_base_ptr + tcb->adj_stack_size)
        {
          phdr->p_filesz = ((uintptr_t)tcb->stack_base_ptr +
                           tcb->adj_stack_size) - sp;
          phdr->p_vaddr  = sp;
        }
#ifdef CONFIG_STACK_COLORATION
      else
        {
          phdr->p_filesz = up_check_tcbstack(tcb, tcb->adj_stack_size);
          phdr->p_vaddr  = (uintptr_t)tcb->stack_base_ptr +
                           (tcb->adj_stack_size - phdr->p_filesz);
        }
#endif
    }

  if (phdr->p_vaddr == 0)
    {
      phdr->p_vaddr  = (uintptr_t)tcb->stack_alloc_ptr;
      phdr->p_filesz = tcb->adj_stack_size +
                      (tcb->stack_base_ptr - tcb->stack_alloc_ptr);
    }

  sp = ALIGN_DOWN(phdr->p_vaddr, PROGRAM_ALIGNMENT);
  phdr->p_filesz = ALIGN_UP(phdr->p_filesz +
                            (phdr->p_vaddr - sp), PROGRAM_ALIGNMENT);
  phdr->p_vaddr  = sp;

  phdr->p_type   = PT_LOAD;
  phdr->p_offset = ALIGN_UP(*offset, ELF_PAGESIZE);
  phdr->p_paddr  = phdr->p_vaddr;
  phdr->p_memsz  = phdr->p_filesz;
  phdr->p_flags  = PF_X | PF_W | PF_R;
  *offset       += ALIGN_UP(phdr->p_memsz, ELF_PAGESIZE);

  elf_emit(cinfo, phdr, sizeof(*phdr));
}

/****************************************************************************
 * Name: elf_emit_phdr
 *
 * Description:
 *   Fill the program segment header
 *
 ****************************************************************************/

static void elf_emit_phdr(FAR struct elf_dumpinfo_s *cinfo)
{
  off_t offset = cinfo->stream->nput + sizeof(Elf_Phdr) *
                 (cinfo->stksegs + cinfo->memsegs + 1 + 1);
  FAR const struct memory_region_s *region;
  FAR struct tcb_s *tcb;
  Elf_Phdr phdr;
  int i;

  memset(&phdr, 0, sizeof(Elf_Phdr));

  phdr.p_type   = PT_NOTE;
  phdr.p_offset = offset;
  phdr.p_filesz = elf_get_note_size(cinfo);
  offset       += phdr.p_filesz;

  elf_emit(cinfo, &phdr, sizeof(phdr));

  phdr.p_align  = ELF_PAGESIZE;
  if (cinfo->tcb == NULL)
    {
      FAR struct tcb_s *rtcb = running_task();

      if (rtcb != NULL)
        {
          elf_emit_tcb_phdr(cinfo, rtcb, &phdr, &offset);
        }

      for (i = 0; i < g_npidhash; i++)
        {
          tcb = nxsched_get_tcb_by_index(i);
          if (tcb)
            {
              if (tcb != rtcb)
                {
                  elf_emit_tcb_phdr(cinfo, tcb, &phdr, &offset);
                }

              nxsched_put_tcb(tcb);
            }
        }
    }
  else
    {
      elf_emit_tcb_phdr(cinfo, cinfo->tcb, &phdr, &offset);
    }

  /* Write program headers for segments dump */

  for (i = 0; i < cinfo->memsegs; i++)
    {
      region = i < COREDUMP_BUILTIN_REGIONS ?
               &cinfo->builtin_regions[i] :
               &cinfo->regions[i - COREDUMP_BUILTIN_REGIONS];

      phdr.p_type   = PT_LOAD;
      phdr.p_offset = ALIGN_UP(offset, ELF_PAGESIZE);
      phdr.p_vaddr  = region->start;
      phdr.p_paddr  = phdr.p_vaddr;
      phdr.p_filesz = region->end - region->start;
      phdr.p_memsz  = phdr.p_filesz;
      phdr.p_flags  = region->flags;
      offset       += ALIGN_UP(phdr.p_memsz, ELF_PAGESIZE);
      elf_emit(cinfo, &phdr, sizeof(phdr));
    }

  memset(&phdr, 0, sizeof(Elf_Phdr));
  phdr.p_type   = PT_NOTE;
  phdr.p_offset = ALIGN_UP(offset, ELF_PAGESIZE);
  phdr.p_filesz = elf_get_info_note_size();
  offset       += phdr.p_filesz;

  elf_emit(cinfo, &phdr, sizeof(phdr));
}

/****************************************************************************
 * Name: coredump_print_memory_regions
 *
 * Description:
 *   Print out the memory range when coredump is generated.
 *
 ****************************************************************************/

#ifdef CONFIG_BOARD_COREDUMP_PRINT_REGIONS
static void
coredump_print_memory_regions(FAR const struct memory_region_s *regions)
{
  int i;

  if (regions == NULL)
    {
      _alert("No memory regions defined.\n");
      return;
    }

  _alert("Memory regions will be printed:\n");
  for (i = 0; regions[i].start < regions[i].end; i++)
    {
      _alert("Region[%d] start=0x%" PRIxPTR ", end=0x%" PRIxPTR,
             i, regions[i].start, regions[i].end);
      *(volatile int *)regions[i].start;
      *(volatile int *)(regions[i].end - sizeof(int));
    }
}
#endif

/****************************************************************************
 * Name: elf_setup_dumpinfo
 *
 * Description:
 *   Initialize dump info structure.
 *
 ****************************************************************************/

static int elf_setup_dumpinfo(FAR struct elf_dumpinfo_s *cinfo,
                              FAR const struct memory_region_s *regions,
                              FAR struct lib_outstream_s *stream,
                              FAR struct tcb_s *tcb)
{
  cinfo->regions = regions;
  cinfo->stream  = stream;
  cinfo->tcb     = tcb;
  cinfo->memsegs = 0;

  if (cinfo->tcb != NULL)
    {
      cinfo->stksegs = 1;
    }
  else
    {
      cinfo->stksegs = elf_get_ntcb();
    }

  if (cinfo->regions != NULL)
    {
      for (; cinfo->regions[cinfo->memsegs].start <
             cinfo->regions[cinfo->memsegs].end; cinfo->memsegs++);
    }

  cinfo->memsegs += COREDUMP_BUILTIN_REGIONS;

  cinfo->builtin_regions[0].start = (uintptr_t)this_cpu_var(g_last_regs);
  cinfo->builtin_regions[0].end   = cinfo->builtin_regions[0].start +
                                    sizeof(last_regs_t);
  cinfo->builtin_regions[0].flags = PF_R;

#if CONFIG_ARCH_INTERRUPTSTACK > 0
  cinfo->builtin_regions[1].start = up_get_intstackbase(this_cpu());
  cinfo->builtin_regions[1].end   = cinfo->builtin_regions[1].start +
                                    CONFIG_ARCH_INTERRUPTSTACK;
  cinfo->builtin_regions[1].flags = PF_R;
#endif

  return OK;
}

/****************************************************************************
 * Name: coredump_with_tcb
 *
 * Description:
 *   This function for generating core dump stream.
 *
 ****************************************************************************/

static int coredump_with_tcb(FAR const struct memory_region_s *regions,
                             FAR struct lib_outstream_s *stream,
                             FAR struct tcb_s *tcb)
{
  struct elf_dumpinfo_s cinfo;
  int ret;

  ret = elf_setup_dumpinfo(&cinfo, regions, stream, tcb);
  if (ret < 0)
    {
      return ret;
    }

  /* Fill notes section, with additional one for program header,
   * and one for the core file info defined by NuttX.
   */

  elf_emit_hdr(&cinfo);

  /* Fill all the program information about the process for the
   * notes.  This also sets up the file header.
   */

  elf_emit_phdr(&cinfo);

  /* Fill note information */

  elf_emit_note(&cinfo);

  /* Align to page */

  elf_emit_align(&cinfo);

  /* Dump stack */

  elf_emit_stack(&cinfo);

  /* Dump memory segments */

  elf_emit_memory(&cinfo);

  /* Emit core info note */

  elf_emit_info_note(&cinfo);

  /* Flush the dump */

  elf_flush(&cinfo);

  return OK;
}

#ifdef CONFIG_BOARD_COREDUMP_NETPOLL
/****************************************************************************
 * Name: coredump_dump_netpoll
 *
 * Description:
 *   Send coredump to netpoll.
 *
 ****************************************************************************/

static void coredump_dump_netpoll(FAR struct tcb_s *tcb)
{
  FAR struct lib_netpolloutstream_s *netstream = &g_coredump.netpollstream;
  FAR struct lib_outstream_s *stream;
  int ret;

  stream = &netstream->common;

#  ifdef CONFIG_BOARD_COREDUMP_COMPRESSION
  lib_lzfoutstream(&g_coredump.lzfstream, stream);
  stream = (FAR struct lib_outstream_s *)&g_coredump.lzfstream;
#  endif

  ret = coredump_with_tcb(g_coredump.regions, stream, tcb);
  if (ret < 0)
    {
      _alert("Coredump fail %d\n", ret);
      return;
    }

  _alert("Finish coredump, send %" PRIuOFF " bytes over netpoll\n",
         netstream->common.nput);
}
#endif

/****************************************************************************
 * Name: coredump_dump_syslog
 *
 * Description:
 *   Put coredump to block device.
 *
 ****************************************************************************/

#ifdef CONFIG_BOARD_COREDUMP_SYSLOG
static void coredump_dump_syslog(FAR struct tcb_s *tcb)
{
  FAR void *stream;
  FAR const char *streamname;
  int logmask;

  logmask = nx_setlogmask(LOG_UPTO(LOG_ALERT));

  _alert("Start coredump:\n");

  /* Initialize hex output stream */

  lib_syslogstream(&g_coredump.syslogstream, LOG_EMERG);
  stream = &g_coredump.syslogstream;
#ifdef CONFIG_BOARD_COREDUMP_BASE64STREAM
  lib_base64outstream(&g_coredump.base64stream, stream);
  stream = &g_coredump.base64stream;
  streamname = "base64";
#else
  lib_hexdumpstream(&g_coredump.hexstream, stream);
  stream = &g_coredump.hexstream;
  streamname = "hex";
#endif

#  ifdef CONFIG_BOARD_COREDUMP_COMPRESSION

  /* Initialize LZF compression stream */

  lib_lzfoutstream(&g_coredump.lzfstream, stream);
  stream = &g_coredump.lzfstream;
#  endif

  /* Do core dump */

  coredump_with_tcb(g_coredump.regions, stream, tcb);

#  ifdef CONFIG_BOARD_COREDUMP_COMPRESSION
  _alert("Coredump finish (Compression Enabled). %s formatted\n",
         streamname);
#  else
  _alert("Coredump finish. %s formatted\n", streamname);
#  endif

  nx_setlogmask(logmask);
}
#endif

/****************************************************************************
 * Name: coredump_dump_dev
 *
 * Description:
 *   Save coredump to storage device.
 *
 ****************************************************************************/

#ifdef CONFIG_BOARD_COREDUMP_DEV
static void coredump_dump_dev(FAR struct tcb_s *tcb)
{
  FAR void *stream = &g_coredump.devstream;
  int ret;

# if defined(CONFIG_BOARD_COREDUMP_BLKDEV) || defined(CONFIG_BOARD_COREDUMP_MTDDEV)
  if (g_coredump.devstream.inode == NULL)
    {
      _alert("Coredump device not found\n");
      return;
    }
# endif

# ifdef CONFIG_BOARD_COREDUMP_COMPRESSION
  lib_lzfoutstream(&g_coredump.lzfstream, stream);
  stream = &g_coredump.lzfstream;
# endif

  ret = coredump_with_tcb(g_coredump.regions, stream, tcb);
  if (ret < 0)
    {
      _alert("Coredump fail %d\n", ret);
      return;
    }

  _alert("Coredump finish, write %"PRIuOFF" bytes to %s\n",
         g_coredump.devstream.common.nput, CONFIG_BOARD_COREDUMP_DEVPATH);
}
#endif

/****************************************************************************
 * Name: coredump_initialize_memory_region
 *
 * Description:
 *   initialize the memory region with board memory range specified in config
 *
 ****************************************************************************/

noinline_function
static int coredump_initialize_memory_region(void)
{
  if (g_coredump.regions == NULL)
    {
      nxmutex_init(&g_coredump.regions_lock);
#ifdef CONFIG_BOARD_MEMORY_RANGE
      g_coredump.regions = g_memory_region;
#endif
    }

  return OK;
}

/****************************************************************************
 * Name: coredump_add_memory_region
 *
 * Description:
 *   Use coredump to dump the memory of the specified area.
 *
 ****************************************************************************/

int coredump_add_memory_region(FAR const void *ptr, size_t size,
                               uint32_t flags)
{
  FAR struct memory_region_s *region;
  size_t count = 1; /* 1 for end flag */
  int ret;

  ret = coredump_initialize_memory_region();
  if (ret < 0)
    {
      return ret;
    }

  nxmutex_lock(&g_coredump.regions_lock);

  if (g_coredump.regions != NULL)
    {
      region = (FAR struct memory_region_s *)g_coredump.regions;

      while (region->start < region->end)
        {
          if ((uintptr_t)ptr >= region->start &&
              (uintptr_t)ptr + size < region->end)
            {
              /* Already watched */

              goto out;
            }
          else if ((uintptr_t)ptr < region->start &&
                   (uintptr_t)ptr + size >= region->end)
            {
              /* start out of region, end out of region */

              region->start = (uintptr_t)ptr;
              region->end = (uintptr_t)ptr + size;
              goto out;
            }
          else if ((uintptr_t)ptr < region->start &&
                   (uintptr_t)ptr + size >= region->start)
            {
              /* start out of region, end in region */

              region->start = (uintptr_t)ptr;
              goto out;
            }
          else if ((uintptr_t)ptr < region->end &&
                   (uintptr_t)ptr + size >= region->end)
            {
              /* start in region, end out of region */

              region->end = (uintptr_t)ptr + size;
              goto out;
            }

          count++;
          region++;
        }

      /* Need a new region */
    }

  region = lib_malloc(sizeof(struct memory_region_s) * (count + 1));
  if (region == NULL)
    {
      ret = -ENOMEM;
      goto out;
    }

  if (g_coredump.regions != NULL)
    {
      memcpy(region, g_coredump.regions,
             sizeof(struct memory_region_s) * count);
    }

  if (g_coredump.regions != NULL
#ifdef CONFIG_BOARD_MEMORY_RANGE
    && g_coredump.regions != g_memory_region
#endif
    )
    {
      lib_free((FAR void *)g_coredump.regions);
    }

  region[count - 1].start = (uintptr_t)ptr;
  region[count - 1].end = (uintptr_t)ptr + size;
  region[count - 1].flags = flags;
  region[count].start = 0;
  region[count].end = 0;
  region[count].flags = 0;

  g_coredump.regions = region;

out:
  nxmutex_unlock(&g_coredump.regions_lock);
  return ret;
}

/****************************************************************************
 * Name: coredump_remove_memory_region
 *
 * Description:
 *   Remove a previously registered memory region from the coredump list.
 *
 ****************************************************************************/

int coredump_remove_memory_region(FAR const void *ptr, size_t size)
{
  FAR struct memory_region_s *region;
  size_t i;

  nxmutex_lock(&g_coredump.regions_lock);

  if (g_coredump.regions == NULL)
    {
      nxmutex_unlock(&g_coredump.regions_lock);
      return -ENOENT;
    }

  region = (FAR struct memory_region_s *)g_coredump.regions;

  for (i = 0; region[i].start < region[i].end; i++)
    {
      if (region[i].start == (uintptr_t)ptr &&
          region[i].end == (uintptr_t)ptr + size)
        {
          /* Copy each subsequent entry forward, including terminator */

          do
            {
              region[i] = region[i + 1];
            }
          while (region[i++].start != 0);

          nxmutex_unlock(&g_coredump.regions_lock);
          return 0;
        }
    }

  nxmutex_unlock(&g_coredump.regions_lock);
  return -ENOENT;
}

#if defined(CONFIG_FS_PROCFS_REGISTER) && defined(CONFIG_BOARD_COREDUMP_DEVPATH)
/****************************************************************************
 * Name: coredump_get_info
 *
 * Description:
 *   Scan a device for a valid coredump and extract its metadata.
 *   Supports both raw ELF and LZF-compressed formats.
 *
 ****************************************************************************/

static int coredump_get_info(FAR struct lib_sistream_s *stream,
                             FAR struct coredump_info_s *info,
                             FAR bool *compressed)
{
#ifdef CONFIG_LIBC_LZF
  struct lib_lzfsistream_s lzfstream;
#endif
  char name[COREDUMP_INFONAME_SIZE];
  char magic[2];
  Elf_Ehdr ehdr;
  Elf_Phdr phdr;
  Elf_Nhdr nhdr;
  int ret;

  /* Detect format by reading the first 2 bytes */

  ret = lib_stream_gets(stream, magic, sizeof(magic));
  if (ret != sizeof(magic))
    {
      return ret < 0 ? ret : -EIO;
    }

  lib_stream_seek(stream, 0, SEEK_SET);

  *compressed = false;
  if (magic[0] == 'Z' && magic[1] == 'V')
    {
      *compressed = true;
#ifdef CONFIG_LIBC_LZF
      lib_lzfsistream(&lzfstream, stream);
      stream = (FAR struct lib_sistream_s *)&lzfstream;
#else
      return -ENOTSUP;
#endif
    }

  ret = lib_stream_gets(stream, &ehdr, sizeof(ehdr));
  if (ret != sizeof(ehdr))
    {
      return ret < 0 ? ret : -EIO;
    }

  if (memcmp(ehdr.e_ident, ELFMAG, EI_MAGIC_SIZE) != 0)
    {
      return -EINVAL;
    }

  /* The last program header contains the info note */

  ret = lib_stream_seek(stream,
                        ehdr.e_phoff +
                        ehdr.e_phentsize * (ehdr.e_phnum - 1),
                        SEEK_SET);
  if (ret < 0)
    {
      return ret;
    }

  ret = lib_stream_gets(stream, &phdr, sizeof(phdr));
  if (ret != sizeof(phdr))
    {
      return ret < 0 ? ret : -EIO;
    }

  ret = lib_stream_seek(stream, phdr.p_offset, SEEK_SET);
  if (ret < 0)
    {
      return ret;
    }

  ret = lib_stream_gets(stream, &nhdr, sizeof(nhdr));
  if (ret != sizeof(nhdr))
    {
      return ret < 0 ? ret : -EIO;
    }

  if (nhdr.n_type != COREDUMP_MAGIC ||
      nhdr.n_namesz != COREDUMP_INFONAME_SIZE ||
      nhdr.n_descsz != sizeof(struct coredump_info_s))
    {
      return -EINVAL;
    }

  ret = lib_stream_gets(stream, name, nhdr.n_namesz);
  if (ret != nhdr.n_namesz)
    {
      return ret < 0 ? ret : -EIO;
    }

  if (strncmp(name, "NuttX", 6) != 0)
    {
      return -EINVAL;
    }

  ret = lib_stream_gets(stream, info, sizeof(*info));
  if (ret != sizeof(*info))
    {
      return ret < 0 ? ret : -EIO;
    }

  return OK;
}

/****************************************************************************
 * Name: coredump_procfs_read / coredump_procfs_write
 *
 * Description:
 *   procfs callbacks for coredump file read and clear operations.
 *
 ****************************************************************************/

static ssize_t coredump_procfs_read(FAR struct file *filep,
                                    FAR char *buffer, size_t buflen,
                                    FAR void *priv)
{
  FAR struct coredump_procfs_s *ctx = priv;
  off_t offset = filep->f_pos;
  ssize_t nread;

  if (buflen > ctx->info.size - offset)
    {
      buflen = ctx->info.size - offset;
    }

  nread = file_pread(&ctx->file, buffer, buflen, offset);
  if (nread > 0)
    {
      filep->f_pos += nread;
    }

  return nread;
}

static ssize_t coredump_procfs_write(FAR struct file *filep,
                                     FAR const char *buffer, size_t buflen,
                                     FAR void *priv)
{
  FAR struct coredump_procfs_s *ctx = priv;
  char zero[EI_MAGIC_SIZE] =
    {
      0
    };

  int ret;

  if (buflen < 5 || strncmp(buffer, "clear", 5) != 0)
    {
      return -EINVAL;
    }

  /* Clear file header magic */

  ret = file_pwrite(&ctx->file, zero, EI_MAGIC_SIZE, 0);
  if (ret < 0)
    {
      return ret;
    }

  file_ioctl(&ctx->file, BIOC_FLUSH, 0);
  return buflen;
}

static void coredump_procfs_register(FAR const char *devpath)
{
  struct lib_filesistream_s filestream;
  struct coredump_info_s info;
  FAR struct coredump_procfs_s *ctx;
  FAR const char *ext;
  FAR char *path;
  bool compressed = false;
  int ret;

  ret = lib_filesistream_open(&filestream, devpath, O_RDONLY | O_CLOEXEC, 0);
  if (ret < 0)
    {
      return;
    }

  ret = coredump_get_info(&filestream.common, &info, &compressed);
  lib_filesistream_close(&filestream);
  if (ret < 0)
    {
      return;
    }

  ctx = kmm_zalloc(sizeof(struct coredump_procfs_s));
  if (ctx == NULL)
    {
      return;
    }

  ctx->info = info;
  ret = file_open(&ctx->file, devpath, O_RDWR | O_CLOEXEC);
  if (ret < 0)
    {
      kmm_free(ctx);
      return;
    }

  ext = compressed ? "lzf" : "core";
  ret = asprintf(&path, "coredump/%.16s-%jx.%s",
                 info.name.version, (intmax_t)info.time.tv_sec, ext);
  if (ret < 0)
    {
      file_close(&ctx->file);
      kmm_free(ctx);
      return;
    }

  /* Replace spaces and colons with dashes in filename */

  for (ret = 0; path[ret] != '\0'; ret++)
    {
      if (path[ret] == ' ' || path[ret] == ':')
        {
          path[ret] = '-';
        }
    }

  procfs_create_custom(path, 0644,
                       coredump_procfs_read,
                       coredump_procfs_write, ctx);
}

#endif /* CONFIG_FS_PROCFS_REGISTER && CONFIG_BOARD_COREDUMP_DEVPATH */

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: coredump_initialize
 *
 * Description:
 *   Initialize the coredump facility.  Called once and only from
 *   nx_start_application.
 *
 ****************************************************************************/

int coredump_initialize(void)
{
  int ret = 0;

  g_coredump.enabled = true;

  ret = coredump_initialize_memory_region();
  if (ret < 0)
    {
      return ret;
    }

#ifdef CONFIG_BOARD_COREDUMP_BLKDEV
  ret = lib_blkoutstream_open(&g_coredump.devstream,
                              CONFIG_BOARD_COREDUMP_DEVPATH);
#elif defined(CONFIG_BOARD_COREDUMP_MTDDEV)
  ret = lib_mtdoutstream_open(&g_coredump.devstream,
                              CONFIG_BOARD_COREDUMP_DEVPATH);
#elif defined(CONFIG_BOARD_COREDUMP_MEMDEV)
  ret = lib_fileoutstream_open(&g_coredump.devstream,
                               CONFIG_BOARD_COREDUMP_DEVPATH,
                               O_WRONLY, 0666);
#elif defined(CONFIG_BOARD_COREDUMP_NETPOLL)
  ret = lib_netpolloutstream_init(&g_coredump.netpollstream,
                                  CONFIG_BOARD_COREDUMP_NETPOLL_INTF,
                                  CONFIG_BOARD_COREDUMP_NETPOLL_PORT,
                                  CONFIG_BOARD_COREDUMP_NETPOLL_IPADDR,
                                  CONFIG_BOARD_COREDUMP_NETPOLL_MAC);
  if (ret < 0)
    {
      _alert("%s Coredump netpoll init failed:%d\n",
             CONFIG_BOARD_COREDUMP_NETPOLL_INTF, ret);
      return ret;
    }
#endif

#ifdef CONFIG_BOARD_COREDUMP_DEV
  if (ret < 0)
    {
      _alert("%s Coredump device init failed:%d\n",
             CONFIG_BOARD_COREDUMP_DEVPATH, ret);
    }

#endif

  g_coredump.stream_initialized = true;

#ifdef CONFIG_FS_PROCFS_REGISTER
  procfs_create_bool("coredump/ctl", 0644, &g_coredump.enabled);
#endif

#if defined(CONFIG_FS_PROCFS_REGISTER) && defined(CONFIG_BOARD_COREDUMP_DEVPATH)
  coredump_procfs_register(CONFIG_BOARD_COREDUMP_DEVPATH);
#endif

  return ret;
}

/****************************************************************************
 * Name: coredump_dump
 *
 * Description:
 *   Do coredump of the task specified by tcb.
 *
 * Input Parameters:
 *   tcb - The TCB of the task to dump.  NULL means dump all tasks.
 *
 ****************************************************************************/

void coredump_dump(FAR struct tcb_s *tcb)
{
  if (!g_coredump.stream_initialized)
    {
      _alert("Coredump device is not initialized.\n");
      return;
    }

  if (!g_coredump.enabled)
    {
      _alert("Coredump is disabled.\n");
      return;
    }

#ifdef CONFIG_BOARD_COREDUMP_PRINT_REGIONS
  coredump_print_memory_regions(g_coredump.regions);
#endif
#ifdef CONFIG_BOARD_COREDUMP_SYSLOG
  coredump_dump_syslog(tcb);
#elif defined(CONFIG_BOARD_COREDUMP_DEV)
  coredump_dump_dev(tcb);
#elif defined(CONFIG_BOARD_COREDUMP_NETPOLL)
  coredump_dump_netpoll(tcb);
#endif
}

/****************************************************************************
 * Name: coredump
 *
 * Description:
 *   This function for generating core dump stream.
 *
 * Input Parameters:
 *   regions - The memory regions to dump.
 *   stream  - The output stream.
 *   pid     - The task ID to dump.  INVALID_PROCESS_ID means dump all
 *             tasks.
 *
 ****************************************************************************/

int coredump(FAR const struct memory_region_s *regions,
             FAR struct lib_outstream_s *stream,
             pid_t pid)
{
  FAR struct tcb_s *tcb = NULL;
  int ret;

  /* Resolve the pid to a TCB once, holding a reference for the whole dump.
   * INVALID_PROCESS_ID keeps the "dump all tasks" semantics (tcb == NULL).
   */

  if (pid != INVALID_PROCESS_ID)
    {
      tcb = nxsched_get_tcb(pid);
      if (tcb == NULL)
        {
          return -EINVAL;
        }
    }

  ret = coredump_with_tcb(regions, stream, tcb);

  nxsched_put_tcb(tcb);

  return ret;
}
