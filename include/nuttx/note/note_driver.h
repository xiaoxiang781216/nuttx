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

#include <nuttx/fs/ioctl.h>
#include <nuttx/sched.h>
#include <nuttx/sched_note.h>

/* For system call numbers definition */

#ifdef CONFIG_LIB_SYSCALL
#  include <syscall.h>
#else
#  define CONFIG_LIB_SYSCALL
#  include <syscall.h>
#  undef CONFIG_LIB_SYSCALL
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Provide defaults for some configuration settings (could be undefined with
 * old configuration files)
 */

#ifndef CONFIG_SCHED_INSTRUMENTATION_CPUSET
#  define CONFIG_SCHED_INSTRUMENTATION_CPUSET ~0
#endif

/* Note filter mode flag definitions */

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
   (1ULL << NOTE_DUMP_END)     |  (1ULL << NOTE_DUMP_MARK)  | \
   (1ULL << NOTE_DUMP_BINARY)  | (1ULL << NOTE_DUMP_COUNTER)| \
   (1ULL << NOTE_DUMP_THREADTIME))

#define NOTE_FILTER_MODE_FLAG_PREEMPTION                      \
  ((1ULL << NOTE_PREEMPT_LOCK) | (1ULL << NOTE_PREEMPT_UNLOCK))

#define NOTE_FILTER_MODE_FLAG_CSECTION                        \
  ((1ULL << NOTE_CSECTION_ENTER) | (1ULL << NOTE_CSECTION_EXIT))

#define NOTE_FILTER_MODE_FLAG_SPINLOCKS                       \
  ((1ULL << NOTE_SPINLOCK_ENTER) | (1ULL << NOTE_SPINLOCK_EXIT))

#define NOTE_FILTER_MODE_FLAG_WDOG                            \
  ((1ULL << NOTE_WDOG_START) | (1ULL << NOTE_WDOG_CANCEL) |   \
   (1ULL << NOTE_WDOG_ENTER) | (1ULL << NOTE_WDOG_LEAVE))

#define NOTE_FILTER_MODE_FLAG_HEAP                            \
  ((1ULL << NOTE_HEAP_ADD) | (1ULL << NOTE_HEAP_REMOVE) |     \
   (1ULL << NOTE_HEAP_ALLOC) | (1ULL << NOTE_HEAP_FREE))

/* Helper macros for type instrumentation filter */

#define NOTE_FILTER_TYPEMASK_SET(type, s) \
          (((uint8_t *)&(s)->type_mask)[(type) / 8] |= (1 << ((type) % 8)))
#define NOTE_FILTER_TYPEMASK_CLR(type, s) \
          (((uint8_t *)&(s)->type_mask)[(type) / 8] &= ~(1 << ((type) % 8)))
#define NOTE_FILTER_TYPEMASK_ISSET(type, s) \
          (((uint8_t *)&(s)->type_mask)[(type) / 8] & (1 << ((type) % 8)))
#define NOTE_FILTER_TYPEMASK_ZERO(s) \
          memset((s), 0, sizeof(struct note_filter_mode_s))
#define NOTE_FILTER_TYPEMASK_FILL(s) \
          memset((s), 0xff, sizeof(struct note_filter_mode_s))

/* Helper macros for syscall instrumentation filter */

#ifdef CONFIG_SCHED_INSTRUMENTATION_SYSCALL
#  define NOTE_FILTER_SYSCALLMASK_SET(nr, s) \
          ((s)->syscall_mask[(nr) / 8] |= (1 << ((nr) % 8)))
#  define NOTE_FILTER_SYSCALLMASK_CLR(nr, s) \
          ((s)->syscall_mask[(nr) / 8] &= ~(1 << ((nr) % 8)))
#  define NOTE_FILTER_SYSCALLMASK_ISSET(nr, s) \
          ((s)->syscall_mask[(nr) / 8] & (1 << ((nr) % 8)))
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
          ((s)->irq_mask[(nr) / 8] |= (1 << ((nr) % 8)))
#  define NOTE_FILTER_IRQMASK_CLR(nr, s) \
          ((s)->irq_mask[(nr) / 8] &= ~(1 << ((nr) % 8)))
#  define NOTE_FILTER_IRQMASK_ISSET(nr, s) \
          ((s)->irq_mask[(nr) / 8] & (1 << ((nr) % 8)))
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

#ifdef CONFIG_SCHED_INSTRUMENTATION_DUMP
#  define NOTE_FILTER_TAGMASK_SET(tag, s) \
          ((s)->tag_mask[(tag) / 8] |= (1 << ((tag) % 8)))
#  define NOTE_FILTER_TAGMASK_CLR(tag, s) \
          ((s)->tag_mask[(tag) / 8] &= ~(1 << ((tag) % 8)))
#  define NOTE_FILTER_TAGMASK_ISSET(tag, s) \
          ((s)->tag_mask[(tag) / 8] & (1 << ((tag) % 8)))
#  define NOTE_FILTER_TAGMASK_ZERO(s) \
          memset((s), 0, sizeof(struct note_filter_tag_s));
#  define NOTE_FILTER_TAGMASK_FILL(s) \
          memset((s), 0xff, sizeof(struct note_filter_tag_s))
#  define NOTE_FILTER_LEVEL_SET(level, s) \
          ((s)->level = (level))
#else
#  define NOTE_FILTER_TAGMASK_SET(tag, s)
#  define NOTE_FILTER_TAGMASK_CLR(tag, s)
#  define NOTE_FILTER_TAGMASK_ISSET(tag, s) (0)
#  define NOTE_FILTER_TAGMASK_ZERO(s)
#  define NOTE_FILTER_TAGMASK_FILL(s)
#  define NOTE_FILTER_LEVEL_SET(level, s)
#endif

/* Sometimes perf_getime is not a syscall */

#ifdef CONFIG_ARCH_PERF_EVENTS_USER_ACCESS
#  define NOTE_PERF_GETTIME perf_gettime
#else
clock_t UP_REALSYM(perf_gettime)(void);
#  define NOTE_PERF_GETTIME UP_REALSYM(perf_gettime)
#endif

#define note_driver_event(driver, tag, event, buf, len) \
        note_driver_event_ip(driver, tag, up_getpc(), event, buf, len)
#define note_driver_vprintf(driver, tag, fmt, va) \
        note_driver_vprintf_ip(driver, tag, up_getpc(), 0, fmt, va)
#define note_driver_printf(driver, tag, fmt, ...) \
        note_driver_printf_ip(driver, tag, up_getpc(), 0, fmt, ##__VA_ARGS__)

#define note_driver_begin(driver, tag) \
        note_driver_event(driver, tag, NOTE_DUMP_BEGIN, NULL, 0)
#define note_driver_end(driver, tag) \
        note_driver_event(driver, tag, NOTE_DUMP_END, NULL, 0)
#define note_driver_mark(driver, tag, buf) \
        note_driver_event(driver, tag, NOTE_DUMP_MARK, buf, strlen(buf))
#define note_driver_binary(driver, tag, buf, len) \
        note_driver_event(driver, tag, NOTE_DUMP_BINARY, buf, len)

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

/* Overwrite mode definitions */

#define NOTE_MODE_OVERWRITE_DISABLE      0
#define NOTE_MODE_OVERWRITE_ENABLE       1
#define NOTE_MODE_OVERWRITE_OVERFLOW     2

#define NOTE_MODE_READ_ASCII             0
#define NOTE_MODE_READ_BINARY            1

/****************************************************************************
 * Public Types
 ****************************************************************************/

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
#ifdef CONFIG_SMP
  cpu_set_t cpuset;           /* The set of monitored CPUs */
#endif
};

struct note_filter_named_mode_s
{
  char name[NAME_MAX];
  struct note_filter_mode_s mode;
};

/* This is the type of the argument passed to the NOTECTL_GETSYSCALLFILTER
 * and NOTECTL_SETSYSCALLFILTER ioctls
 */

#ifdef CONFIG_SCHED_INSTRUMENTATION_SYSCALL
struct note_filter_syscall_s
{
  uint8_t syscall_mask[(SYS_nsyscalls + 7) / 8];
};

struct note_filter_named_syscall_s
{
  char name[NAME_MAX];
  struct note_filter_syscall_s syscall_mask;
};
#endif

/* This is the type of the argument passed to the NOTECTL_GETIRQFILTER and
 * NOTECTL_SETIRQFILTER ioctls
 */

struct note_filter_irq_s
{
  uint8_t irq_mask[(NR_IRQS + 7) / 8];
};

struct note_filter_named_irq_s
{
  char name[NAME_MAX];
  struct note_filter_irq_s irq_mask;
};

struct note_filter_tag_s
{
  uint8_t tag_mask[(NOTE_TAG_MAX + 7) / 8];
};

struct note_filter_level_s
{
  uint8_t level;
};

struct note_filter_named_tag_s
{
  char name[NAME_MAX];
  struct note_filter_tag_s tag_mask;
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
#  endif
#  ifdef CONFIG_SCHED_INSTRUMENTATION_SYSCALL
  struct note_filter_syscall_s syscall_mask;
#  endif
};

struct note_driver_s
{
#ifdef CONFIG_SCHED_INSTRUMENTATION_FILTER
  FAR const char *name;
  struct note_filter_s filter;
#endif
  FAR const struct note_driver_ops_s *ops;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

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
#endif

#ifdef CONFIG_SCHED_INSTRUMENTATION_DUMP
void note_driver_printf_ip(FAR struct note_driver_s *driver, uint8_t tag,
                           uint8_t level, uintptr_t ip, uint64_t type,
                           FAR const char *fmt, ...) printf_like(6, 7);

void note_driver_vprintf_ip(FAR struct note_driver_s *driver, uint8_t tag,
                            uint8_t level, uintptr_t ip, uint64_t type,
                            FAR const char *fmt,
                            va_list *va) printf_like(6, 0);

size_t note_driver_event_ip(FAR struct note_driver_s *driver, uint8_t tag,
                            uint8_t level, uintptr_t ip, uint8_t event,
                            FAR const void *buf, size_t len);
#else
#  define note_driver_printf_ip(d,t,l,i,p,f,a)
#  define note_driver_vprintf_ip(d,t,l,i,p,f,a)
#  define note_driver_event_ip(d,t,l,i,e,b,n)
#endif

/****************************************************************************
 * Name: note_driver_register
 ****************************************************************************/

int note_driver_register(FAR struct note_driver_s *driver);

/****************************************************************************
 * Name: note_filter_mode
 *
 * Description:
 *   Set and get note filter mode.
 *   (Same as NOTECTL_GETMODE / NOTECTL_SETMODE ioctls)
 *
 * Input Parameters:
 *   oldm - A writable pointer to struct note_filter_mode_s to get current
 *          filter mode
 *          If 0, no data is written.
 *   newm - A read-only pointer to struct note_filter_mode_s which holds the
 *          new filter mode
 *          If 0, the filter mode is not updated.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

#ifdef CONFIG_SCHED_INSTRUMENTATION_FILTER
void note_filter_mode(FAR struct note_filter_named_mode_s *oldm,
                      FAR struct note_filter_named_mode_s *newm);
#endif

/****************************************************************************
 * Name: note_filter_syscall
 *
 * Description:
 *   Set and get syscall filter setting
 *   (Same as NOTECTL_GETSYSCALLFILTER / NOTECTL_SETSYSCALLFILTER ioctls)
 *
 * Input Parameters:
 *   oldf - A writable pointer to struct note_filter_syscall_s to get
 *          current syscall filter setting
 *          If 0, no data is written.
 *   newf - A read-only pointer to struct note_filter_syscall_s of the
 *          new syscall filter setting
 *          If 0, the setting is not updated.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

#if defined(CONFIG_SCHED_INSTRUMENTATION_FILTER) && \
    defined(CONFIG_SCHED_INSTRUMENTATION_SYSCALL)
void note_filter_syscall(FAR struct note_filter_named_syscall_s *oldf,
                         FAR struct note_filter_named_syscall_s *newf);
#endif

/****************************************************************************
 * Name: note_filter_irq
 *
 * Description:
 *   Set and get IRQ filter setting
 *   (Same as NOTECTL_GETIRQFILTER / NOTECTL_SETIRQFILTER ioctls)
 *
 * Input Parameters:
 *   oldf - A writable pointer to struct note_filter_irq_s to get
 *          current IRQ filter setting
 *          If 0, no data is written.
 *   newf - A read-only pointer to struct note_filter_irq_s of the new
 *          IRQ filter setting
 *          If 0, the setting is not updated.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

#if defined(CONFIG_SCHED_INSTRUMENTATION_FILTER) && \
    defined(CONFIG_SCHED_INSTRUMENTATION_IRQHANDLER)
void note_filter_irq(FAR struct note_filter_named_irq_s *oldf,
                     FAR struct note_filter_named_irq_s *newf);
#endif

#if defined(CONFIG_SCHED_INSTRUMENTATION_FILTER) && \
    defined(CONFIG_SCHED_INSTRUMENTATION_DUMP)
void note_filter_tag(FAR struct note_filter_named_tag_s *oldf,
                     FAR struct note_filter_named_tag_s *newf);
#endif
#endif /* __INCLUDE_NUTTX_NOTE_NOTE_DRIVER_H */
