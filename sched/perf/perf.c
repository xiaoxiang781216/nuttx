/****************************************************************************
 * sched/perf/perf.c
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

#include <nuttx/atomic.h>
#include <nuttx/fs/fs.h>
#include <nuttx/kmalloc.h>
#include <nuttx/mutex.h>
#include <nuttx/perf.h>
#include <nuttx/sched.h>

#include "perf.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define PERF_GET_COUNT(event) \
        (atomic64_read(&(event)->count) + atomic64_read(&(event)->child_count))

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

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Private Data
 ****************************************************************************/

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
 * Name: perf_pmu_enable
 *
 * Description:
 *   Enable pmu
 *
 * Input Parameters:
 *   pmu - PMU entry
 *
 ****************************************************************************/

static void perf_pmu_enable(FAR struct pmu_s *pmu)
{
  if (pmu->ops->pmu_enable)
    {
      pmu->ops->pmu_enable(pmu);
    }
}

/****************************************************************************
 * Name: perf_pmu_disable
 *
 * Description:
 *   Disable pmu
 *
 * Input Parameters:
 *   pmu - PMU entry
 *
 ****************************************************************************/

static void perf_pmu_disable(FAR struct pmu_s *pmu)
{
  if (pmu->ops->pmu_disable)
    {
      pmu->ops->pmu_disable(pmu);
    }
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

static void perf_add_event_to_count(FAR struct perf_event_s *event,
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

static FAR struct perf_event_s *
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

  event = perf_event_alloc(&parent_event->attr, parent_event->cpu,
                           group_leader, parent_event);
  if (event == NULL)
    {
      serr("perf_inherit_event alloc perf event fail\n");
      return NULL;
    }

  nxmutex_lock(&parent_event->child_mutex);

  /* Decide the initial state of the inherited child event.
   *
   * With enable_on_exec the parent event is deliberately left OFF so that
   * the task that opened it (e.g. the perf tool itself) is never counted;
   * the event is meant to start counting only in the descendants spawned
   * afterwards.  Force such children INACTIVE so they get scheduled onto
   * the PMU even though the parent is OFF.  Otherwise the child simply
   * mirrors the parent's enabled/disabled state.
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
  perf_buffer_set_output(event, parent_event);

  nxmutex_unlock(&parent_event->child_mutex);

  perf_add_event_to_count(event, event->ctx);

  return event;
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

static int perf_inherit_group_events(FAR struct perf_event_s *group_leader,
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
 * Name: perf_event_sched_in
 *
 * Description:
 *   Perf event sched in
 *
 * Input Parameters:
 *   event - Perf event
 *
 * Returned Value:
 *   Sched result
 *
 ****************************************************************************/

static int perf_event_sched_in(FAR struct perf_event_s *event)
{
  FAR struct perf_event_context_s *ctx = event->ctx;
  FAR struct pmu_s *pmu = event->pmu;
  int ret;

  if (event->state == PERF_EVENT_STATE_OFF ||
      event->state == PERF_EVENT_STATE_ACTIVE)
    {
      return -EPERM;
    }

#ifdef CONFIG_SMP
  event->oncpu = this_cpu();
#endif
  event->state = PERF_EVENT_STATE_ACTIVE;

  perf_pmu_disable(pmu);

  ret = pmu->ops->event_add(event, 0);
  if (ret < 0)
    {
      event->state = PERF_EVENT_STATE_INACTIVE;
      goto out;
    }

  ret = pmu->ops->event_start(event, 0);
  if (ret < 0)
    {
      event->state = PERF_EVENT_STATE_INACTIVE;
      pmu->ops->event_del(event, 0);
      goto out;
    }

  ctx->active_num++;

out:
  perf_pmu_enable(pmu);
  return ret;
}

/****************************************************************************
 * Name: perf_event_sched_out
 *
 * Description:
 *   Perf event sched out
 *
 * Input Parameters:
 *   event - Perf event
 *
 * Returned Value:
 *   Sched result
 *
 ****************************************************************************/

static int perf_event_sched_out(FAR struct perf_event_s *event)
{
  FAR struct perf_event_context_s *ctx = event->ctx;
  FAR struct pmu_s *pmu = event->pmu;
  int ret;

#ifdef CONFIG_SMP
  if (event->oncpu != this_cpu())
    {
      return -EAGAIN;
    }
#endif

  if (event->state != PERF_EVENT_STATE_ACTIVE)
    {
      return -EPERM;
    }

  perf_pmu_disable(pmu);

  ret = pmu->ops->event_stop(event, PERF_EF_UPDATE);
  if (ret < 0)
    {
      serr("pmu event stop fail\n");
      goto out;
    }

  pmu->ops->event_del(event, 0);

#ifdef CONFIG_SMP
  event->oncpu = -1;
#endif
  event->state = PERF_EVENT_STATE_INACTIVE;
  ctx->active_num--;

out:
  perf_pmu_enable(pmu);
  return ret;
}

/****************************************************************************
 * Name: perf_group_sched_in
 *
 * Description:
 *   Perf group event sched in
 *
 * Input Parameters:
 *   group_leader  - Group leader event
 *   ctx           - Perf event context
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void perf_group_sched_in(FAR struct perf_event_s *group_leader,
                                FAR struct perf_event_context_s *ctx)
{
  FAR struct perf_event_s *sub_event;

  /* Sched in group leader first */

  perf_event_sched_in(group_leader);

  /* Sched out event in this group */

  list_for_every_entry(&group_leader->sibling_list, sub_event,
                       struct perf_event_s, group_node)
    {
      perf_event_sched_in(sub_event);
    }
}

/****************************************************************************
 * Name: perf_group_sched_out
 *
 * Description:
 *   Perf group event sched out
 *
 * Input Parameters:
 *   group_leader  - Group leader event
 *   ctx           - Perf event context
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void perf_group_sched_out(FAR struct perf_event_s *group_leader,
                                 FAR struct perf_event_context_s *ctx)
{
  FAR struct perf_event_s *sub_event;

  /* Sched out group leader first */

  perf_event_sched_out(group_leader);

  /* Sched out event in this group */

  list_for_every_entry(&group_leader->sibling_list, sub_event,
                       struct perf_event_s, group_node)
    {
      perf_event_sched_out(sub_event);
    }
}

/****************************************************************************
 * Name: perf_context_sched_in
 *
 * Description:
 *   Perf context event sched in
 *
 * Input Parameters:
 *   ctx  - Perf event context
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void perf_context_sched_in(FAR struct perf_event_context_s *ctx)
{
  FAR struct perf_event_s *group_leader;

  /* Sched all the perf event in the context */

  list_for_every_entry(&ctx->group_list, group_leader,
                       struct perf_event_s, group_node)
    {
      perf_group_sched_in(group_leader, ctx);
    }
}

/****************************************************************************
 * Name: perf_context_sched_out
 *
 * Description:
 *   Perf context event sched out
 *
 * Input Parameters:
 *   ctx  - Perf event context
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void perf_context_sched_out(FAR struct perf_event_context_s *ctx)
{
  FAR struct perf_event_s *group_leader;

  if (ctx->active_num == 0)
    {
      return;
    }

  /* Stop all the perf event in the context */

  list_for_every_entry(&ctx->group_list, group_leader,
                       struct perf_event_s, group_node)
    {
      perf_group_sched_out(group_leader, ctx);
    }
}

/****************************************************************************
 * Name: perf_event_do_enable
 *
 * Description:
 *   Perf event enable internal
 *
 * Input Parameters:
 *   arg - Perf event
 *
 * Returned Value:
 *   Enable result
 *
 ****************************************************************************/

static int perf_event_do_enable(FAR void *arg)
{
  FAR struct perf_event_s *event = arg;
  FAR struct perf_event_context_s *ctx = event->ctx;

  if (event->state >= PERF_EVENT_STATE_INACTIVE)
    {
      return OK;
    }

  perf_context_sched_out(ctx);

  event->state = PERF_EVENT_STATE_INACTIVE;

  perf_context_sched_in(ctx);

  return OK;
}

/****************************************************************************
 * Name: perf_event_do_disable
 *
 * Description:
 *   Perf event disable internal
 *
 * Input Parameters:
 *   arg - Perf event
 *
 * Returned Value:
 *   Disable result
 *
 ****************************************************************************/

static int perf_event_do_disable(FAR void *arg)
{
  FAR struct perf_event_s *event = arg;
  FAR struct perf_event_context_s *ctx = event->ctx;

  if (event->state == PERF_EVENT_STATE_OFF)
    {
      return OK;
    }

  if (event->group_leader == event)
    {
      perf_group_sched_out(event, ctx);
    }
  else
    {
      perf_event_sched_out(event);
    }

  event->state = PERF_EVENT_STATE_OFF;

  return OK;
}

/****************************************************************************
 * Name: perf_task_function_call
 *
 * Description:
 *   Perf event call function at designated task
 *
 * Input Parameters:
 *   tcb   - Tcb for this event
 *   event - Perf event
 *   func  - Executive function
 *
 * Returned Value:
 *   Execution result
 *
 ****************************************************************************/

static int perf_task_function_call(FAR struct tcb_s *tcb,
                                   FAR struct perf_event_s *event,
                                   nxsched_cpu_call_t func)
{
#ifdef CONFIG_SMP
  if (tcb->cpu != this_cpu())
    {
      return nxsched_cpu_call_single(tcb->cpu, func, event);
    }
#endif

  /* If task running on the current CPU, run func directly */

  return func(event);
}

/****************************************************************************
 * Name: perf_cpu_function_call
 *
 * Description:
 *   Perf event call function at designated cpu
 *
 * Input Parameters:
 *   event - Perf event
 *   func  - Executive function
 *
 * Returned Value:
 *   Execution result
 *
 ****************************************************************************/

static int perf_cpu_function_call(int cpu, FAR struct perf_event_s *event,
                                  nxsched_cpu_call_t func)
{
#ifdef CONFIG_SMP
  if (cpu != this_cpu())
    {
      return nxsched_cpu_call_single(cpu, func, event);
    }
#endif

  /* If on the current CPU, run func directly */

  return func(event);
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

static void perf_group_detach(FAR struct perf_event_s *event)
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

static void perf_child_detach(FAR struct perf_event_s *event)
{
  FAR struct perf_event_s *parent_event = event->parent_event;
  uint64_t child_count;

  if (!(event->attach_state & PERF_ATTACH_CHILD))
    {
      return;
    }

  event->attach_state &= ~PERF_ATTACH_CHILD;

  nxmutex_lock(&parent_event->child_mutex);

  child_count = PERF_GET_COUNT(event);
  parent_event->child_count += child_count;

  nxmutex_unlock(&parent_event->child_mutex);

  list_delete(&event->child_list);
}

/****************************************************************************
 * Name: perf_context_detach_internal
 *
 * Description:
 *   Remove perf event from context
 *
 * Input Parameters:
 *   arg - Perf event
 *
 * Returned Value:
 *   Detach result
 *
 ****************************************************************************/

static int perf_context_detach_internal(FAR void *arg)
{
  FAR struct perf_event_s *event = arg;

  perf_event_sched_out(event);

  perf_group_detach(event);

  perf_child_detach(event);

  list_delete(&event->event_node);

  return OK;
}

/****************************************************************************
 * Name: perf_setup_task_context
 *
 * Description:
 *   Perf event task context initialization
 *
 * Input Parameters:
 *   tcb   - Task for init.
 *
 * Returned Value:
 *   Initialization result
 *
 ****************************************************************************/

static int perf_setup_task_context(FAR struct tcb_s *tcb)
{
  FAR struct tcb_s *parent = this_task();
  FAR struct perf_event_context_s *parent_ctx = parent->perf_event_ctx;
  FAR struct perf_event_s *group_leader;
  irqstate_t flags;
  int ret = OK;

  DEBUGASSERT(tcb->perf_event_ctx == NULL);
  nxmutex_init(&tcb->perf_event_mutex);

  /* Inherit parent event if it has */

  if (parent_ctx == NULL)
    {
      return OK;
    }

  tcb->perf_event_ctx = kmm_zalloc(sizeof(struct perf_event_context_s));
  if (tcb->perf_event_ctx == NULL)
    {
      serr("malloc task perf event fail\n");
      return -ENOMEM;
    }

  perf_init_context(tcb->perf_event_ctx);
  tcb->perf_event_ctx->tcb = tcb;

  flags = spin_lock_irqsave(&parent_ctx->lock);

  list_for_every_entry(&parent_ctx->group_list, group_leader,
                       struct perf_event_s, group_node)
    {
      ret = perf_inherit_group_events(group_leader, parent, tcb);
      if (ret < 0)
        {
          break;
        }
    }

  spin_unlock_irqrestore(&parent_ctx->lock, flags);
  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: perf_free_event
 *
 * Description:
 *   Free perf event
 *
 * Input Parameters:
 *   event - Free perf event
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void perf_free_event(FAR struct perf_event_s *event)
{
  if (event->pmuctx != NULL)
    {
      perf_free_pmu_context(event->pmuctx);
    }

  nxmutex_destroy(&event->child_mutex);
  perf_buffer_release(event);
  kmm_free(event);
}

/****************************************************************************
 * Name: perf_function_call
 *
 * Description:
 *   Perf event call function
 *
 * Input Parameters:
 *   event - Perf event
 *   func  - Executive function
 *
 * Returned Value:
 *   Execution result
 *
 ****************************************************************************/

int perf_function_call(FAR struct perf_event_s *event,
                       nxsched_cpu_call_t func)
{
  FAR struct perf_event_context_s *ctx = event->ctx;

  /* Enable event immediately if event bound in cpu */

  if (ctx->tcb == NULL)
    {
      return perf_cpu_function_call(event->cpu, event, func);
    }
  else
    {
      return perf_task_function_call(ctx->tcb, event, func);
    }
}

/****************************************************************************
 * Name: perf_event_enable
 *
 * Description:
 *   Perf event enable
 *
 * Input Parameters:
 *   arg - Perf event
 *
 * Returned Value:
 *   Execution result
 *
 ****************************************************************************/

int perf_event_enable(FAR void *arg)
{
  FAR struct perf_event_s *event = arg;

  if (event->state >= PERF_EVENT_STATE_INACTIVE)
    {
      serr("Perf event was already enable\n");
      return OK;
    }

  return perf_function_call(event, perf_event_do_enable);
}

/****************************************************************************
 * Name: perf_event_disable
 *
 * Description:
 *   Perf event disable
 *
 * Input Parameters:
 *   arg - Perf event
 *
 * Returned Value:
 *   Disable result
 *
 ****************************************************************************/

int perf_event_disable(FAR void *arg)
{
  FAR struct perf_event_s *event = arg;

  if (event->state != PERF_EVENT_STATE_ACTIVE)
    {
      return OK;
    }

  return perf_function_call(event, perf_event_do_disable);
}

/****************************************************************************
 * Name: perf_event_reset
 *
 * Description:
 *   Perf event reset
 *
 * Input Parameters:
 *   arg - Perf event
 *
 * Returned Value:
 *   Reset result
 *
 ****************************************************************************/

int perf_event_reset(FAR void *arg)
{
  FAR struct perf_event_s *event = arg;

  atomic64_set(&event->count, 0);
  return 0;
}

/****************************************************************************
 * Name: perf_context_detach
 *
 * Description:
 *   Remove perf event from context
 *
 * Input Parameters:
 *   event  - Perf event
 *   ctx    - Perf event context
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void perf_context_detach(FAR struct perf_event_s *event,
                         FAR struct perf_event_context_s *ctx)
{
  irqstate_t flags = spin_lock_irqsave(&ctx->lock);

  /* If no event active, call remove directly */

  if (ctx->active_num == 0)
    {
      perf_context_detach_internal(event);
      spin_unlock_irqrestore(&ctx->lock, flags);
      return;
    }

  spin_unlock_irqrestore(&ctx->lock, flags);

  perf_function_call(event, perf_context_detach_internal);
}

int perf_event_overflow(FAR struct perf_event_s *event)
{
  struct perf_sample_data_s data;

  if (!event->buf)
    {
      return 0;
    }

  /* TODO: ISR thread cannot get pc */

  uintptr_t pc = up_getusrpc(NULL);

  perf_sample_data_init(&data, event->hw.last_period);

  perf_event_data_overflow(event, &data, pc);
  return 0;
}

/****************************************************************************
 * Name: perf_event_init
 *
 * Description:
 *   Perf event initialization
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   Init result
 *
 ****************************************************************************/

int perf_event_init(void)
{
  /* Init the per-CPU (system-wide) perf event contexts */

  perf_init_cpu_contexts();

  /* Register the built-in software and cpu-clock PMUs */

  perf_pmu_builtin_register();

  return OK;
}

/****************************************************************************
 * Name: perf_event_open
 *
 * Description:
 *   Create and open a perf event
 *
 * Input Parameters:
 *   attr     - Perf event attribute
 *   pid      - Task pid perf event bind
 *   cpu      - Cpu id perf event bind
 *   group_fd - Perf event fd for group leader
 *   flags    - Perf event flags
 *
 * Returned Value:
 *   Perf event fd
 *
 ****************************************************************************/

int perf_event_open(FAR struct perf_event_attr_s *attr, pid_t pid,
                    int cpu, int group_fd, unsigned long flags)
{
  FAR struct perf_event_s *group_leader = NULL;
  FAR struct perf_event_s *event = NULL;
  FAR struct tcb_s *tcb = NULL;
  FAR struct file *group_file = NULL;
  int event_fd = -1;
  int ret;

  if (pid == -1 && cpu == -1)
    {
      serr("perf event pid & cpu error\n");
      return -EINVAL;
    }

  if (cpu != -1 && cpu >= CONFIG_SMP_NCPUS)
    {
      serr("perf event cpu fail:%d\n", cpu);
      return -EINVAL;
    }

  if (group_fd >= 0)
    {
      ret = file_get(group_fd, &group_file);
      if (ret < 0)
        {
          serr("perf event group_fd fail:%d\n", group_fd);
          return ret;
        }

      group_leader = group_file->f_priv;

      if (group_leader->cpu != cpu)
        {
          file_put(group_file);
          serr("Event cpu must be same as group leader\n");
          return -EINVAL;
        }
    }

  if (pid != -1)
    {
      if (pid == 0)
        {
          tcb = this_task();
        }
      else if (pid > 0)
        {
          tcb = nxsched_get_tcb(pid);
        }

      if (tcb == NULL)
        {
          serr("perf event pid fail:%d\n", pid);
          ret = -ESRCH;
          goto exit;
        }
    }

  event = perf_event_create(attr, cpu, tcb, group_leader, &ret);
  if (event == NULL)
    {
      goto exit;
    }

  event_fd = perf_install_fd(event, flags);
  if (event_fd < 0)
    {
      ret = -EINVAL;
      goto err_with_event;
    }

  event->id = event_fd;

  if (pid > 0)
    {
      nxsched_put_tcb(tcb);
    }

  if (group_file != NULL)
    {
      file_put(group_file);
    }

  perf_add_event_to_count(event, event->ctx);
  return event_fd;

err_with_event:
  perf_free_event(event);

exit:
  if (pid > 0)
    {
      nxsched_put_tcb(tcb);
    }

  if (group_file != NULL)
    {
      file_put(group_file);
    }

  return ret;
}

/****************************************************************************
 * Name: perf_event_task_init
 *
 * Description:
 *   Perf event task init, call during task initialization.
 *
 * Input Parameters:
 *   tcb   - Task for init.
 *
 * Returned Value:
 *   Initialization result
 *
 ****************************************************************************/

int perf_event_task_init(FAR struct tcb_s *tcb)
{
  int ret;

  ret = perf_setup_task_context(tcb);
  if (ret < 0)
    {
      perf_event_task_exit(tcb);
    }

  return ret;
}

/****************************************************************************
 * Name: perf_event_task_exit
 *
 * Description:
 *   Perf event task free
 *
 * Input Parameters:
 *   tcb   - Task for init.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void perf_event_task_exit(FAR struct tcb_s *tcb)
{
  FAR struct perf_event_context_s *ctx = tcb->perf_event_ctx;

  if (ctx != NULL)
    {
      FAR struct perf_event_s *event;
      FAR struct perf_event_s *next;

      list_for_every_entry_safe(&ctx->event_list, event, next,
                                struct perf_event_s, event_node)
        {
          perf_context_detach(event, ctx);

          if (event->buf && event->buf->ref_count > 1)
            {
              perf_free_event(event);
            }
          else
            {
              /* wait for perf_close free */

              event->state = PERF_EVENT_STATE_EXIT;
              event->ctx = NULL;

              if (event->pmuctx != NULL)
                {
                  perf_free_pmu_context(event->pmuctx);
                  event->pmuctx = NULL;
                }
            }
        }

      perf_free_context(ctx);
      nxmutex_destroy(&tcb->perf_event_mutex);
    }
}

/****************************************************************************
 * Name: perf_event_task_sched_in
 *
 * Description:
 *   Record task sched in event
 *
 * Input Parameters:
 *   tcb - Refers to the head task which will be executed.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void perf_event_task_sched_in(FAR struct tcb_s *tcb)
{
  FAR struct perf_event_context_s *ctx = tcb->perf_event_ctx;

  if (ctx != NULL)
    {
      irqstate_t flags = spin_lock_irqsave(&ctx->lock);

      perf_context_sched_in(ctx);

      spin_unlock_irqrestore(&ctx->lock, flags);
    }
}

/****************************************************************************
 * Name: perf_event_task_sched_out
 *
 * Description:
 *   Record task sched out event
 *
 * Input Parameters:
 *   tcb - Refers to the running task which will be blocked.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void perf_event_task_sched_out(FAR struct tcb_s *tcb)
{
  FAR struct perf_event_context_s *ctx = tcb->perf_event_ctx;

  perf_swevent(PERF_COUNT_SW_CONTEXT_SWITCHES, up_getpc());

  if (ctx != NULL)
    {
      irqstate_t flags = spin_lock_irqsave(&ctx->lock);

      perf_context_sched_out(ctx);

      spin_unlock_irqrestore(&ctx->lock, flags);
    }
}
