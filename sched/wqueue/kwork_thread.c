/****************************************************************************
 * sched/wqueue/kwork_thread.c
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

#include <unistd.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#include <debug.h>

#include <nuttx/list.h>
#include <nuttx/wqueue.h>
#include <nuttx/kthread.h>
#include <nuttx/semaphore.h>
#include <nuttx/sched.h>
#include <nuttx/tls.h>

#include "sched/sched.h"
#include "wqueue/wqueue.h"

#if defined(CONFIG_SCHED_WORKQUEUE)

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef CONFIG_SCHED_CRITMONITOR_MAXTIME_WQUEUE
#  define CONFIG_SCHED_CRITMONITOR_MAXTIME_WQUEUE 0
#endif

#if CONFIG_SCHED_CRITMONITOR_MAXTIME_WQUEUE > 0
#  define CALL_WORKER(worker, arg) \
     do \
       { \
         clock_t start; \
         clock_t elapsed; \
         start = perf_gettime(); \
         worker(arg); \
         elapsed = perf_gettime() - start; \
         if (elapsed > CONFIG_SCHED_CRITMONITOR_MAXTIME_WQUEUE) \
           { \
             CRITMONITOR_PANIC("WORKER %p execute too long %ju\n", \
                               worker, (uintmax_t)elapsed); \
           } \
       } \
     while (0)
#else
#  define CALL_WORKER(worker, arg) worker(arg)
#endif

/****************************************************************************
 * Public Data
 ****************************************************************************/

#if defined(CONFIG_SCHED_HPWORK)
/* The state of the kernel mode, high priority work queue(s). */

struct hp_wqueue_s aligned_data(64) g_hpwork =
{
  {
    SP_UNLOCKED,
    {
      NULL,
      NULL,
    },
    {
      NULL,
      NULL,
    },
    SEM_INITIALIZER(0),
    {
      {
        0
      },
    },
    CONFIG_SCHED_HPNTHREADS,
    false,
    SEM_INITIALIZER(0),
  },
};

#endif /* CONFIG_SCHED_HPWORK */

#if defined(CONFIG_SCHED_LPWORK)
/* The state of the kernel mode, low priority work queue(s). */

struct lp_wqueue_s aligned_data(64) g_lpwork =
{
  {
    SP_UNLOCKED,
    {
      NULL,
      NULL,
    },
    {
      NULL,
      NULL,
    },
    SEM_INITIALIZER(0),
    {
      {
        0
      },
    },
    CONFIG_SCHED_LPNTHREADS,
    false,
    SEM_INITIALIZER(0),
  }
};

#endif /* CONFIG_SCHED_LPWORK */

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: work_dispatch
 *
 * Description:
 *   This function is traversing wqueue to call all the worker thread.
 *
 * Input Parameters:
 *   wqueue  - The work queue.
 *   kworker - The worker thread.
 *
 ****************************************************************************/

static void work_dispatch(FAR struct kwork_wqueue_s *wqueue,
                          FAR struct kworker_s *kworker)
{
  FAR struct work_s *work;
  FAR struct work_s *next;
  irqstate_t flags;
  worker_t worker;
  FAR void *arg;

  /* And check first entry in the work queue. Since we have disabled
   * interrupts we know:  (1) we will not be suspended unless we do
   * so ourselves, and (2) there will be no changes to the work queue
   */

  flags = spin_lock_irqsave_nopreempt(&wqueue->lock);

  /* If the worker thread is the first woken thread, the thread
   * should check the pending queue and dispatch the expired work.
   */

  if (!WDOG_ISACTIVE(&wqueue->timer) && !list_is_empty(&wqueue->pending))
    {
      unsigned int count = 0u;
      clock_t      ticks = clock_systime_ticks();

      /* Wake up the worker thread once there is expired work.
       * If some worker threads are busy, here the callback will
       * wake up another waiting work thread.
       *
       * Becareful of the special case that the pending work
       * has been canceled but the timer is expired.
       * In this case we should not wake up any worker thread.
       */

      list_for_every_entry_safe(&wqueue->pending, work, next,
                                struct work_s, node)
        {
          /* Check whether the work has expired. */

          if (!clock_compare(work->qtime, ticks))
            {
              wd_start_abstick(&wqueue->timer, work->qtime,
                              work_timer_expired, (wdparm_t)wqueue);
              break;
            }

          /* Expired work will be moved to tail of the expired queue. */

          list_delete(&work->node);
          list_add_tail(&wqueue->expired, &work->node);

          /* Note that the thread execution this function is also
           * a worker thread, which has already been woken up by the timer.
           * So only `count - 1` semaphore will be posted.
           */

          if (count++ > 0u)
            {
              nxsem_post(&wqueue->sem);
            }
        }
    }

  while (!list_is_empty(&wqueue->expired))
    {
      work = list_first_entry(&wqueue->expired, struct work_s, node);

      list_delete(&work->node);

      /* Extract the work description from the entry (in case the
       * work instance will be reused after it has been de-queued).
       */

      worker = work->worker;

      /* Extract the work argument (before re-enabling interrupts) */

      arg = work->arg;

      /* Return the work structure ownership to the work owner. */

      work->worker = NULL;

      /* Mark the thread busy */

      kworker->work = work;

      spin_unlock_irqrestore_nopreempt(&wqueue->lock, flags);

      /* Do the work.  Re-enable interrupts while the work is being
       * performed... we don't have any idea how long this will take!
       */

      CALL_WORKER(worker, arg);
      flags = spin_lock_irqsave_nopreempt(&wqueue->lock);

      /* Mark the thread un-busy */

      kworker->work = NULL;

      /* Check if someone is waiting, if so, wakeup it */

      while (kworker->wait_count > 0)
        {
          kworker->wait_count--;
          nxsem_post(&kworker->wait);
        }
    }

  spin_unlock_irqrestore_nopreempt(&wqueue->lock, flags);
}

/****************************************************************************
 * Name: work_thread
 *
 * Description:
 *   These are the worker threads that perform the actions placed on the
 *   high priority work queue.
 *
 *   These, along with the lower priority worker thread(s) are the kernel
 *   mode work queues (also built in the flat build).
 *
 *   All kernel mode worker threads are started by the OS during normal
 *   bring up.  This entry point is referenced by OS internally and should
 *   not be accessed by application logic.
 *
 * Input Parameters:
 *   argc, argv
 *
 * Returned Value:
 *   Does not return
 *
 ****************************************************************************/

static int work_thread(int argc, FAR char *argv[])
{
  FAR struct kwork_wqueue_s *wqueue;
  FAR struct kworker_s      *kworker;

  /* Get the handle from argv */

  wqueue  = (FAR struct kwork_wqueue_s *)
            ((uintptr_t)strtoul(argv[1], NULL, 16));
  kworker = (FAR struct kworker_s *)
            ((uintptr_t)strtoul(argv[2], NULL, 16));

  /* Loop until wqueue->exit != 0.
   * Since the only way to set wqueue->exit is to call work_queue_free(),
   * there is no need for entering the critical section.
   */

  while (!wqueue->exit)
    {
      work_dispatch(wqueue, kworker);

      /* Wait for the semaphore to be posted by the wqueue timer. */

      nxsem_wait_uninterruptible(&wqueue->sem);
    }

  nxsem_post(&wqueue->exsem);
  return OK;
}

/****************************************************************************
 * Name: work_thread_create
 *
 * Description:
 *   This function creates and activates a work thread task with kernel-
 *   mode privileges.
 *
 * Input Parameters:
 *   name       - Name of the new task
 *   priority   - Priority of the new task
 *   stack_addr - Stack buffer of the new task
 *   stack_size - size (in bytes) of the stack needed
 *   wqueue     - Work queue instance
 *
 * Returned Value:
 *   A negated errno value is returned on failure.
 *
 ****************************************************************************/

static int work_thread_create(FAR const char *name, int priority,
                              FAR void *stack_addr, int stack_size,
                              FAR struct kwork_wqueue_s *wqueue)
{
  FAR struct kworker_s *worker = wq_get_worker(wqueue);
  FAR void *stack = NULL;
  FAR char *argv[3];
  char arg0[32];
  char arg1[32];
  uint8_t wndx;
  int ret = OK;

  /* Don't permit any of the threads to run until we have fully initialized
   * all of them.
   */

  sched_lock();

  for (wndx = 0u; wndx < wqueue->nthreads; wndx++)
    {
      nxsem_init(&worker[wndx].wait, 0, 0u);

      snprintf(arg0, sizeof(arg0), "%p", wqueue);
      snprintf(arg1, sizeof(arg1), "%p", &worker[wndx]);
      argv[0] = arg0;
      argv[1] = arg1;
      argv[2] = NULL;

      /* In case of the stack_addr is NULL */

      if (stack_addr)
        {
          stack = ((FAR char *)stack_addr +
                             (int)wndx * stack_size);
        }

      ret = kthread_create_with_stack(name, priority, stack,
                                      stack_size, work_thread, argv);

      DEBUGASSERT(ret > 0);
      if (ret < 0)
        {
          serr("ERROR: work_thread_create %d failed: %d\n", wndx, ret);
          break;
        }
      else
        {
          worker[wndx].pid = ret;
        }
    }

  sched_unlock();
  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: work_qeueue_dispatch
 *
 * Description:
 *   Execute all pending work items on a given work queue by iterating
 *   through each worker thread and invoking its execution routine.
 *
 * Input Parameters:
 *   wqueue - Work queue instance.
 *
 ****************************************************************************/

void work_qeueue_dispatch(FAR struct kwork_wqueue_s *wqueue)
{
  FAR struct kworker_s *kworker = wq_get_worker(wqueue);
  pid_t pid = nxsched_gettid();
  uint8_t wndx;

  for (wndx = 0; wndx < wqueue->nthreads; wndx++)
    {
      if (pid == kworker[wndx].pid)
        {
          work_dispatch(wqueue, &kworker[wndx]);
          break;
        }
    }
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

void work_timer_expired(wdparm_t arg)
{
  /* The work time expired callback will wake up at least one worker thread
   * to dispatch the expired work.
   */

  FAR struct kwork_wqueue_s *wq = (FAR struct kwork_wqueue_s *)arg;
  nxsem_post(&wq->sem);
}

/****************************************************************************
 * Name: work_queue_create
 *
 * Description:
 *   Create a new work queue. The work queue is identified by its work
 *   queue ID, which is used to queue works to the work queue and to
 *   perform other operations on the work queue.
 *   This function will create a work thread pool with nthreads threads.
 *   The work queue ID is returned on success.
 *
 * Input Parameters:
 *   name       - Name of the new task
 *   priority   - Priority of the new task
 *   stack_addr - Stack buffer of the new task
 *   stack_size - size (in bytes) of the stack needed
 *   nthreads   - Number of work thread should be created
 *
 * Returned Value:
 *   The work queue handle returned on success.  Otherwise, NULL
 *
 ****************************************************************************/

FAR struct kwork_wqueue_s *work_queue_create(FAR const char *name,
                                             int priority,
                                             FAR void *stack_addr,
                                             int stack_size,
                                             uint8_t nthreads)
{
  FAR struct kwork_wqueue_s *wqueue = NULL;
  int ret;

  if (nthreads >= 1u)
    {
      /* Allocate a new work queue */

      wqueue = kmm_zalloc(sizeof(struct kwork_wqueue_s) +
                          nthreads * sizeof(struct kworker_s));
      if (wqueue)
        {
          /* Initialize the work queue structure */

          list_initialize(&wqueue->expired);
          list_initialize(&wqueue->pending);
          nxsem_init(&wqueue->sem, 0, 0u);
          nxsem_init(&wqueue->exsem, 0, 0u);
          wqueue->nthreads = nthreads;
          spin_lock_init(&wqueue->lock);

          /* Create the work queue thread pool */

          ret = work_thread_create(name, priority, stack_addr, stack_size,
                                   wqueue);
          if (ret < 0)
            {
              kmm_free(wqueue);
              wqueue = NULL;
            }
        }
    }

  return wqueue;
}

/****************************************************************************
 * Name: work_queue_free
 *
 * Description:
 *   Destroy a work queue. The work queue is identified by its work queue ID.
 *   All worker threads will be destroyed and the work queue will be freed.
 *   The work queue ID is invalid after this function returns.
 *
 * Input Parameters:
 *  qid - The work queue ID
 *
 * Returned Value:
 *   Zero on success, a negated errno value on failure.
 *
 ****************************************************************************/

int work_queue_free(FAR struct kwork_wqueue_s *wqueue)
{
  uint8_t wndx;
  int ret = -EINVAL;

  if (wqueue != NULL)
    {
      ret = OK;
      wd_cancel(&wqueue->timer);

      /* Mark the work queue as exiting */

      wqueue->exit = true;

      /* Queue a exit work for all threads */

      for (wndx = 0u; wndx < wqueue->nthreads; wndx++)
        {
          nxsem_post(&wqueue->sem);
        }

      for (wndx = 0u; wndx < wqueue->nthreads; wndx++)
        {
          nxsem_wait_uninterruptible(&wqueue->exsem);
        }

      nxsem_destroy(&wqueue->sem);
      nxsem_destroy(&wqueue->exsem);
      kmm_free(wqueue);
    }

  return ret;
}

/****************************************************************************
 * Name: work_queue_in_queue
 *
 * Description:
 *   Check if the execute context is in the work queue thread.
 *
 * Input Parameters:
 *  wqueue - The work queue handle
 *
 * Returned Value:
 *   true if the current thread is in the work queue, false otherwise.
 *
 * **************************************************************************/

bool work_queue_in_queue(FAR struct kwork_wqueue_s *wqueue)
{
  FAR struct kworker_s *worker = wq_get_worker(wqueue);
  pid_t pid = nxsched_gettid();
  bool ret = false;
  uint8_t i;

  for (i = 0u; i < wqueue->nthreads; i++)
    {
      if (pid == worker[i].pid)
        {
          ret = true;
        }
    }

  return ret;
}

/****************************************************************************
 * Name: work_queue_priority_wq
 *
 * Description: Get priority of the wqueue. We believe that all worker
 *   threads have the same priority.
 *
 * Input Parameters:
 *  wqueue - The work queue handle
 *
 * Returned Value:
 *   SCHED_PRIORITY_MIN ~ SCHED_PRIORITY_MAX  on success,
 *   a negated errno value on failure.
 *
 ****************************************************************************/

int work_queue_priority_wq(FAR struct kwork_wqueue_s *wqueue)
{
  FAR struct kworker_s *worker;
  FAR struct tcb_s *tcb;
  int ret = -EINVAL;

  if (wqueue != NULL)
    {
      /* Find for the TCB associated with matching PID */

      worker = wq_get_worker(wqueue);

      tcb = nxsched_get_tcb(worker[0].pid);

      if (!tcb)
        {
          ret = -ESRCH;
        }
      else
        {
          ret = (int)tcb->sched_priority;
          nxsched_put_tcb(tcb);
        }
    }

  return ret;
}

int work_queue_priority(int qid)
{
  return work_queue_priority_wq(work_qid2wq(qid));
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
int work_start_highpri(void)
{
  /* Start the high-priority, kernel mode worker thread(s) */

#ifdef SCHED_HPWORKSTACKSECTION
  static aligned_data(STACK_ALIGNMENT) uint8_t
  g_hp_work_stack[CONFIG_SCHED_HPNTHREADS * CONFIG_SCHED_HPWORKSTACKSIZE]
  locate_data(CONFIG_SCHED_HPWORKSTACKSECTION);
#else
  static aligned_data(STACK_ALIGNMENT) uint8_t
  g_hp_work_stack[CONFIG_SCHED_HPNTHREADS * CONFIG_SCHED_HPWORKSTACKSIZE];
#endif

  sinfo("Starting high-priority kernel worker thread(s)\n");

  return work_thread_create(HPWORKNAME,
                            CONFIG_SCHED_HPWORKPRIORITY,
                            g_hp_work_stack,
                            CONFIG_SCHED_HPWORKSTACKSIZE,
                            &g_hpwork.wq);
}
#endif /* CONFIG_SCHED_HPWORK */

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
int work_start_lowpri(void)
{
  /* Start the low-priority, kernel mode worker thread(s) */

#ifdef SCHED_LPWORKSTACKSECTION
  static aligned_data(STACK_ALIGNMENT) uint8_t
  g_lp_work_stack[CONFIG_SCHED_LPNTHREADS * CONFIG_SCHED_LPWORKSTACKSIZE]
  locate_data(CONFIG_SCHED_LPWORKSTACKSECTION);
#else
  static aligned_data(STACK_ALIGNMENT) uint8_t
  g_lp_work_stack[CONFIG_SCHED_LPNTHREADS * CONFIG_SCHED_LPWORKSTACKSIZE];
#endif

  sinfo("Starting low-priority kernel worker thread(s)\n");

  return work_thread_create(LPWORKNAME,
                            CONFIG_SCHED_LPWORKPRIORITY,
                            g_lp_work_stack,
                            CONFIG_SCHED_LPWORKSTACKSIZE,
                            &g_lpwork.wq);
}
#endif /* CONFIG_SCHED_LPWORK */

/****************************************************************************
 * Name: worklist_initialize
 *
 * Description:
 *   Init worker_list handler BMP case pointer reference core0 value.
 *
 ****************************************************************************/

void worklist_initialize(void)
{
  FAR struct kwork_wqueue_s *wq;

  UNUSED(wq);

#ifdef CONFIG_SCHED_HPWORK
  wq = &g_hpwork.wq;

  /* For BMP compile time init will make cpun point to cpu0 */

  list_initialize(&wq->expired);
  list_initialize(&wq->pending);
#endif

#ifdef CONFIG_SCHED_LPWORK
  wq = &g_lpwork.wq;

  /* For BMP compile time init will make cpun point to cpu0 */

  list_initialize(&wq->expired);
  list_initialize(&wq->pending);
#endif
}

#endif /* CONFIG_SCHED_WORKQUEUE */
