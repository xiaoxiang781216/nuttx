/****************************************************************************
 * arch/x86_64/src/common/x86_64_tcbinfo.c
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
  REGINFO("rax", 8, 0,  TCB_REG_OFF(REG_RAX),    REGINFO_OFFSET_AUTO, 80),
  REGINFO("rbx", 8, 1,  TCB_REG_OFF(REG_RBX),    REGINFO_OFFSET_AUTO, 40),
  REGINFO("rcx", 8, 2,  TCB_REG_OFF(REG_RCX),    REGINFO_OFFSET_AUTO, 88),
  REGINFO("rdx", 8, 3,  TCB_REG_OFF(REG_RDX),    REGINFO_OFFSET_AUTO, 96),
  REGINFO("rsi", 8, 4,  TCB_REG_OFF(REG_RSI),    REGINFO_OFFSET_AUTO, 104),
  REGINFO("rdi", 8, 5,  TCB_REG_OFF(REG_RDI),    REGINFO_OFFSET_AUTO, 112),
  REGINFO("rbp", 8, 6,  TCB_REG_OFF(REG_RBP),    REGINFO_OFFSET_AUTO, 32),
  REGINFO("rsp", 8, 7,  TCB_REG_OFF(REG_RSP),    REGINFO_OFFSET_AUTO, 152),
  REGINFO("r8",  8, 8,  TCB_REG_OFF(REG_R8),     REGINFO_OFFSET_AUTO, 72),
  REGINFO("r9",  8, 9,  TCB_REG_OFF(REG_R9),     REGINFO_OFFSET_AUTO, 64),
  REGINFO("r10", 8, 10, TCB_REG_OFF(REG_R10),    REGINFO_OFFSET_AUTO, 56),
  REGINFO("r11", 8, 11, TCB_REG_OFF(REG_R11),    REGINFO_OFFSET_AUTO, 48),
  REGINFO("r12", 8, 12, TCB_REG_OFF(REG_R12),    REGINFO_OFFSET_AUTO, 24),
  REGINFO("r13", 8, 13, TCB_REG_OFF(REG_R13),    REGINFO_OFFSET_AUTO, 16),
  REGINFO("r14", 8, 14, TCB_REG_OFF(REG_R14),    REGINFO_OFFSET_AUTO, 8),
  REGINFO("r15", 8, 15, TCB_REG_OFF(REG_R15),    REGINFO_OFFSET_AUTO, 0),
  REGINFO("rip", 8, 16, TCB_REG_OFF(REG_RIP),    REGINFO_OFFSET_AUTO, 128),
  REGINFO("eflags", 4, 17, TCB_REG_OFF(REG_RFLAGS), REGINFO_OFFSET_AUTO,
          144),
  REGINFO("cs",  4, 18, TCB_REG_OFF(REG_CS),     REGINFO_OFFSET_AUTO, 136),
  REGINFO("ss",  4, 19, TCB_REG_OFF(REG_SS),     REGINFO_OFFSET_AUTO, 160),
  REGINFO("ds",  4, 20, TCB_REG_OFF(REG_DS),     REGINFO_OFFSET_AUTO, 184),
  REGINFO("es",  4, 21, TCB_REG_OFF(REG_ES),     REGINFO_OFFSET_AUTO, 192),
  REGINFO("fs",  4, 22, TCB_REG_OFF(REG_FS),     REGINFO_OFFSET_AUTO, 200),
  REGINFO("gs",  4, 23, TCB_REG_OFF(REG_GS),     REGINFO_OFFSET_AUTO, 208),
  REGINFO("fs",  4, 22, TCB_REG_OFF(REG_FS),     REGINFO_OFFSET_INVALID, 168), /* fs_base */
  REGINFO("gs",  4, 23, TCB_REG_OFF(REG_GS),     REGINFO_OFFSET_INVALID, 176), /* gs_base */
  REGINFO("rax", 8, 0,  TCB_REG_OFF(REG_RAX),    REGINFO_OFFSET_INVALID, 120), /* orig_ax */
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
    .reginfo = g_reginfo,
  },
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/
