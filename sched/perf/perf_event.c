/****************************************************************************
 * sched/perf/perf_event.c
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
#include <inttypes.h>

#include <nuttx/kmalloc.h>
#include <nuttx/list.h>
#include <nuttx/mutex.h>
#include <nuttx/perf.h>
#include <nuttx/sched.h>
#include <nuttx/spinlock.h>

#include "perf.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Default sampling periods.
 *
 * PERF_HW_SAMPLE_PERIOD: hardware PMU events count raw hardware occurrences,
 * so one sample every 100000 events keeps the interrupt rate sane.
 *
 * PERF_SW_CPU_CLOCK_PERIOD: software cpu-clock counts in nanoseconds; 10ms
 * keeps the sampling interrupt rate low enough that profiling a CPU-bound
 * task does not drown the CPU in sampling interrupts (an interrupt storm
 * that starves the profiled task).
 */

#define PERF_HW_SAMPLE_PERIOD    1000000
#define PERF_SW_CPU_CLOCK_PERIOD 1000000

/* PERF_SW_MIN_SAMPLE_PERIOD: lower bound (ns) for the sampling period of
 * software timer-driven events.  A tiny period (e.g. "perf record -c 1000")
 * lets the hrtimer re-arm faster than its interrupt-context handler can be
 * serviced, so the CPU takes back-to-back sampling interrupts and livelocks.
 * Clamp to a floor the handler can sustain; builds with heavier per-sample
 * cost (e.g. KASAN) need a larger floor, so scale accordingly.
 */

#ifdef CONFIG_MM_KASAN_INSTRUMENT
#  define PERF_SW_MIN_SAMPLE_PERIOD 100000  /* 100us == 10kHz max */
#else
#  define PERF_SW_MIN_SAMPLE_PERIOD 10000   /* 10us  == 100kHz max */
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: perf_default_sample_period
 *
 * Description:
 *   Return the default sampling period used when the user did not specify
 *   one (attr.sample_period == 0).  Replaces the former
 *   g_perf_sample_period[][] lookup table, which also avoided an
 *   out-of-bounds read for unhandled type/config values coming from
 *   user space.
 *
 * Input Parameters:
 *   type   - perf_event_attr.type
 *   config - perf_event_attr.config
 *
 * Returned Value:
 *   Default sample period for the event.
 *
 ****************************************************************************/

static int64_t perf_default_sample_period(uint32_t type, uint64_t config)
{
  if (type == PERF_TYPE_HARDWARE)
    {
      return PERF_HW_SAMPLE_PERIOD;
    }

  if (type == PERF_TYPE_SOFTWARE && config == PERF_COUNT_SW_CPU_CLOCK)
    {
      return PERF_SW_CPU_CLOCK_PERIOD;
    }

  return UINT32_MAX - 1;
}

/****************************************************************************
 * Name: perf_event_alloc
 *
 * Description:
 *   Create perf event
 *
 * Input Parameters:
 *   attr         - Perf event attribute
 *   cpu          - Cpu id perf event bind
 *   group_leader - Group leader
 *   parent_event - Parent event
 *
 * Returned Value:
 *   Perf event
 *
 ****************************************************************************/

static FAR struct perf_event_s *
perf_event_alloc(FAR struct perf_event_attr_s *attr, int cpu,
                 FAR struct perf_event_s *group_leader,
                 FAR struct perf_event_s *parent_event)
{
  FAR struct perf_event_s *event;
  FAR struct pmu_s *pmu;

  event = kmm_zalloc(sizeof(struct perf_event_s));
  if (event == NULL)
    {
      serr("perf event alloc fail\n");
      return NULL;
    }

  if (!attr->sample_period)
    {
      attr->sample_period =
        perf_default_sample_period(attr->type, attr->config);
    }

  /* Clamp the cpu-clock period to PERF_SW_MIN_SAMPLE_PERIOD (see its
   * definition).  Only cpu-clock is time-based; other software events count
   * discrete occurrences, so their period must not be treated as time.
   */

  if (attr->type == PERF_TYPE_SOFTWARE &&
      attr->config == PERF_COUNT_SW_CPU_CLOCK &&
      attr->sample_period < PERF_SW_MIN_SAMPLE_PERIOD)
    {
      sinfo("sw cpu-clock sample_period %" PRIu64
            " below floor, clamping to %d ns\n",
            attr->sample_period, PERF_SW_MIN_SAMPLE_PERIOD);
      attr->sample_period = PERF_SW_MIN_SAMPLE_PERIOD;
    }

  event->attr        = *attr;
  event->cpu         = cpu;
  event->exclude_pid = INVALID_PROCESS_ID;

#ifdef CONFIG_SMP
  event->oncpu = -1;
#endif

  event->parent_event = parent_event;

  if (group_leader == NULL)
    {
      event->group_leader = event;
    }
  else
    {
      event->group_leader = group_leader;
    }

  list_initialize(&event->sibling_list);
  list_initialize(&event->child_list);
  nxmutex_init(&event->child_mutex);

  /* Find the suitable pmu */

  pmu = perf_get_pmu(event);
  if (pmu == NULL)
    {
      goto nxmutex_destroy;
    }

  return event;

nxmutex_destroy:
  kmm_free(event);
  return NULL;
}

/****************************************************************************
 * Name: perf_inherit_event
 *
 * Description:
 *   Inherit one perf event
 *
 * Input Parameters:
 *   parent_event - Inherit parent event
 *   group_leader - Group leader for this event
 *   parent       - Parent tcb for this event
 *   child        - Current tcb for this event
 *
 * Returned Value:
 *   Perf event
 *
 ****************************************************************************/

static FAR struct perf_event_s *
perf_inherit_event(FAR struct perf_event_s *parent_event,
                   FAR struct perf_event_s *group_leader,
                   FAR struct tcb_s *parent,
                   FAR struct tcb_s *child)
{
  FAR struct perf_event_s *event;

  if (parent_event->parent_event != NULL)
    {
      parent_event = parent_event->parent_event;
    }

  event = perf_event_alloc(&parent_event->attr, parent_event->cpu,
                           group_leader, parent_event);
  if (event == NULL)
    {
      serr("perf_inherit_event alloc perf event fail\n");
      return NULL;
    }

  nxmutex_lock(&parent_event->child_mutex);

  /* Decide the child's initial state.  With enable_on_exec the parent is
   * left OFF (so the opener, e.g. the perf tool, is not counted) but its
   * descendants must count, so force such children INACTIVE to schedule them
   * onto the PMU.  Otherwise the child mirrors the parent's state.
   */

  if (parent_event->attr.enable_on_exec ||
      parent_event->state >= PERF_EVENT_STATE_INACTIVE)
    {
      event->state = PERF_EVENT_STATE_INACTIVE;
    }
  else
    {
      event->state = PERF_EVENT_STATE_OFF;
    }

  list_add_tail(&parent_event->child_list, &event->child_list);
  event->attach_state |= PERF_ATTACH_CHILD;
  event->ctx = child->perf_event_ctx;

  /* Cache the parent's id in the child so the sampling hot path never
   * dereferences parent_event: the fd-owned parent can be freed by
   * perf_close() while this child is still sampling, and reading
   * parent_event->id from the child's hrtimer callback after that would be a
   * use-after-free.
   */

  event->id = parent_event->id;
  perf_buffer_set_output(event, parent_event);

  nxmutex_unlock(&parent_event->child_mutex);

  perf_add_event_to_count(event, event->ctx);

  return event;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: perf_add_event_to_count
 *
 * Description:
 *   Add perf event to context
 *
 * Input Parameters:
 *   event - Perf event
 *   ctx   - Perf context
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void perf_add_event_to_count(FAR struct perf_event_s *event,
                             FAR struct perf_event_context_s *ctx)
{
  irqstate_t flags = spin_lock_irqsave(&ctx->lock);

  /* Add perf event to group if it has group_leader */

  if (event->group_leader != event)
    {
      list_add_tail(&event->group_leader->sibling_list,
                    &event->sibling_list);
      event->group_leader->sibling_num++;
    }
  else
    {
      list_add_tail(&ctx->group_list, &event->group_node);
    }

  /* Add perf event to event list */

  list_add_tail(&ctx->event_list, &event->event_node);

  spin_unlock_irqrestore(&ctx->lock, flags);
}

/****************************************************************************
 * Name: perf_event_create
 *
 * Description:
 *   Allocate a perf event and bind it to its task/CPU context and PMU
 *   context.  This is the file-descriptor agnostic core of event creation,
 *   shared by perf_event_open() and reusable by in-kernel event creators.
 *
 * Input Parameters:
 *   attr         - Perf event attribute
 *   cpu          - Cpu id the event binds to (-1 for task-bound events)
 *   tcb          - Task the event binds to (NULL for a system-wide event)
 *   group_leader - Group leader for this event, or NULL
 *   err          - On failure, receives the negated errno
 *
 * Returned Value:
 *   The created event on success, NULL on failure with *err set.
 *
 ****************************************************************************/

FAR struct perf_event_s *
perf_event_create(FAR struct perf_event_attr_s *attr, int cpu,
                  FAR struct tcb_s *tcb,
                  FAR struct perf_event_s *group_leader,
                  FAR int *err)
{
  FAR struct perf_event_s *event;
  FAR struct perf_event_context_s *ctx;
  FAR struct pmu_event_context_s *pmu_ctx;

  event = perf_event_alloc(attr, cpu, group_leader, NULL);
  if (event == NULL)
    {
      *err = -ENOMEM;
      return NULL;
    }

  /* For a system-wide (CPU-bound) event, record the opener's process so the
   * perf tool itself is excluded from the system profile.  The sampling path
   * skips samples whose running task belongs to this pid.
   */

  if (tcb == NULL)
    {
      FAR struct tcb_s *self = this_task();

      event->exclude_pid = (self->group != NULL) ?
                           self->group->tg_pid : self->pid;
    }

  if (attr->disabled)
    {
      event->state = PERF_EVENT_STATE_OFF;
    }
  else
    {
      event->state = PERF_EVENT_STATE_INACTIVE;
    }

  ctx = perf_get_context(event, tcb);
  if (ctx == NULL)
    {
      *err = -ESRCH;
      goto err_with_event;
    }

  event->ctx = ctx;

  pmu_ctx = perf_get_pmu_context(event->pmu, event, ctx);
  if (pmu_ctx == NULL)
    {
      *err = -ESRCH;
      goto err_with_event;
    }

  event->pmuctx = pmu_ctx;
  return event;

err_with_event:
  perf_free_event(event);
  return NULL;
}

/****************************************************************************
 * Name: perf_inherit_group_events
 *
 * Description:
 *   Inherit perf event from parent group
 *
 * Input Parameters:
 *   group_leader  - Perf event group leader
 *   parent        - Parent tcb for this event
 *   child         - Current tcb for this event
 *
 * Returned Value:
 *   Inherit result
 *
 ****************************************************************************/

int perf_inherit_group_events(FAR struct perf_event_s *group_leader,
                              FAR struct tcb_s *parent,
                              FAR struct tcb_s *child)
{
  FAR struct perf_event_s *sub_event;
  FAR struct perf_event_s *child_leader;

  if (group_leader->attr.inherit == 0)
    {
      return OK;
    }

  child_leader = perf_inherit_event(group_leader, NULL, parent, child);
  if (child_leader == NULL)
    {
      return -EINVAL;
    }

  list_for_every_entry(&group_leader->sibling_list, sub_event,
                       struct perf_event_s, group_node)
    {
      FAR struct perf_event_s *child_event;
      child_event = perf_inherit_event(sub_event, child_leader,
                                       parent, child);
      if (child_event == NULL)
        {
          return -EINVAL;
        }
    }

  return OK;
}

/****************************************************************************
 * Name: perf_group_detach
 *
 * Description:
 *   Remove perf event from group
 *
 * Input Parameters:
 *   event  - Perf event
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void perf_group_detach(FAR struct perf_event_s *event)
{
  FAR struct perf_event_s *sub_event;

  /* If is not the group leader, remove it */

  if (event->group_leader != event)
    {
      list_delete(&event->sibling_list);
      event->group_leader->sibling_num--;
      return;
    }

  /* If is group leader, upgrade the sibling node to singleton event */

  list_for_every_entry(&event->sibling_list, sub_event,
                       struct perf_event_s, group_node)
    {
      sub_event->group_leader = sub_event;
      list_delete(&sub_event->sibling_list);
    }

  list_delete(&event->group_node);
}

/****************************************************************************
 * Name: perf_child_detach
 *
 * Description:
 *   Remove child events
 *
 * Input Parameters:
 *   event  - Perf event
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void perf_child_detach(FAR struct perf_event_s *event)
{
  FAR struct perf_event_s *parent_event = event->parent_event;
  uint64_t child_count;

  if (!(event->attach_state & PERF_ATTACH_CHILD))
    {
      return;
    }

  /* If the parent was already freed it orphans its children first (clears
   * parent_event and PERF_ATTACH_CHILD under child_mutex in
   * perf_free_event()); the check above normally catches that.  Guard the
   * pointer as well so a NULL parent can never be dereferenced.
   */

  event->attach_state &= ~PERF_ATTACH_CHILD;
  if (parent_event == NULL)
    {
      return;
    }

  nxmutex_lock(&parent_event->child_mutex);

  child_count = PERF_GET_COUNT(event);
  parent_event->child_count += child_count;

  list_delete(&event->child_list);

  nxmutex_unlock(&parent_event->child_mutex);
}
