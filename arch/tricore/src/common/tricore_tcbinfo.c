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
    {"pcx",    4,    34,      TCB_REG_OFF(REG_LPCXI),   136},
    {"pc",     4,    36,      TCB_REG_OFF(REG_LA11),    144},
    {"d0",     4,    0,       TCB_REG_OFF(REG_D0),      0  },
    {"d1",     4,    1,       TCB_REG_OFF(REG_D1),      4  },
    {"d2",     4,    2,       TCB_REG_OFF(REG_D2),      8  },
    {"d3",     4,    3,       TCB_REG_OFF(REG_D3),      12 },
    {"d4",     4,    4,       TCB_REG_OFF(REG_D4),      16 },
    {"d5",     4,    5,       TCB_REG_OFF(REG_D5),      20 },
    {"d6",     4,    6,       TCB_REG_OFF(REG_D6),      24 },
    {"d7",     4,    7,       TCB_REG_OFF(REG_D7),      28 },
    {"a2",     4,    18,      TCB_REG_OFF(REG_A2),      72 },
    {"a3",     4,    19,      TCB_REG_OFF(REG_A3),      76 },
    {"a4",     4,    20,      TCB_REG_OFF(REG_A4),      80 },
    {"a5",     4,    21,      TCB_REG_OFF(REG_A5),      84 },
    {"a6",     4,    22,      TCB_REG_OFF(REG_A6),      88 },
    {"a7",     4,    23,      TCB_REG_OFF(REG_A7),      92 },
    {"psw",    4,    35,      TCB_REG_OFF(REG_PSW),     140},
    {"sp",     4,    26,      TCB_REG_OFF(REG_A10),     152},
    {"a11",    4,    27,      TCB_REG_OFF(REG_UA11),    108},
    {"d8",     4,    8,       TCB_REG_OFF(REG_D8),      32 },
    {"d9",     4,    9,       TCB_REG_OFF(REG_D9),      36 },
    {"d10",    4,    10,      TCB_REG_OFF(REG_D10),     40 },
    {"d11",    4,    11,      TCB_REG_OFF(REG_D11),     44 },
    {"a12",    4,    28,      TCB_REG_OFF(REG_A12),     112},
    {"a13",    4,    29,      TCB_REG_OFF(REG_A13),     116},
    {"a14",    4,    30,      TCB_REG_OFF(REG_A14),     120},
    {"a15",    4,    31,      TCB_REG_OFF(REG_A15),     124},
    {"d12",    4,    12,      TCB_REG_OFF(REG_D12),     48 },
    {"d13",    4,    13,      TCB_REG_OFF(REG_D13),     52 },
    {"d14",    4,    14,      TCB_REG_OFF(REG_D14),     56 },
    {"d15",    4,    15,      TCB_REG_OFF(REG_D15),     60 },
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
