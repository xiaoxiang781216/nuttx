/****************************************************************************
 * sched/wqueue/wqueue.h
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
 ****************************************************************************/

#ifndef __SCHED_WQUEUE_WQUEUE_H
#define __SCHED_WQUEUE_WQUEUE_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <semaphore.h>
#include <sys/types.h>
#include <stdbool.h>

#include <nuttx/clock.h>
#include <nuttx/list.h>
#include <nuttx/wqueue.h>
#include <nuttx/spinlock.h>

#ifdef CONFIG_SCHED_WORKQUEUE

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Kernel thread names */

#define HPWORKNAME "hpwork"
#define LPWORKNAME "lpwork"

/* Get the worker structure from the work queue.
 * This function requires the workers are located next to the wqueue.
 */

#define wq_get_worker(wq) (FAR struct kworker_s *)(FAR void *) \
          ((FAR char *)(FAR void *)(wq) + sizeof(struct kwork_wqueue_s))

/****************************************************************************
 * Public Type Definitions
 ****************************************************************************/

/* This represents one worker */

struct kworker_s
{
  pid_t             pid;       /* The task ID of the worker thread */
  FAR struct work_s *work;     /* The work structure */
  sem_t             wait;      /* Sync waiting for worker done */
  int16_t           wait_count;
};

/* This structure defines the state of one kernel-mode work queue */

struct kwork_wqueue_s
{
  struct list_node expired;   /* The queue of expired work. */
  struct list_node pending;   /* The queue of pending work. */
  sem_t            sem;       /* The counting semaphore of the wqueue */
  struct wdog_s    timer;     /* Timer to pending. */
  uint8_t          nthreads;  /* Number of worker threads */
  bool             exit;      /* A flag to request the thread to exit */
  sem_t            exsem;     /* Sync waiting for thread exit */
};

/* This structure defines the state of one high-priority work queue.  This
 * structure must be cast-compatible with kwork_wqueue_s.
 */

#ifdef CONFIG_SCHED_HPWORK
struct hp_wqueue_s
{
  struct kwork_wqueue_s wq;

  /* Describes each thread in the high priority queue's thread pool */

  struct kworker_s      worker[CONFIG_SCHED_HPNTHREADS];
};
#endif

/* This structure defines the state of one low-priority work queue.  This
 * structure must be cast compatible with kwork_wqueue_s
 */

#ifdef CONFIG_SCHED_LPWORK
struct lp_wqueue_s
{
  struct kwork_wqueue_s wq;

  /* Describes each thread in the low priority queue's thread pool */

  struct kworker_s      worker[CONFIG_SCHED_LPNTHREADS];
};
#endif

/****************************************************************************
 * Public Data
 ****************************************************************************/

#ifdef CONFIG_SCHED_HPWORK
/* The state of the kernel mode, high priority work queue. */

DECLARE_PER_CPU_BMP(struct hp_wqueue_s, g_hpwork);
#  define g_hpwork this_cpu_var_bmp(g_hpwork)
#endif

#ifdef CONFIG_SCHED_LPWORK
/* The state of the kernel mode, low priority work queue(s). */

DECLARE_PER_CPU_BMP(struct lp_wqueue_s, g_lpwork);
#  define g_lpwork this_cpu_var_bmp(g_lpwork)
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

static inline_function FAR struct kwork_wqueue_s *work_qid2wq(int qid)
{
  FAR struct kwork_wqueue_s *wq = NULL;
#ifdef CONFIG_SCHED_HPWORK
  if (qid == HPWORK)
    {
      wq = &g_hpwork.wq;
    }
#endif
#ifdef CONFIG_SCHED_LPWORK
  if (qid == LPWORK)
    {
      wq = &g_lpwork.wq;
    }
#endif

  return wq;
}

/****************************************************************************
 * Name: work_insert_pending
 *
 * Description:
 *   Internal public function to insert the work to the workqueue.
 *   Require wqueue != NULL and work != NULL.
 *
 * Input Parameters:
 *   wqueue - The work queue.
 *   work   - The work to be inserted.
 *   is_expired - The work is expired.
 *
 * Returned Value:
 *   Return whether the work is inserted at the head of the pending queue.
 *
 ****************************************************************************/

static inline_function
bool work_insert_pending(FAR struct kwork_wqueue_s *wqueue,
                         FAR struct work_s         *work, bool is_expired)
{
  bool ret = true;

  DEBUGASSERT(wqueue != NULL && work != NULL);

  if (!is_expired)
    {
      FAR struct work_s *head = list_first_entry(&wqueue->pending,
                                                 struct work_s, node);
      FAR struct work_s *curr;

      list_for_every_entry(&wqueue->pending, curr, struct work_s, node)
        {
          if (!clock_compare(curr->qtime, work->qtime))
            {
              break;
            }
        }

      /* After the insertion, we do not violate the invariant that
       * the wait queue is sorted by the expired time. Because
       * curr->qtime > work->qtime.
       * In the case of the wqueue is empty, we insert
       * the work at the head of the wait queue.
       */

      list_add_before(&curr->node, &work->node);
      ret = curr == head;
    }
  else
    {
      list_add_tail(&wqueue->expired, &work->node);
    }

  return ret;
}

/****************************************************************************
 * Name: work_remove
 *
 * Description:
 *   Internal public function to remove the work from the workqueue.
 *   Require wqueue != NULL and work != NULL.
 *
 * Input Parameters:
 *   wqueue - The work queue.
 *   work   - The work to be inserted.
 *
 * Returned Value:
 *   Return whether the head of the pending queue has changed.
 *
 ****************************************************************************/

static inline_function
bool work_remove(FAR struct kwork_wqueue_s *wqueue,
                 FAR struct work_s         *work)
{
  FAR struct work_s *head;

  head = list_first_entry(&wqueue->pending, struct work_s, node);

  /* Seize the ownership from the work thread. */

  work->worker = NULL;

  list_delete(&work->node);

  return head == work;
}

/****************************************************************************
 * Name: work_timer_expired
 *
 * Description:
 *   The wqueue timer callback.
 *
 * Input Parameters:
 *   arg  - The work queue.
 *
 ****************************************************************************/

void work_timer_expired(wdparm_t arg);

/****************************************************************************
 * Name: work_timer_reset
 *
 * Description:
 *   Internal public function to reset the timer of the workqueue.
 *   Require wqueue != NULL.
 *
 * Input Parameters:
 *   wqueue - The work queue.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static inline_function
void work_timer_reset(FAR struct kwork_wqueue_s *wqueue)
{
  if (!list_is_empty(&wqueue->pending))
    {
      FAR struct work_s *work;

      work = list_first_entry(&wqueue->pending, struct work_s, node);

      wd_start_abstick(&wqueue->timer, work->qtime,
                       work_timer_expired, (wdparm_t)wqueue);
    }
  else
    {
      wd_cancel(&wqueue->timer);
    }
}

/****************************************************************************
 * Name: wqueue_lock
 *
 * Description:
 *   Enter critical section and save interrupt state.
 *   When nopreempt is true, additionally call sched_lock (for non-SMP or
 *   contexts that must not trigger preemption on non-SMP).
 *
 * Input Parameters:
 *   wqueue    - The work queue.
 *   nopreempt - Use sched_lock when true on non-SMP.
 *
 * Returned Value:
 *   The saved interrupt state.
 *
 ****************************************************************************/

static inline_function
irqstate_t wqueue_lock(FAR struct kwork_wqueue_s *wqueue, bool nopreempt)
{
  if (nopreempt)
    {
      irqstate_t flags;
      flags = enter_critical_section_notrace();
      sched_lock();
      return flags;
    }
  else
    {
      return enter_critical_section_notrace();
    }
}

/****************************************************************************
 * Name: wqueue_unlock
 *
 * Description:
 *   Leave critical section and restore interrupt state.
 *   The nopreempt flag must match the one used in wqueue_lock.
 *
 * Input Parameters:
 *   wqueue    - The work queue.
 *   flags     - The saved interrupt state from wqueue_lock.
 *   nopreempt - Use sched_unlock when true on non-SMP.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static inline_function
void wqueue_unlock(FAR struct kwork_wqueue_s *wqueue,
                   irqstate_t flags, bool nopreempt)
{
  if (nopreempt)
    {
      leave_critical_section_notrace(flags);
      sched_unlock();
    }
  else
    {
      leave_critical_section_notrace(flags);
    }
}

/****************************************************************************
 * Name: work_start_highpri
 *
 * Description:
 *   Start the high-priority, kernel-mode work queue.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   Return zero (OK) on success.  A negated errno value is returned on
 *   errno value is returned on failure.
 *
 ****************************************************************************/

#ifdef CONFIG_SCHED_HPWORK
int work_start_highpri(void);
#endif

/****************************************************************************
 * Name: work_start_lowpri
 *
 * Description:
 *   Start the low-priority, kernel-mode worker thread(s)
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   Return zero (OK) on success.  A negated errno value is returned on
 *   errno value is returned on failure.
 *
 ****************************************************************************/

#ifdef CONFIG_SCHED_LPWORK
int work_start_lowpri(void);
#endif

/****************************************************************************
 * Name: worklist_initialize
 *
 * Description:
 *   Init worker_list handler BMP case pointer reference core0 value.
 *
 ****************************************************************************/

void worklist_initialize(void);

#else
#  define worklist_initialize()
#endif /* CONFIG_SCHED_WORKQUEUE */
#endif /* __SCHED_WQUEUE_WQUEUE_H */
