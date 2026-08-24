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
#include <stdint.h>
#include <syscall.h>
#include <sys/types.h>

#include <nuttx/fs/ioctl.h>
#include <nuttx/note/note_driver.h>
#include <nuttx/percpu.h>
#include <nuttx/ring.h>
#include <nuttx/sched.h>
#include <nuttx/sched_note.h>
#include <nuttx/streams.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define NOTERPMSG_EPT_NAME           "rpmsg-note"

/* nc_length is a single byte, so bound the staging buffer by UINT8_MAX and
 * let every note built in it fit the length field by construction.
 */

#define NOTE_BUFFER_SIZE UINT8_MAX
static_assert(NOTE_BUFFER_SIZE >= sizeof(struct note_event_s),
              "Buffer size too small");

/* Sometimes perf_getime is not a syscall */

#ifdef CONFIG_ARCH_HAVE_PERF_EVENTS_USER_ACCESS
#  define NOTE_PERF_GETTIME perf_gettime
#  define NOTE_PERF_CONVERT(e, t) perf_convert(e, t)
#else
clock_t UP_REALSYM(perf_gettime)(void);
void UP_REALSYM(perf_convert)(clock_t elapsed, FAR struct timespec *ts);
#  define NOTE_PERF_GETTIME UP_REALSYM(perf_gettime)
#  define NOTE_PERF_CONVERT(e, t) UP_REALSYM(perf_convert)(e, t)
#endif

#ifdef CONFIG_DRIVERS_NOTE_TEXT_DUMP

/* Renumber idle task PIDs
 *  In NuttX, PID number less than CONFIG_SMP_NCPUS are idle tasks.
 *  In Linux, there is only one idle task of PID 0.
 */

#define get_pid(pid) ((pid) < CONFIG_SMP_NCPUS ? 0 : (pid))

#define get_task_state(s)                                                    \
  ((s) == 0 ? 'X' : ((s) <= LAST_READY_TO_RUN_STATE ? 'R' : 'S'))

#if CONFIG_TASK_NAME_SIZE > 0
#  define TASK_NAME_SIZE (CONFIG_TASK_NAME_SIZE + 1)
#else
#  define TASK_NAME_SIZE 16
#endif

#endif /* CONFIG_DRIVERS_NOTE_TEXT_DUMP */

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* The structure to hold the per-CPU context data of trace dump */

struct note_dump_context_s
{
  int intr_nest;            /* Interrupt nest level */
  bool pendingswitch;       /* sched_switch pending flag */
  int current_state;        /* Task state of the current line */
  pid_t current_pid;        /* Task PID of the current line */
  pid_t next_pid;           /* Task PID of the next line */
  uint8_t current_priority; /* Task Priority of the current line */
  uint8_t next_priority;    /* Task Priority of the next line */
};

struct note_dump_stream_s
{
  struct lib_instream_s      common;  /* Embedded non-consuming stream */
  FAR struct ring_s         *ring;    /* Ring being read, NULL if none */
  uint32_t                   pos;     /* Absolute ring index of next note */
  bool                       binary;  /* Emit raw notes instead of text */
  struct note_max_s          note;    /* Scratch note read from the ring */
  struct note_dump_context_s ctx[CONFIG_SMP_NCPUS];
};

/****************************************************************************
 * Public Data
 ****************************************************************************/

#ifdef CONFIG_DRIVERS_NOTELOG
DECLARE_PER_CPU_BMP(struct note_driver_s, g_notelog_driver);
#define g_notelog_driver this_cpu_var_bmp(g_notelog_driver)
#endif

#ifdef CONFIG_DRIVERS_NOTERAM
DECLARE_PER_CPU_BMP(struct noteram_driver_s, g_noteram_driver);
#define g_noteram_driver this_cpu_var_bmp(g_noteram_driver)
#endif

#ifdef CONFIG_DRIVERS_NOTERPMSG
DECLARE_PER_CPU_BMP(struct noterpmsg_driver_s, g_noterpmsg_driver);
#define g_noterpmsg_driver this_cpu_var_bmp(g_noterpmsg_driver)
#endif

#ifdef CONFIG_DRIVERS_NOTELOWEROUT
DECLARE_PER_CPU_BMP(struct note_driver_s, g_notelowout);
#define g_notelowout this_cpu_var_bmp(g_notelowout)
#endif

#ifdef CONFIG_DRIVERS_NOTET32FDX
DECLARE_PER_CPU_BMP(struct notefdx_s, g_notefdx);
#define g_notefdx this_cpu_var_bmp(g_notefdx)
#endif

#ifdef CONFIG_DRIVERS_NOTESNAP
DECLARE_PER_CPU_BMP(struct notesnap_s, g_notesnap);
#define g_notesnap this_cpu_var_bmp(g_notesnap)
#endif

#ifdef CONFIG_DRIVERS_NOTENET
DECLARE_PER_CPU_BMP(struct notenet_driver_s, g_notenet);
#define g_notenet this_cpu_var_bmp(g_notenet)
#endif

#ifdef CONFIG_DRIVERS_NOTESTREAM
DECLARE_PER_CPU_BMP(struct notestream_s, g_notestream);
#define g_notestream this_cpu_var_bmp(g_notestream)
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
 * Name: note_dump_stream
 *
 * Description:
 *   Initialize a trace dump stream and wire its embedded non-consuming
 *   input stream to a byte ring buffer.  Notes are then read from the
 *   embedded stream->common with lib_stream_gets(); the live ring is read
 *   without consuming it, so the caller must freeze the producer while
 *   dumping so the ring contents stay stable.
 *
 * Input Parameters:
 *   stream - The dump stream to initialize
 *   ring   - The byte ring buffer (element size one) to read from
 *   binary - Emit raw binary notes when true, ftrace text when false
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void note_dump_stream(FAR struct note_dump_stream_s *stream,
                      FAR struct ring_s *ring, bool binary);

#ifdef CONFIG_DRIVERS_NOTE_TEXT_DUMP

/****************************************************************************
 * Name: note_dump_one
 *
 * Description:
 *   Parse a single binary note into human-readable ftrace text and write
 *   it to the output stream.
 *
 * Input Parameters:
 *   stream - The trace dump stream (tracks sched_switch state per CPU)
 *   p      - The binary note to parse
 *   s      - The output stream
 *
 * Returned Value:
 *   The number of bytes written to the stream.
 *
 ****************************************************************************/

int note_dump_one(FAR struct note_dump_stream_s *stream,
                  FAR const struct note_max_s *p,
                  FAR struct lib_outstream_s *s);

#endif /* CONFIG_DRIVERS_NOTE_TEXT_DUMP */

/****************************************************************************
 * Name: notesnap_notify_register
 ****************************************************************************/

#ifdef CONFIG_DRIVERS_NOTESNAP_CRASH_DUMP
void notesnap_notify_register(void);
#else
#define notesnap_notify_register()
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
