/****************************************************************************
 * include/nuttx/sched_note.h
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

#ifndef __INCLUDE_NUTTX_SCHED_NOTE_H
#define __INCLUDE_NUTTX_SCHED_NOTE_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>

#include <nuttx/arch.h>
#include <nuttx/macro.h>
#include <nuttx/ratelimit.h>
#include <nuttx/sched.h>
#include <nuttx/spinlock_type.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define NOTE_ALIGN(a) (((a) + sizeof(uintptr_t) - 1) & \
                       ~(sizeof(uintptr_t) - 1))

/* Printf argument type */

#define NOTE_PRINTF_UINT32 0
#define NOTE_PRINTF_UINT64 1
#define NOTE_PRINTF_DOUBLE 2
#define NOTE_PRINTF_STRING 3

/* Get/set printf tag. each parameter occupies 2 bits. The highest
 * four bits are used to represent the number of parameters, So up to
 * 29 variable arguments can be passed.
 */

#define NOTE_PRINTF_GET_TYPE(tag, index) (((tag) >> (index) * 2) & 0x03)
#define NOTE_PRINTF_GET_COUNT(tag)       ((tag) >> 58)

#define sched_note_event(tag, event, buf, len) \
        sched_note_event_ip(tag, up_getpc(), event, buf, len)
#define sched_note_vprintf(tag, fmt, va) \
        sched_note_vprintf_ip(tag, up_getpc(), fmt, 0, &(va))

#ifdef CONFIG_DRIVERS_NOTE_STRIP_FORMAT
#  define sched_note_printf(tag, fmt, ...)                                  \
          do                                                                \
            {                                                               \
              union fmt_type_u                                              \
              {                                                             \
                FAR const char *__fmt__;                                    \
                uint64_t __type__;                                          \
              };                                                            \
              locate_data("note_format") used_data static const             \
              char __fmt__[] = fmt;                                         \
              locate_data("note_type") used_data static const               \
              union fmt_type_u __fmt_type__ = (union fmt_type_u)__fmt__;    \
              static_assert(GET_ARG_COUNT(__VA_ARGS__) <= 29,               \
                            "The number of sched_note_nprintf "             \
                            "parameters needs to be less than 29");         \
              sched_note_printf_ip(tag, up_getpc(), __fmt__,                \
                                  __fmt_type__.__type__, ##__VA_ARGS__);    \
            }                                                               \
          while (0)
#else
#  define sched_note_printf(tag, fmt, ...) \
          sched_note_printf_ip(tag, up_getpc(), fmt, 0, ##__VA_ARGS__)
#endif

#define sched_note_begin(tag) \
        sched_note_event(tag, NOTE_DUMP_BEGIN, NULL, 0)
#define sched_note_end(tag) \
        sched_note_event(tag, NOTE_DUMP_END, NULL, 0)
#define sched_note_beginex(tag, str) \
        sched_note_event(tag, NOTE_DUMP_BEGIN, str, strlen(str))
#define sched_note_endex(tag, str) \
        sched_note_event(tag, NOTE_DUMP_END, str, strlen(str))
#define sched_note_mark(tag, str) \
        sched_note_event(tag, NOTE_DUMP_MARK, str, strlen(str))

#define sched_note_counter(tag, name_, value_) \
        do \
          { \
            struct note_counter_s counter; \
            counter.value = value_; \
            strlcpy(counter.name, name_, NAME_MAX); \
            sched_note_event(tag, NOTE_DUMP_COUNTER, \
                             &counter, sizeof(counter)); \
          } \
        while (0)

#define sched_note_threadtime(_elapsed) \
        do \
          { \
            struct note_threadtime_s threadtime; \
            threadtime.elapsed = (_elapsed); \
            sched_note_event(NOTE_TAG_SCHED, NOTE_DUMP_THREADTIME, \
                             &threadtime, sizeof(threadtime)); \
          } \
        while (0)

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* This type identifies a note structure */

enum note_type_e
{
  NOTE_ALL,
  NOTE_START,
  NOTE_STOP,
  NOTE_SUSPEND,
  NOTE_RESUME,
  NOTE_TASKNAME,
  NOTE_CPU_START,
  NOTE_CPU_STARTED,
  NOTE_PREEMPT_LOCK,
  NOTE_PREEMPT_UNLOCK,
  NOTE_CSECTION_ENTER,
  NOTE_CSECTION_LEAVE,
  NOTE_SPINLOCK_LOCK,
  NOTE_SPINLOCK_LOCKED,
  NOTE_SPINLOCK_UNLOCK,
  NOTE_SPINLOCK_ABORT,
  NOTE_SYSCALL_ENTER,
  NOTE_SYSCALL_LEAVE,
  NOTE_IRQ_ENTER,
  NOTE_IRQ_LEAVE,
  NOTE_WDOG_START,
  NOTE_WDOG_CANCEL,
  NOTE_WDOG_ENTER,
  NOTE_WDOG_LEAVE,
  NOTE_HEAP_ADD,
  NOTE_HEAP_REMOVE,
  NOTE_HEAP_ALLOC,
  NOTE_HEAP_FREE,
  NOTE_DUMP_PRINTF,
  NOTE_DUMP_BEGIN,
  NOTE_DUMP_END,
  NOTE_DUMP_MARK,
  NOTE_DUMP_BINARY,
  NOTE_DUMP_COUNTER,
  NOTE_DUMP_THREADTIME,

  /* Always last */

  NOTE_TYPE_LAST
};

enum note_tag_e
{
  NOTE_TAG_ALWAYS = 0,
  NOTE_TAG_LOG,
  NOTE_TAG_LOG_EMERG = NOTE_TAG_LOG,
  NOTE_TAG_LOG_ALERT,
  NOTE_TAG_LOG_CRIT,
  NOTE_TAG_LOG_ERR,
  NOTE_TAG_LOG_WARNING,
  NOTE_TAG_LOG_NOTICE,
  NOTE_TAG_LOG_INFO,
  NOTE_TAG_LOG_DEBUG,
  NOTE_TAG_APP,
  NOTE_TAG_ARCH,
  NOTE_TAG_AUDIO,
  NOTE_TAG_BOARDS,
  NOTE_TAG_CRYPTO,
  NOTE_TAG_DRIVERS,
  NOTE_TAG_FS,
  NOTE_TAG_GRAPHICS,
  NOTE_TAG_INPUT,
  NOTE_TAG_LIBS,
  NOTE_TAG_MM,
  NOTE_TAG_NET,
  NOTE_TAG_SCHED,
  NOTE_TAG_VIDEO,
  NOTE_TAG_WIRLESS,
  NOTE_TAG_RPMSG,

  /* Always last */

  NOTE_TAG_LAST,
  NOTE_TAG_MAX = NOTE_TAG_LAST + 16
};

/* This structure provides the common header of each note */

struct note_common_s
{
  uint8_t nc_length;           /* Length of the note */
  uint8_t nc_type;             /* See enum note_type_e */
  uint8_t nc_priority;         /* Thread/task priority */
  uint8_t nc_cpu;              /* CPU thread/task running on */
  pid_t   nc_pid;              /* ID of the thread/task */
  clock_t nc_systime;          /* Time when note was buffered */
};

/* This is the specific form of the NOTE_STOP note */

struct note_stop_s
{
  struct note_common_s nsp_cmn; /* Common note parameters */
};

/* This is the specific form of the NOTE_SUSPEND note */

struct note_suspend_s
{
  struct note_common_s nsu_cmn; /* Common note parameters */
  uint8_t nsu_state;            /* Task state */
};

/* This is the specific form of the NOTE_RESUME note */

struct note_resume_s
{
  struct note_common_s nre_cmn; /* Common note parameters */
};

/* This is the specific form of the NOTE_CPU_START note */

struct note_cpu_start_s
{
  struct note_common_s ncs_cmn; /* Common note parameters */
  uint8_t ncs_target;           /* CPU being started */
};

/* This is the specific form of the NOTE_CPU_STARTED note */

struct note_cpu_started_s
{
  struct note_common_s ncs_cmn; /* Common note parameters */
};

/* This is the specific form of the NOTE_PREEMPT_LOCK/UNLOCK note */

struct note_preempt_s
{
  struct note_common_s npr_cmn; /* Common note parameters */
  uint16_t npr_count;           /* Count of nested locks */
};

/* This is the specific form of the NOTE_CSECTION_ENTER/LEAVE note */

struct note_csection_s
{
  struct note_common_s ncs_cmn; /* Common note parameters */
#ifdef CONFIG_SMP
  uint16_t ncs_count;           /* Count of nested csections */
#endif
};

/* This is the specific form of the NOTE_SPINLOCK_LOCK/LOCKED/UNLOCK/ABORT
 * note.
 */

struct note_spinlock_s
{
  struct note_common_s nsp_cmn; /* Common note parameters */
  uintptr_t nsp_spinlock;       /* Address of spinlock */
  uint8_t nsp_value;            /* Value of spinlock */
};

/* This is the specific form of the NOTE_SYSCALL_ENTER/LEAVE notes */

#define MAX_SYSCALL_ARGS  6
#define SIZEOF_NOTE_SYSCALL_ENTER(n) (sizeof(struct note_common_s) + \
                                      sizeof(uint8_t) + sizeof(uint8_t) + \
                                      (sizeof(uintptr_t) * (n)))

struct note_syscall_enter_s
{
  struct note_common_s nsc_cmn;         /* Common note parameters */
  uint8_t nsc_nr;                       /* System call number */
#ifdef CONFIG_SCHED_INSTRUMENTATION_FILTER_SYSCALL_ARGS
  uint8_t nsc_argc;                     /* Number of system call arguments */
  uintptr_t nsc_args[MAX_SYSCALL_ARGS]; /* System call arguments */
#endif
};

struct note_syscall_leave_s
{
  struct note_common_s nsc_cmn;         /* Common note parameters */
  uint8_t nsc_nr;                       /* System call number */
  uintptr_t nsc_result;                 /* Result of the system call */
};

/* This is the specific form of the NOTE_IRQ_ENTER/LEAVE notes */

struct note_irqhandler_s
{
  struct note_common_s nih_cmn; /* Common note parameters */
  uintptr_t nih_handler;        /* IRQ handler address */
  uint8_t nih_irq;              /* IRQ number */
};

struct note_wdog_s
{
  struct note_common_s nwd_cmn;      /* Common note parameters */
  uintptr_t handler;
  uintptr_t arg;
};

struct note_heap_s
{
  struct note_common_s nhp_cmn;      /* Common note parameters */
  FAR void *heap;
  FAR void *mem;
  size_t size;
  size_t used;
};

/* Mannually aligned to the
 * 4-byte boundary as some compilers
 * like GHS prefers alignment and it's
 * no harm anyway.
 * Without packing the struct, implicitly
 * alignment will lead to an incorrect result
 * from SIZEOF_NOTE_PRINTF/SIZEOF_NOTE_EVENT
 */

struct note_printf_s
{
  struct note_common_s npt_cmn; /* Common note parameters */
  uintptr_t npt_ip;             /* Instruction pointer called from */
  FAR const char *npt_fmt;      /* Printf format string */
  uint64_t npt_type;            /* Printf parameter type */
  uint32_t npt_tag;             /* Printf tag */
  char npt_data[4];             /* Print arguments */
};

#define SIZEOF_NOTE_PRINTF(n) (sizeof(struct note_printf_s) + \
                              ((n) - 4) * sizeof(uint8_t))

struct note_event_s
{
  struct note_common_s nev_cmn;      /* Common note parameters */
  uintptr_t nev_ip;                  /* Instruction pointer called from */
  uint32_t nev_tag;                  /* Event tag */
  uint8_t nev_data[4];               /* Event data */
};

#define SIZEOF_NOTE_EVENT(n) (sizeof(struct note_event_s) + \
                             ((n - 4)) * sizeof(uint8_t))

struct note_counter_s
{
  long int value;
  char name[NAME_MAX];
};

struct note_threadtime_s
{
  clock_t elapsed;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#undef EXTERN
#if defined(__cplusplus)
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

/****************************************************************************
 * Name: sched_note_*
 *
 * Description:
 *   If instrumentation of the scheduler is enabled, then some outboard
 *   logic must provide the following interfaces.  These interfaces are not
 *   available to application code.
 *
 * Input Parameters:
 *   tcb - The TCB of the thread.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

#if defined(CONFIG_SCHED_INSTRUMENTATION) && defined(CONFIG_DRIVERS_NOTE)
void sched_note_add(FAR const void *note, size_t notelen);
#else
#  define sched_note_add(n,l)
#endif

#if defined(CONFIG_SCHED_INSTRUMENTATION_SWITCH) && defined(CONFIG_DRIVERS_NOTE)
void sched_note_start(FAR struct tcb_s *tcb);
void sched_note_stop(FAR struct tcb_s *tcb);
void sched_note_suspend(FAR struct tcb_s *tcb);
void sched_note_resume(FAR struct tcb_s *tcb);
void sched_note_taskname(void (FAR *handler)(FAR const void *,
                                             size_t, FAR void *),
                         FAR void *arg);
#else
#  define sched_note_stop(t)
#  define sched_note_start(t)
#  define sched_note_suspend(t)
#  define sched_note_resume(t)
#  define sched_note_taskname(t,a)
#endif

#if defined(CONFIG_SMP) && defined(CONFIG_SCHED_INSTRUMENTATION_SWITCH) && defined(CONFIG_DRIVERS_NOTE)
void sched_note_cpu_start(FAR struct tcb_s *tcb, int cpu);
void sched_note_cpu_started(FAR struct tcb_s *tcb);
#else
#  define sched_note_cpu_start(t,c)
#  define sched_note_cpu_started(t)
#endif

#if defined(CONFIG_SCHED_INSTRUMENTATION_PREEMPTION) && defined(CONFIG_DRIVERS_NOTE)
void sched_note_preemption(FAR struct tcb_s *tcb, bool locked);
#else
#  define sched_note_preemption(t,l)
#endif

#if defined(CONFIG_SCHED_INSTRUMENTATION_CSECTION) && defined(CONFIG_DRIVERS_NOTE)
void sched_note_csection(FAR struct tcb_s *tcb, bool enter);
#else
#  define sched_note_csection(t,e)
#endif

#if defined(CONFIG_SCHED_INSTRUMENTATION_SPINLOCKS) && defined(CONFIG_DRIVERS_NOTE)
void sched_note_spinlock(FAR volatile spinlock_t *spinlock, int type);
#else
#  define sched_note_spinlock(spinlock, type)
#endif

#if defined(CONFIG_SCHED_INSTRUMENTATION_SYSCALL) && defined(CONFIG_DRIVERS_NOTE)
void sched_note_syscall_enter(int nr, int argc, ...);
void sched_note_syscall_leave(int nr, uintptr_t result);
#else
#  define sched_note_syscall_enter(n,a,...)
#  define sched_note_syscall_leave(n,r)
#endif

#if defined(CONFIG_SCHED_INSTRUMENTATION_IRQHANDLER) && defined(CONFIG_DRIVERS_NOTE)
void sched_note_irqhandler(int irq, FAR void *handler, bool enter);
#else
#  define sched_note_irqhandler(i,h,e)
#endif

#if defined(CONFIG_SCHED_INSTRUMENTATION_WDOG) && defined(CONFIG_DRIVERS_NOTE)
void sched_note_wdog(uint8_t event, FAR void *handler, FAR const void *arg);
#else
#  define sched_note_wdog(e,h,a)
#endif

#if defined(CONFIG_SCHED_INSTRUMENTATION_HEAP) && defined(CONFIG_DRIVERS_NOTE)
void sched_note_heap(uint8_t event, FAR void *heap, FAR void *mem,
                     size_t size, size_t used);
#else
#  define sched_note_heap(e,h,m,s,c)
#endif

#if defined(CONFIG_SCHED_INSTRUMENTATION_DUMP) && defined(CONFIG_DRIVERS_NOTE)
void sched_note_event_ip(uint32_t tag, uintptr_t ip, uint8_t event,
                         FAR const void *buf, size_t len);
void sched_note_vprintf_ip(uint32_t tag, uintptr_t ip, FAR const char *fmt,
                           uint64_t type, va_list *va) printf_like(3, 0);
void sched_note_printf_ip(uint32_t tag, uintptr_t ip, FAR const char *fmt,
                          uint64_t type, ...) printf_like(3, 5);

void sched_note_event_ip_ratelimit(uint32_t tag, uintptr_t ip, uint8_t event,
                                   FAR const void *buf, size_t len);
void sched_note_vprintf_ip_ratelimit(uint32_t tag, uintptr_t ip,
                                     FAR const char *fmt, uint64_t type,
                                     va_list *va) printf_like(3, 0);
void sched_note_printf_ip_ratelimit(uint32_t tag, uintptr_t ip,
                                    FAR const char *fmt,
                                    uint64_t type, ...) printf_like(3, 5);

#define sched_note_event_ip_ratelimited(tag, ip, event, buf, len) \
  ({                                                              \
    static DEFINE_RATELIMIT_STATE(_rs,                            \
      CONFIG_DRIVERS_NOTE_RATELIMIT_INTERVAL,                     \
      CONFIG_DRIVERS_NOTE_RATELIMIT_BURST);                       \
    if (!ratelimit_islimited(&_rs))                               \
      {                                                           \
        sched_note_event_ip(tag, ip, event, buf, len);            \
      }                                                           \
  })

#define sched_note_vprintf_ip_ratelimited(tag, ip, fmt, type, va) \
  ({                                                              \
    static DEFINE_RATELIMIT_STATE(_rs,                            \
      CONFIG_DRIVERS_NOTE_RATELIMIT_INTERVAL,                     \
      CONFIG_DRIVERS_NOTE_RATELIMIT_BURST);                       \
    if (!ratelimit_islimited(&_rs))                               \
      {                                                           \
        sched_note_vprintf_ip(tag, ip, fmt, type, va);            \
      }                                                           \
  })

#define sched_note_printf_ip_ratelimited(tag, ip, fmt, type, ...) \
  ({                                                              \
    static DEFINE_RATELIMIT_STATE(_rs,                            \
      CONFIG_DRIVERS_NOTE_RATELIMIT_INTERVAL,                     \
      CONFIG_DRIVERS_NOTE_RATELIMIT_BURST);                       \
    if (!ratelimit_islimited(&_rs))                               \
      {                                                           \
        sched_note_printf_ip(tag, ip, fmt, type, ##__VA_ARGS__);  \
      }                                                           \
  })

#else
#  define sched_note_event_ip(t,ip,e,b,l)
#  define sched_note_vprintf_ip(t,ip,f,p,v)
#  define sched_note_printf_ip(t,ip,f,p,...)
#  define sched_note_event_ip_ratelimit(t,ip,e,b,l)
#  define sched_note_vprintf_ip_ratelimit(t,ip,f,p,v)
#  define sched_note_printf_ip_ratelimit(t,ip,f,p,...)
#  define sched_note_event_ip_ratelimited(tag, ip, event, buf, len)
#  define sched_note_vprintf_ip_ratelimited(tag, ip, fmt, type, va)
#  define sched_note_printf_ip_ratelimited(tag, ip, fmt, type, ...)
#endif /* CONFIG_SCHED_INSTRUMENTATION_DUMP */

#undef EXTERN
#if defined(__cplusplus)
}
#endif
#endif /* __INCLUDE_NUTTX_SCHED_NOTE_H */
