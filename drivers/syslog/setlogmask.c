/****************************************************************************
 * drivers/syslog/setlogmask.c
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

#include <stdint.h>
#include <syslog.h>

#include <nuttx/tls_task.h>
#include <nuttx/sched.h>
#include <nuttx/syslog/syslog.h>

/****************************************************************************
 * Public Data
 ****************************************************************************/

uint8_t g_syslog_mask = CONFIG_SYSLOG_DEFAULT_MASK;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void task_syslogmask(FAR struct tcb_s *tcb, FAR void *arg)
{
  FAR struct task_info_s *info = tcb->group->tg_info;

  /* When task is exiting, tg_info could be free'd in task_uninit_info */

  if (info)
    {
      info->ta_syslog_mask = (uint8_t)(*(FAR const int *)arg);
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: nx_setlogmask
 *
 * Description:
 *   Like the standard setlogmask() function, nx_setlogmask() sets the all
 *   system log mask to the specified value.  It also sets the per-task
 *   log mask for all tasks.
 *
 ****************************************************************************/

int nx_setlogmask(int mask)
{
  uint8_t oldmask;

  oldmask = g_syslog_mask;
  g_syslog_mask = (uint8_t)mask;

  nxsched_foreach(task_syslogmask, &mask);
  return oldmask;
}
