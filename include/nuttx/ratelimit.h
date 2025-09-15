/****************************************************************************
 * include/nuttx/ratelimit.h
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

#ifndef __INCLUDE_NUTTX_RATELIMIT_H
#define __INCLUDE_NUTTX_RATELIMIT_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __KERNEL__
#  include <nuttx/spinlock_type.h>
#else
#  include <pthread.h>
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifdef __KERNEL__
#  define RATELIMIT_LOCK_INITIALIZER SP_UNLOCKED
#else
#  define RATELIMIT_LOCK_INITIALIZER PTHREAD_MUTEX_INITIALIZER
#endif

#define RATELIMIT_STATE_INITIALIZER(interval_init, burst_init) \
  {                                                            \
    .lock     = RATELIMIT_LOCK_INITIALIZER,                    \
    .interval = interval_init,                                 \
    .burst    = burst_init,                                    \
    .begin    = 0,                                             \
    .count    = 0,                                             \
  }

#define DEFINE_RATELIMIT_STATE(name, interval_init, burst_init) \
  struct ratelimit_state_s name =                               \
  RATELIMIT_STATE_INITIALIZER(interval_init, burst_init)

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Rate limiting is carried out according to the time window. The
 * flow within a fixed time window is not allowed to exceed the
 * maximum burst value.
 */

struct ratelimit_state_s
{
  /* A lock used to protect the current-limiting state. */

#ifdef __KERNEL__
  spinlock_t lock;
#else
  pthread_mutex_t lock;
#endif

  unsigned int interval; /* The size of the time window (in seconds). */
  unsigned int burst;    /* The burst traffic allowed within the window. */
  unsigned int count;    /* The number of events that have occurred. */
  clock_t begin;         /* The timestamp of the beginning of the window. */
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef __cplusplus
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

/****************************************************************************
 * Name: ratelimit_islimited
 *
 * Description:
 *   Rate limiting is carried out according to the time window.
 *
 ****************************************************************************/

bool ratelimit_islimited(FAR struct ratelimit_state_s *state);

#undef EXTERN
#ifdef __cplusplus
}
#endif

#endif /* __INCLUDE_NUTTX_RATELIMIT_H */
