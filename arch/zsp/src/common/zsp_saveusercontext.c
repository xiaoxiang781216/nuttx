/****************************************************************************
 * arch/zsp/src/common/zsp_saveusercontext.c
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
#include <stdint.h>
#include <arch/irq.h>

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_saveusercontext
 *
 * Description:
 *   Save the current register context into the provided save area.
 *
 ****************************************************************************/

int up_saveusercontext(void *saveregs)
{
  uint32_t *regs = (uint32_t *)saveregs;

  __asm__ __volatile__("st32 r0, %0, 0"   : : "p"(regs) : "memory");
  __asm__ __volatile__("st32 r2, %0, 4"   : : "p"(regs) : "memory");
  __asm__ __volatile__("st32 r4, %0, 8"   : : "p"(regs) : "memory");
  __asm__ __volatile__("st32 r6, %0, 12"  : : "p"(regs) : "memory");
  __asm__ __volatile__("st32 r8, %0, 16"  : : "p"(regs) : "memory");
  __asm__ __volatile__("st32 r10, %0, 20" : : "p"(regs) : "memory");
  __asm__ __volatile__("st32 r12, %0, 24" : : "p"(regs) : "memory");
  __asm__ __volatile__("st32 r14, %0, 28" : : "p"(regs) : "memory");

  __asm__ __volatile__("st32 a0, %0, 64"  : : "p"(regs) : "memory");
  __asm__ __volatile__("st32 a1, %0, 68"  : : "p"(regs) : "memory");
  __asm__ __volatile__("st32 a2, %0, 72"  : : "p"(regs) : "memory");
  __asm__ __volatile__("st32 a3, %0, 76"  : : "p"(regs) : "memory");
  __asm__ __volatile__("st32 a7, %0, 92"  : : "p"(regs) : "memory");

  __asm__ __volatile__("mov32 r4, %%rpc\n"
                       "st32 r4, %0, 128"  : : "p"(regs) : "r4", "r5",
                                                            "memory");
  __asm__ __volatile__("mov32 r4, %%imask\n"
                       "st32 r4, %0, 136"  : : "p"(regs) : "r4", "r5",
                                                            "memory");
  return 0;
}
