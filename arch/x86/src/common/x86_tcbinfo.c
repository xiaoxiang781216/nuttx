/****************************************************************************
 * arch/x86/src/common/x86_tcbinfo.c
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
  REGINFO_AUTO("eax",    4, 0,  TCB_REG_OFF(REG_EAX)),
  REGINFO_AUTO("ecx",    4, 1,  TCB_REG_OFF(REG_ECX)),
  REGINFO_AUTO("edx",    4, 2,  TCB_REG_OFF(REG_EDX)),
  REGINFO_AUTO("ebx",    4, 3,  REGINFO_OFFSET_INVALID),
  REGINFO_AUTO("esp",    4, 4,  TCB_REG_OFF(REG_ESP)),
  REGINFO_AUTO("ebp",    4, 5,  TCB_REG_OFF(REG_EBP)),
  REGINFO_AUTO("esi",    4, 6,  TCB_REG_OFF(REG_ESI)),
  REGINFO_AUTO("edi",    4, 7,  TCB_REG_OFF(REG_EDI)),
  REGINFO_AUTO("eip",    4, 8,  TCB_REG_OFF(REG_EIP)),
  REGINFO_AUTO("eflags", 4, 9,  TCB_REG_OFF(REG_EFLAGS)),
  REGINFO_AUTO("cs",     4, 10, TCB_REG_OFF(REG_CS)),
  REGINFO_AUTO("ss",     4, 11, TCB_REG_OFF(REG_SS)),
  REGINFO_AUTO("ds",     4, 12, TCB_REG_OFF(REG_DS)),
};

/****************************************************************************
 * Public Data
 ****************************************************************************/

const struct tcbinfo_s g_tcbinfo =
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
    .reginfo = g_reginfo,
  },
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/
