/****************************************************************************
 * sched/sched/sched_process_delivered.c
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

#include <stdbool.h>
#include <assert.h>

#include <nuttx/queue.h>

#include "irq/irq.h"
#include "sched/sched.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name:  nxsched_process_delivered
 *
 * Description:
 *    This function is used to process the tcb in g_delivertasks.
 * 1 We use direct locking instead of enter_critical_section
 * to save processing time
 * 2 If there is a higher priority task, we will still perform
 * the higher priority task
 * 3 If the schedule lock is on, the task will be placed in g_pendingtasks
 *
 * Input Parameters:
 *   cpu
 *
 * Returned Value:
 *   OK
 *
 * Assumptions:
 * - The caller must be in irq
 * - current cpu must not be locked
 *
 ****************************************************************************/

void nxsched_process_delivered(int cpu)
{
  FAR dq_queue_t *tasklist;
  FAR struct tcb_s *next;
  FAR struct tcb_s *prev;
  struct tcb_s *btcb = NULL;
  struct tcb_s *tcb;

  DEBUGASSERT(g_cpu_nestcount[cpu] == 0);
  DEBUGASSERT(up_interrupt_context());

  if ((g_cpu_irqset & (1 << cpu)) == 0)
    {
      spin_lock_wo_note(&g_cpu_irqlock);

      g_cpu_irqset |= (1 << cpu);
    }

  tcb = current_task(cpu);

  if (g_delivertasks[cpu] == NULL)
    {
      if (tcb->irqcount <= 0)
        {
          cpu_irqlock_clear();
        }

      return;
    }

  if (nxsched_islocked_tcb(tcb))
    {
      btcb = g_delivertasks[cpu];
      g_delivertasks[cpu] = NULL;
      nxsched_add_prioritized(btcb, &g_pendingtasks);
      btcb->task_state = TSTATE_TASK_PENDING;
      if (tcb->irqcount <= 0)
        {
          cpu_irqlock_clear();
        }

      return;
    }

  btcb = g_delivertasks[cpu];

  for (next = tcb; btcb->sched_priority <= next->sched_priority;
      next = next->flink);

  DEBUGASSERT(next);

  prev = next->blink;
  if (prev == NULL)
    {
      /* Special case:  Insert at the head of the list */

      tasklist = &g_assignedtasks[cpu];
      btcb->flink = next;
      btcb->blink = NULL;
      next->blink = btcb;
      tasklist->head = (FAR dq_entry_t *)btcb;
      btcb->cpu = cpu;
      btcb->task_state = TSTATE_TASK_RUNNING;
      up_update_task(btcb);

      DEBUGASSERT(btcb->flink != NULL);
      next = btcb->flink;
      next->task_state = TSTATE_TASK_ASSIGNED;
    }
  else
    {
      /* Insert in the middle of the list */

      btcb->flink = next;
      btcb->blink = prev;
      prev->flink = btcb;
      next->blink = btcb;
      btcb->cpu = cpu;
      btcb->task_state = TSTATE_TASK_ASSIGNED;
    }

  g_delivertasks[cpu] = NULL;
  tcb = current_task(cpu);

  if (tcb->irqcount <= 0)
    {
      cpu_irqlock_clear();
    }
}
