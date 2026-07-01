/****************************************************************************
 * sched/perf/perf_context.c
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
#include <debug.h>
#include <stdbool.h>

#include <nuttx/kmalloc.h>
#include <nuttx/mutex.h>
#include <nuttx/percpu.h>
#include <nuttx/perf.h>
#include <nuttx/sched.h>
#include <nuttx/spinlock.h>

#include "perf.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Per-CPU (system-wide) perf event contexts, used by CPU-bound events that
 * are not tied to any task.  Kept private to this file; the core framework
 * initializes them through perf_init_cpu_contexts().
 */

static DEFINE_PER_CPU_BSS_SMP(struct perf_event_context_s, g_perf_cpu_ctx);

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: perf_init_context
 *
 * Description:
 *   Perf event context initialization
 *
 * Input Parameters:
 *   ctx - Perf event context for init
 *
 * Returned Value:
 *   Initialization result
 *
 ****************************************************************************/

void perf_init_context(FAR struct perf_event_context_s *ctx)
{
  spin_lock_init(&ctx->lock);
  list_initialize(&ctx->event_list);
  list_initialize(&ctx->group_list);
  list_initialize(&ctx->pmu_ctx_list);
}

/****************************************************************************
 * Name: perf_get_context
 *
 * Description:
 *   Find the suitable perf context for the event
 *
 * Input Parameters:
 *   event - Perf event
 *   tcb   - tcb the event bind
 *
 * Returned Value:
 *   Perf context entry
 *
 ****************************************************************************/

FAR struct perf_event_context_s *
perf_get_context(FAR struct perf_event_s *event,
                 FAR struct tcb_s *tcb)
{
  FAR struct perf_event_context_s *ctx = NULL;

  /* No tcb is specified bound to cpu, else bound to task */

  if (tcb == NULL)
    {
      ctx = &per_cpu_var_smp(g_perf_cpu_ctx, event->cpu);
      return ctx;
    }

  nxmutex_lock(&tcb->perf_event_mutex);

  ctx = tcb->perf_event_ctx;
  if (ctx == NULL)
    {
      ctx = kmm_zalloc(sizeof(struct perf_event_context_s));
      if (ctx == NULL)
        {
          serr("task perf event alloc fail\n");
          nxmutex_unlock(&tcb->perf_event_mutex);
          return NULL;
        }

      perf_init_context(ctx);
      tcb->perf_event_ctx = ctx;
      ctx->tcb = tcb;
    }

  nxmutex_unlock(&tcb->perf_event_mutex);
  return ctx;
}

/****************************************************************************
 * Name: perf_free_context
 *
 * Description:
 *   Free the perf event context for the event
 *
 * Input Parameters:
 *   ctx - perf event context
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void perf_free_context(FAR struct perf_event_context_s *ctx)
{
  FAR struct tcb_s *tcb = ctx->tcb;

  if (tcb == NULL)
    {
      return;
    }

  nxmutex_lock(&tcb->perf_event_mutex);

  if (list_is_empty(&ctx->event_list))
    {
      tcb->perf_event_ctx = NULL;
      ASSERT(ctx->active_num == 0);
      ASSERT(list_is_empty(&ctx->event_list));
      ASSERT(list_is_empty(&ctx->group_list));
      ASSERT(list_is_empty(&ctx->pmu_ctx_list));
      kmm_free(ctx);
    }

  nxmutex_unlock(&tcb->perf_event_mutex);
}

/****************************************************************************
 * Name: perf_init_pmu_context
 *
 * Description:
 *   Init pmu context for the event
 *
 * Input Parameters:
 *   pmu_ctx - Pmu context
 *   pmu     - Pmu entry
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void perf_init_pmu_context(FAR struct pmu_event_context_s *pmu_ctx,
                           FAR struct pmu_s *pmu)
{
  pmu_ctx->pmu = pmu;
  list_clear_node(&pmu_ctx->pmu_ctx_node);
  pmu_ctx->refcount = 0;
}

/****************************************************************************
 * Name: perf_get_pmu_context
 *
 * Description:
 *   Find the suitable pmu context for the event
 *
 * Input Parameters:
 *   pmu   - Pmu entry
 *   event - Perf event
 *   tcb   - Tcb the event bind
 *
 * Returned Value:
 *   Pmu context entry
 *
 ****************************************************************************/

FAR struct pmu_event_context_s *
perf_get_pmu_context(FAR struct pmu_s *pmu, FAR struct perf_event_s *event,
                     FAR struct perf_event_context_s *ctx)
{
  FAR struct pmu_event_context_s *pmu_ctx;
  irqstate_t flags;

  /* This event is bound to cpu, get pmu cpu context */

  if (ctx->tcb == NULL)
    {
      pmu_ctx = &pmu->cpu_pmu_ctx[event->cpu].pmuctx;

      flags = spin_lock_irqsave(&ctx->lock);

      if (pmu_ctx->ctx == NULL)
        {
          pmu_ctx->ctx = ctx;
          list_add_tail(&ctx->pmu_ctx_list, &pmu_ctx->pmu_ctx_node);
        }

      pmu_ctx->refcount++;
      goto out;
    }

  /* This event is bound to task, found the task ctx or malloc new one */

  flags = spin_lock_irqsave(&ctx->lock);

  list_for_every_entry(&ctx->pmu_ctx_list, pmu_ctx,
                       struct pmu_event_context_s, pmu_ctx_node)
    {
      if (pmu_ctx->pmu == pmu)
        {
          pmu_ctx->refcount++;
          goto out;
        }
    }

  spin_unlock_irqrestore(&ctx->lock, flags);

  pmu_ctx = kmm_malloc(sizeof(struct pmu_event_context_s));
  if (pmu_ctx == NULL)
    {
      serr("malloc pmu_event_context_s fail\n");
      return NULL;
    }

  perf_init_pmu_context(pmu_ctx, pmu);

  flags = spin_lock_irqsave(&ctx->lock);

  pmu_ctx->ctx = ctx;
  pmu_ctx->refcount++;
  list_add_tail(&ctx->pmu_ctx_list, &pmu_ctx->pmu_ctx_node);

out:
  spin_unlock_irqrestore(&ctx->lock, flags);
  return pmu_ctx;
}

/****************************************************************************
 * Name: perf_free_pmu_context
 *
 * Description:
 *   Free the pmu context for the event
 *
 * Input Parameters:
 *   pmu_ctx - PMU context
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void perf_free_pmu_context(FAR struct pmu_event_context_s *pmu_ctx)
{
  bool free_flag = false;
  irqstate_t flags;

  flags = spin_lock_irqsave(&pmu_ctx->ctx->lock);

  pmu_ctx->refcount--;

  if (pmu_ctx->refcount == 0)
    {
      list_delete(&pmu_ctx->pmu_ctx_node);

      if (pmu_ctx->ctx->tcb != NULL)
        {
          free_flag = true;
        }
    }

  spin_unlock_irqrestore(&pmu_ctx->ctx->lock, flags);

  if (pmu_ctx->refcount == 0)
    {
      pmu_ctx->ctx = NULL;
    }

  if (free_flag)
    {
      kmm_free(pmu_ctx);
    }
}

/****************************************************************************
 * Name: perf_init_cpu_contexts
 *
 * Description:
 *   Initialize the per-CPU (system-wide) perf event contexts.  Called once
 *   from perf_event_init() in the core framework.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void perf_init_cpu_contexts(void)
{
  int i;

  for (i = 0; i < CONFIG_SMP_NCPUS; i++)
    {
      perf_init_context(&per_cpu_var_smp(g_perf_cpu_ctx, i));
    }
}
