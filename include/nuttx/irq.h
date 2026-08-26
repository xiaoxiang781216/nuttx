/****************************************************************************
 * include/nuttx/irq.h
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

#ifndef __INCLUDE_NUTTX_IRQ_H
#define __INCLUDE_NUTTX_IRQ_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifndef __ASSEMBLY__
#  include <stdint.h>
#  include <stdbool.h>
#  include <sys/types.h>
#endif

/* Now include architecture-specific types */

#include <arch/irq.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* IRQ detach is a convenience definition, it detach all the handlers
 * sharing the same IRQ. Detaching an interrupt handler is equivalent to
 * setting a NULL interrupt handler.
 */

#define irq_detach(irq) irq_attach(irq, NULL, NULL)
#define irq_detach_wqueue(irq) irq_attach_wqueue(irq, NULL, NULL, NULL, 0)
#define irq_detach_thread(irq) irq_attach_thread(irq, NULL, NULL, NULL, 0, 0)

/* Maximum/minimum values of IRQ integer types */

#if NR_IRQS <= 256
#  define IRQT_MAX UINT8_MAX
#elif NR_IRQS <= 65536
#  define IRQT_MAX UINT16_MAX
#else
#  define IRQT_MAX UINT32_MAX
#endif

#ifdef CONFIG_ARCH_MINIMAL_VECTORTABLE
#  if CONFIG_ARCH_NUSER_INTERRUPTS <= 256
#    define IRQMAPPED_MAX UINT8_MAX
#  elif CONFIG_ARCH_NUSER_INTERRUPTS <= 65536
#    define IRQMAPPED_MAX UINT16_MAX
#  else
#    define IRQMAPPED_MAX UINT32_MAX
#  endif
#endif

#if defined(CONFIG_ARCH_MINIMAL_VECTORTABLE) && \
    !defined(CONFIG_ARCH_NUSER_INTERRUPTS)
#  error CONFIG_ARCH_NUSER_INTERRUPTS is not defined
#endif

#if defined(CONFIG_ARCH_MINIMAL_VECTORTABLE_DYNAMIC)
#  define IRQ_TO_NDX(irq) \
     ((irq) < 0 || (irq) >= NR_IRQS ? -EINVAL : \
      (g_irqmap[irq] ? g_irqmap[irq] : irq_to_ndx(irq)))
#  define NDX_TO_IRQ(ndx) g_irqrevmap[ndx]
#elif defined(CONFIG_ARCH_MINIMAL_VECTORTABLE)
#  define IRQ_TO_NDX(irq) \
     ((irq) < 0 || (irq) >= NR_IRQS ? -EINVAL : \
      (g_irqmap[(irq)] < CONFIG_ARCH_NUSER_INTERRUPTS ? g_irqmap[(irq)] : -EINVAL))
#  define NDX_TO_IRQ(ndx) ndx_to_irq(ndx)
#else
#  define IRQ_TO_NDX(irq) ((irq) < 0 || (irq) >= NR_IRQS ? -EINVAL : (irq))
#  define NDX_TO_IRQ(ndx) (ndx)
#endif

/* Stack alignment macros */

#ifdef CONFIG_TLS_ALIGNED
#  define STACK_ALIGNMENT        TLS_STACK_ALIGN
#else
#  define STACK_ALIGNMENT        STACKFRAME_ALIGN
#endif

#define STACK_ALIGN_MASK         (STACK_ALIGNMENT - 1)
#define STACK_ALIGN_DOWN(a)      ((a) & ~STACK_ALIGN_MASK)
#define STACK_ALIGN_UP(a)        (((a) + STACK_ALIGN_MASK) & ~STACK_ALIGN_MASK)
#define STACK_IS_ALIGNED(a)      (((uintptr_t)(a) & STACK_ALIGN_MASK) == 0)

#define STACKFRAME_ALIGN_MASK    (STACKFRAME_ALIGN - 1)
#define STACKFRAME_ALIGN_DOWN(a) ((a) & ~STACKFRAME_ALIGN_MASK)
#define STACKFRAME_ALIGN_UP(a)   (((a) + STACKFRAME_ALIGN_MASK) & ~STACKFRAME_ALIGN_MASK)
#define STACKFRAME_IS_ALIGNED(a) (((uintptr_t)(a) & STACKFRAME_ALIGN_MASK) == 0)

/* Aligned size of the kernel stack */

#ifdef CONFIG_ARCH_KERNEL_STACK
#  define ARCH_KERNEL_STACKSIZE STACKFRAME_ALIGN_UP(CONFIG_ARCH_KERNEL_STACKSIZE)
#endif

/* Interrupt was handled by this device */

#define IRQ_HANDLED     0

/* Handler requests to wake the handler thread */

#define IRQ_WAKE_THREAD 1

/* Scheduling monitor */

#ifndef CONFIG_SCHED_CRITMONITOR_MAXTIME_THREAD
#  define CONFIG_SCHED_CRITMONITOR_MAXTIME_THREAD -1
#endif

#ifndef CONFIG_SCHED_CRITMONITOR_MAXTIME_WQUEUE
#  define CONFIG_SCHED_CRITMONITOR_MAXTIME_WQUEUE -1
#endif

#ifndef CONFIG_SCHED_CRITMONITOR_MAXTIME_PREEMPTION
#  define CONFIG_SCHED_CRITMONITOR_MAXTIME_PREEMPTION -1
#endif

#ifndef CONFIG_SCHED_CRITMONITOR_MAXTIME_IRQDISABLE
#  define CONFIG_SCHED_CRITMONITOR_MAXTIME_IRQDISABLE -1
#endif

#ifndef CONFIG_SCHED_CRITMONITOR_MAXTIME_BUSYWAIT
#  define CONFIG_SCHED_CRITMONITOR_MAXTIME_BUSYWAIT -1
#endif

#ifndef CONFIG_SCHED_CRITMONITOR_MAXTIME_IRQ
#  define CONFIG_SCHED_CRITMONITOR_MAXTIME_IRQ -1
#endif

#ifndef CONFIG_SCHED_CRITMONITOR_MAXTIME_WDOG
#  define CONFIG_SCHED_CRITMONITOR_MAXTIME_WDOG -1
#endif

/****************************************************************************
 * Public Types
 ****************************************************************************/

#ifndef __ASSEMBLY__
/* This type is an unsigned integer type large enough to hold the largest
 * IRQ number.
 */

#if NR_IRQS <= 256
typedef uint8_t irq_t;
#elif NR_IRQS <= 65536
typedef uint16_t irq_t;
#else
typedef uint32_t irq_t;
#endif

/* This type is an unsigned integer type large enough to hold the largest
 * mapped vector table index.
 */

#ifdef CONFIG_ARCH_MINIMAL_VECTORTABLE
#if CONFIG_ARCH_NUSER_INTERRUPTS <= 256
typedef uint8_t irq_mapped_t;
#elif CONFIG_ARCH_NUSER_INTERRUPTS <= 65536
typedef uint16_t irq_mapped_t;
#else
typedef uint32_t irq_mapped_t;
#endif
#endif /* CONFIG_ARCH_MINIMAL_VECTORTABLE */

/* This struct defines the form of an interrupt service routine */

typedef CODE int (*xcpt_t)(int irq, FAR void *context, FAR void *arg);
#endif /* __ASSEMBLY__ */

/****************************************************************************
 * Public Data
 ****************************************************************************/

#ifndef __ASSEMBLY__
#ifdef __cplusplus
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

/* This is the interrupt vector mapping table.  This must be provided by
 * architecture specific logic if CONFIG_ARCH_MINIMAL_VECTORTABLE is define
 * in the configuration.
 *
 * REVISIT: Currently declared in sched/irq/irq.h.  This declaration here
 * introduces a circular dependency since it depends on NR_IRQS which is
 * defined in arch/irq.h but arch/irq.h includes nuttx/irq.h and we get
 * here with NR_IRQS undefined.
 */

#if defined(CONFIG_ARCH_MINIMAL_VECTORTABLE_DYNAMIC)
extern irq_mapped_t g_irqmap[];
extern irq_t g_irqrevmap[];
int irq_to_ndx(int irq);
#elif defined(CONFIG_ARCH_MINIMAL_VECTORTABLE)
extern const irq_mapped_t g_irqmap[];
int ndx_to_irq(int ndx);
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: irq_save / irq_restore
 *
 * Description:
 *   Wrapper around up_irq_save/up_irq_restore with IRQ disabled duration
 *   monitoring via nxsched_critmon_irqdisable.
 *
 ****************************************************************************/

#ifndef __ASSEMBLY__
#  if CONFIG_SCHED_CRITMONITOR_MAXTIME_IRQDISABLE >= 0
void nxsched_critmon_irqdisable(bool state, bool force, FAR void *caller);

static inline_function irqstate_t irq_save(void)
{
  irqstate_t flags = up_irq_save();
  nxsched_critmon_irqdisable(true, false, return_address(0));
  return flags;
}

static inline_function void irq_restore(irqstate_t flags)
{
  nxsched_critmon_irqdisable(false, false, return_address(0));
  up_irq_restore(flags);
}
#  else
#    define nxsched_critmon_irqdisable(state, force, caller)
#    define irq_save()          up_irq_save()
#    define irq_restore(flags)  up_irq_restore(flags)
#  endif
#endif /* __ASSEMBLY__ */

/****************************************************************************
 * Name: irq_attach
 *
 * Description:
 *   Configure the IRQ subsystem so that IRQ number 'irq' is dispatched to
 *   'isr' with argument 'arg'
 *
 ****************************************************************************/

int irq_attach(int irq, xcpt_t isr, FAR void *arg);

/****************************************************************************
 * Name: irq_attach_thread_with_stack
 *
 * Description:
 *   Configure the IRQ subsystem so that IRQ number 'irq' is dispatched to
 *   'isrthread'. If stack_addr is NULL, a kernel thread will be created
 *   using system heap with the specified stack_size. Otherwise, the
 *   caller-provided stack buffer is used.
 *
 * Input Parameters:
 *   irq        - Irq num
 *   isr        - Function to be called when the IRQ occurs (interrupt ctx).
 *                If NULL the default handler is installed.
 *   isrthread  - Called in thread context. If NULL, the ISR is detached.
 *   arg        - Private data
 *   priority   - Priority of the new task
 *   stack_addr - Stack buffer provided by the caller, or NULL to use heap
 *   stack_size - Size of the stack buffer in bytes
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

int irq_attach_thread_with_stack(int irq, xcpt_t isr, xcpt_t isrthread,
                                 FAR void *arg, int priority,
                                 FAR void *stack_addr, int stack_size);

#define irq_attach_thread(irq, isr, isrthread, arg, priority, stack_size) \
    irq_attach_thread_with_stack(irq, isr, isrthread, arg, priority, \
                                 NULL, stack_size)

/****************************************************************************
 * Name: irq_attach_wqueue_with_stack
 *
 * Description:
 *   Configure the IRQ subsystem so that IRQ number 'irq' is dispatched to
 *   a work queue thread. Work queues are shared per priority: if a work
 *   queue with the same priority already exists, it will be reused.
 *   Otherwise, a new work queue will be created.
 *   If stack_addr is NULL, the stack for a newly created queue is
 *   allocated from the kernel heap. Otherwise, the caller-provided stack
 *   buffer is used.
 *
 *   Because queues are shared per priority, a caller-provided stack can
 *   only take effect when a new work queue is created. If a queue with the
 *   same priority already exists, the caller-provided stack cannot be
 *   honored and -EEXIST is returned. On any failure the caller retains
 *   ownership of stack_addr and is responsible for releasing it; on
 *   success the buffer is owned by the work queue and must not be released
 *   or reused by the caller.
 *
 * Input Parameters:
 *   irq        - Irq num
 *   isr        - Function to be called when the IRQ occurs (interrupt ctx).
 *                If NULL the default handler is installed.
 *   isrwork    - Called in thread context. If NULL, the ISR is detached.
 *   arg        - Private data
 *   priority   - isrwork priority
 *   stack_addr - Stack buffer provided by the caller, or NULL to allocate
 *                from the kernel heap
 *   stack_size - Size of the stack buffer in bytes (ignored when
 *                stack_addr is NULL)
 *
 * Returned Value:
 *   Zero on success. On failure a negated errno value:
 *     -EINVAL - stack_addr is non-NULL but stack_size is not positive
 *     -EEXIST - a queue with the same priority already exists and the
 *               caller-provided stack cannot be used
 *     -ENOMEM - no free queue slot, heap allocation failed, or work queue
 *               creation failed
 *
 ****************************************************************************/

int irq_attach_wqueue_with_stack(int irq, xcpt_t isr, xcpt_t isrwork,
                                 FAR void *arg, int priority,
                                 FAR void *stack_addr, int stack_size);

#define irq_attach_wqueue(irq, isr, isrwork, arg, priority) \
    irq_attach_wqueue_with_stack(irq, isr, isrwork, arg, priority, NULL, 0)

/****************************************************************************
 * Name: irq_sched_wqueue
 *
 * Description:
 *   Schedule the work handler for an IRQ that was previously attached via
 *   irq_attach_wqueue().  This queues the isrwork callback to the work
 *   queue with the specified delay, causing it to execute in thread
 *   context on the IRQ's designated work queue.
 *
 * Input Parameters:
 *   irq   - The IRQ number that was previously attached.
 *   delay - Delay in clock ticks before the work handler runs.
 *           0 means run as soon as possible.
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

int irq_sched_wqueue(int irq, clock_t delay);

#ifdef CONFIG_IRQCHAIN
int irqchain_detach(int irq, xcpt_t isr, FAR void *arg);
#else
#  define irqchain_detach(irq, isr, arg) irq_detach(irq)
#endif

#undef EXTERN
#ifdef __cplusplus
}
#endif

#endif /* __ASSEMBLY__ */
#endif /* __INCLUDE_NUTTX_IRQ_H */
