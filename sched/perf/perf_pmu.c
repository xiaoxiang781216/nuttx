/****************************************************************************
 * sched/perf/perf_pmu.c
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

#include <nuttx/list.h>
#include <nuttx/mutex.h>
#include <nuttx/perf.h>

#include "perf.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct list_node g_perf_pmus = LIST_INITIAL_VALUE(g_perf_pmus);
static mutex_t g_perf_pmus_lock = NXMUTEX_INITIALIZER;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: perf_get_pmu
 *
 * Description:
 *   Find the suitable pmu from the event
 *
 * Input Parameters:
 *   event - Perf event
 *
 * Returned Value:
 *   Perf pmu entry
 *
 ****************************************************************************/

FAR struct pmu_s *perf_get_pmu(FAR struct perf_event_s *event)
{
  FAR struct pmu_s *pmu;

  if (event->parent_event && event->parent_event->pmu)
    {
      pmu = event->parent_event->pmu;
      event->pmu = pmu;
      if (pmu->ops->event_init(event) == 0)
        {
          return pmu;
        }
    }

  list_for_every_entry(&g_perf_pmus, pmu, struct pmu_s, node)
    {
      if (pmu->ops->event_match && pmu->ops->event_match(event) != 0)
        {
          continue;
        }

      event->pmu = pmu;
      if (pmu->ops->event_init(event) == 0)
        {
          return pmu;
        }
    }

  return NULL;
}

/****************************************************************************
 * Name: perf_pmu_register
 *
 * Description:
 *   Register perf pmu
 *
 * Input Parameters:
 *   pmu  - Pmu entry.
 *   name - Pmu name.
 *
 * Returned Value:
 *   Register result
 *
 ****************************************************************************/

int perf_pmu_register(FAR struct pmu_s *pmu, FAR const char *name)
{
  int i;

  pmu->name = name;

  for (i = 0; i < CONFIG_SMP_NCPUS; i++)
    {
      FAR struct pmu_cpu_context_s *cpu_pmu_ctx = &pmu->cpu_pmu_ctx[i];
      perf_init_pmu_context(&cpu_pmu_ctx->pmuctx, pmu);
    }

  nxmutex_lock(&g_perf_pmus_lock);
  list_add_tail(&g_perf_pmus, &pmu->node);
  nxmutex_unlock(&g_perf_pmus_lock);

  return OK;
}

/****************************************************************************
 * Name: perf_pmu_unregister
 *
 * Description:
 *   Unregister perf pmu
 *
 * Input Parameters:
 *   pmu  - Pmu entry.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void perf_pmu_unregister(FAR struct pmu_s *pmu)
{
  nxmutex_lock(&g_perf_pmus_lock);
  list_delete(&pmu->node);
  nxmutex_unlock(&g_perf_pmus_lock);
}
