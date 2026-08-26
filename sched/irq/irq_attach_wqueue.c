/****************************************************************************
 * sched/irq/irq_attach_wqueue.c
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

#include <nuttx/config.h>

#include <debug.h>
#include <errno.h>
#include <stdio.h>

#include <nuttx/irq.h>
#include <nuttx/wqueue.h>
#include <nuttx/tls.h>
#include <nuttx/mutex.h>

#include "irq/irq.h"
#include "sched/sched.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* This is the type of the list of interrupt handlers, one for each IRQ.
 * This type provided all of the information necessary to irq_dispatch to
 * transfer control to interrupt handlers after the occurrence of an
 * interrupt.
 */

struct irq_work_info_s
{
  xcpt_t handler;     /* Address of the interrupt handler */
  xcpt_t isrwork;     /* Address of the interrupt worked handler */
  FAR void *arg;      /* The argument provided to the interrupt handler. */
  int irq;            /* Irq id */
  struct work_s work; /* Interrupt work to the wq */

  FAR struct kwork_wqueue_s *wqueue;   /* Work queue. */
};

typedef FAR struct kwork_wqueue_s *irq_nworkq_t[CONFIG_IRQ_NWORKS];

/****************************************************************************
 * Private Data
 ****************************************************************************/

#ifdef CONFIG_ARCH_MINIMAL_VECTORTABLE
static DEFINE_PER_CPU_BSS_BMP(struct irq_work_info_s,
g_irq_work_vector[CONFIG_ARCH_NUSER_INTERRUPTS]);
#else
static DEFINE_PER_CPU_BSS_BMP(struct irq_work_info_s,
g_irq_work_vector[NR_IRQS]);
#endif
#define g_irq_work_vector this_cpu_var_bmp(g_irq_work_vector)

#if CONFIG_IRQ_WORK_STACKSIZE > 0
typedef uint8_t irq_work_stack_t[CONFIG_IRQ_WORK_STACKSIZE];
typedef irq_work_stack_t irq_nwork_stack_t[CONFIG_IRQ_NWORKS];

static aligned_data(STACK_ALIGNMENT)
  DEFINE_PER_CPU_BSS_BMP(irq_nwork_stack_t, g_irq_work_stack)
#  ifdef IRQ_WORK_SECTION
  locate_data(CONFIG_IRQ_WORK_SECTION)
#  endif
  ;
#  define g_irq_work_stack this_cpu_var_bmp(g_irq_work_stack)
#endif /* CONFIG_IRQ_WORK_STACKSIZE > 0 */

static
DEFINE_PER_CPU_BMP(mutex_t, g_irq_wqueue_lock) = NXMUTEX_INITIALIZER;
static DEFINE_PER_CPU_BSS_BMP(irq_nworkq_t, g_irq_wqueue);

#define g_irq_wqueue_lock this_cpu_var_bmp(g_irq_wqueue_lock)
#define g_irq_wqueue this_cpu_var_bmp(g_irq_wqueue)

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: irq_get_wqueue
 *
 * Description:
 *   Get or create a work queue with the specified priority and return it
 *   through 'wqueue'. Work queues are shared per priority: if a work queue
 *   with the same priority already exists, it is reused. Otherwise a new
 *   one is created. When stack_addr is NULL, the static per-CPU stack pool
 *   is used; otherwise the caller-provided stack buffer is used.
 *
 *   Because queues are shared per priority, a caller-provided stack can
 *   only take effect when a new queue is created. If a queue with the same
 *   priority already exists, the caller-provided stack cannot be honored;
 *   in that case -EEXIST is returned and the caller retains ownership of
 *   the stack buffer (it is not referenced by the work queue).
 *
 * Input Parameters:
 *   priority   - Work queue thread priority
 *   stack_addr - Caller-provided stack buffer, or NULL to use the static
 *                per-CPU stack pool
 *   stack_size - Size of the stack buffer in bytes (ignored when
 *                stack_addr is NULL)
 *   wqueue     - Location to return the work queue on success
 *
 * Returned Value:
 *   Zero (OK) on success. On failure a negated errno value:
 *     -EINVAL - stack_addr is non-NULL but stack_size is not positive
 *     -EEXIST - a queue with the same priority already exists and the
 *               caller-provided stack cannot be used
 *     -ENOMEM - no free queue slot, no static stack pool available, or
 *               work queue creation failed
 *
 ****************************************************************************/

static int irq_get_wqueue(int priority, FAR void *stack_addr,
                          int stack_size,
                          FAR struct kwork_wqueue_s **wqueue)
{
  FAR struct kwork_wqueue_s *queue;
  int wqueue_priority;
  int ret = OK;
  int i;

  /* A caller-provided stack must have a valid non-zero size. When
   * stack_addr is NULL, stack_size is don't-care (the static pool size
   * is used, or the request is rejected when no pool is compiled in).
   */

  if (stack_addr != NULL && stack_size <= 0)
    {
      return -EINVAL;
    }

  nxmutex_lock(&g_irq_wqueue_lock);

  for (i = 0; i < CONFIG_IRQ_NWORKS && g_irq_wqueue[i] != NULL; i++)
    {
      wqueue_priority = work_queue_priority_wq(g_irq_wqueue[i]);
      DEBUGASSERT(wqueue_priority >= SCHED_PRIORITY_MIN &&
                  wqueue_priority <= SCHED_PRIORITY_MAX);

      if (wqueue_priority == priority)
        {
          /* A queue with this priority already exists. It can be reused
           * only when the caller does not require its own stack, since
           * the queue's stack was fixed at creation time.
           */

          if (stack_addr != NULL)
            {
              ret = -EEXIST;
            }
          else
            {
              *wqueue = g_irq_wqueue[i];
            }

          nxmutex_unlock(&g_irq_wqueue_lock);
          return ret;
        }
    }

  if (i >= CONFIG_IRQ_NWORKS)
    {
      nxmutex_unlock(&g_irq_wqueue_lock);
      return -ENOMEM;
    }

  if (stack_addr == NULL)
    {
#if CONFIG_IRQ_WORK_STACKSIZE > 0
      stack_addr = g_irq_work_stack[i];
      stack_size = CONFIG_IRQ_WORK_STACKSIZE;
#else
      nxmutex_unlock(&g_irq_wqueue_lock);
      return -ENOMEM;
#endif
    }

  queue = work_queue_create("isrwork", priority,
                            stack_addr, stack_size, 1u);
  if (queue == NULL)
    {
      nxmutex_unlock(&g_irq_wqueue_lock);
      return -ENOMEM;
    }

  g_irq_wqueue[i] = queue;
  *wqueue = queue;

  nxmutex_unlock(&g_irq_wqueue_lock);
  return OK;
}

/* Default interrupt handler for worked interrupts.
 * Useful for oneshot interrupts.
 */

static void irq_work_handler(FAR void *arg)
{
  FAR struct irq_work_info_s *info = arg;

  info->isrwork(info->irq, NULL, info->arg);
}

static int irq_default_handler(int irq, FAR void *context, FAR void *arg)
{
  FAR struct irq_work_info_s *info = arg;
  int ret = IRQ_WAKE_THREAD;

  if (info->handler)
    {
      ret = info->handler(irq, context, info->arg);
    }

  if (ret == IRQ_WAKE_THREAD)
    {
      work_queue_wq(info->wqueue, &info->work, irq_work_handler, info, 0u);
      ret = OK;
    }

  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: irq_attach_wqueue_with_stack
 *
 * Description:
 *   Configure the IRQ subsystem so that IRQ number 'irq' is dispatched to
 *   a work queue thread. If a work queue with the same priority already
 *   exists, it will be reused. Otherwise, a new work queue will be created.
 *   When stack_addr is NULL, the static per-CPU stack pool is used.
 *   Otherwise, the caller-provided stack buffer is used.
 *
 * Input Parameters:
 *   irq        - Irq num
 *   isr        - Function to be called when the IRQ occurs (interrupt ctx).
 *                If NULL the default handler is installed.
 *   isrwork    - Called in thread context. If NULL, the ISR is detached.
 *   arg        - Private data
 *   priority   - isrwork priority
 *   stack_addr - Stack buffer provided by the caller, or NULL to use
 *                the static per-CPU stack pool
 *   stack_size - Size of the stack buffer in bytes (ignored when
 *                stack_addr is NULL)
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

int irq_attach_wqueue_with_stack(int irq, xcpt_t isr, xcpt_t isrwork,
                                 FAR void *arg, int priority,
                                 FAR void *stack_addr, int stack_size)
{
  FAR struct irq_work_info_s *info;
  int ret = OK;
#if NR_IRQS > 0
  int ndx = IRQ_TO_NDX(irq);
  if (ndx < 0)
    {
      ret = ndx;
    }
  else
    {
      info = &g_irq_work_vector[ndx];

      if (isrwork == NULL)
        {
          irq_detach(irq);
          info->isrwork = NULL;
          info->handler = NULL;
          info->arg     = NULL;
          info->wqueue  = NULL;
        }
      else
        {
          info->isrwork = isrwork;
          info->handler = isr;
          info->arg     = arg;
          info->irq     = irq;
          if (info->wqueue == NULL)
            {
              ret = irq_get_wqueue(priority, stack_addr, stack_size,
                                   &info->wqueue);
            }

          if (ret >= 0)
            {
              irq_attach(irq, irq_default_handler, info);
            }
        }
    }
#endif /* NR_IRQS */

  return ret;
}

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

int irq_sched_wqueue(int irq, clock_t delay)
{
  int ret = OK;
  FAR struct irq_work_info_s *info;
#if NR_IRQS > 0
  int ndx = IRQ_TO_NDX(irq);
  if (ndx < 0)
    {
      ret = ndx;
    }
  else
    {
      info = &g_irq_work_vector[ndx];
      ret = work_queue_wq(info->wqueue, &info->work,
                          irq_work_handler, info, delay);
    }
#endif /* NR_IRQS */

  return ret;
}
