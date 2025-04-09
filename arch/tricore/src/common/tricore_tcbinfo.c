/****************************************************************************
 * arch/tricore/src/common/tricore_tcbinfo.c
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

/* Name,    Size, Regnum, TCB offset,               g/G offset */

static const struct reginfo_s g_reginfo[] =
{
  {"d0",     4,    0,       TCB_REG_OFF(REG_D0),        REGINFO_OFFSET_AUTO},
  {"d1",     4,    1,       TCB_REG_OFF(REG_D1),        REGINFO_OFFSET_AUTO},
  {"d2",     4,    2,       TCB_REG_OFF(REG_D2),        REGINFO_OFFSET_AUTO},
  {"d3",     4,    3,       TCB_REG_OFF(REG_D3),        REGINFO_OFFSET_AUTO},
  {"d4",     4,    4,       TCB_REG_OFF(REG_D4),        REGINFO_OFFSET_AUTO},
  {"d5",     4,    5,       TCB_REG_OFF(REG_D5),        REGINFO_OFFSET_AUTO},
  {"d6",     4,    6,       TCB_REG_OFF(REG_D6),        REGINFO_OFFSET_AUTO},
  {"d7",     4,    7,       TCB_REG_OFF(REG_D7),        REGINFO_OFFSET_AUTO},
  {"d8",     4,    8,       TCB_REG_OFF(16 + REG_D8),   REGINFO_OFFSET_AUTO},
  {"d9",     4,    9,       TCB_REG_OFF(16 + REG_D9),   REGINFO_OFFSET_AUTO},
  {"d10",    4,    10,      TCB_REG_OFF(16 + REG_D10),  REGINFO_OFFSET_AUTO},
  {"d11",    4,    11,      TCB_REG_OFF(16 + REG_D11),  REGINFO_OFFSET_AUTO},
  {"d12",    4,    12,      TCB_REG_OFF(16 + REG_D12),  REGINFO_OFFSET_AUTO},
  {"d13",    4,    13,      TCB_REG_OFF(16 + REG_D13),  REGINFO_OFFSET_AUTO},
  {"d14",    4,    14,      TCB_REG_OFF(16 + REG_D14),  REGINFO_OFFSET_AUTO},
  {"d15",    4,    15,      TCB_REG_OFF(16 + REG_D15),  REGINFO_OFFSET_AUTO},
  {"a0",     4,    16,      REGINFO_OFFSET_INVALID,     REGINFO_OFFSET_AUTO},
  {"a1",     4,    17,      REGINFO_OFFSET_INVALID,     REGINFO_OFFSET_AUTO},
  {"a2",     4,    18,      TCB_REG_OFF(REG_A2),        REGINFO_OFFSET_AUTO},
  {"a3",     4,    19,      TCB_REG_OFF(REG_A3),        REGINFO_OFFSET_AUTO},
  {"a4",     4,    20,      TCB_REG_OFF(REG_A4),        REGINFO_OFFSET_AUTO},
  {"a5",     4,    21,      TCB_REG_OFF(REG_A5),        REGINFO_OFFSET_AUTO},
  {"a6",     4,    22,      TCB_REG_OFF(REG_A6),        REGINFO_OFFSET_AUTO},
  {"a7",     4,    23,      TCB_REG_OFF(REG_A7),        REGINFO_OFFSET_AUTO},
  {"a8",     4,    24,      REGINFO_OFFSET_INVALID,     REGINFO_OFFSET_AUTO},
  {"a9",     4,    25,      REGINFO_OFFSET_INVALID,     REGINFO_OFFSET_AUTO},
  {"sp",     4,    26,      TCB_REG_OFF(16 + REG_A10),  REGINFO_OFFSET_AUTO},
  {"a11",    4,    27,      TCB_REG_OFF(16 + REG_UA11), REGINFO_OFFSET_AUTO},
  {"a12",    4,    28,      TCB_REG_OFF(16 + REG_A12),  REGINFO_OFFSET_AUTO},
  {"a13",    4,    29,      TCB_REG_OFF(16 + REG_A13),  REGINFO_OFFSET_AUTO},
  {"a14",    4,    30,      TCB_REG_OFF(16 + REG_A14),  REGINFO_OFFSET_AUTO},
  {"a15",    4,    31,      TCB_REG_OFF(16 + REG_A15),  REGINFO_OFFSET_AUTO},
  {"lcx",    4,    32,      REGINFO_OFFSET_INVALID,     REGINFO_OFFSET_AUTO},
  {"fcx",    4,    33,      REGINFO_OFFSET_INVALID,     REGINFO_OFFSET_AUTO},
  {"pcx",    4,    34,      TCB_REG_OFF(REG_LPCXI),     REGINFO_OFFSET_AUTO},
  {"psw",    4,    35,      TCB_REG_OFF(16 + REG_PSW),  REGINFO_OFFSET_AUTO},
  {"pc",     4,    36,      TCB_REG_OFF(REG_LA11),      REGINFO_OFFSET_AUTO},
  {"icr",    4,    37,      REGINFO_OFFSET_INVALID,     REGINFO_OFFSET_AUTO},
  {"isp",    4,    38,      REGINFO_OFFSET_INVALID,     REGINFO_OFFSET_AUTO},
  {"btv",    4,    39,      REGINFO_OFFSET_INVALID,     REGINFO_OFFSET_AUTO},
  {"biv",    4,    40,      REGINFO_OFFSET_INVALID,     REGINFO_OFFSET_AUTO},
  {"syscon", 4,    41,      REGINFO_OFFSET_INVALID,     REGINFO_OFFSET_AUTO},
  {"pcon0",  4,    42,      REGINFO_OFFSET_INVALID,     REGINFO_OFFSET_AUTO},
  {"dcon0",  4,    43,      REGINFO_OFFSET_INVALID,     REGINFO_OFFSET_AUTO},
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
  },
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/
