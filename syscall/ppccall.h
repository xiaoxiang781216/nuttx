/****************************************************************************
 * syscall/ppcs/ppccall.h
 *
 * SPDX-License-Identifier: Apache-2.0
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

#include <syscall.h>

#include <nuttx/ppccall.h>

/****************************************************************************
 * Public Function
 ****************************************************************************/

static inline_function uintptr_t ppc_call0(uintptr_t nbr)
{
  struct ppccall_param_s param;

  param.nbr = nbr;

  return sys_call1(SYS_ppccall, (uintptr_t)&param);
}

static inline_function uintptr_t ppc_call1(uintptr_t nbr, uintptr_t parm1)
{
  struct ppccall_param_s param;

  param.nbr   = nbr;
  param.parm1 = parm1;

  return sys_call1(SYS_ppccall, (uintptr_t)&param);
}

static inline_function uintptr_t ppc_call2(uintptr_t nbr, uintptr_t parm1,
                                           uintptr_t parm2)
{
  struct ppccall_param_s param;

  param.nbr   = nbr;
  param.parm1 = parm1;
  param.parm2 = parm2;

  return sys_call1(SYS_ppccall, (uintptr_t)&param);
}

static inline_function uintptr_t ppc_call3(uintptr_t nbr, uintptr_t parm1,
                                           uintptr_t parm2, uintptr_t parm3)
{
  struct ppccall_param_s param;

  param.nbr   = nbr;
  param.parm1 = parm1;
  param.parm2 = parm2;
  param.parm3 = parm3;

  return sys_call1(SYS_ppccall, (uintptr_t)&param);
}

static inline_function uintptr_t ppc_call4(uintptr_t nbr, uintptr_t parm1,
                                           uintptr_t parm2, uintptr_t parm3,
                                           uintptr_t parm4)
{
  struct ppccall_param_s param;

  param.nbr   = nbr;
  param.parm1 = parm1;
  param.parm2 = parm2;
  param.parm3 = parm3;
  param.parm4 = parm4;

  return sys_call1(SYS_ppccall, (uintptr_t)&param);
}

static inline_function uintptr_t ppc_call5(uintptr_t nbr, uintptr_t parm1,
                                           uintptr_t parm2, uintptr_t parm3,
                                           uintptr_t parm4, uintptr_t parm5)
{
  struct ppccall_param_s param;

  param.nbr   = nbr;
  param.parm1 = parm1;
  param.parm2 = parm2;
  param.parm3 = parm3;
  param.parm4 = parm4;
  param.parm5 = parm5;

  return sys_call1(SYS_ppccall, (uintptr_t)&param);
}

static inline_function uintptr_t ppc_call6(uintptr_t nbr, uintptr_t parm1,
                                           uintptr_t parm2, uintptr_t parm3,
                                           uintptr_t parm4, uintptr_t parm5,
                                           uintptr_t parm6)
{
  struct ppccall_param_s param;

  param.nbr   = nbr;
  param.parm1 = parm1;
  param.parm2 = parm2;
  param.parm3 = parm3;
  param.parm4 = parm4;
  param.parm5 = parm5;
  param.parm6 = parm6;

  return sys_call1(SYS_ppccall, (uintptr_t)&param);
}
