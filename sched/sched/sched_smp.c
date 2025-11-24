/****************************************************************************
 * sched/sched/sched_smp.c
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

#include <assert.h>
#include <nuttx/arch.h>
#include <nuttx/nuttx.h>
#include <nuttx/queue.h>
#include <nuttx/semaphore.h>
#include <nuttx/sched.h>
#include <nuttx/spinlock.h>
#include <nuttx/mm/mempool.h>

#include "sched/sched.h"

/****************************************************************************
 * Private Predefinitions
 ****************************************************************************/

#ifndef CONFIG_SMP_CALL_POOL_COUNT
#  define CONFIG_SMP_CALL_POOL_COUNT 0
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct smp_call_cookie_s
{
  struct smp_call_data_s req;
  sem_t  sem;
  int    error;
#if CONFIG_SMP_CALL_POOL_COUNT > 0
  /* Use prealloc smp_call cookie */

  int    cpu;
  struct smp_call_data_s ack;
#endif
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static sq_queue_t g_smp_call_queue[CONFIG_NCPUS];
static spinlock_t g_smp_call_lock = SP_UNLOCKED;

#if CONFIG_SMP_CALL_POOL_COUNT > 0
MEMPOOL_DEFINE(g_smp_call_pool, sizeof(struct smp_call_cookie_s),
               CONFIG_SMP_CALL_POOL_COUNT, CONFIG_SMP_CALL_POOL_COUNT, 0);
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: nxsched_smp_call_add
 *
 * Description:
 *   Add call data to other processors
 *
 * Input Parameters:
 *   cpu        - Target cpu id
 *   data  - Call data
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void nxsched_smp_call_add(int cpu,
                                 FAR struct smp_call_data_s *data)
{
  FAR sq_queue_t *call_queue = &g_smp_call_queue[cpu];
  irqstate_t flags;

  flags = spin_lock_irqsave(&g_smp_call_lock);
  if (!sq_inqueue(&data->node[cpu], call_queue))
    {
      sq_addlast(&data->node[cpu], call_queue);
    }

  spin_unlock_irqrestore(&g_smp_call_lock, flags);
}

/****************************************************************************
 * Name: nxsched_smp_call_delay_cb
 *
 * Description:
 *   The delay finished callback, do phase 2 call other processors
 *
 * Input Parameters:
 *   arg  - Delay Call Instance data
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void nxsched_smp_call_delay_cb(wdparm_t arg)
{
  FAR struct smp_call_delay_data_s *data;
  data = (FAR struct smp_call_delay_data_s *)arg;
  nxsched_smp_call_async(data->cpuset, &data->call);
}

#if CONFIG_SMP_CALL_POOL_COUNT > 0
static int nxsched_smp_call_ack_cb(FAR void *arg)
{
  FAR struct smp_call_cookie_s *cookie = arg;
  sem_post(&cookie->sem);
  return OK;
}

static inline_function
FAR struct smp_call_cookie_s *nxsched_smp_call_cookie_alloc(void)
{
  return mempool_allocate(&g_smp_call_pool, 0);
}
#endif

static inline_function void
nxsched_smp_call_cookie_free(FAR struct smp_call_cookie_s *c)
{
  nxsem_destroy(&c->sem);

#if CONFIG_SMP_CALL_POOL_COUNT > 0
  mempool_release(&g_smp_call_pool, c);
#endif
}

static inline_function void
nxsched_smp_call_cookie_init(FAR struct smp_call_cookie_s *c,
                             nxsched_smp_call_t func, FAR void *arg)
{
  nxsched_smp_call_init(&c->req, func, arg);
  nxsem_init(&c->sem, 0, 0);
  c->req.cookie = c;
  c->error = 0;
#if CONFIG_SMP_CALL_POOL_COUNT > 0
  c->cpu = this_cpu();
  nxsched_smp_call_init(&c->ack, nxsched_smp_call_ack_cb, c);
#endif
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: nxsched_smp_call_handler
 *
 * Description:
 *   SMP function call handler
 *
 * Input Parameters:
 *   irq     - Interrupt id
 *   context - Regs context before irq
 *   arg     - Interrupt arg
 *
 * Returned Value:
 *   Result
 *
 ****************************************************************************/

int nxsched_smp_call_handler(int irq, FAR void *context,
                             FAR void *arg)
{
  FAR sq_queue_t *call_queue;
  FAR sq_entry_t *curr;
  FAR sq_entry_t *next;
  int cpu = this_cpu();

  irqstate_t flags = spin_lock_irqsave(&g_smp_call_lock);

  call_queue = &g_smp_call_queue[cpu];

  sq_for_every_safe(call_queue, curr, next)
    {
      FAR struct smp_call_data_s *data =
        container_of(curr, struct smp_call_data_s, node[cpu]);
      FAR struct smp_call_cookie_s *cookie;
      int ret;

      sq_rem(&data->node[cpu], call_queue);

      spin_unlock_irqrestore(&g_smp_call_lock, flags);

      ret = data->func(data->arg);

      flags = spin_lock_irqsave(&g_smp_call_lock);

      cookie = data->cookie;
      if (cookie != NULL)
        {
          if (ret < 0)
            {
              cookie->error = ret;
            }

          /* Insert ack back to original cpu call queue,
           * add direct avoid nxsched_smp_call_add g_smp_call_lock again.
           */

#if CONFIG_SMP_CALL_POOL_COUNT > 0
          sq_addlast(&cookie->ack.node[cookie->cpu],
                     &g_smp_call_queue[cookie->cpu]);
          up_send_smp_call(1 << cookie->cpu);
#else
          nxsem_post(&cookie->sem);
#endif
        }
    }

  spin_unlock_irqrestore(&g_smp_call_lock, flags);
  return OK;
}

/****************************************************************************
 * Name: nxsched_smp_call_init
 *
 * Description:
 *   Init call_data
 *
 * Input Parameters:
 *   data - Call data
 *   func - Function
 *   arg  - Function args
 *
 * Returned Value:
 *   Result
 *
 ****************************************************************************/

void nxsched_smp_call_init(FAR struct smp_call_data_s *data,
                           nxsched_smp_call_t func, FAR void *arg)
{
  DEBUGASSERT(data != NULL && func != NULL);

  memset(data, 0, sizeof(struct smp_call_data_s));
  data->func = func;
  data->arg = arg;
}

/****************************************************************************
 * Name: nxsched_smp_call_single
 *
 * Description:
 *   Call function on single processor, wait function callback
 *
 * Input Parameters:
 *   cpuid - Target cpu id
 *   func  - Function
 *   arg   - Function args
 *
 * Returned Value:
 *   Result
 *
 ****************************************************************************/

int nxsched_smp_call_single(int cpuid, nxsched_smp_call_t func,
                            FAR void *arg)
{
  cpu_set_t cpuset;

  CPU_ZERO(&cpuset);
  CPU_SET(cpuid, &cpuset);
  return nxsched_smp_call(cpuset, func, arg);
}

/****************************************************************************
 * Name: nxsched_smp_call
 *
 * Description:
 *   Call function on multi processors, wait function callback
 *
 * Input Parameters:
 *   cpuset - Target cpuset
 *   func   - Function
 *   arg    - Function args
 *
 * Returned Value:
 *   Result
 *
 ****************************************************************************/

int nxsched_smp_call(cpu_set_t cpuset, nxsched_smp_call_t func,
                     FAR void *arg)
{
  FAR struct smp_call_cookie_s *c;
  int semcount;
  int cpucnt;
  int ret = OK;
  int i;

#if CONFIG_SMP_CALL_POOL_COUNT > 0
  c = nxsched_smp_call_cookie_alloc();
#else
  struct smp_call_cookie_s cookie;

  c = &cookie;
#endif

  nxsched_smp_call_cookie_init(c, func, arg);

  ret = nxsched_smp_call_async(cpuset, &c->req);

  if (ret < 0)
    {
      nxsem_destroy(&c->sem);
      return ret;
    }

  cpucnt = CPU_COUNT(&cpuset);
  if (up_interrupt_context())
    {
      /* Wait until completion. */

      do
        {
          nxsem_get_value(&c->sem, &semcount);
        }
      while (semcount != cpucnt);
    }
  else
    {
      for (i = 0; i < cpucnt; i++)
        {
          int rc = nxsem_wait_uninterruptible(&c->sem);
          if (rc < 0)
            {
              ret = rc;
            }
        }
    }

  if (c->error < 0)
    {
      ret = c->error;
    }

  nxsched_smp_call_cookie_free(c);
  return ret;
}

/****************************************************************************
 * Name: nxsched_smp_call_single_async
 *
 * Description:
 *   Call function on single processor async
 *
 * Input Parameters:
 *   cpuset - Target cpuset
 *   data   - Call data
 *
 * Returned Value:
 *   Result
 *
 ****************************************************************************/

int nxsched_smp_call_single_async(int cpuid,
                                  FAR struct smp_call_data_s *data)
{
  cpu_set_t cpuset;

  CPU_ZERO(&cpuset);
  CPU_SET(cpuid, &cpuset);
  return nxsched_smp_call_async(cpuset, data);
}

/****************************************************************************
 * Name: nxsched_smp_call_single_delay
 *
 * Description:
 *   Call function on single processor async after ticks delay
 *
 * Input Parameters:
 *   cpuset - Target cpuset
 *   delay  - Delay ticks
 *   data   - Call data
 *
 * Returned Value:
 *   Result
 *
 ****************************************************************************/

int nxsched_smp_call_single_delay(int cpuid, sclock_t delay,
                                  FAR struct smp_call_delay_data_s *data)
{
  cpu_set_t cpuset;

  CPU_ZERO(&cpuset);
  CPU_SET(cpuid, &cpuset);
  return nxsched_smp_call_delay(cpuset, delay, data);
}

/****************************************************************************
 * Name: nxsched_smp_call_async
 *
 * Description:
 *   Call function on multi processors async
 *
 * Input Parameters:
 *   cpuset - Target cpuset
 *   data   - Call data
 *
 * Returned Value:
 *   Result
 *
 ****************************************************************************/

int nxsched_smp_call_async(cpu_set_t cpuset,
                           FAR struct smp_call_data_s *data)
{
  int cpucnt;
  int ret = OK;
  int i;

  /* Prevent reschedule on another processor */

  if (!up_interrupt_context())
    {
      sched_lock();
    }

  if (CPU_ISSET(this_cpu(), &cpuset))
    {
      ret = data->func(data->arg);
      if (data->cookie != NULL)
        {
          data->cookie->error = ret;
          nxsem_post(&data->cookie->sem);
          if (ret < 0)
            {
              goto out;
            }
        }

      CPU_CLR(this_cpu(), &cpuset);
    }

  cpucnt = CPU_COUNT(&cpuset);
  if (cpucnt == 0)
    {
      goto out;
    }

  for (i = 0; i < CONFIG_NCPUS; i++)
    {
      if (CPU_ISSET(i, &cpuset))
        {
          nxsched_smp_call_add(i, data);
          if (--cpucnt == 0)
            {
              break;
            }
        }
    }

  up_send_smp_call(cpuset);

out:
  if (!up_interrupt_context())
    {
      sched_unlock();
    }

  return ret;
}

/****************************************************************************
 * Name: nxsched_smp_call_delay
 *
 * Description:
 *   Call function on multi processors async after ticks delay
 *
 * Input Parameters:
 *   cpuset - Target cpuset
 *   delay  - Delay ticks
 *   data   - Call data
 *
 * Returned Value:
 *   Result
 *
 ****************************************************************************/

int nxsched_smp_call_delay(cpu_set_t cpuset, sclock_t delay,
                           FAR struct smp_call_delay_data_s *data)
{
  data->cpuset = cpuset;
  return wd_start(&data->delay, delay, nxsched_smp_call_delay_cb,
                  (wdparm_t)data);
}
