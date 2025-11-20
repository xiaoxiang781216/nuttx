/****************************************************************************
 * include/nuttx/ppccall.h
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

#ifndef __INCLUDE_NUTTX_PPCCALL_H
#define __INCLUDE_NUTTX_PPCCALL_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_LIB_PPCCALL

#include <nuttx/sched.h>

/****************************************************************************
 * Public Types
 ****************************************************************************/

enum
{
#  define PPCCALL_LOOKUP_(f)   PPC_##f
#  define PPCCALL_LOOKUP(f,n)  PPCCALL_LOOKUP_(f),
#  include <ppccall_lookup.h>
  PPC_maxppccall
#  undef PPCCALL_LOOKUP_
#  undef PPCCALL_LOOKUP
};

struct ppccall_param_s
{
  uintptr_t nbr;
  uintptr_t parm1; /* offset 1 * sizeof(uintptr_t) */
  uintptr_t parm2; /* offset 2 * sizeof(uintptr_t) */
  uintptr_t parm3; /* offset 3 * sizeof(uintptr_t) */
  uintptr_t parm4; /* offset 4 * sizeof(uintptr_t) */
  uintptr_t parm5; /* offset 5 * sizeof(uintptr_t) */
  uintptr_t parm6; /* offset 6 * sizeof(uintptr_t) */
};

struct ppccall_item_s
{
  uintptr_t nbr;
  uintptr_t func;
};

struct ppccall_info_s
{
  uintptr_t         func;
  FAR struct tcb_s *tcb;
};

/****************************************************************************
 * Public Function
 ****************************************************************************/

int ppc_register(FAR const struct ppccall_item_s *items, size_t nitem);
int ppc_unregister(FAR const struct ppccall_item_s *items, size_t nitem);
FAR const struct ppccall_info_s *ppccall_find_info(uintptr_t nbr);

#endif /* CONFIG_LIB_PPCCALL */
#endif /* __INCLUDE_NUTTX_PPCCALL_H */
