/****************************************************************************
 * arch/arm64/src/common/arm64_registerdump.c
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

#include <stdio.h>
#include <stdint.h>
#include <debug.h>

#include <nuttx/arch.h>
#include <nuttx/irq.h>

#include "sched/sched.h"
#include "arm64_arch.h"
#include "arm64_internal.h"
#include "chip.h"

#ifdef CONFIG_ARCH_FPU
#include "arm64_fpu.h"
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_dump_register
 ****************************************************************************/

void up_dump_register(void *dumpregs)
{
  volatile uint64_t *regs = dumpregs ? dumpregs : running_regs();

  _emerg("stack = %p\n", regs);
  _emerg("x0:   0x%-16"PRIx64"  x1:   0x%"PRIx64"\n",
    regs[REG_X0], regs[REG_X1]);
  _emerg("x2:   0x%-16"PRIx64"  x3:   0x%"PRIx64"\n",
    regs[REG_X2], regs[REG_X3]);
  _emerg("x4:   0x%-16"PRIx64"  x5:   0x%"PRIx64"\n",
    regs[REG_X4], regs[REG_X5]);
  _emerg("x6:   0x%-16"PRIx64"  x7:   0x%"PRIx64"\n",
    regs[REG_X6], regs[REG_X7]);
  _emerg("x8:   0x%-16"PRIx64"  x9:   0x%"PRIx64"\n",
    regs[REG_X8], regs[REG_X9]);
  _emerg("x10:  0x%-16"PRIx64"  x11:  0x%"PRIx64"\n",
    regs[REG_X10], regs[REG_X11]);
  _emerg("x12:  0x%-16"PRIx64"  x13:  0x%"PRIx64"\n",
    regs[REG_X12], regs[REG_X13]);
  _emerg("x14:  0x%-16"PRIx64"  x15:  0x%"PRIx64"\n",
    regs[REG_X14], regs[REG_X15]);
  _emerg("x16:  0x%-16"PRIx64"  x17:  0x%"PRIx64"\n",
    regs[REG_X16], regs[REG_X17]);
  _emerg("x18:  0x%-16"PRIx64"  x19:  0x%"PRIx64"\n",
    regs[REG_X18], regs[REG_X19]);
  _emerg("x20:  0x%-16"PRIx64"  x21:  0x%"PRIx64"\n",
    regs[REG_X20], regs[REG_X21]);
  _emerg("x22:  0x%-16"PRIx64"  x23:  0x%"PRIx64"\n",
    regs[REG_X22], regs[REG_X23]);
  _emerg("x24:  0x%-16"PRIx64"  x25:  0x%"PRIx64"\n",
    regs[REG_X24], regs[REG_X25]);
  _emerg("x26:  0x%-16"PRIx64"  x27:  0x%"PRIx64"\n",
    regs[REG_X26], regs[REG_X27]);
  _emerg("x28:  0x%-16"PRIx64"  x29:  0x%"PRIx64"\n",
    regs[REG_X28], regs[REG_X29]);
  _emerg("x30:  0x%-16"PRIx64"\n", regs[REG_X30]);

  _emerg("\n");

  /* Use ELR as PC for script to parse */

  _emerg("STATUS Registers:\n");
  _emerg("PC:        0x%-16"PRIx64"\n", regs[REG_ELR]);
  _emerg("SPSR:      0x%-16"PRIx64"\n", regs[REG_SPSR]);
  _emerg("ELR:       0x%-16"PRIx64"\n", regs[REG_ELR]);
  _emerg("SP_EL0:    0x%-16"PRIx64"\n", regs[REG_SP_EL0]);
  _emerg("SP:        0x%-16"PRIx64"\n", regs[REG_SP_ELX]);

#ifdef CONFIG_ARM64_MTE
  _emerg("SCTLR_EL1: 0x%-16"PRIx64"\n", regs[REG_SCTLR_EL1]);
#endif
}
