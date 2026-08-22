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
#include <nuttx/hrtimer.h>
#include <nuttx/kmalloc.h>
#include <nuttx/mutex.h>
#include <nuttx/perf.h>
#include <nuttx/sched.h>

#include "perf.h"

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Global perf teardown serialization lock.
 *
 * perf_close() (tool closes its fd) and perf_event_task_exit() (monitored
 * task exits) can tear down the same event/ctx concurrently on different
 * CPUs.  Both detach events via a synchronous cross-CPU IPI and then free
 * the ctx/event, so without mutual exclusion one path can free a ctx while
 * the peer's in-flight IPI still dereferences it, faulting in IRQ context.
 * Serialize both paths here.  Safe to hold across the IPI: the callback
 * never takes this lock, and the peer must acquire it before freeing.  Both
 * callers run in blockable task context, so this sleeping mutex forms no
 * g_schedlock <-> ctx->lock inversion.
 *
 * Lock order: g_perf_teardown_lock -> child_mutex / perf_event_mutex ->
 * ctx->lock.  Placed outside the objects it protects so it is never freed
 * with them.
 */

static mutex_t g_perf_teardown_lock = NXMUTEX_INITIALIZER;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

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
  irqstate_t flags;

  /* Hold ctx->lock with interrupts disabled so the scheduler hooks
   * (perf_event_task_sched_in/out) cannot preempt us on the same CPU and
   * reenter sched_in/out on the same context while we are mid-update.
   */

  flags = spin_lock_irqsave(&ctx->lock);

  if (event->state >= PERF_EVENT_STATE_INACTIVE)
    {
      spin_unlock_irqrestore(&ctx->lock, flags);
      return OK;
    }

  perf_context_sched_out(ctx);

  event->state = PERF_EVENT_STATE_INACTIVE;

  perf_context_sched_in(ctx);

  spin_unlock_irqrestore(&ctx->lock, flags);

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
  irqstate_t flags;

  /* Hold ctx->lock with interrupts disabled so the scheduler hooks cannot
   * preempt us on the same CPU and reenter sched_out on the same context
   * while we are tearing this event down.
   */

  flags = spin_lock_irqsave(&ctx->lock);

  if (event->state == PERF_EVENT_STATE_OFF)
    {
      spin_unlock_irqrestore(&ctx->lock, flags);
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

  spin_unlock_irqrestore(&ctx->lock, flags);

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
  FAR struct perf_event_context_s *ctx = event->ctx;

  /* Best-effort PMU teardown.  perf_event_sched_out() only clears ACTIVE
   * state and drops the ctx->active_num reference when it can actually stop
   * the counter on its owning CPU (state == ACTIVE && oncpu == this_cpu).
   */

  perf_event_sched_out(event);

  /* Reconcile ctx->active_num for the event we are about to unlink.
   *
   * perf_event_sched_out() above may leave the event ACTIVE: on SMP it
   * returns -EAGAIN when the event migrated to another CPU (its per-CPU PMU
   * state cannot be torn down from the wrong CPU), and the scheduler-hook
   * trylock may have skipped deactivation under contention.  We unlink the
   * event regardless, so drop its active_num reference now; otherwise
   * perf_free_context() trips ASSERT(active_num == 0) on an empty
   * event_list.
   */

  if (event->state == PERF_EVENT_STATE_ACTIVE)
    {
      event->state = PERF_EVENT_STATE_INACTIVE;
#ifdef CONFIG_SMP
      event->oncpu = -1;
#endif
      if (ctx != NULL && ctx->active_num > 0)
        {
          ctx->active_num--;
        }
    }

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

  /* Walk the parent's group list and inherit each group into the child.
   *
   * Do NOT hold parent_ctx->lock here: perf_inherit_event() may sleep
   * (kmm_zalloc, child_mutex), and sleeping under a spinlock with interrupts
   * disabled deadlocks against the scheduler hook.  The walk is safe without
   * it: the parent's group_list is mutated only by the parent itself (via
   * perf open/close), and the parent is currently running this spawn path;
   * the scheduler hooks only read-traverse the list.
   */

  list_for_every_entry(&parent_ctx->group_list, group_leader,
                       struct perf_event_s, group_node)
    {
      ret = perf_inherit_group_events(group_leader, parent, tcb);
      if (ret < 0)
        {
          break;
        }
    }

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
  /* Set EXIT before cancelling the sampling timer to close a cross-CPU
   * re-arm race.  A periodic sampling hrtimer re-arms itself from its own
   * callback; if that callback runs on another CPU during
   * hrtimer_cancel_sync(), it re-inserts the timer right after cancel
   * removes it, so it survives the free and keeps firing on freed memory
   * (leaked timers eventually saturate hrtimer_lock and wedge every CPU).
   * EXIT makes perf_swevent_timer_handle return 0 (do-not-re-arm), then the
   * sync cancel waits for any running callback to finish.  Callers run in
   * task context without ctx->lock, so the sleeping cancel is safe.
   */

  event->state = PERF_EVENT_STATE_EXIT;

  hrtimer_cancel_sync(&event->hw.timer);

  /* Purge the event from any per-CPU software-event list.  Cross-CPU
   * teardown can skip the normal event_del() unlink (sched_out -EAGAIN or
   * a skipped scheduler-hook trylock), leaving a freed node that
   * perf_swevent() would dereference on the next context switch.  No-op if
   * never added.
   */

  perf_swevent_purge(event);

  /* Orphan any inherited children that still point back at this event.
   *
   * A freed fd-owned parent leaves its children (living in still-running
   * tasks' contexts) pointing at it via parent_event/child_list; when such a
   * child later exits, perf_child_detach() would write its count into the
   * freed parent -- a heap-corrupting use-after-free.  Under child_mutex,
   * clear each child's parent_event and PERF_ATTACH_CHILD (making
   * perf_child_detach() a no-op) and unlink it.
   *
   * Only a top-level event owns child_list as a list head; for an inherited
   * child (parent_event != NULL) that member is a node in its parent's list,
   * so restrict the walk to parents.
   */

  if (event->parent_event == NULL)
    {
      nxmutex_lock(&event->child_mutex);
      while (!list_is_empty(&event->child_list))
        {
          FAR struct perf_event_s *child =
            list_first_entry(&event->child_list, struct perf_event_s,
                             child_list);

          child->parent_event = NULL;
          child->attach_state &= ~PERF_ATTACH_CHILD;
          list_delete(&child->child_list);
        }

      nxmutex_unlock(&event->child_mutex);
    }

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

/****************************************************************************
 * Name: perf_teardown_lock / perf_teardown_unlock
 *
 * Description:
 *   Acquire/release the global perf teardown serialization mutex.  See the
 *   g_perf_teardown_lock comment for the SMP lifecycle rationale.  Both are
 *   called only from blockable task context (fd close, task exit, fd
 *   read/ioctl), never from an ISR or the scheduler hooks.
 *
 ****************************************************************************/

void perf_teardown_lock(void)
{
  nxmutex_lock(&g_perf_teardown_lock);
}

void perf_teardown_unlock(void)
{
  nxmutex_unlock(&g_perf_teardown_lock);
}

int perf_function_call(FAR struct perf_event_s *event,
                       nxsched_cpu_call_t func)
{
  FAR struct perf_event_context_s *ctx = event->ctx;

  /* The monitored task may have already exited: perf_event_task_exit()
   * clears event->ctx while the tool still holds the fd and can issue
   * read()/ioctl().  The event is then unscheduled with a final counter, so
   * run func directly on this CPU (a safe no-op / final read); dereferencing
   * a NULL ctx below would crash.
   */

  if (ctx == NULL)
    {
      return func(event);
    }

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

  if (cpu != -1 && cpu >= CPU_SETSIZE)
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
  FAR struct perf_event_context_s *ctx;

  /* Serialize the whole teardown against a concurrent perf_close() on
   * another CPU.  Without this, perf_close() can read one of these events'
   * ctx and fire a synchronous cross-CPU detach IPI referencing it while we
   * free that ctx / event below, faulting in the IPI callback.  Holding the
   * lock across perf_context_detach() (which itself may issue an IPI) is
   * safe: the IPI callback never takes this lock, and perf_close() must
   * acquire it before it can free anything.  perf_event_task_exit() runs in
   * blockable task context (nxtask_exithook, before sched_lock()), so the
   * sleeping mutex is safe here.  Lock order: teardown -> child_mutex
   * (perf_free_event) / perf_event_mutex (perf_free_context) -> ctx->lock.
   */

  perf_teardown_lock();

  ctx = tcb->perf_event_ctx;
  if (ctx != NULL)
    {
      FAR struct perf_event_s *event;
      FAR struct perf_event_s *next;

      list_for_every_entry_safe(&ctx->event_list, event, next,
                                struct perf_event_s, event_node)
        {
          perf_context_detach(event, ctx);

          /* Only free inherited child events here; fd-owned events
           * (parent_event == NULL) are freed by perf_close().
           */

          if (event->parent_event != NULL)
            {
              perf_free_event(event);
            }
          else
            {
              /* fd-owned event: the struct is freed later by perf_close(),
               * but its sampling hrtimer must stop now.  Left running, the
               * self-re-arming timer keeps firing on this exiting task's CPU
               * until the tool closes the fd; if the tool is killed
               * mid-sample each orphan leaks a per-CPU timer that never
               * stops and eventually wedges every CPU.  Cancel synchronously
               * before clearing ctx / freeing pmuctx (safe to sleep in
               * task-exit context; the IPI callback never takes our lock).
               */

              hrtimer_cancel_sync(&event->hw.timer);

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

  perf_teardown_unlock();
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
  irqstate_t flags;

  if (ctx != NULL)
    {
      /* Non-blocking trylock to break a cross-CPU lock-order inversion.
       * This hook runs under g_schedlock; blocking on ctx->lock deadlocks
       * when a concurrent cross-CPU enable/disable holds it via an IPI whose
       * servicing core is itself spinning on g_schedlock (held by us).  If
       * contended, skip activating the events this switch; a later switch
       * or the enable/disable path reconciles the PMU state.  Perf
       * accounting is best-effort, so a rare skipped activation is
       * acceptable.
       */

      if (spin_trylock_irqsave(&ctx->lock, flags))
        {
          perf_context_sched_in(ctx);
          spin_unlock_irqrestore(&ctx->lock, flags);
        }
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
  irqstate_t flags;

  perf_swevent(PERF_COUNT_SW_CONTEXT_SWITCHES, up_getpc());

  if (ctx != NULL)
    {
      /* Non-blocking trylock, same rationale as perf_event_task_sched_in().
       * If contended, skip deactivating the events this switch; they stay
       * ACTIVE and are reconciled on a later switch or the enable/disable
       * path.
       */

      if (spin_trylock_irqsave(&ctx->lock, flags))
        {
          perf_context_sched_out(ctx);
          spin_unlock_irqrestore(&ctx->lock, flags);
        }
    }
}
