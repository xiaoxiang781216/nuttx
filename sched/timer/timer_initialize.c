/****************************************************************************
 * sched/timer/timer_initialize.c
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
#include <nuttx/compiler.h>

#include <sys/types.h>
#include <time.h>
#include <errno.h>

#include <nuttx/irq.h>
#include <nuttx/queue.h>
#include <nuttx/trace.h>

#include "timer/timer.h"

#ifndef CONFIG_DISABLE_POSIX_TIMERS

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef struct posix_timer_s posix_timer_prealloc_t[CONFIG_PREALLOC_TIMERS];

/****************************************************************************
 * Public Data
 ****************************************************************************/

#if CONFIG_PREALLOC_TIMERS > 0
/* This is a list of free, preallocated timer structures */

#  undef g_freetimers
DEFINE_PER_CPU_BSS_BMP(struct posix_timer_s *, g_freetimers);
#  define g_freetimers this_cpu_var_bmp(g_freetimers)
#endif

/* This is a list of instantiated timer structures -- active and inactive.
 * The timers are place on this list by timer_create() and removed from the
 * list by timer_delete() or when the owning thread exits.
 */

#undef g_alloctimers
DEFINE_PER_CPU_BSS_BMP(struct posix_timer_s *, g_alloctimers);
#define g_alloctimers this_cpu_var_bmp(g_alloctimers)

#undef g_locktimers
DEFINE_PER_CPU_BMP(spinlock_t, g_locktimers) = SP_UNLOCKED;
#define g_locktimers this_cpu_var_bmp(g_locktimers)

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: timer_initialize
 *
 * Description:
 *   Boot up configuration of the POSIX timer facility.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void timer_initialize(void)
{
  /* These are the preallocated times */

#if CONFIG_PREALLOC_TIMERS > 0
  static DEFINE_PER_CPU_BSS_BMP(posix_timer_prealloc_t, g_prealloctimers);
#  define g_prealloctimers this_cpu_var_bmp(g_prealloctimers)
#endif

  sched_trace_begin();

#if CONFIG_PREALLOC_TIMERS > 0
  int i;

  /* Place all of the pre-allocated timers into the free timer list */

  for (i = CONFIG_PREALLOC_TIMERS - 1; i >= 0; i--)
    {
      g_prealloctimers[i].pt_flags = PT_FLAGS_PREALLOCATED;
      g_prealloctimers[i].flink = g_freetimers;
      g_freetimers = &g_prealloctimers[i];
    }
#endif

  sched_trace_end();
}

/****************************************************************************
 * Name: timer_deleteall
 *
 * Description:
 *   This function is called whenever a thread exits.  Any timers owned by
 *   that thread are deleted as though called by timer_delete().
 *
 *   It is provided in this file so that it can be weakly defined but also,
 *   like timer_intitialize(), be brought into the link whenever the timer
 *   resources are referenced.
 *
 * Input Parameters:
 *   pid - the task ID of the thread that exited
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void timer_deleteall(pid_t pid)
{
  FAR struct posix_timer_s *timer;
  FAR struct posix_timer_s *next;
  FAR struct posix_timer_s *free = NULL;
  FAR struct posix_timer_s **prev_flink = &g_alloctimers;
  irqstate_t flags;

  flags = spin_lock_irqsave(&g_locktimers);
  for (timer = g_alloctimers; timer != NULL; timer = next)
    {
      next = timer->flink;
      if (timer->pt_owner == pid)
        {
          /* Remove the timer from the allocated timer list  */

          *prev_flink = next;

          /* Push the timer to the free list */

          timer->flink = free;
          free = timer;
        }
      else
        {
          prev_flink = &timer->flink;
        }
    }

  spin_unlock_irqrestore(&g_locktimers, flags);

  for (timer = free; timer != NULL; timer = next)
    {
      next = timer->flink;
      timer_release(timer);
    }
}

/****************************************************************************
 * Name: timer_gethandle
 *
 * Description:
 *   Returns the posix timer in the activity from the corresponding timerid
 *
 * Input Parameters:
 *   timerid - The pre-thread timer, previously created by the call to
 *     timer_create(), to be be set.
 *
 * Returned Value:
 *   On success, timer_gethandle() returns pointer to the posix_timer_s;
 *   On error, NULL is returned.
 *
 ****************************************************************************/

FAR struct posix_timer_s *timer_gethandle(timer_t timerid)
{
  FAR struct posix_timer_s *timer = NULL;
  FAR struct posix_timer_s *entry;
  irqstate_t flags;

  if (timerid != 0u)
    {
      flags = spin_lock_irqsave(&g_locktimers);

      for (entry = g_alloctimers; entry != NULL; entry = entry->flink)
        {
          if ((uintptr_t)entry == timerid)
            {
              timer = entry;
              break;
            }
        }

      spin_unlock_irqrestore(&g_locktimers, flags);
    }

  return timer;
}

#endif /* CONFIG_DISABLE_POSIX_TIMERS */
