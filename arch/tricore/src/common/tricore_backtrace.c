/****************************************************************************
 * arch/tricore/src/common/tricore_backtrace.c
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

#include "sched/sched.h"
#include "tricore_internal.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Name: backtrace
 *
 * Description:
 *  backtrace() parsing the return address through frame pointer
 *
 ****************************************************************************/

nosanitize_address
static int backtrace(uintptr_t *reg, void **buffer,
                     int size, int *skip)
{
  int i = 0;
  uintptr_t *csa = reg;
  uintptr_t pcxi = csa[REG_UPCXI];

  for (; csa && i < size; csa = tricore_csa2addr(csa[REG_UPCXI]))
    {
      if ((pcxi & PCXI_UL) == 0 || csa[REG_UA11] == 0)
        {
            pcxi = csa[REG_UPCXI];
            continue;
        }

      if ((*skip)-- <= 0)
        {
          buffer[i++] = (void *)csa[REG_UA11];
        }

      pcxi = csa[REG_UPCXI];
    }

  return i;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_backtrace
 *
 * Description:
 *  up_backtrace()  returns  a backtrace for the TCB, in the array
 *  pointed to by buffer.  A backtrace is the series of currently active
 *  function calls for the program.  Each item in the array pointed to by
 *  buffer is of type void *, and is the return address from the
 *  corresponding stack frame.  The size argument specifies the maximum
 *  number of addresses that can be stored in buffer.   If  the backtrace is
 *  larger than size, then the addresses corresponding to the size most
 *  recent function calls are returned; to obtain the complete backtrace,
 *  make sure that buffer and size are large enough.
 *
 * Input Parameters:
 *   tcb    - Address of the task's TCB
 *   buffer - Return address from the corresponding stack frame
 *   size   - Maximum number of addresses that can be stored in buffer
 *   skip   - number of addresses to be skipped
 *
 * Returned Value:
 *   up_backtrace() returns the number of addresses returned in buffer
 *
 * Assumptions:
 *   Have to make sure tcb keep safe during function executing, it means
 *   1. Tcb have to be self or not-running.  In SMP case, the running task
 *      PC & SP cannot be backtrace, as whose get from tcb is not the newest.
 *   2. Tcb have to keep not be freed.  In task exiting case, have to
 *      make sure the tcb get from pid and up_backtrace in one critical
 *      section procedure.
 *
 ****************************************************************************/

nosanitize_address
int up_backtrace(struct tcb_s *tcb,
                 void **buffer, int size, int skip)
{
  struct tcb_s *rtcb = running_task();
  uintptr_t *regs;
  int ret = 0;

  if (size <= 0 || !buffer)
    {
      return 0;
    }

  if (tcb == NULL || tcb == rtcb)
    {
      regs = tricore_csa2addr(__mfcr(CPU_PCXI));
      ret = backtrace(regs, buffer, size, &skip);
    }
  else
    {
      ret = backtrace(tcb->xcp.regs + TC_CONTEXT_REGS,
                      buffer, size, &skip);
    }

  return ret;
}
