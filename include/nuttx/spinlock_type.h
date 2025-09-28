/****************************************************************************
 * include/nuttx/spinlock_type.h
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

#ifndef __INCLUDE_NUTTX_SPINLOCK_TYPE_H
#define __INCLUDE_NUTTX_SPINLOCK_TYPE_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <sys/types.h>
#if defined(CONFIG_SPINLOCK_DEBUG)
#  include <nuttx/queue.h>
#endif

#undef EXTERN
#if defined(__cplusplus)
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

#if defined(CONFIG_SPINLOCK_DEBUG)
typedef struct spinlock_debug_s
{
  sq_entry_t flink;
  FAR struct tcb_s *holder;
#  if defined(CONFIG_SPINLOCK_BACKTRACE)
  FAR void *stack;
#  endif
} spinlock_debug_t;
#  define SPINLOCK_DEBUG_INFO spinlock_debug_t info;
#  if defined(CONFIG_SPINLOCK_BACKTRACE)
#    define SPINLOCK_DEBUG_INIT {{NULL}, NULL, NULL},
#  else
#    define SPINLOCK_DEBUG_INIT {{NULL}, NULL},
#  endif
#else
#  define SPINLOCK_DEBUG_INFO
#  define SPINLOCK_DEBUG_INIT
#endif

#if defined(CONFIG_RW_SPINLOCK)
typedef struct rwlock_s
{
  SPINLOCK_DEBUG_INFO
  atomic_t lock;
} rwlock_t;
#  define RW_SP_UNLOCKED      0
#  define RW_SP_READ_LOCKED   1
#  define RW_SP_WRITE_LOCKED -1
#endif

#if defined(CONFIG_TICKET_SPINLOCK)
typedef struct spinlock_s
{
  SPINLOCK_DEBUG_INFO
  atomic_t owner;
  atomic_t next;
} spinlock_t;

#  define SP_UNLOCKED {SPINLOCK_DEBUG_INIT 0, 0}
#  define SP_LOCKED   {SPINLOCK_DEBUG_INIT 0, 1}
#elif defined(CONFIG_SPINLOCK) || !defined(CONFIG_HAVE_ZERO_SIZE_ARRAY)
typedef struct spinlock_s
{
  SPINLOCK_DEBUG_INFO
  atomic_t lock;
} spinlock_t;

#  define SP_UNLOCKED {SPINLOCK_DEBUG_INIT 0}
#  define SP_LOCKED   {SPINLOCK_DEBUG_INIT 1}

#else
#  define SP_LOCKED   SP_UNLOCKED
#  define SP_UNLOCKED {}

struct spinlock_s
{
  atomic_t lock[0];
};

typedef struct spinlock_s spinlock_t;
#endif

#define RSPINLOCK_CPU_INVALID (-1)
#define RSPINLOCK_INITIALIZER {SPINLOCK_DEBUG_INIT {0}}

typedef struct rspinlock_s
{
  SPINLOCK_DEBUG_INFO

  union
    {
      /* Which cpu is holding spinlock, and taking recursive count */

      atomic_t val;
      struct
        {
          uint16_t owner;
          uint16_t count;
        };
    };
} rspinlock_t;

#undef EXTERN
#if defined(__cplusplus)
}
#endif

#endif /* __INCLUDE_NUTTX_SPINLOCK_TYPE_H */
