/****************************************************************************
 * sched/sched/nxsched_checkstackoverflow.c
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

#include <time.h>
#include <assert.h>

#include <nuttx/arch.h>
#include <nuttx/sched.h>
#include <nuttx/sched_note.h>

#include "sched/sched.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: nxsched_checkstackoverflow
 *
 * Description:
 *   Verify that the specified thread has not overflowed its stack.
 *
 *   Behavior depends on CONFIG_STACKCHECK_MARGIN:
 *
 *   - CONFIG_STACKCHECK_MARGIN == 0:
 *       Stack checking is disabled at build time.  In this case, calls to
 *       nxsched_checkstackoverflow() are replaced with a no-op macro.
 *
 *   - CONFIG_STACKCHECK_MARGIN > 0:
 *       Perform an architecture-specific check with a safety margin.
 *       The stack must not extend beyond the reserved margin area.
 *
 * Input Parameters:
 *   tcb - Pointer to the TCB of the thread to be checked.
 *
 * Returned Value:
 *   None.  The function will trigger a DEBUGASSERT if a stack overflow
 *   condition is detected.
 *
 ****************************************************************************/

void nxsched_checkstackoverflow(FAR struct tcb_s *tcb)
{
#if CONFIG_STACKCHECK_MARGIN > 0
  /* Margin-based stack check:
   * Allow some reserved margin area before reporting overflow.
   */

  DEBUGASSERT(up_check_tcbstack(tcb, CONFIG_STACKCHECK_MARGIN) == 0);
#endif
}
