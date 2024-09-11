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
#include <nuttx/kthread.h>

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
  xcpt_t isrthread;   /* Address of the interrupt thread handler */
  FAR void *arg;      /* The argument provided to the interrupt handler. */
  sem_t sem;          /* irq sem used to notify irq thread */
  pid_t threadid;     /* irq threadid */
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct irq_thread_info_s g_irq_thread_vector[NR_IRQS];

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* Default interrupt handler for threaded interrupts.
 * Useful for oneshot interrupts.
 */

static int irq_default_handler(int irq, FAR void *regs, FAR void *arg)
{
  int ret = IRQ_WAKE_THREAD;
  int ndx = IRQ_TO_NDX(irq);
  xcpt_t vector;

  if (ndx < 0)
    {
      return ndx;
    }

  vector = g_irq_thread_vector[ndx].handler;

  if (vector)
    {
      ret = vector(irq, regs, arg);
    }

  if (ret == IRQ_WAKE_THREAD)
    {
      sem_post(&g_irq_thread_vector[ndx].sem);
      ret = OK;
    }

  return ret;
}

static int isr_thread_main(int argc, FAR char *argv[])
{
  unsigned int irq = atoi(argv[1]);
  int ndx = IRQ_TO_NDX(irq);
  xcpt_t vector;
  FAR void *arg;
  FAR sem_t *sem;

  if (ndx < 0)
    {
      return ndx;
    }

  vector = g_irq_thread_vector[ndx].isrthread;
  sem = &g_irq_thread_vector[ndx].sem;
  arg = g_irq_thread_vector[ndx].arg;

  for (; ; )
    {
      int ret;
      ret = nxsem_wait(sem);
      if (ret < 0)
        {
          continue;
        }

      vector(irq, NULL, arg);
    }

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: irq_attach_thread
 *
 * Description:
 *   Configure the IRQ subsystem so that IRQ number 'irq' is dispatched to
 *   'isrthread'
 *
 * Input Parameters:
 *   irq - Irq num
 *   isr - Function to be called when the IRQ occurs, called in interrupt
 *   context.
 *   If isr is NULL the default handler is installed(irq_default_handler).
 *   isrthread - called in thread context, If the isrthread is NULL,
 *   then the ISR is being detached.
 *   arg - privdate data
 *   priority   - Priority of the new task
 *   stack_size - size (in bytes) of the stack needed
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

int irq_attach_thread(int irq, xcpt_t isr, xcpt_t isrthread, FAR void *arg,
                      int priority, int stack_size)
{
#if NR_IRQS > 0
  FAR char *argv[2];
  char arg1[32];
  pid_t pid;
  int ndx;

  if ((unsigned)irq >= NR_IRQS)
    {
      return -EINVAL;
    }

  ndx = IRQ_TO_NDX(irq);
  if (ndx < 0)
    {
      return ndx;
    }

  /* If the isrthread is NULL, then the ISR is being detached. */

  if (isrthread == NULL)
    {
      irq_detach(irq);
      DEBUGASSERT(g_irq_thread_vector[ndx].threadid != 0);

      kthread_delete(g_irq_thread_vector[ndx].threadid);

      g_irq_thread_vector[ndx].isrthread = NULL;
      g_irq_thread_vector[ndx].threadid  = 0;
      g_irq_thread_vector[ndx].handler   = NULL;
      g_irq_thread_vector[ndx].arg       = NULL;
      nxsem_destroy(&g_irq_thread_vector[ndx].sem);
      return OK;
    }

  if (g_irq_thread_vector[ndx].threadid != 0)
    {
      return -EINVAL;
    }

  g_irq_thread_vector[ndx].isrthread = isrthread;
  g_irq_thread_vector[ndx].handler   = isr;
  g_irq_thread_vector[ndx].arg       = arg;

  nxsem_init(&g_irq_thread_vector[ndx].sem, 0, 0);
  snprintf(arg1, sizeof(arg1), "%d", irq);
  argv[0] = arg1;
  argv[1] = NULL;
  pid = kthread_create("isr_thread", priority, stack_size,
                        isr_thread_main, argv);
  if (pid < 0)
    {
      return pid;
    }

  g_irq_thread_vector[ndx].threadid = pid;
  irq_attach(irq, irq_default_handler, arg);
#endif /* NR_IRQS */

  return OK;
}
