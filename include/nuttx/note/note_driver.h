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

#ifdef CONFIG_SCHED_INSTRUMENTATION_SYSCALL
#  ifdef CONFIG_LIB_SYSCALL
#    include <syscall.h>
#  else
#    define CONFIG_LIB_SYSCALL
#    include <syscall.h>
#    undef CONFIG_LIB_SYSCALL
#  endif
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Provide defaults for some configuration settings (could be undefined with
 * old configuration files)
 */

#ifndef CONFIG_SCHED_INSTRUMENTATION_CPUSET
#  define CONFIG_SCHED_INSTRUMENTATION_CPUSET 0xffff
#endif

/* Note filter mode flag definitions */

#define NOTE_FILTER_MODE_FLAG_ENABLE       (1 << 0) /* Enable instrumentation */
#define NOTE_FILTER_MODE_FLAG_SWITCH       (1 << 1) /* Enable syscall instrumentation */
#define NOTE_FILTER_MODE_FLAG_SYSCALL      (1 << 2) /* Enable syscall instrumentation */
#define NOTE_FILTER_MODE_FLAG_IRQ          (1 << 3) /* Enable IRQ instrumentaiton */
#define NOTE_FILTER_MODE_FLAG_DUMP         (1 << 4) /* Enable dump instrumentaiton */
#define NOTE_FILTER_MODE_FLAG_SYSCALL_ARGS (1 << 5) /* Enable collecting syscall arguments */

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
#else
#  define NOTE_FILTER_TAGMASK_SET(tag, s)
#  define NOTE_FILTER_TAGMASK_CLR(tag, s)
#  define NOTE_FILTER_TAGMASK_ISSET(tag, s) (0)
#  define NOTE_FILTER_TAGMASK_ZERO(s)
#  define NOTE_FILTER_TAGMASK_FILL(s)
#endif

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
                   FAR const void *note, size_t notelen);
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
#  ifdef CONFIG_SCHED_INSTRUMENTATION_SWITCH
  CODE void (*cpu_pause)(FAR struct note_driver_s *drv,
                         FAR struct tcb_s *tcb, int cpu);
  CODE void (*cpu_paused)(FAR struct note_driver_s *drv,
                          FAR struct tcb_s *tcb);
  CODE void (*cpu_resume)(FAR struct note_driver_s *drv,
                          FAR struct tcb_s *tcb, int cpu);
  CODE void (*cpu_resumed)(FAR struct note_driver_s *drv,
                           FAR struct tcb_s *tcb);
#  endif
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
  CODE void (*event)(FAR struct note_driver_s *drv, uintptr_t ip,
                     uint8_t event, FAR const void *buf, size_t len);
  CODE void (*vprintf)(FAR struct note_driver_s *drv, uintptr_t ip,
                       FAR const char *fmt, va_list va) printf_like(3, 0);
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
  unsigned int flag;          /* Filter mode flag */
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

struct note_filter_named_tag_s
{
  char name[NAME_MAX];
  struct note_filter_tag_s tag_mask;
};

#ifdef CONFIG_SCHED_INSTRUMENTATION_FILTER
struct note_filter_s
{
  struct note_filter_mode_s mode;
#  ifdef CONFIG_SCHED_INSTRUMENTATION_DUMP
  struct note_filter_tag_s tag_mask;
#  endif
#  ifdef CONFIG_SCHED_INSTRUMENTATION_IRQHANDLER
  struct note_filter_irq_s irq_mask;
#  endif
#  ifdef CONFIG_SCHED_INSTRUMENTATION_SYSCALL
  struct note_filter_syscall_s syscall_mask;
#  endif
};
#endif

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

#if defined(__KERNEL__) || defined(CONFIG_BUILD_FLAT)

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

#endif /* defined(__KERNEL__) || defined(CONFIG_BUILD_FLAT) */

/****************************************************************************
 * Name: note_get_taskname
 *
 * Description:
 *   Get the task name string of the specified PID
 *
 * Input Parameters:
 *   PID - Task ID
 *   buf - A writable buffer to hold the task name
 *   len - The length of the buffer
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void note_get_taskname(pid_t pid, FAR char *buf, size_t len);

/****************************************************************************
 * Name: note_driver_register
 ****************************************************************************/

int note_driver_register(FAR struct note_driver_s *driver);

#if defined(__KERNEL__) || defined(CONFIG_BUILD_FLAT)

/****************************************************************************
 * Name: sched_note_filter_mode
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
void sched_note_filter_mode(FAR struct note_filter_named_mode_s *oldm,
                            FAR struct note_filter_named_mode_s *newm);
#endif

/****************************************************************************
 * Name: sched_note_filter_syscall
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
void sched_note_filter_syscall(FAR struct note_filter_named_syscall_s *oldf,
                               FAR struct note_filter_named_syscall_s *newf);
#endif

/****************************************************************************
 * Name: sched_note_filter_irq
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
void sched_note_filter_irq(FAR struct note_filter_named_irq_s *oldf,
                           FAR struct note_filter_named_irq_s *newf);
#endif

#if defined(CONFIG_SCHED_INSTRUMENTATION_FILTER) && \
    defined(CONFIG_SCHED_INSTRUMENTATION_DUMP)
void sched_note_filter_tag(FAR struct note_filter_named_tag_s *oldf,
                           FAR struct note_filter_named_tag_s *newf);
#endif

#endif /* defined(__KERNEL__) || defined(CONFIG_BUILD_FLAT) */

#endif /* __INCLUDE_NUTTX_NOTE_NOTE_DRIVER_H */
