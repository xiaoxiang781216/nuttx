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
