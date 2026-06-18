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

#if defined(__KERNEL__) || defined(CONFIG_BUILD_FLAT)
#  include <nuttx/spinlock.h>
#else
#  include <pthread.h>
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: ratelimit_lock
 ****************************************************************************/

static inline irqstate_t
ratelimit_lock(FAR struct ratelimit_state_s *state)
{
#if defined(__KERNEL__) || defined(CONFIG_BUILD_FLAT)
  return spin_lock_irqsave_notrace(&state->lock.spin);
#else
  pthread_mutex_lock(&state->lock.mutex);
  return 0;
#endif
}

/****************************************************************************
 * Name: ratelimit_unlock
 ****************************************************************************/

static inline void
ratelimit_unlock(FAR struct ratelimit_state_s *state, irqstate_t flags)
{
#if defined(__KERNEL__) || defined(CONFIG_BUILD_FLAT)
  spin_unlock_irqrestore_notrace(&state->lock.spin, flags);
#else
  (void)flags;
  pthread_mutex_unlock(&state->lock.mutex);
#endif
}

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
  irqstate_t flags;
  clock_t now;

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

  flags = ratelimit_lock(state);

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
  else
    {
      state->missed++;
    }

  ratelimit_unlock(state, flags);

  return result;
}

/****************************************************************************
 * Name: ratelimit_setlimit
 *
 * Description:
 *   Update the rate limiting parameters (time window and burst). The
 *   accounting window is reset so that the new setting takes effect from a
 *   fresh time window.
 *
 ****************************************************************************/

void ratelimit_setlimit(FAR struct ratelimit_state_s *state,
                        unsigned int interval, unsigned int burst)
{
  irqstate_t flags;

  if (state == NULL)
    {
      return;
    }

  flags = ratelimit_lock(state);

  state->interval = interval;
  state->burst    = burst;

  /* Reset the accounting window so the new setting applies immediately. */

  state->begin = 0;
  state->count = 0;

  ratelimit_unlock(state, flags);
}

/****************************************************************************
 * Name: ratelimit_getlimit
 *
 * Description:
 *   Read back the current rate limiting parameters.  NULL output pointers
 *   are ignored.
 *
 ****************************************************************************/

void ratelimit_getlimit(FAR struct ratelimit_state_s *state,
                        FAR unsigned int *interval, FAR unsigned int *burst)
{
  irqstate_t flags;

  if (state == NULL)
    {
      return;
    }

  flags = ratelimit_lock(state);

  if (interval != NULL)
    {
      *interval = state->interval;
    }

  if (burst != NULL)
    {
      *burst = state->burst;
    }

  ratelimit_unlock(state, flags);
}
