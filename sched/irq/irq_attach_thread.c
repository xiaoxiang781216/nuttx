/****************************************************************************
 * sched/irq/irq_attach_thread.c
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

#include <errno.h>
#include <stdio.h>

#include <nuttx/irq.h>
#include <nuttx/arch.h>
#include <nuttx/kthread.h>
#include <nuttx/semaphore.h>

#include "irq/irq.h"
#include "sched/sched.h"

/****************************************************************************
 * Privte Types
 ****************************************************************************/

/* This is the type of the list of interrupt handlers, one for each IRQ.
 * This type provided all of the information necessary to irq_dispatch to
 * transfer control to interrupt handlers after the occurrence of an
 * interrupt.
 */

struct irq_thread_info_s
{
  xcpt_t handler;     /* Address of the interrupt handler */
  FAR void *arg;      /* The argument provided to the interrupt handler. */
  FAR sem_t *sem;     /* irq sem used to notify irq thread */
};

typedef pid_t irq_thread_pid_t[NR_IRQS];

/****************************************************************************
 * Private Data
 ****************************************************************************/

#if NR_IRQS > 0
static DEFINE_PER_CPU_BSS_BMP(irq_thread_pid_t, g_irq_thread_pid);
#  define g_irq_thread_pid this_cpu_var_bmp(g_irq_thread_pid)
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* Default interrupt handler for threaded interrupts.
 * Useful for oneshot interrupts.
 */

static int irq_thread_default_handler(int irq, FAR void *context,
                                      FAR void *arg)
{
  FAR struct irq_thread_info_s *info = arg;
  int ret = IRQ_WAKE_THREAD;

  if (info->handler)
    {
      ret = info->handler(irq, context, info->arg);
    }

  if (ret == IRQ_WAKE_THREAD)
    {
      nxsem_post(info->sem);
      ret = OK;
    }

  return ret;
}

static int isr_thread_main(int argc, FAR char *argv[])
{
  int irq = atoi(argv[1]);
  xcpt_t isr = (xcpt_t)((uintptr_t)strtoul(argv[2], NULL, 16));
  xcpt_t isrthread = (xcpt_t)((uintptr_t)strtoul(argv[3], NULL, 16));
  FAR void *arg = (FAR char *)((uintptr_t)strtoul(argv[4], NULL, 16));
  struct irq_thread_info_s info;
  sem_t sem;

  info.sem = &sem;
  info.arg = arg;
  info.handler = isr;

  nxsem_init(&sem, 0, 0u);

  irq_attach(irq, irq_thread_default_handler, &info);

#if !defined(CONFIG_ARCH_NOINTC)
  up_enable_irq(irq);
#endif

  for (; ; )
    {
      if (nxsem_wait_uninterruptible(&sem) < 0)
        {
          continue;
        }

      isrthread(irq, NULL, arg);
    }

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: irq_attach_thread_with_stack
 *
 * Description:
 *   Configure the IRQ subsystem so that IRQ number 'irq' is dispatched to
 *   'isrthread' and up_enable_irq will be invoked after isrthread started.
 *   If stack_addr is NULL, the thread is created with system heap using
 *   stack_size. Otherwise, the caller-provided stack buffer is used.
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
                                 FAR void *stack_addr, int stack_size)
{
  int ret = OK;
#if NR_IRQS > 0
  FAR char *argv[5];
  char arg1[32];  /* irq */
  char arg2[32];  /* isr */
  char arg3[32];  /* isrthread */
  char arg4[32];  /* arg */
  pid_t pid;
  int ndx = IRQ_TO_NDX(irq);
  if (ndx < 0)
    {
      ret = ndx;
    }
  else if (isrthread == NULL)
    {
      /* If the isrthread is NULL, then the ISR is being detached. */

      irq_detach(irq);
      DEBUGASSERT(g_irq_thread_pid[ndx] != 0);
      kthread_delete(g_irq_thread_pid[ndx]);
      g_irq_thread_pid[ndx] = 0;
    }
  else if (g_irq_thread_pid[ndx] != 0)
    {
      ret = -EINVAL;
    }
  else if (stack_addr != NULL && stack_size <= 0)
    {
      /* A caller-provided stack must have a valid non-zero size. */

      ret = -EINVAL;
    }
  else
    {
      snprintf(arg1, sizeof(arg1), "%d", irq);
      snprintf(arg2, sizeof(arg2), "%p", isr);
      snprintf(arg3, sizeof(arg3), "%p", isrthread);
      snprintf(arg4, sizeof(arg4), "%p", arg);
      argv[0] = arg1;
      argv[1] = arg2;
      argv[2] = arg3;
      argv[3] = arg4;
      argv[4] = NULL;

      if (stack_addr != NULL)
        {
          pid = kthread_create_with_stack("isr_thread", priority,
                                          stack_addr, stack_size,
                                          isr_thread_main, argv);
        }
      else
        {
          pid = kthread_create("isr_thread", priority, stack_size,
                                isr_thread_main, argv);
        }

      if (pid < 0)
        {
          ret = pid;
        }
      else
        {
          g_irq_thread_pid[ndx] = pid;
        }
    }
#endif /* NR_IRQS */

  return ret;
}
