/****************************************************************************
 * sched/addrenv/addrenv.c
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
#include <debug.h>

#include <nuttx/addrenv.h>
#include <nuttx/irq.h>
#include <nuttx/sched.h>
#include <nuttx/wqueue.h>

#include "sched/sched.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* This variable holds the current address environment. These contents are
 * _never_ NULL, besides when the system is started and there are only the
 * initial kernel mappings available.
 *
 * This must only be accessed with interrupts disabled.
 *
 * REVISIT: Try to get rid of this, global bookkeeping for this is dangerous.
 */

static DEFINE_PER_CPU_BSS(FAR struct addrenv_s *, g_addrenv);
static spinlock_t g_addrenv_lock = SP_UNLOCKED;

#define g_addrenv this_cpu_var(g_addrenv)

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: addrenv_destroy
 *
 * Description:
 *   Deferred service routine for destroying an address environment. This is
 *   so that the heavy lifting is not done when the context is switching, or
 *   from ISR.
 *
 * Input Parameters:
 *   arg - Contains pointer to the address environment that is freed.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void addrenv_destroy(FAR void *arg)
{
  FAR struct addrenv_s *addrenv = (FAR struct addrenv_s *)arg;

  /* Destroy the address environment */

  up_addrenv_destroy(&addrenv->addrenv);

  /* Then finally release the memory */

  kmm_delayfree(addrenv);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: addrenv_switch
 *
 * Description:
 *   Instantiate the group address environment for the current thread at the
 *   the head of the ready to run list.
 *
 *   This function is called from platform-specific code after any context
 *   switch (i.e., after any change in the thread at the head of the
 *   ready-to-run list).  This function will change the address environment
 *   if the new thread is part of a different task group.
 *
 * Input Parameters:
 *   tcb - The TCB of thread that needs an address environment.  This should
 *         be the TCB at the head of the ready-to-run list, but that is not
 *         enough.
 *
 * Returned Value:
 *   Zero (OK) is returned on success.  A negated errno value is returned on
 *   any failure.
 *
 * Assumptions:
 *   This function should only be called within critical OS sections with
 *   interrupts disabled.  Interrupts are disabled internally just to be
 *   certain, however.
 *
 ****************************************************************************/

int addrenv_switch(FAR struct tcb_s *tcb)
{
  FAR struct addrenv_s *curr;
  FAR struct addrenv_s *next;
  irqstate_t flags;
  int ret;

  next = tcb->addrenv_curr;

  flags = spin_lock_irqsave(&g_addrenv_lock);

  curr = g_addrenv;

  /* Are we going to change address environments? */

  if (curr != next)
    {
      /* Yes.. Is there a current address environment in place? */

      if (curr)
        {
          /* We need to flush the D-Cache and Invalidate the I-Cache for
           * the group whose environment is disappearing.
           */

          up_addrenv_coherent(&curr->addrenv);
        }

      if (next)
        {
          /* While the address environment is instantiated, it cannot
           * be freed.
           */

          addrenv_take(next);

          /* Instantiate the new address environment (removing the old
           * environment in the process).  For the case of kernel threads,
           * the old mappings will be removed and no new mappings will be
           * instantiated.
           */

          ret = up_addrenv_select(&next->addrenv);
          if (ret < 0)
            {
              berr("ERROR: up_addrenv_select failed: %d\n", ret);
            }
        }

      /* Save the new, current address environment group */

      g_addrenv = next;
    }

#ifdef CONFIG_ARCH_STACK_PROTECT
  if ((atomic_read(&tcb->flags) & TCB_FLAG_TTYPE_MASK) !=
      TCB_FLAG_TTYPE_KERNEL)
    {
      up_addrenv_ustackswitch(tcb);
    }
#  ifdef CONFIG_ARCH_KSTACK_PROTECT
  else
    {
      up_addrenv_kstackswitch(tcb);
    }
#  endif
#endif

  spin_unlock_irqrestore(&g_addrenv_lock, flags);

  /* addrenv->refs already atomic, once we reach 0, always need drop
   * so not necessary to drop inside g_addrenv_lock.
   */

  if (curr != next && curr != NULL)
    {
      addrenv_drop(curr, true);
    }

  return OK;
}

/****************************************************************************
 * Name: addrenv_allocate
 *
 * Description:
 *   Allocate an address environment for a new process.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   Pointer to the new address environment, or NULL if out of memory.
 *
 ****************************************************************************/

FAR struct addrenv_s *addrenv_allocate(void)
{
  FAR struct addrenv_s *addrenv;

  addrenv = kmm_zalloc(sizeof(struct addrenv_s));
  if (addrenv)
    {
      /* Take reference so this won't get freed */

      atomic_set(&addrenv->refs, 1);
    }

  return addrenv;
}

/****************************************************************************
 * Name: addrenv_attach
 *
 * Description:
 *   Attach address environment to a newly created group. Called by exec()
 *   right before injecting the new process into the system.
 *
 * Input Parameters:
 *   tcb     - The tcb of the newly loaded task.
 *   addrenv - The address environment that is attached.
 *
 * Returned Value:
 *   This is a NuttX internal function so it follows the convention that
 *   0 (OK) is returned on success and a negated errno is returned on
 *   failure.
 *
 ****************************************************************************/

int addrenv_attach(FAR struct tcb_s *tcb, FAR struct addrenv_s *addrenv)
{
  /* Attach the address environment */

  FAR struct task_group_s *group = tcb->group;

  if (group == NULL)
    {
      group = (FAR struct task_group_s *)(tcb + 1);
    }

  group->tg_addrenv_own = addrenv;
  tcb->addrenv_curr = group->tg_addrenv_own;

  return OK;
}

/****************************************************************************
 * Name: addrenv_join
 *
 * Description:
 *   Join the parent process's address environment.
 *
 * Input Parameters:
 *   ptcb - The tcb of the parent process
 *   tcb  - The tcb of the child process
 *
 * Returned Value:
 *   This is a NuttX internal function so it follows the convention that
 *   0 (OK) is returned on success and a negated errno is returned on
 *   failure.
 *
 ****************************************************************************/

int addrenv_join(FAR struct tcb_s *ptcb, FAR struct tcb_s *tcb)
{
  FAR struct task_group_s *group = tcb->group;
  int ret;

  ret = up_addrenv_attach(ptcb, tcb);
  if (ret < 0)
    {
      berr("ERROR: up_addrenv_attach failed: %d\n", ret);
    }
  else
    {
      if (group == NULL)
        {
          group = (FAR struct task_group_s *)(tcb + 1);
        }

      /* Take a reference to the address environment */

      addrenv_take(ptcb->group->tg_addrenv_own);

      /* Share the parent's address environment */

      group->tg_addrenv_own = ptcb->group->tg_addrenv_own;
      tcb->addrenv_curr = group->tg_addrenv_own;
    }

  return ret;
}

/****************************************************************************
 * Name: addrenv_leave
 *
 * Description:
 *   Leave a process's address environment.
 *
 * Input Parameters:
 *   tcb  - The tcb of the process
 *
 * Returned Value:
 *   This is a NuttX internal function so it follows the convention that
 *   0 (OK) is returned on success and a negated errno is returned on
 *   failure.
 *
 ****************************************************************************/

int addrenv_leave(FAR struct tcb_s *tcb)
{
  int ttype = atomic_read(&tcb->flags) & TCB_FLAG_TTYPE_MASK;
  int ret;

  /* Detach from the address environment */

  ret = up_addrenv_detach(tcb);

  /* Then drop the address environment */

  addrenv_drop(tcb->group->tg_addrenv_own, false);
  if (ttype == TCB_FLAG_TTYPE_TASK)
    {
      tcb->group->tg_addrenv_own = NULL;
    }

  return ret;
}

/****************************************************************************
 * Name: addrenv_select
 *
 * Description:
 *   Temporarily select a different address environment for the currently
 *   running process.
 *
 * Input Parameters:
 *   addrenv - The address environment to instantiate.
 *   oldenv  - The old active address environment is placed here.
 *
 * Returned Value:
 *   This is a NuttX internal function so it follows the convention that
 *   0 (OK) is returned on success and a negated errno is returned on
 *   failure.
 *
 ****************************************************************************/

int addrenv_select(FAR struct addrenv_s *addrenv,
                   FAR struct addrenv_s **oldenv)
{
  FAR struct tcb_s *tcb = this_task();
  addrenv_take(addrenv);
  *oldenv = tcb->addrenv_curr;
  tcb->addrenv_curr = addrenv;
  return addrenv_switch(tcb);
}

/****************************************************************************
 * Name: addrenv_restore
 *
 * Description:
 *   Switch back to the procces's previous address environment.
 *
 * Input Parameters:
 *   addrenv - The address environment to restore.
 *
 * Returned Value:
 *   This is a NuttX internal function so it follows the convention that
 *   0 (OK) is returned on success and a negated errno is returned on
 *   failure.
 *
 ****************************************************************************/

int addrenv_restore(FAR struct addrenv_s *addrenv)
{
  FAR struct tcb_s *tcb = this_task();
  addrenv_give(tcb->addrenv_curr);
  tcb->addrenv_curr = addrenv;
  return addrenv_switch(tcb);
}

/****************************************************************************
 * Name: addrenv_take
 *
 * Description:
 *   Take a reference to an address environment.
 *
 * Input Parameters:
 *   addrenv - The address environment.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

void addrenv_take(FAR struct addrenv_s *addrenv)
{
  if (addrenv)
    {
      atomic_fetch_add(&addrenv->refs, 1);
    }
}

/****************************************************************************
 * Name: addrenv_give
 *
 * Description:
 *   Give back a reference to an address environment, obtaining the resulting
 *   reference counter as returned value.
 *
 * Input Parameters:
 *   addrenv - The address environment.
 *
 * Returned Value:
 *   Remaining reference count.
 *
 ****************************************************************************/

int addrenv_give(FAR struct addrenv_s *addrenv)
{
  int refs = 0;

  if (addrenv)
    {
      refs = atomic_fetch_sub(&addrenv->refs, 1);
      refs--;
    }

  return refs;
}

/****************************************************************************
 * Name: addrenv_drop
 *
 * Description:
 *   Drop an address environment.
 *
 * Input Parameters:
 *   addrenv - The address environment.
 *   deferred - yes: The address environment should be dropped by the worker
 *              no:  The address environment can be dropped at once
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

void addrenv_drop(FAR struct addrenv_s *addrenv, bool deferred)
{
  /* If no more users, the address environment can be dropped */

  if (addrenv != NULL && addrenv_give(addrenv) == 0)
    {
      /* Defer dropping if requested to do so, otherwise drop at once */

      if (deferred)
        {
          /* Let the DSR do the heavy lifting */

          work_queue(LPWORK, &addrenv->work, addrenv_destroy, addrenv, 0);
        }
      else
        {
          addrenv_destroy(addrenv);
        }
    }
}
