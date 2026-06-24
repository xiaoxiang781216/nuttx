/****************************************************************************
 * arch/risc-v/src/common/riscv_tcbinfo.c
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

#include <nuttx/sched.h>
#include <arch/irq.h>
#include <sys/param.h>

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct reginfo_s g_reginfo[] =
{
  REGINFO_AUTO("zero", INT_REG_SIZE, 0,  TCB_REG_OFF(REG_EPC_NDX)),
  REGINFO_AUTO("ra",   INT_REG_SIZE, 1,  TCB_REG_OFF(REG_X1_NDX)),
  REGINFO_AUTO("sp",   INT_REG_SIZE, 2,  TCB_REG_OFF(REG_X2_NDX)),
  REGINFO_AUTO("gp",   INT_REG_SIZE, 3,  TCB_REG_OFF(REG_X3_NDX)),
  REGINFO_AUTO("tp",   INT_REG_SIZE, 4,  TCB_REG_OFF(REG_X4_NDX)),
  REGINFO_AUTO("t0",   INT_REG_SIZE, 5,  TCB_REG_OFF(REG_X5_NDX)),
  REGINFO_AUTO("t1",   INT_REG_SIZE, 6,  TCB_REG_OFF(REG_X6_NDX)),
  REGINFO_AUTO("t2",   INT_REG_SIZE, 7,  TCB_REG_OFF(REG_X7_NDX)),
  REGINFO_AUTO("fp",   INT_REG_SIZE, 8,  TCB_REG_OFF(REG_X8_NDX)),
  REGINFO_AUTO("s1",   INT_REG_SIZE, 9,  TCB_REG_OFF(REG_X9_NDX)),
  REGINFO_AUTO("a0",   INT_REG_SIZE, 10, TCB_REG_OFF(REG_X10_NDX)),
  REGINFO_AUTO("a1",   INT_REG_SIZE, 11, TCB_REG_OFF(REG_X11_NDX)),
  REGINFO_AUTO("a2",   INT_REG_SIZE, 12, TCB_REG_OFF(REG_X12_NDX)),
  REGINFO_AUTO("a3",   INT_REG_SIZE, 13, TCB_REG_OFF(REG_X13_NDX)),
  REGINFO_AUTO("a4",   INT_REG_SIZE, 14, TCB_REG_OFF(REG_X14_NDX)),
  REGINFO_AUTO("a5",   INT_REG_SIZE, 15, TCB_REG_OFF(REG_X15_NDX)),
  REGINFO_AUTO("a6",   INT_REG_SIZE, 16, TCB_REG_OFF(REG_X16_NDX)),
  REGINFO_AUTO("a7",   INT_REG_SIZE, 17, TCB_REG_OFF(REG_X17_NDX)),
  REGINFO_AUTO("s2",   INT_REG_SIZE, 18, TCB_REG_OFF(REG_X18_NDX)),
  REGINFO_AUTO("s3",   INT_REG_SIZE, 19, TCB_REG_OFF(REG_X19_NDX)),
  REGINFO_AUTO("s4",   INT_REG_SIZE, 20, TCB_REG_OFF(REG_X20_NDX)),
  REGINFO_AUTO("s5",   INT_REG_SIZE, 21, TCB_REG_OFF(REG_X21_NDX)),
  REGINFO_AUTO("s6",   INT_REG_SIZE, 22, TCB_REG_OFF(REG_X22_NDX)),
  REGINFO_AUTO("s7",   INT_REG_SIZE, 23, TCB_REG_OFF(REG_X23_NDX)),
  REGINFO_AUTO("s8",   INT_REG_SIZE, 24, TCB_REG_OFF(REG_X24_NDX)),
  REGINFO_AUTO("s9",   INT_REG_SIZE, 25, TCB_REG_OFF(REG_X25_NDX)),
  REGINFO_AUTO("s10",  INT_REG_SIZE, 26, TCB_REG_OFF(REG_X26_NDX)),
  REGINFO_AUTO("s11",  INT_REG_SIZE, 27, TCB_REG_OFF(REG_X27_NDX)),
  REGINFO_AUTO("t3",   INT_REG_SIZE, 28, TCB_REG_OFF(REG_X28_NDX)),
  REGINFO_AUTO("t4",   INT_REG_SIZE, 29, TCB_REG_OFF(REG_X29_NDX)),
  REGINFO_AUTO("t5",   INT_REG_SIZE, 30, TCB_REG_OFF(REG_X30_NDX)),
  REGINFO_AUTO("t6",   INT_REG_SIZE, 31, TCB_REG_OFF(REG_X31_NDX)),
  REGINFO_AUTO("pc",   INT_REG_SIZE, 32, TCB_REG_OFF(REG_EPC_NDX)),

  /* Floating point registers f0-f31 (GDB regnum 33-64).
   *
   * These are only exposed when the FPU context is saved inline after the
   * integer context (the default, non-lazy case).  With CONFIG_ARCH_LAZYFPU
   * the FPU registers live in a separate buffer (tcb->xcp.fregs) that cannot
   * be expressed as a fixed byte offset within tcb->xcp.regs, so they are
   * not described here.
   *
   * Each register is FPU_REG_SIZE integer-register slots wide
   * (i.e. FLEN bytes); the offset is computed past the integer context
   * (INT_XCPT_REGS slots) plus the register's slot index inside the FPU
   * save area.  The low bytes naturally contain narrower values (float,
   * _Float16) thanks to RISC-V NaN-boxing, so half-precision is covered
   * without any special handling.
   */

#if defined(CONFIG_ARCH_FPU) && !defined(CONFIG_ARCH_LAZYFPU)
  REGINFO_AUTO("ft0",  FPU_REG_SIZE * INT_REG_SIZE, 33,
               TCB_REG_OFF(INT_XCPT_REGS + REG_F0_NDX)),
  REGINFO_AUTO("ft1",  FPU_REG_SIZE * INT_REG_SIZE, 34,
               TCB_REG_OFF(INT_XCPT_REGS + REG_F1_NDX)),
  REGINFO_AUTO("ft2",  FPU_REG_SIZE * INT_REG_SIZE, 35,
               TCB_REG_OFF(INT_XCPT_REGS + REG_F2_NDX)),
  REGINFO_AUTO("ft3",  FPU_REG_SIZE * INT_REG_SIZE, 36,
               TCB_REG_OFF(INT_XCPT_REGS + REG_F3_NDX)),
  REGINFO_AUTO("ft4",  FPU_REG_SIZE * INT_REG_SIZE, 37,
               TCB_REG_OFF(INT_XCPT_REGS + REG_F4_NDX)),
  REGINFO_AUTO("ft5",  FPU_REG_SIZE * INT_REG_SIZE, 38,
               TCB_REG_OFF(INT_XCPT_REGS + REG_F5_NDX)),
  REGINFO_AUTO("ft6",  FPU_REG_SIZE * INT_REG_SIZE, 39,
               TCB_REG_OFF(INT_XCPT_REGS + REG_F6_NDX)),
  REGINFO_AUTO("ft7",  FPU_REG_SIZE * INT_REG_SIZE, 40,
               TCB_REG_OFF(INT_XCPT_REGS + REG_F7_NDX)),
  REGINFO_AUTO("fs0",  FPU_REG_SIZE * INT_REG_SIZE, 41,
               TCB_REG_OFF(INT_XCPT_REGS + REG_F8_NDX)),
  REGINFO_AUTO("fs1",  FPU_REG_SIZE * INT_REG_SIZE, 42,
               TCB_REG_OFF(INT_XCPT_REGS + REG_F9_NDX)),
  REGINFO_AUTO("fa0",  FPU_REG_SIZE * INT_REG_SIZE, 43,
               TCB_REG_OFF(INT_XCPT_REGS + REG_F10_NDX)),
  REGINFO_AUTO("fa1",  FPU_REG_SIZE * INT_REG_SIZE, 44,
               TCB_REG_OFF(INT_XCPT_REGS + REG_F11_NDX)),
  REGINFO_AUTO("fa2",  FPU_REG_SIZE * INT_REG_SIZE, 45,
               TCB_REG_OFF(INT_XCPT_REGS + REG_F12_NDX)),
  REGINFO_AUTO("fa3",  FPU_REG_SIZE * INT_REG_SIZE, 46,
               TCB_REG_OFF(INT_XCPT_REGS + REG_F13_NDX)),
  REGINFO_AUTO("fa4",  FPU_REG_SIZE * INT_REG_SIZE, 47,
               TCB_REG_OFF(INT_XCPT_REGS + REG_F14_NDX)),
  REGINFO_AUTO("fa5",  FPU_REG_SIZE * INT_REG_SIZE, 48,
               TCB_REG_OFF(INT_XCPT_REGS + REG_F15_NDX)),
  REGINFO_AUTO("fa6",  FPU_REG_SIZE * INT_REG_SIZE, 49,
               TCB_REG_OFF(INT_XCPT_REGS + REG_F16_NDX)),
  REGINFO_AUTO("fa7",  FPU_REG_SIZE * INT_REG_SIZE, 50,
               TCB_REG_OFF(INT_XCPT_REGS + REG_F17_NDX)),
  REGINFO_AUTO("fs2",  FPU_REG_SIZE * INT_REG_SIZE, 51,
               TCB_REG_OFF(INT_XCPT_REGS + REG_F18_NDX)),
  REGINFO_AUTO("fs3",  FPU_REG_SIZE * INT_REG_SIZE, 52,
               TCB_REG_OFF(INT_XCPT_REGS + REG_F19_NDX)),
  REGINFO_AUTO("fs4",  FPU_REG_SIZE * INT_REG_SIZE, 53,
               TCB_REG_OFF(INT_XCPT_REGS + REG_F20_NDX)),
  REGINFO_AUTO("fs5",  FPU_REG_SIZE * INT_REG_SIZE, 54,
               TCB_REG_OFF(INT_XCPT_REGS + REG_F21_NDX)),
  REGINFO_AUTO("fs6",  FPU_REG_SIZE * INT_REG_SIZE, 55,
               TCB_REG_OFF(INT_XCPT_REGS + REG_F22_NDX)),
  REGINFO_AUTO("fs7",  FPU_REG_SIZE * INT_REG_SIZE, 56,
               TCB_REG_OFF(INT_XCPT_REGS + REG_F23_NDX)),
  REGINFO_AUTO("fs8",  FPU_REG_SIZE * INT_REG_SIZE, 57,
               TCB_REG_OFF(INT_XCPT_REGS + REG_F24_NDX)),
  REGINFO_AUTO("fs9",  FPU_REG_SIZE * INT_REG_SIZE, 58,
               TCB_REG_OFF(INT_XCPT_REGS + REG_F25_NDX)),
  REGINFO_AUTO("fs10", FPU_REG_SIZE * INT_REG_SIZE, 59,
               TCB_REG_OFF(INT_XCPT_REGS + REG_F26_NDX)),
  REGINFO_AUTO("fs11", FPU_REG_SIZE * INT_REG_SIZE, 60,
               TCB_REG_OFF(INT_XCPT_REGS + REG_F27_NDX)),
  REGINFO_AUTO("ft8",  FPU_REG_SIZE * INT_REG_SIZE, 61,
               TCB_REG_OFF(INT_XCPT_REGS + REG_F28_NDX)),
  REGINFO_AUTO("ft9",  FPU_REG_SIZE * INT_REG_SIZE, 62,
               TCB_REG_OFF(INT_XCPT_REGS + REG_F29_NDX)),
  REGINFO_AUTO("ft10", FPU_REG_SIZE * INT_REG_SIZE, 63,
               TCB_REG_OFF(INT_XCPT_REGS + REG_F30_NDX)),
  REGINFO_AUTO("ft11", FPU_REG_SIZE * INT_REG_SIZE, 64,
               TCB_REG_OFF(INT_XCPT_REGS + REG_F31_NDX)),

  /* fcsr is GDB regnum 65 in the org.gnu.gdb.riscv.fpu feature.  It is a
   * 32-bit status/control register; store its 4 low bytes.
   */

  REGINFO_AUTO("fcsr", 4, 65,
               TCB_REG_OFF(INT_XCPT_REGS + REG_FCSR_NDX)),
#endif
};

/****************************************************************************
 * Public Data
 ****************************************************************************/

const struct tcbinfo_s g_tcbinfo used_data =
{
  .pid_off        = TCB_PID_OFF,
  .state_off      = TCB_STATE_OFF,
  .pri_off        = TCB_PRI_OFF,
  .name_off       = TCB_NAME_OFF,
  .stack_off      = TCB_STACK_OFF,
  .stack_size_off = TCB_STACK_SIZE_OFF,
  .regs_off       = TCB_REGS_OFF,
  .regs_num       = nitems(g_reginfo),
  {
    .reginfo      = g_reginfo,
  }
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/
