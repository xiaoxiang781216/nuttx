/****************************************************************************
 * include/nuttx/mutex_type.h
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

#ifndef __INCLUDE_NUTTX_MUTEX_TYPE_H
#define __INCLUDE_NUTTX_MUTEX_TYPE_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <nuttx/semaphore.h>

#undef EXTERN
#if defined(__cplusplus)
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define NXMUTEX_INITIALIZER \
  {NXSEM_INITIALIZER(NXSEM_NO_MHOLDER, SEM_TYPE_MUTEX | SEM_PRIO_INHERIT)}

#define NXRMUTEX_INITIALIZER {NXMUTEX_INITIALIZER, 0}

/****************************************************************************
 * Public Type Definitions
 ****************************************************************************/

struct mutex_s
{
  sem_t sem;
#ifdef CONFIG_LIBC_MUTEX_BACKTRACE
  FAR void *stack;
#endif
};

typedef struct mutex_s mutex_t;

struct rmutex_s
{
  mutex_t mutex;
  unsigned int count;
};

typedef struct rmutex_s rmutex_t;

#undef EXTERN
#if defined(__cplusplus)
}
#endif

#endif /* __INCLUDE_NUTTX_MUTEX_TYPE_H */
