/****************************************************************************
 * arch/arm64/src/common/arm64_tcbinfo.c
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
  REGINFO_AUTO("x0",  8, 0,  TCB_REG_OFF(REG_X0)),
  REGINFO_AUTO("x1",  8, 1,  TCB_REG_OFF(REG_X1)),
  REGINFO_AUTO("x2",  8, 2,  TCB_REG_OFF(REG_X2)),
  REGINFO_AUTO("x3",  8, 3,  TCB_REG_OFF(REG_X3)),
  REGINFO_AUTO("x4",  8, 4,  TCB_REG_OFF(REG_X4)),
  REGINFO_AUTO("x5",  8, 5,  TCB_REG_OFF(REG_X5)),
  REGINFO_AUTO("x6",  8, 6,  TCB_REG_OFF(REG_X6)),
  REGINFO_AUTO("x7",  8, 7,  TCB_REG_OFF(REG_X7)),
  REGINFO_AUTO("x8",  8, 8,  TCB_REG_OFF(REG_X8)),
  REGINFO_AUTO("x9",  8, 9,  TCB_REG_OFF(REG_X9)),
  REGINFO_AUTO("x10", 8, 10, TCB_REG_OFF(REG_X10)),
  REGINFO_AUTO("x11", 8, 11, TCB_REG_OFF(REG_X11)),
  REGINFO_AUTO("x12", 8, 12, TCB_REG_OFF(REG_X12)),
  REGINFO_AUTO("x13", 8, 13, TCB_REG_OFF(REG_X13)),
  REGINFO_AUTO("x14", 8, 14, TCB_REG_OFF(REG_X14)),
  REGINFO_AUTO("x15", 8, 15, TCB_REG_OFF(REG_X15)),
  REGINFO_AUTO("x16", 8, 16, TCB_REG_OFF(REG_X16)),
  REGINFO_AUTO("x17", 8, 17, TCB_REG_OFF(REG_X17)),
  REGINFO_AUTO("x18", 8, 18, TCB_REG_OFF(REG_X18)),
  REGINFO_AUTO("x19", 8, 19, TCB_REG_OFF(REG_X19)),
  REGINFO_AUTO("x20", 8, 20, TCB_REG_OFF(REG_X20)),
  REGINFO_AUTO("x21", 8, 21, TCB_REG_OFF(REG_X21)),
  REGINFO_AUTO("x22", 8, 22, TCB_REG_OFF(REG_X22)),
  REGINFO_AUTO("x23", 8, 23, TCB_REG_OFF(REG_X23)),
  REGINFO_AUTO("x24", 8, 24, TCB_REG_OFF(REG_X24)),
  REGINFO_AUTO("x25", 8, 25, TCB_REG_OFF(REG_X25)),
  REGINFO_AUTO("x26", 8, 26, TCB_REG_OFF(REG_X26)),
  REGINFO_AUTO("x27", 8, 27, TCB_REG_OFF(REG_X27)),
  REGINFO_AUTO("x28", 8, 28, TCB_REG_OFF(REG_X28)),
  REGINFO_AUTO("x29", 8, 29, TCB_REG_OFF(REG_X29)),
  REGINFO_AUTO("x30", 8, 30, TCB_REG_OFF(REG_X30)),
  REGINFO_AUTO("sp",  8, 31, TCB_REG_OFF(REG_SP_ELX)),
  REGINFO_AUTO("pc",  8, 32, TCB_REG_OFF(REG_ELR)),
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
