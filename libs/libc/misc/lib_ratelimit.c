/****************************************************************************
 * libs/libc/misc/lib_ratelimit.c
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

#include <sys/types.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include <nuttx/clock.h>
#include <nuttx/ratelimit.h>

#ifdef __KERNEL__
#  include <nuttx/spinlock.h>
#else
#  include <pthread.h>
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: ratelimit_islimited
 *
 * Description:
 *   Rate limiting is carried out according to the time window.
 *
 ****************************************************************************/

bool ratelimit_islimited(FAR struct ratelimit_state_s *state)
{
  bool result = true;
  clock_t now;
#ifdef __KERNEL__
  irqstate_t flags;
#endif

  /* The current-limiting parameter is invalid and no
   * current-limiting will be performed.
   */

  if (!state || state->interval == 0)
    {
      return false;
    }

  /* When burst equals zero, it indicates that any
   * traffic is prohibited.
   */

  if (state->burst == 0)
    {
      return true;
    }

#ifdef __KERNEL__
  flags = spin_lock_irqsave_notrace(&state->lock);
#else
  pthread_mutex_lock(&state->lock);
#endif

  now = TICK2SEC(clock());
  if (state->begin == 0)
    {
      state->begin = now;
    }

  /* Reset statistical information. */

  if ((clock_t)(now - state->begin) >= state->interval)
    {
      state->begin = now;
      state->count = 0;
    }

  /* Check if the event is limited. */

  if (state->count < state->burst)
    {
      state->count++;
      result = false;
    }

#ifdef __KERNEL__
  spin_unlock_irqrestore_notrace(&state->lock, flags);
#else
  pthread_mutex_unlock(&state->lock);
#endif

  return result;
}
