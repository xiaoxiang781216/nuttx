/****************************************************************************
 * libs/libc/pthread/pthread_attr_init.c
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

#include <pthread.h>
#include <string.h>
#include <debug.h>
#include <errno.h>

#include <nuttx/pthread.h>

/****************************************************************************
 * Public Data
 ****************************************************************************/

/* Default pthread attributes (see include/nuttx/pthread.h).  When configured
 * to build separate kernel- and user-address spaces, this global is
 * duplicated in each address spaced.  This copy can only be shared within
 * the user address space.
 */

#if !defined(CONFIG_BUILD_FLAT) && !defined(__KERNEL__)
const pthread_attr_t g_default_pthread_attr = PTHREAD_ATTR_INITIALIZER;
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name:  pthread_attr_init
 *
 * Description:
 *   Initializes a thread attributes object (attr) with
 *   default values for all of the individual attributes
 *   used by a given implementation.
 *
 * Input Parameters:
 *   attr
 *
 * Returned Value:
 *   0 on success, otherwise an error number
 *
 * Assumptions:
 *
 ****************************************************************************/

int pthread_attr_init(FAR pthread_attr_t *attr)
{
  struct sched_param param;
  int ret = OK;

  linfo("attr=%p\n", attr);
  if (!attr)
    {
      ret = ENOMEM;
    }
  else
    {
      /* Set the child thread priority to be the default
       * priority. Set the child stack size to some arbitrary
       * default value.
       */

      memcpy(attr, &g_default_pthread_attr, sizeof(pthread_attr_t));

      /* Inherit parent priority by default. */

      ret = sched_getparam(0, &param);
      if (ret < 0)
        {
          return ret;
        }

      attr->priority            = param.sched_priority;
#ifdef CONFIG_SCHED_SPORADIC
      attr->low_priority        = param.sched_ss_low_priority;
      attr->max_repl            = param.sched_ss_max_repl;
      attr->repl_period.tv_sec  = param.sched_ss_repl_period.tv_sec;
      attr->repl_period.tv_nsec = param.sched_ss_repl_period.tv_nsec;
      attr->budget.tv_sec       = param.sched_ss_init_budget.tv_sec;
      attr->budget.tv_nsec      = param.sched_ss_init_budget.tv_nsec;
#endif
    }

  linfo("Returning %d\n", ret);
  return ret;
}
