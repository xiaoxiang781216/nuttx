/****************************************************************************
 * drivers/syslog/vsyslog_binary.c
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
 * This file provides the binary syslog backend (CONFIG_BINARY_SYSLOG).
 * When that option is selected via the Kconfig choice, the build system
 * compiles this file instead of vsyslog.c, so the nx_vsyslog/nx_syslog
 * symbols defined here are the only definitions visible to the linker.
 *
 * Each log record is encoded into a note_printf_s binary frame via
 * lib_bsprintf() and dispatched through the syslog channel infrastructure
 * (syslog_write).  The frame layout matches what note_driver_vprintf_ip()
 * produces, so a single decoder handles both paths.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <stdarg.h>

#include <syslog.h>

#include <nuttx/arch.h>
#include <nuttx/clock.h>
#include <nuttx/sched.h>
#include <nuttx/percpu.h>
#include <nuttx/syslog/syslog.h>
#include <nuttx/note/note_driver.h>

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: nx_vsyslog
 *
 * Description:
 *   nx_vsyslog() handles the system logging system calls.  It is
 *   functionally equivalent to vsyslog() but is used within the kernel
 *   space.
 *
 *   In the CONFIG_BINARY_SYSLOG backend, the log record is encoded into
 *   a compact note_printf_s binary frame instead of being formatted as
 *   human-readable text.
 *
 ****************************************************************************/

void nx_vsyslog(uint64_t meta, FAR const IPTR char *fmt, FAR va_list *ap)
{
  char data[NOTE_BUFFER_SIZE];
  FAR struct note_printf_s *note;
  FAR struct tcb_s *tcb;
  ssize_t len;

  if (!syslog_is_enabled(meta))
    {
      return;
    }

  note = (FAR struct note_printf_s *)data;
  len = sizeof(data) - SIZEOF_NOTE_PRINTF(0);

  len = lib_bsprintf(note->npt_data, len, NOTE_META_TYPE(meta), fmt, *ap);
  if (len > 0)
    {
      len = SIZEOF_NOTE_PRINTF(len);
      tcb = nxsched_self();
      note->npt_cmn.nc_length  = len;
      note->npt_cmn.nc_type    = NOTE_DUMP_PRINTF;
      note->npt_cmn.nc_cpu     = this_cpu();
      note->npt_cmn.nc_systime = perf_gettime();

      if (tcb == NULL)
        {
          note->npt_cmn.nc_priority = CONFIG_INIT_PRIORITY;
          note->npt_cmn.nc_pid      = 0;
        }
      else
        {
          note->npt_cmn.nc_priority = tcb->sched_priority;
          note->npt_cmn.nc_pid      = tcb->pid;
        }

      note->npt_ip   = 0;
      note->npt_fmt  = (uintptr_t)fmt;
      note->npt_meta = meta;

      syslog_write(data, len);
    }
}
