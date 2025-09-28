/****************************************************************************
 * sched/irq/irq_spinlock.c
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
#include <nuttx/spinlock.h>

#include <assert.h>
#include <debug.h>
#include <sys/types.h>
#include <arch/irq.h>
#include <execinfo.h>

#include "sched/sched.h"

#if defined(CONFIG_SPINLOCK)

/****************************************************************************
 * Public Data
 ****************************************************************************/

/****************************************************************************
 * Public Functions
 ****************************************************************************/

#ifdef CONFIG_RW_SPINLOCK

/****************************************************************************
 * Name: read_lock_irqsave
 *
 * Description:
 *   If SMP is enabled:
 *     The argument lock should be specified,
 *     disable local interrupts and take the lock spinlock and return
 *     the interrupt state.
 *
 *     NOTE: This API is very simple to protect data (e.g. H/W register
 *     or internal data structure) in SMP mode. Do not use this API
 *     with kernel APIs which suspend a caller thread. (e.g. nxsem_wait)
 *
 *   If SMP is not enabled:
 *     This function is equivalent to up_irq_save().
 *
 * Input Parameters:
 *   lock - Caller specific spinlock, not NULL.
 *
 * Returned Value:
 *   An opaque, architecture-specific value that represents the state of
 *   the interrupts prior to the call to write_lock_irqsave(lock);
 *
 ****************************************************************************/

irqstate_t read_lock_irqsave(FAR rwlock_t *lock)
{
  irqstate_t ret;
  ret = up_irq_save();

  read_lock(lock);

  return ret;
}

/****************************************************************************
 * Name: read_unlock_irqrestore
 *
 * Description:
 *   If SMP is enabled:
 *     The argument lock should be specified, release the lock and
 *     restore the interrupt state as it was prior to the previous call to
 *     read_lock_irqsave(lock).
 *
 *   If SMP is not enabled:
 *     This function is equivalent to up_irq_restore().
 *
 * Input Parameters:
 *   lock - Caller specific spinlock, not NULL.
 *
 *   flags - The architecture-specific value that represents the state of
 *           the interrupts prior to the call to read_lock_irqsave(lock);
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void read_unlock_irqrestore(rwlock_t *lock, irqstate_t flags)
{
  read_unlock(lock);
  up_irq_restore(flags);
}

/****************************************************************************
 * Name: write_lock_irqsave
 *
 * Description:
 *   If SMP is enabled:
 *     The argument lock should be specified,
 *     disable local interrupts and take the lock spinlock and return
 *     the interrupt state.
 *
 *     NOTE: This API is very simple to protect data (e.g. H/W register
 *     or internal data structure) in SMP mode. But do not use this API
 *     with kernel APIs which suspend a caller thread. (e.g. nxsem_wait)
 *
 *   If SMP is not enabled:
 *     This function is equivalent to up_irq_save().
 *
 * Input Parameters:
 *   lock - Caller specific spinlock, not NULL.
 *
 * Returned Value:
 *   An opaque, architecture-specific value that represents the state of
 *   the interrupts prior to the call to write_lock_irqsave(lock);
 *
 ****************************************************************************/

irqstate_t write_lock_irqsave(rwlock_t *lock)
{
  irqstate_t ret;
  ret = up_irq_save();

  write_lock(lock);

  return ret;
}

/****************************************************************************
 * Name: write_unlock_irqrestore
 *
 * Description:
 *   If SMP is enabled:
 *     The argument lock should be specified, release the lock and
 *     restore the interrupt state as it was prior to the previous call to
 *     write_lock_irqsave(lock).
 *
 *   If SMP is not enabled:
 *     This function is equivalent to up_irq_restore().
 *
 * Input Parameters:
 *   lock - Caller specific spinlock, not NULL.
 *
 *   flags - The architecture-specific value that represents the state of
 *           the interrupts prior to the call to write_lock_irqsave(lock);
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void write_unlock_irqrestore(rwlock_t *lock, irqstate_t flags)
{
  write_unlock(lock);

  up_irq_restore(flags);
}
#endif /* CONFIG_RW_SPINLOCK */

#ifdef CONFIG_SPINLOCK_BACKTRACE

/****************************************************************************
 * Name: spinlock_add_backtrace
 *
 * Description:
 *   Record the backtrace of the caller who locks the spinlock.
 *
 *   NOTE：The backtrace_record function is relatively complex and
 *   involves malloc (which may cause context switching).
 *
 * Input Parameters:
 *   info - Caller specific spinlock, not NULL.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void spinlock_add_backtrace(FAR spinlock_debug_t *info)
{
  info->stack = backtrace_record(0);
}

static void spinlock_remove_backtrace(FAR spinlock_debug_t *info)
{
  backtrace_remove(info->stack);
  info->stack = NULL;
}
#else
#  define spinlock_add_backtrace(info)
#  define spinlock_remove_backtrace(info)
#endif

#ifdef CONFIG_SPINLOCK_DEBUG
static void dump_spinlock(FAR sq_queue_t *hold_spinlock)
{
  FAR sq_entry_t *entry;

  sq_for_every(hold_spinlock, entry)
    {
      FAR spinlock_debug_t *info = (FAR spinlock_debug_t *)entry;
#  ifdef CONFIG_SPINLOCK_BACKTRACE
      char buf[BACKTRACE_BUFFER_SIZE(CONFIG_LIBC_BACKTRACE_DEPTH)];
      FAR void **stack;
      int depth;

      stack = backtrace_get(info->stack, &depth);
      backtrace_format(buf, sizeof(buf), stack, depth);
      _alert("Spinlock(%p) backtrace: %s\n", info, buf);
#  else
      _alert("Spinlock(%p) has been locked\n", info);
#  endif
    }
}

void spinlock_mark_locked(FAR spinlock_debug_t *info)
{
  FAR struct tcb_s *rtcb = running_task();

  /* Since enter_critical_section allows lock context switching and
   * repeated locking, no mark is required.
   */

  if (info == &g_schedlock.info)
    {
      return;
    }

  /* Check if the spinlock has been held. */

  if (info->holder != NULL)
    {
      _alert("Detect thread(%d) relocks spinlock(%p)!\n",
             rtcb->pid, info);
      dump_spinlock(&rtcb->hold_spinlock);
      PANIC();
    }

  /* Set info and enqueue. */

  info->holder = rtcb;
  spinlock_add_backtrace(info);
  sq_addlast(&info->flink, &rtcb->hold_spinlock);
}

void spinlock_mark_unlocked(FAR spinlock_debug_t *info)
{
  FAR struct tcb_s *rtcb = running_task();

  /* Since enter_critical_section allows lock context switching and
   * repeated locking, no mark is required.
   */

  if (info == &g_schedlock.info)
    {
      return;
    }

  /* Check if locking and unlocking are the same task. */

  if (info->holder != rtcb)
    {
      _alert("Detect thread(%d) unlocks spinlock(%p) which is not held!\n",
             rtcb->pid, info);
      dump_spinlock(&rtcb->hold_spinlock);
      PANIC();
    }

  /* Currently, NuttX does not support unlock in the reverse order, since
   * this will pollute flags. So check if unlocking is the reverse order.
   */

  if (sq_tail(&rtcb->hold_spinlock) != &info->flink)
    {
      _alert("Detect thread(%d) unlocks spinlock(%p) in the wrong order!\n",
             rtcb->pid, info);
      dump_spinlock(&rtcb->hold_spinlock);
      PANIC();
    }

  /* Reset info and dequeue. */

  info->holder = NULL;
  spinlock_remove_backtrace(info);
  sq_remlast(&rtcb->hold_spinlock);
}

void spinlock_switch_context(FAR struct tcb_s *from)
{
  if (!sq_empty(&from->hold_spinlock))
    {
      _alert("Detect thread(%d) switches context while holding spinlock!\n",
             from->pid);
      dump_spinlock(&from->hold_spinlock);
      PANIC();
    }
}

#endif /* CONFIG_SPINLOCK_DEBUG */

#endif /* CONFIG_SPINLOCK */
