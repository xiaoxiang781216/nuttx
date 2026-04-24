/****************************************************************************
 * drivers/note/note_driver.h
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

#ifndef __NUTTX_DRIVERS_NOTE_NOTE_DRIVER_H
#define __NUTTX_DRIVERS_NOTE_NOTE_DRIVER_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <syscall.h>
#include <sys/types.h>

#include <nuttx/fs/ioctl.h>
#include <nuttx/note/note_driver.h>
#include <nuttx/percpu.h>
#include <nuttx/sched.h>
#include <nuttx/sched_note.h>
#include <nuttx/streams.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define NOTERPMSG_EPT_NAME           "rpmsg-note"

#define NOTE_BUFFER_SIZE 256
static_assert(NOTE_BUFFER_SIZE >= sizeof(struct note_event_s),
              "Buffer size too small");

/* Sometimes perf_getime is not a syscall */

#ifdef CONFIG_ARCH_PERF_EVENTS_USER_ACCESS
#  define NOTE_PERF_GETTIME perf_gettime
#  define NOTE_PERF_CONVERT(e, t) perf_convert(e, t)
#else
clock_t UP_REALSYM(perf_gettime)(void);
void UP_REALSYM(perf_convert)(clock_t elapsed, FAR struct timespec *ts);
#  define NOTE_PERF_GETTIME UP_REALSYM(perf_gettime)
#  define NOTE_PERF_CONVERT(e, t) UP_REALSYM(perf_convert)(e, t)
#endif

/****************************************************************************
 * Public Types
 ****************************************************************************/

/****************************************************************************
 * Public Data
 ****************************************************************************/

#ifdef CONFIG_DRIVERS_NOTELOG
DECLARE_PER_CPU_BMP(struct note_driver_s, g_notelog_driver);
#endif

#ifdef CONFIG_DRIVERS_NOTERAM
DECLARE_PER_CPU_BMP(struct noteram_driver_s, g_noteram_driver);
#endif

#ifdef CONFIG_DRIVERS_NOTERPMSG
DECLARE_PER_CPU_BMP(struct noterpmsg_driver_s, g_noterpmsg_driver);
#endif

#ifdef CONFIG_DRIVERS_NOTELOWEROUT
DECLARE_PER_CPU_BMP(struct note_driver_s, g_notelowout);
#endif

#ifdef CONFIG_DRIVERS_NOTET32FDX
DECLARE_PER_CPU_BMP(struct notefdx_s, g_notefdx);
#endif

#ifdef CONFIG_DRIVERS_NOTESNAP
DECLARE_PER_CPU_BMP(struct notesnap_s, g_notesnap);
#endif

#ifdef CONFIG_DRIVERS_NOTENET
extern struct notenet_driver_s g_notenet;
#endif

#ifdef CONFIG_DRIVERS_NOTESTREAM
DECLARE_PER_CPU_BMP(struct notestream_s, g_notestream);
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

static inline bool note_is_safe(FAR const void *note, size_t len,
                                bool noswitches)
{
  FAR const struct note_common_s *cmn =
    (FAR const struct note_common_s *)note;

  if (noswitches)
    {
      return false;
    }

  switch (cmn->nc_type)
    {
      case NOTE_SUSPEND:
      case NOTE_RESUME:
      case NOTE_PREEMPT_LOCK:
      case NOTE_PREEMPT_UNLOCK:
      case NOTE_CSECTION_ENTER:
      case NOTE_CSECTION_LEAVE:
      case NOTE_SPINLOCK_LOCK:
      case NOTE_SPINLOCK_LOCKED:
      case NOTE_SPINLOCK_UNLOCK:
      case NOTE_SPINLOCK_ABORT:
      case NOTE_IRQ_ENTER:
      case NOTE_IRQ_LEAVE:
      case NOTE_WDOG_ENTER:
      case NOTE_WDOG_LEAVE:
        return false;
      default:
        return true;
    }
}

/****************************************************************************
 * Name: note_common
 *
 * Description:
 *   Fill in some of the common fields in the note structure.
 *
 * Input Parameters:
 *   tcb    - The TCB containing the information
 *   note   - The common note structure to use
 *   length - The total lengthof the note structure
 *   type   - The type of the note
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void note_common(FAR struct tcb_s *tcb, FAR struct note_common_s *note,
                 uint8_t length, uint8_t type);

/****************************************************************************
 * Name: noteram_register
 *
 * Description:
 *   Register RAM note driver at /dev/note/ram that can be used by an
 *   application to read note data from the circular note buffer.
 *
 * Input Parameters:
 *   None.
 *
 * Returned Value:
 *   Zero on succress. A negated errno value is returned on a failure.
 *
 ****************************************************************************/

#ifdef CONFIG_DRIVERS_NOTERAM
int noteram_register(void);
#endif

/****************************************************************************
 * Name: notesnap_notify_register
 ****************************************************************************/

#ifdef CONFIG_DRIVERS_NOTESNAP_CRASH_DUMP
void notesnap_notify_register(void);
#else
#define notesnap_notify_register()
#endif

/****************************************************************************
 * Name: notesnap_dump_with_stream
 ****************************************************************************/

#ifdef CONFIG_DRIVERS_NOTESNAP
void notesnap_dump_with_stream(FAR struct lib_outstream_s *stream);
#endif

/****************************************************************************
 * Name: noterpmsg_server_init
 ****************************************************************************/

#ifdef CONFIG_DRIVERS_NOTERPMSG_SERVER
int noterpmsg_server_init(void);
#endif

/****************************************************************************
 * Name: noterpmsg_init
 ****************************************************************************/

#ifdef CONFIG_DRIVERS_NOTERPMSG
int noterpmsg_init(void);
#endif

/****************************************************************************
 * Name: note_driver_initialize
 ****************************************************************************/

int note_driver_initialize(void);

#endif /* __NUTTX_DRIVERS_NOTE_NOTE_DRIVER_H */
