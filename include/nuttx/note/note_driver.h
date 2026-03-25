/****************************************************************************
 * include/nuttx/note/note_driver.h
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

#ifndef __INCLUDE_NUTTX_NOTE_NOTE_DRIVER_H
#define __INCLUDE_NUTTX_NOTE_NOTE_DRIVER_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

#include <nuttx/fs/ioctl.h>
#include <nuttx/tag.h>

/* For system call numbers definition */

#ifdef CONFIG_SCHED_INSTRUMENTATION_SYSCALL
  #ifdef CONFIG_LIB_SYSCALL
  #  include <syscall.h>
  #else
  #  define CONFIG_LIB_SYSCALL
  #  include <syscall.h>
  #  undef CONFIG_LIB_SYSCALL
  #endif
#endif

#ifdef CONFIG_SCHED_INSTRUMENTATION_IRQHANDLER
#  include <nuttx/irq.h>
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Backward compatibility: map old NOTE_TAG_* to new NXTAG_* */

#define NOTE_TAG_ALWAYS   NXTAG_GLOBAL
#define NOTE_TAG_LOG      NXTAG_LOG
#define NOTE_TAG_APP      NXTAG_APP
#define NOTE_TAG_GRAPHICS NXTAG_GRAPHICS
#define NOTE_TAG_LAST     NXTAG_KERNEL_MAX
#define NOTE_TAG_MAX      NXTAG_MAX

/* Provide defaults for some configuration settings (could be undefined with
 * old configuration files)
 */

#ifndef CONFIG_SCHED_INSTRUMENTATION_CPUSET
#  define CONFIG_SCHED_INSTRUMENTATION_CPUSET ~0
#endif

/* Note filter mode flag definitions — bitmasks grouping note types */

#define NOTE_FILTER_MODE_FLAG_SWITCH                         \
  ((1ULL << NOTE_START)       | (1ULL << NOTE_STOP)        | \
   (1ULL << NOTE_SUSPEND)     | (1ULL << NOTE_RESUME)      | \
   (1ULL << NOTE_TASKNAME)    | (1ULL << NOTE_CPU_START)   | \
   (1ULL << NOTE_CPU_STARTED))

#define NOTE_FILTER_MODE_FLAG_SYSCALL                         \
  ((1ULL << NOTE_SYSCALL_ENTER) | (1ULL << NOTE_SYSCALL_LEAVE))

#define NOTE_FILTER_MODE_FLAG_IRQ                             \
  ((1ULL << NOTE_IRQ_ENTER) | (1ULL << NOTE_IRQ_LEAVE))

#define NOTE_FILTER_MODE_FLAG_DUMP                            \
  ((1ULL << NOTE_DUMP_PRINTF)  | (1ULL << NOTE_DUMP_BEGIN)  | \
   (1ULL << NOTE_DUMP_END)     | (1ULL << NOTE_DUMP_MARK)   | \
   (1ULL << NOTE_DUMP_BINARY)  | (1ULL << NOTE_DUMP_COUNTER)| \
   (1ULL << NOTE_DUMP_THREADTIME))

#define NOTE_FILTER_MODE_FLAG_PREEMPTION                      \
  ((1ULL << NOTE_PREEMPT_LOCK) | (1ULL << NOTE_PREEMPT_UNLOCK))

#define NOTE_FILTER_MODE_FLAG_CSECTION                        \
  ((1ULL << NOTE_CSECTION_ENTER) | (1ULL << NOTE_CSECTION_LEAVE))

#define NOTE_FILTER_MODE_FLAG_SPINLOCKS                       \
  ((1ULL << NOTE_SPINLOCK_LOCK) | (1ULL << NOTE_SPINLOCK_LOCKED) | \
   (1ULL << NOTE_SPINLOCK_UNLOCK) | (1ULL << NOTE_SPINLOCK_ABORT))

#define NOTE_FILTER_MODE_FLAG_WDOG                            \
  ((1ULL << NOTE_WDOG_START) | (1ULL << NOTE_WDOG_CANCEL) |   \
   (1ULL << NOTE_WDOG_ENTER) | (1ULL << NOTE_WDOG_LEAVE))

#define NOTE_FILTER_MODE_FLAG_HEAP                            \
  ((1ULL << NOTE_HEAP_ADD) | (1ULL << NOTE_HEAP_REMOVE) |     \
   (1ULL << NOTE_HEAP_ALLOC) | (1ULL << NOTE_HEAP_FREE))

/* Helper macros for type instrumentation filter */

#define NOTE_BPL (sizeof(unsigned long) * CHAR_BIT)

#define NOTE_FILTER_TYPEMASK_SET(type, s) \
          (((unsigned long *)&(s)->type_mask)[(type) / NOTE_BPL] |= \
           (1ul << ((type) % NOTE_BPL)))
#define NOTE_FILTER_TYPEMASK_CLR(type, s) \
          (((unsigned long *)&(s)->type_mask)[(type) / NOTE_BPL] &= \
           ~(1ul << ((type) % NOTE_BPL)))
#define NOTE_FILTER_TYPEMASK_ISSET(type, s) \
          (((unsigned long *)&(s)->type_mask)[(type) / NOTE_BPL] & \
           (1ul << ((type) % NOTE_BPL)))
#define NOTE_FILTER_TYPEMASK_ZERO(s) \
          memset((s), 0, sizeof(struct note_filter_mode_s))
#define NOTE_FILTER_TYPEMASK_FILL(s) \
          memset((s), 0xff, sizeof(struct note_filter_mode_s))

/* Helper macros for syscall instrumentation filter */

#ifdef CONFIG_SCHED_INSTRUMENTATION_SYSCALL
#  define NOTE_FILTER_SYSCALLMASK_SET(nr, s) \
          ((s)->syscall_mask[(nr) / NOTE_BPL] |= (1ul << ((nr) % NOTE_BPL)))
#  define NOTE_FILTER_SYSCALLMASK_CLR(nr, s) \
          ((s)->syscall_mask[(nr) / NOTE_BPL] &= ~(1ul << ((nr) % NOTE_BPL)))
#  define NOTE_FILTER_SYSCALLMASK_ISSET(nr, s) \
          ((s)->syscall_mask[(nr) / NOTE_BPL] & (1ul << ((nr) % NOTE_BPL)))
#  define NOTE_FILTER_SYSCALLMASK_ZERO(s) \
          memset((s), 0, sizeof(struct note_filter_syscall_s))
#  define NOTE_FILTER_SYSCALLMASK_FILL(s) \
          memset((s), 0xff, sizeof(struct note_filter_syscall_s))
#else
#  define NOTE_FILTER_SYSCALLMASK_SET(nr, s)
#  define NOTE_FILTER_SYSCALLMASK_CLR(nr, s)
#  define NOTE_FILTER_SYSCALLMASK_ISSET(nr, s) (0)
#  define NOTE_FILTER_SYSCALLMASK_ZERO(s)
#  define NOTE_FILTER_SYSCALLMASK_FILL(s)
#endif

/* Helper macros for IRQ instrumentation filter */

#ifdef CONFIG_SCHED_INSTRUMENTATION_IRQHANDLER
#  define NOTE_FILTER_IRQMASK_SET(nr, s) \
          ((s)->irq_mask[(nr) / NOTE_BPL] |= (1ul << ((nr) % NOTE_BPL)))
#  define NOTE_FILTER_IRQMASK_CLR(nr, s) \
          ((s)->irq_mask[(nr) / NOTE_BPL] &= ~(1ul << ((nr) % NOTE_BPL)))
#  define NOTE_FILTER_IRQMASK_ISSET(nr, s) \
          ((s)->irq_mask[(nr) / NOTE_BPL] & (1ul << ((nr) % NOTE_BPL)))
#  define NOTE_FILTER_IRQMASK_ZERO(s) \
          memset((s), 0, sizeof(struct note_filter_irq_s))
#  define NOTE_FILTER_IRQMASK_FILL(s) \
          memset((s), 0xff, sizeof(struct note_filter_irq_s))
#else
#  define NOTE_FILTER_IRQMASK_SET(nr, s)
#  define NOTE_FILTER_IRQMASK_CLR(nr, s)
#  define NOTE_FILTER_IRQMASK_ISSET(nr, s) (0)
#  define NOTE_FILTER_IRQMASK_ZERO(s)
#  define NOTE_FILTER_IRQMASK_FILL(s)
#endif

/* Helper macros for dump instrumentation filter */

#define NOTE_FILTER_TAGMASK_SET(tag, s) \
        NXTAG_MASK_SET((s)->tag_mask, (tag))
#define NOTE_FILTER_TAGMASK_CLR(tag, s) \
        NXTAG_MASK_CLR((s)->tag_mask, (tag))
#define NOTE_FILTER_TAGMASK_ISSET(tag, s) \
        NXTAG_MASK_ISSET((s)->tag_mask, (tag))
#define NOTE_FILTER_TAGMASK_ZERO(s) \
        NXTAG_MASK_ZERO((s)->tag_mask)
#define NOTE_FILTER_TAGMASK_FILL(s) \
        NXTAG_MASK_FILL((s)->tag_mask)
#define NOTE_FILTER_LEVEL_SET(l, s) \
        ((s)->level = (l))

#ifdef CONFIG_SCHED_INSTRUMENTATION_FILTER

/* Global filter macros — single check per event type.
 * NOTE_ALL and type bits are pre-computed into the sub-masks
 * by note_filter_update_global(), so each macro only needs
 * one mask check.
 */

#  define note_filter_type(type) \
     NOTE_FILTER_TYPEMASK_ISSET(type, &g_note_filter.mode)

#  ifdef CONFIG_SCHED_INSTRUMENTATION_SYSCALL
#    define note_filter_syscall(nr) \
        NOTE_FILTER_SYSCALLMASK_ISSET((nr) - CONFIG_SYS_RESERVED, \
                                      &g_note_filter.syscall_mask)
#  endif

#  ifdef CONFIG_SCHED_INSTRUMENTATION_IRQHANDLER
#    define note_filter_irq(irq) \
       NOTE_FILTER_IRQMASK_ISSET(irq, &g_note_filter.irq_mask)
#  endif

#  ifdef CONFIG_SCHED_INSTRUMENTATION_DUMP
#    define note_filter_dump(tag) \
       NOTE_FILTER_TAGMASK_ISSET(tag, &g_note_filter.tag_mask)
#  endif

#else /* CONFIG_SCHED_INSTRUMENTATION_FILTER */
#  define note_filter_type(type) (0)
#  define note_filter_syscall(nr) (0)
#  define note_filter_irq(irq) (0)
#  define note_filter_dump(tag) (0)
#endif /* CONFIG_SCHED_INSTRUMENTATION_FILTER */

/* IOCTL Commands ***********************************************************/

/* NOTE_CLEAR
 *              - Clear all contents of the circular buffer
 *                Argument: Ignored
 * NOTE_GETMODE
 *              - Get overwrite mode
 *                Argument: A writable pointer to unsigned int
 * NOTE_SETMODE
 *              - Set overwrite mode
 *                Argument: A read-only pointer to unsigned int
 * NOTE_GETREADMODE
 *              - Get read mode
 *                Argument: A writable pointer to unsigned int
 * NOTE_SETREADMODE
 *              - Set read mode
 *                Argument: A read-only pointer to unsigned int
 * NOTE_GETFILTER
 *              - Get note filter mode
 *                Argument: A writable pointer to struct note_filter_mode_s
 * NOTE_SETFILTER
 *              - Set note filter mode
 *                Argument: A read-only pointer to struct note_filter_mode_s
 * NOTE_GETSYSCALLFILTER
 *              - Get syscall filter setting
 *                Argument: A writable pointer to struct
 *                          note_filter_syscall_s
 * NOTE_SETSYSCALLFILTER
 *              - Set syscall filter setting
 *                Argument: A read-only pointer to struct
 *                          note_filter_syscall_s
 * NOTE_GETIRQFILTER
 *              - Get IRQ filter setting
 *                Argument: A writable pointer to struct
 *                          note_filter_irq_s
 * NOTE_SETIRQFILTER
 *              - Set IRQ filter setting
 *                Argument: A read-only pointer to struct
 *                          note_filter_irq_s
 */

#define NOTE_CLEAR                       _NOTEIOC(0x01)
#define NOTE_GETMODE                     _NOTEIOC(0x02)
#define NOTE_SETMODE                     _NOTEIOC(0x03)
#define NOTE_GETREADMODE                 _NOTEIOC(0x04)
#define NOTE_SETREADMODE                 _NOTEIOC(0x05)
#define NOTE_GETFILTER                   _NOTEIOC(0x06)
#define NOTE_SETFILTER                   _NOTEIOC(0x07)
#define NOTE_GETSYSCALLFILTER            _NOTEIOC(0x08)
#define NOTE_SETSYSCALLFILTER            _NOTEIOC(0x09)
#define NOTE_GETIRQFILTER                _NOTEIOC(0x0A)
#define NOTE_SETIRQFILTER                _NOTEIOC(0x0B)
#define NOTE_GETTAGFILTER                _NOTEIOC(0x0C)
#define NOTE_SETTAGFILTER                _NOTEIOC(0x0D)
#define NOTE_GETFREQ                     _NOTEIOC(0x0E)
#define NOTE_GETRATELIMIT                _NOTEIOC(0x0F)
#define NOTE_SETRATELIMIT                _NOTEIOC(0x10)
#define NOTE_GETLEVEL                    _NOTEIOC(0x11)
#define NOTE_SETLEVEL                    _NOTEIOC(0x12)

/* NOTE_CREATE
 *              - Dynamically create a new note backend device
 *                Argument: Backend-specific configuration pointer
 * NOTE_DESTROY
 *              - Dynamically destroy a note backend device
 *                Argument: A read-only pointer to the device name string
 */

#define NOTE_CREATE                      _NOTEIOC(0x13)
#define NOTE_DESTROY                     _NOTEIOC(0x14)

/* Overwrite mode definitions */

#define NOTE_MODE_OVERWRITE_DISABLE      0
#define NOTE_MODE_OVERWRITE_ENABLE       1
#define NOTE_MODE_OVERWRITE_OVERFLOW     2

#define NOTE_MODE_READ_ASCII             0
#define NOTE_MODE_READ_BINARY            1

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
  NOTE_DUMP_ASYNC_BEGIN,
  NOTE_DUMP_ASYNC_END,

  /* Always last */

  NOTE_TYPE_LAST
};

/* This structure provides the common header of each note */

struct tcb_s;
struct note_driver_s;

struct note_driver_ops_s
{
  CODE void (*add)(FAR struct note_driver_s *drv,
                   FAR const void *note, size_t notelen, bool noswitches);
  CODE void (*start)(FAR struct note_driver_s *drv, FAR struct tcb_s *tcb);
  CODE void (*stop)(FAR struct note_driver_s *drv, FAR struct tcb_s *tcb);
#ifdef CONFIG_SCHED_INSTRUMENTATION_SWITCH
  CODE void (*suspend)(FAR struct note_driver_s *drv, FAR struct tcb_s *tcb);
  CODE void (*resume)(FAR struct note_driver_s *drv, FAR struct tcb_s *tcb);
#endif
#ifdef CONFIG_SMP
  CODE void (*cpu_start)(FAR struct note_driver_s *drv,
                         FAR struct tcb_s *tcb, int cpu);
  CODE void (*cpu_started)(FAR struct note_driver_s *drv,
                           FAR struct tcb_s *tcb);
#endif
#ifdef CONFIG_SCHED_INSTRUMENTATION_PREEMPTION
  CODE void (*preemption)(FAR struct note_driver_s *drv,
                          FAR struct tcb_s *tcb, bool locked);
#endif
#ifdef CONFIG_SCHED_INSTRUMENTATION_CSECTION
  CODE void (*csection)(FAR struct note_driver_s *drv,
                        FAR struct tcb_s *tcb, bool enter);
#endif
#ifdef CONFIG_SCHED_INSTRUMENTATION_SPINLOCKS
  CODE void (*spinlock)(FAR struct note_driver_s *drv, FAR struct tcb_s *tcb,
                        FAR volatile void *spinlock, int type);
#endif
#ifdef CONFIG_SCHED_INSTRUMENTATION_SYSCALL
  CODE void (*syscall_enter)(FAR struct note_driver_s *drv,
                             int nr, int argc, va_list *ap);
  CODE void (*syscall_leave)(FAR struct note_driver_s *drv,
                             int nr, uintptr_t result);
#endif
#ifdef CONFIG_SCHED_INSTRUMENTATION_IRQHANDLER
  CODE void (*irqhandler)(FAR struct note_driver_s *drv, int irq,
                          FAR void *handler, bool enter);
#endif
#ifdef CONFIG_SCHED_INSTRUMENTATION_WDOG
  CODE void (*wdog)(FAR struct note_driver_s *drv, uint8_t event,
                    FAR void *handler, FAR const void *arg);
#endif
#ifdef CONFIG_SCHED_INSTRUMENTATION_HEAP
  CODE void (*heap)(FAR struct note_driver_s *drv, uint8_t event,
                    FAR void *heap, FAR void *mem, size_t size,
                    size_t curused);
#endif
#ifdef CONFIG_SCHED_INSTRUMENTATION_DUMP
  CODE void (*event)(FAR struct note_driver_s *drv, uint8_t tag,
                     uint8_t level, uintptr_t ip, uint8_t event,
                     FAR const void *buf, size_t len);
  CODE void (*vprintf)(FAR struct note_driver_s *drv, uint8_t tag,
                       uint8_t level, uintptr_t ip, uint64_t type,
                       FAR const char *fmt, va_list va) printf_like(6, 0);
#endif
  CODE int (*open)(FAR struct file *filep);
  CODE int (*close)(FAR struct file *filep);
  CODE ssize_t (*read)(FAR struct file *filep, FAR char *buffer,
                       size_t buflen);
  CODE ssize_t (*write)(FAR struct file *filep, FAR const char *buffer,
                        size_t buflen);
  CODE int (*ioctl)(FAR struct file *filep, int cmd, unsigned long arg);
  CODE int (*poll)(FAR struct file *filep, FAR struct pollfd *fds,
                   bool setup);
};

/* This is the type of the argument passed to the NOTE_GETRATELIMIT and
 * NOTE_SETRATELIMIT ioctls
 */

struct note_ratelimit_s
{
  unsigned int interval; /* The interval in seconds */
  unsigned int burst;    /* The max allowed note number during interval */
};

/* This is the type of the argument passed to the NOTECTL_GETMODE and
 * NOTECTL_SETMODE ioctls
 */

struct note_filter_mode_s
{
  uint64_t type_mask;         /* The type mask of the note */
#ifndef CONFIG_UP
  cpu_set_t cpuset;           /* The set of monitored CPUs */
#endif
};

/* This is the type of the argument passed to the NOTECTL_GETSYSCALLFILTER
 * and NOTECTL_SETSYSCALLFILTER ioctls
 */

#ifdef CONFIG_SCHED_INSTRUMENTATION_SYSCALL
struct note_filter_syscall_s
{
  unsigned long syscall_mask[(SYS_nsyscalls + NOTE_BPL - 1) / NOTE_BPL];
};
#endif

/* This is the type of the argument passed to the NOTECTL_GETIRQFILTER and
 * NOTECTL_SETIRQFILTER ioctls
 */

#ifdef CONFIG_SCHED_INSTRUMENTATION_IRQHANDLER
struct note_filter_irq_s
{
  unsigned long irq_mask[(NR_IRQS + NOTE_BPL - 1) / NOTE_BPL];
};
#endif

struct note_filter_tag_s
{
  unsigned long tag_mask[NXTAG_MASK_NWORDS];
};

struct note_filter_level_s
{
  uint8_t level;
};

struct note_filter_s
{
  struct note_filter_mode_s mode;
#ifdef CONFIG_SCHED_INSTRUMENTATION_DUMP
  struct note_filter_level_s level;
  struct note_filter_tag_s tag_mask;
#endif
#ifdef CONFIG_SCHED_INSTRUMENTATION_IRQHANDLER
  struct note_filter_irq_s irq_mask;
#endif
#ifdef CONFIG_SCHED_INSTRUMENTATION_SYSCALL
  struct note_filter_syscall_s syscall_mask;
#endif
};

struct note_driver_s
{
#ifdef CONFIG_SCHED_INSTRUMENTATION_FILTER
  FAR const char *name;
  struct note_filter_s filter;
#endif
  FAR const struct note_driver_ops_s *ops;
};

struct note_common_s
{
  uint8_t nc_length;           /* Length of the note */
  uint8_t nc_type;             /* See enum note_type_e */
  uint8_t nc_priority;         /* Thread/task priority */
  uint8_t nc_cpu;              /* CPU thread/task running on */
  uint32_t nc_pid;             /* ID of the thread/task */
  uint64_t nc_systime;         /* Time when note was buffered */
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
#if CONFIG_SCHED_INSTRUMENTATION_SWITCH_BACKTRACE > 0
  uintptr_t nsu_backtrace[CONFIG_SCHED_INSTRUMENTATION_SWITCH_BACKTRACE];
#endif
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
#ifdef CONFIG_SCHED_INSTRUMENTATION_FILTER_SYSCALL_ARGS
  uintptr_t nsc_args[MAX_SYSCALL_ARGS]; /* System call arguments */
  uint8_t nsc_argc;                     /* Number of system call arguments */
#endif
  uint8_t nsc_nr;                       /* System call number */
};

struct note_syscall_leave_s
{
  struct note_common_s nsc_cmn;         /* Common note parameters */
  uintptr_t nsc_result;                 /* Result of the system call */
  uint8_t nsc_nr;                       /* System call number */
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
  uintptr_t heap;
  uintptr_t mem;
  size_t size;
  size_t used;
};

/* Manually aligned to the
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
  uintptr_t npt_fmt;            /* Printf format string */
  uint64_t npt_type;            /* Printf parameter type */
  uint8_t npt_tag;              /* Printf tag */
  uint8_t npt_level;            /* Event level */
  char npt_data[2];             /* Print arguments */
};

#define SIZEOF_NOTE_PRINTF(n) (sizeof(struct note_printf_s) + \
                              ((n) - 2) * sizeof(uint8_t))

struct note_event_s
{
  struct note_common_s nev_cmn;      /* Common note parameters */
  uintptr_t nev_ip;                  /* Instruction pointer called from */
  uint8_t nev_tag;                   /* Event tag */
  uint8_t nev_level;                 /* Event level */
  uint8_t nev_data[2];               /* Event data */
};

#define SIZEOF_NOTE_EVENT(n) (sizeof(struct note_event_s) + \
                             ((n - 2)) * sizeof(uint8_t))

struct note_counter_s
{
  int64_t value;
  char name[NAME_MAX];
};

struct note_threadtime_s
{
  clock_t elapsed;
};

struct note_async_s
{
  int32_t cookie;
  char names[];         /* [name\0][track\0] (track optional) */
};

/* Maximum note structure for temporary buffer when copying from byte stream.
 */

struct note_max_s
{
  struct note_common_s cmn;
  uint8_t data[256 - sizeof(struct note_common_s)];
};

/****************************************************************************
 * Public Data
 ****************************************************************************/

#ifdef CONFIG_SCHED_INSTRUMENTATION_FILTER
extern struct note_filter_s g_note_filter;
#endif

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

#ifdef CONFIG_DRIVERS_NOTE

/****************************************************************************
 * Name: note_early_initialize
 *
 * Description:
 *   Early register sched note related drivers that do not rely on system
 *   features like mm.
 *
 * Input Parameters:
 *   None.
 *
 * Returned Value:
 *   Zero on success. A negative errno value is returned on a failure.
 *
 ****************************************************************************/

int note_early_initialize(void);

/****************************************************************************
 * Name: note_initialize
 *
 * Description:
 *   Register sched note related drivers at /dev folder that can be used by
 *   an application to read or filter the note data.
 *
 * Input Parameters:
 *   None.
 *
 * Returned Value:
 *   Zero on success. A negative errno value is returned on a failure.
 *
 ****************************************************************************/

int note_initialize(void);

/****************************************************************************
 * Name: note_driver_register
 ****************************************************************************/

int note_driver_register(FAR struct note_driver_s *driver);

/****************************************************************************
 * Name: note_driver_unregister
 ****************************************************************************/

void note_driver_unregister(FAR struct note_driver_s *driver);

/****************************************************************************
 * Name: note_add
 ****************************************************************************/

#ifdef CONFIG_SCHED_INSTRUMENTATION
void note_add(FAR const void *note, size_t notelen);
#endif

/****************************************************************************
 * Name: note_start/stop/suspend/resume
 ****************************************************************************/

#ifdef CONFIG_SCHED_INSTRUMENTATION_SWITCH
void note_start(FAR struct tcb_s *tcb);
void note_stop(FAR struct tcb_s *tcb);
void note_suspend(FAR struct tcb_s *tcb);
void note_resume(FAR struct tcb_s *tcb);
void note_taskname(void (FAR *handler)(FAR const void *,
                                      size_t, FAR void *),
                   FAR void *arg);
#  ifdef CONFIG_SMP
void note_cpu_start(FAR struct tcb_s *tcb, int cpu);
void note_cpu_started(FAR struct tcb_s *tcb);
#  endif
#endif

/****************************************************************************
 * Name: note_preemption
 ****************************************************************************/

#ifdef CONFIG_SCHED_INSTRUMENTATION_PREEMPTION
void note_preemption(FAR struct tcb_s *tcb, bool locked);
#endif

/****************************************************************************
 * Name: note_csection
 ****************************************************************************/

#ifdef CONFIG_SCHED_INSTRUMENTATION_CSECTION
void note_csection(FAR struct tcb_s *tcb, bool enter);
#endif

/****************************************************************************
 * Name: note_spinlock
 ****************************************************************************/

#ifdef CONFIG_SCHED_INSTRUMENTATION_SPINLOCKS
void note_spinlock(FAR volatile void *spinlock, int type);
#endif

/****************************************************************************
 * Name: note_syscall_enter/leave
 ****************************************************************************/

#ifdef CONFIG_SCHED_INSTRUMENTATION_SYSCALL
void note_syscall_enter(int nr, int argc, ...);
void note_syscall_leave(int nr, uintptr_t result);
#endif

/****************************************************************************
 * Name: note_irqhandler
 ****************************************************************************/

#ifdef CONFIG_SCHED_INSTRUMENTATION_IRQHANDLER
void note_irqhandler(int irq, FAR void *handler, bool enter);
#endif

/****************************************************************************
 * Name: note_wdog
 ****************************************************************************/

#ifdef CONFIG_SCHED_INSTRUMENTATION_WDOG
void note_wdog(uint8_t event, FAR void *handler, FAR const void *arg);
#endif

/****************************************************************************
 * Name: note_heap
 ****************************************************************************/

#ifdef CONFIG_SCHED_INSTRUMENTATION_HEAP
void note_heap(uint8_t event, FAR void *heap, FAR void *mem,
               size_t size, size_t curused);
#endif

/****************************************************************************
 * Name: note_event_ip/vprintf_ip/printf_ip
 ****************************************************************************/

#ifdef CONFIG_SCHED_INSTRUMENTATION_DUMP
void note_event_ip(uint8_t tag, uint8_t level, uintptr_t ip, uint8_t event,
                   FAR const void *buf, size_t len);
void note_vprintf_ip(uint8_t tag, uint8_t level, uintptr_t ip,
                     FAR const char *fmt, uint64_t type,
                     va_list *va) printf_like(4, 0);
void note_printf_ip(uint8_t tag, uint8_t level, uintptr_t ip,
                    FAR const char *fmt, uint64_t type,
                    ...) printf_like(4, 6);
#endif

#endif /* CONFIG_DRIVERS_NOTE */

#undef EXTERN
#if defined(__cplusplus)
}
#endif

#endif /* __INCLUDE_NUTTX_NOTE_NOTE_DRIVER_H */
