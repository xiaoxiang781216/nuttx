/****************************************************************************
 * libs/libc/misc/lib_note.c
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

#include <stdarg.h>
#include <syslog.h>

#include <nuttx/ratelimit.h>
#include <nuttx/sched_note.h>

#ifdef CONFIG_SCHED_INSTRUMENTATION_DUMP
/****************************************************************************
 * Private Data
 ****************************************************************************/

static DEFINE_RATELIMIT_STATE(g_ratelimit,
  CONFIG_DRIVERS_NOTE_RATELIMIT_INTERVAL,
  CONFIG_DRIVERS_NOTE_RATELIMIT_BURST);

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void sched_note_printf_ip(uint8_t tag, uint8_t level, uintptr_t ip,
                          FAR const char *fmt,
                          uint64_t type, ...)
{
  va_list va;
  va_start(va, type);
  sched_note_vprintf_ip(tag, level, ip, fmt, type, &va);
  va_end(va);
}

void sched_note_event_ip_ratelimit(uint8_t tag, uint8_t level,
                                   uintptr_t ip, uint8_t event,
                                   FAR const void *buf,
                                   size_t len)
{
  if (!ratelimit_islimited(&g_ratelimit))
    {
      sched_note_event_ip(tag, level, ip, event, buf, len);
    }
}

void sched_note_vprintf_ip_ratelimit(uint8_t tag, uint8_t level,
                                     uintptr_t ip, FAR const char *fmt,
                                     uint64_t type, va_list *va)
{
  if (!ratelimit_islimited(&g_ratelimit))
    {
      sched_note_vprintf_ip(tag, level, ip, fmt, type, va);
    }
}

void sched_note_printf_ip_ratelimit(uint8_t tag, uint8_t level,
                                    uintptr_t ip, FAR const char *fmt,
                                    uint64_t type, ...)
{
  if (!ratelimit_islimited(&g_ratelimit))
    {
      va_list va;
      va_start(va, type);
      sched_note_vprintf_ip(tag, level, ip, fmt, type, &va);
      va_end(va);
    }
}

#endif /* CONFIG_SCHED_INSTRUMENTATION_DUMP */

