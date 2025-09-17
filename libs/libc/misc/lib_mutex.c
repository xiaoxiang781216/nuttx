/****************************************************************************
 * libs/libc/misc/lib_mutex.c
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

#include <errno.h>
#include <execinfo.h>
#include <time.h>

#include <nuttx/sched.h>
#include <nuttx/clock.h>
#include <nuttx/mutex.h>
#include <nuttx/semaphore.h>

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: nxmutex_add_backtrace
 *
 * Description:
 *   This function add the backtrace of the holder of the mutex.
 *
 * Parameters:
 *   mutex - mutex descriptor.
 *
 * Return Value:
 *
 ****************************************************************************/

#ifdef CONFIG_LIBC_MUTEX_BACKTRACE
void nxmutex_add_backtrace(FAR mutex_t *mutex)
{
  mutex->stack = backtrace_record(0);
}

void nxmutex_remove_backtrace(FAR mutex_t *mutex)
{
  backtrace_remove(mutex->stack);
  mutex->stack = NULL;
}
#endif

/****************************************************************************
 * Name: nxmutex_is_hold
 *
 * Description:
 *   This function check whether the caller hold the mutex
 *   referenced by 'mutex'.
 *
 * Parameters:
 *   mutex - mutex descriptor.
 *
 * Return Value:
 *
 ****************************************************************************/

bool nxmutex_is_hold(FAR mutex_t *mutex)
{
  return nxmutex_get_holder(mutex) == _SCHED_GETTID();
}
