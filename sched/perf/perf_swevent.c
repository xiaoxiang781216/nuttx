/****************************************************************************
 * sched/perf/perf_swevent.c
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
#include <nuttx/nuttx.h>

#include <errno.h>

#include <nuttx/atomic.h>
#include <nuttx/hrtimer.h>
#include <nuttx/perf.h>
#include <nuttx/sched.h>

#include "perf.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct swevent_manger_s
{
  struct list_node list_swevent;
  spinlock_t lock;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int perf_cpuclock_event_init(FAR struct perf_event_s *event);
static int perf_cpuclock_event_add(FAR struct perf_event_s *event,
                                   int flags);
static void perf_cpuclock_event_del(FAR struct perf_event_s *event,
                                    int flags);
static int perf_cpuclock_event_start(FAR struct perf_event_s *event,
                                     int flags);
static int perf_cpuclock_event_stop(FAR struct perf_event_s *event,
                                    int flags);
static int perf_cpuclock_event_read(FAR struct perf_event_s *event);
static int perf_cpuclock_event_match(FAR struct perf_event_s *event);

static int perf_swevent_init(FAR struct perf_event_s *event);
static int perf_swevent_add(FAR struct perf_event_s *event,
                            int flags);
static void perf_swevent_del(FAR struct perf_event_s *event,
                             int flags);
static int perf_swevent_start(FAR struct perf_event_s *event,
                              int flags);
static int perf_swevent_stop(FAR struct perf_event_s *event,
                             int flags);
static int perf_swevent_read(FAR struct perf_event_s *event);
static int perf_swevent_match(FAR struct perf_event_s *event);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct pmu_ops_s g_perf_cpu_clock_ops =
{
  .event_init  = perf_cpuclock_event_init,
  .event_add   = perf_cpuclock_event_add,
  .event_del   = perf_cpuclock_event_del,
  .event_start = perf_cpuclock_event_start,
  .event_stop  = perf_cpuclock_event_stop,
  .event_read  = perf_cpuclock_event_read,
  .event_match = perf_cpuclock_event_match,
};

static struct pmu_s g_perf_cpu_clock =
{
  .ops = &g_perf_cpu_clock_ops,
};

/* swevent */

static DEFINE_PER_CPU_BSS_SMP(struct swevent_manger_s, g_swevent);

static struct pmu_ops_s g_perf_swevent_ops =
{
  .event_init  = perf_swevent_init,
  .event_add   = perf_swevent_add,
  .event_del   = perf_swevent_del,
  .event_start = perf_swevent_start,
  .event_stop  = perf_swevent_stop,
  .event_read  = perf_swevent_read,
  .event_match = perf_swevent_match,
};

static struct pmu_s g_perf_swevent =
{
  .ops = &g_perf_swevent_ops,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int perf_swevent_timer_handle_cpu(FAR void *arg)
{
  FAR struct perf_event_s *event = (FAR struct perf_event_s *)arg;
  struct perf_sample_data_s data;
  int ret;

  /* TODO: ISR thread cannot get pc */

  ret = perf_get_event_count(event);
  if (ret < 0)
    {
      return ret;
    }

  perf_sample_data_init(&data, atomic64_read(&event->hw.last_period));

  perf_event_data_overflow(event, &data, up_getusrpc(NULL));
  return ret;
}

static uint64_t perf_swevent_timer_handle(FAR const hrtimer_t *timer,
                                          uint64_t expired)
{
  FAR struct perf_event_s *event = container_of(timer, struct perf_event_s,
                                                hw.timer);
#ifdef CONFIG_SMP
  int cpu = this_cpu();

  if (event->oncpu != -1 && event->oncpu != cpu)
    {
      nxsched_cpu_call_single(event->oncpu,
                              perf_swevent_timer_handle_cpu, event);
    }
  else if (event->cpu != -1 && event->cpu != cpu)
    {
      nxsched_cpu_call_single(event->cpu,
                              perf_swevent_timer_handle_cpu, event);
    }
  else
    {
      perf_swevent_timer_handle_cpu(event);
    }
#else
    perf_swevent_timer_handle_cpu(event);
#endif

  return event->attr.sample_period;
}

/****************************************************************************
 * Name: perf_cpuclock_event_init
 *
 * Description:
 *   CPU clock event init
 *
 * Input Parameters:
 *   event  - Perf event
 *
 * Returned Value:
 *   Result
 *
 ****************************************************************************/

static int perf_cpuclock_event_init(FAR struct perf_event_s *event)
{
  if (event->attr.config != PERF_COUNT_SW_CPU_CLOCK)
    {
      return -ENOENT;
    }

  atomic64_set(&event->hw.prev_count, 0);
  atomic64_set(&event->hw.last_period, 0);
  atomic64_set(&event->count, 0);
  return 0;
}

/****************************************************************************
 * Name: perf_cpuclock_event_add
 *
 * Description:
 *   Add cpu clock event
 *
 * Input Parameters:
 *   event  - Perf event
 *
 * Returned Value:
 *   Result
 *
 ****************************************************************************/

static int perf_cpuclock_event_add(FAR struct perf_event_s *event,
                                   int flags)
{
  return 0;
}

/****************************************************************************
 * Name: perf_cpuclock_event_del
 *
 * Description:
 *   Del cpu clock event
 *
 * Input Parameters:
 *   event  - Perf event
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void perf_cpuclock_event_del(FAR struct perf_event_s *event,
                                    int flags)
{
  hrtimer_cancel_sync(&event->hw.timer);
}

/****************************************************************************
 * Name: perf_cpuclock_event_start
 *
 * Description:
 *   Start cpu clock event
 *
 * Input Parameters:
 *   event  - Perf event
 *   flags  - Perf event flags
 *
 * Returned Value:
 *   Result
 *
 ****************************************************************************/

static int perf_cpuclock_event_start(FAR struct perf_event_s *event,
                                     int flags)
{
  atomic64_set(&event->hw.prev_count, clock_systime_nsec());
  hrtimer_start(&event->hw.timer, perf_swevent_timer_handle,
                event->attr.sample_period, HRTIMER_MODE_REL);
  return 0;
}

/****************************************************************************
 * Name: perf_cpuclock_event_stop
 *
 * Description:
 *   Stop cpu clock event
 *
 * Input Parameters:
 *   event  - Perf event
 *   flags  - Perf event flags
 *
 * Returned Value:
 *   Result
 *
 ****************************************************************************/

static int perf_cpuclock_event_stop(FAR struct perf_event_s *event,
                                    int flags)
{
  hrtimer_cancel(&event->hw.timer);

  /* Flush the accumulated run time into event->count so that a counter
   * without a sampling period (e.g. a resident counter) still advances on
   * every schedule-out, and a "perf stat" read after stop is accurate.
   */

  if (flags & PERF_EF_UPDATE)
    {
      perf_cpuclock_event_read(event);
    }

  return 0;
}

/****************************************************************************
 * Name: perf_cpuclock_event_read
 *
 * Description:
 *   Read cpu clock event
 *
 * Input Parameters:
 *   event  - Perf event
 *
 * Returned Value:
 *   Result
 *
 ****************************************************************************/

static int perf_cpuclock_event_read(FAR struct perf_event_s *event)
{
  uint64_t now   = clock_systime_nsec();
  uint64_t delta = now - atomic64_xchg(&event->hw.prev_count, now);
  atomic64_set(&event->hw.last_period, delta);
  atomic64_add(&event->count, delta);
  return 0;
}

static int perf_cpuclock_event_match(FAR struct perf_event_s *event)
{
  if (event->attr.type == PERF_TYPE_SOFTWARE &&
      event->attr.config == PERF_COUNT_SW_CPU_CLOCK)
    {
      return 0;
    }

  return -ENOENT;
}

static int perf_swevent_init(FAR struct perf_event_s *event)
{
  if (event->attr.type != PERF_TYPE_SOFTWARE)
    {
      return -ENOENT;
    }

  return 0;
}

static int perf_swevent_add(FAR struct perf_event_s *event,
                            int flags)
{
  irqstate_t irq_flags;

  irq_flags = spin_lock_irqsave(&this_cpu_var_smp(g_swevent).lock);
  list_add_tail(&this_cpu_var_smp(g_swevent).list_swevent, &event->sw_list);
  spin_unlock_irqrestore(&this_cpu_var_smp(g_swevent).lock, irq_flags);

  return 0;
}

static void perf_swevent_del(FAR struct perf_event_s *event,
                             int flags)
{
  irqstate_t irq_flags;

  irq_flags = spin_lock_irqsave(&this_cpu_var_smp(g_swevent).lock);
  list_delete(&event->sw_list);
  spin_unlock_irqrestore(&this_cpu_var_smp(g_swevent).lock, irq_flags);
}

static int perf_swevent_start(FAR struct perf_event_s *event,
                                     int flags)
{
  event->hw.state = 1;
  return 0;
}

static int perf_swevent_stop(FAR struct perf_event_s *event,
                                    int flags)
{
  event->hw.state = 0;
  return 0;
}

static int perf_swevent_read(FAR struct perf_event_s *event)
{
  return 0;
}

static int perf_swevent_match(FAR struct perf_event_s *event)
{
  if (event->attr.type == PERF_TYPE_SOFTWARE &&
      event->attr.config == PERF_COUNT_SW_CONTEXT_SWITCHES)
    {
      return 0;
    }

  return -ENOENT;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void perf_swevent(uint32_t event_id, uintptr_t ip)
{
  struct perf_sample_data_s data;
  irqstate_t flags;
  FAR struct perf_event_s *node;

  perf_sample_data_init(&data, 0);

  flags = spin_lock_irqsave(&this_cpu_var_smp(g_swevent).lock);

  list_for_every_entry(&this_cpu_var_smp(g_swevent).list_swevent, node,
                       struct perf_event_s, sw_list)
    {
      if (node->attr.type == PERF_TYPE_SOFTWARE &&
          node->attr.config == event_id && node->hw.state == 1)
        {
          /* Accumulate the counter unconditionally.  This is the counting
           * path read back through PERF_GET_COUNT (e.g. by resident
           * counters).  perf_event_data_overflow() below is the separate
           * sampling path and only does work when a ring buffer is attached
           * (perf record); it never updates event->count.
           */

          atomic64_add(&node->count, 1);
          perf_event_data_overflow(node, &data, ip);
        }
    }

  spin_unlock_irqrestore(&this_cpu_var_smp(g_swevent).lock, flags);
}

/****************************************************************************
 * Name: perf_pmu_builtin_register
 *
 * Description:
 *   Initialize the per-CPU software event state and register the built-in
 *   software and cpu-clock PMUs.  Called once from perf_event_init().
 *
 ****************************************************************************/

void perf_pmu_builtin_register(void)
{
  int i;

  for (i = 0; i < CONFIG_SMP_NCPUS; i++)
    {
      list_initialize(&per_cpu_var_smp(g_swevent, i).list_swevent);
      spin_lock_init(&per_cpu_var_smp(g_swevent, i).lock);
    }

  perf_pmu_register(&g_perf_swevent, "software");
  perf_pmu_register(&g_perf_cpu_clock, "cpu_clock");
}
