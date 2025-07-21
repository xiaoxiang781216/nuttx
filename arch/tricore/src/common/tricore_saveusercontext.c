/****************************************************************************
 * arch/tricore/src/common/tricore_saveusercontext.c
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

#include "tricore_internal.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Public Data
 ****************************************************************************/

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_saveusercontext
 *
 * Description:
 *   Save the current thread context
 *
 ****************************************************************************/

int up_saveusercontext(void *saveregs)
{
  uintptr_t *regs;
  uintptr_t pcxi;
  int csa_size = TC_CONTEXT_REGS * sizeof(uintptr_t);

  /* Save and copy the current lowe csa */

  __asm("svlcx");

  pcxi = __mfcr(CPU_PCXI);
  regs = tricore_csa2addr(pcxi);
  memcpy(saveregs, regs, csa_size);

  /* Copy the current upper csa */

  regs = tricore_csa2addr(regs[0]);
  memcpy((char *)saveregs + csa_size, regs, csa_size);

  __asm("rslcx");

  return 0;
}
