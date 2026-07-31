/****************************************************************************
 * arch/zsp/src/common/zsp_registerdump.c
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
#include <debug.h>
#include <stdint.h>
#include <arch/irq.h>

#include "zsp_internal.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_dump_register
 *
 * Description:
 *   Dump the contents of the register save area to the debug console.
 *
 ****************************************************************************/

void up_dump_register(void *dumpregs)
{
  volatile uint32_t *regs = dumpregs;

  if (regs == NULL)
    {
      regs = (volatile uint32_t *)up_current_regs();
    }

  if (regs == NULL)
    {
      return;
    }

  _alert("PC:  %08" PRIx32 "  SP:  %08" PRIx32 "\n",
         regs[REG_PC], regs[REG_SP]);
  _alert("R0:  %08" PRIx32 "  R2:  %08" PRIx32
         "  R4:  %08" PRIx32 "  R6:  %08" PRIx32 "\n",
         regs[REG_R0R1], regs[REG_R2R3],
         regs[REG_R4R5], regs[REG_R6R7]);
  _alert("R8:  %08" PRIx32 "  R10: %08" PRIx32
         "  R12: %08" PRIx32 "  R14: %08" PRIx32 "\n",
         regs[REG_R8R9], regs[REG_R10R11],
         regs[REG_R12R13], regs[REG_R14R15]);
  _alert("A0:  %08" PRIx32 "  A1:  %08" PRIx32
         "  A2:  %08" PRIx32 "  A3:  %08" PRIx32 "\n",
         regs[REG_A0], regs[REG_A1], regs[REG_A2], regs[REG_A3]);
  _alert("A4:  %08" PRIx32 "  A5:  %08" PRIx32
         "  A6:  %08" PRIx32 "  A7:  %08" PRIx32 "\n",
         regs[REG_A4], regs[REG_A5], regs[REG_A6], regs[REG_A7]);
  _alert("IMASK: %08" PRIx32 "\n", regs[REG_IMASK]);
}
