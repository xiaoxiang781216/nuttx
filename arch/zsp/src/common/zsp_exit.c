/****************************************************************************
 * arch/zsp/src/common/zsp_exit.c
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
#include <nuttx/arch.h>
#include <assert.h>

#include "sched/sched.h"
#include "task/task.h"
#include "zsp_internal.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_exit
 *
 * Description:
 *   This function causes the currently executing task to cease to exist.
 *   This follows the ARM/RISC-V pattern: destroy the task, clear
 *   g_running_task, and invoke nxsched_switch to perform the context
 *   switch to the next ready-to-run task.
 *
 ****************************************************************************/

void up_exit(int status)
{
  UNUSED(status);

  /* Destroy the task at the head of the ready to run list. */

  nxtask_exit();

  /* Update g_running_tasks */

  g_running_task = NULL;

  /* Then switch contexts */

  nxsched_switch(this_task(), NULL);

  /* nxsched_switch() should not return but could if the software
   * interrupts are disabled.
   */

  PANIC();
}
