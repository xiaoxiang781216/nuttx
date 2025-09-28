/****************************************************************************
 * arch/tricore/src/common/tricore_getintstack.c
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

#include <stdint.h>

#include "tricore_internal.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_get_intstackbase
 ****************************************************************************/

#ifndef CONFIG_UP
extern unsigned int __USTACK0[];
extern unsigned int __USTACK1[];
extern unsigned int __USTACK2[];
extern unsigned int __USTACK3[];
extern unsigned int __USTACK4[];
extern unsigned int __USTACK5[];
extern unsigned int __ISTACK0[];
extern unsigned int __ISTACK1[];
extern unsigned int __ISTACK2[];
extern unsigned int __ISTACK3[];
extern unsigned int __ISTACK4[];
extern unsigned int __ISTACK5[];

static uintptr_t g_idle_topstack[CONFIG_NCPUS] =
{
  [0] = (uintptr_t)__USTACK0,
#if CONFIG_NCPUS > 1
  [1] = (uintptr_t)__USTACK1,
#if CONFIG_NCPUS > 2
  [2] = (uintptr_t)__USTACK2,
#if CONFIG_NCPUS > 3
  [3] = (uintptr_t)__USTACK3,
#if CONFIG_NCPUS > 4
  [4] = (uintptr_t)__USTACK4,
#if CONFIG_NCPUS > 5
  [5] = (uintptr_t)__USTACK5,
#endif
#endif
#endif
#endif
#endif
};

#if CONFIG_ARCH_INTERRUPTSTACK > 3
static uintptr_t g_int_topstack[CONFIG_NCPUS] =
{
  [0] = (uintptr_t)__ISTACK0,
#if CONFIG_NCPUS > 1
  [1] = (uintptr_t)__ISTACK1,
#if CONFIG_NCPUS > 2
  [2] = (uintptr_t)__ISTACK2,
#if CONFIG_NCPUS > 3
  [3] = (uintptr_t)__ISTACK3,
#if CONFIG_NCPUS > 4
  [4] = (uintptr_t)__ISTACK4,
#if CONFIG_NCPUS > 5
  [5] = (uintptr_t)__ISTACK5,
#endif
#endif
#endif
#endif
#endif
};

uintptr_t up_get_intstackbase(int cpu)
{
  return (uintptr_t)g_int_topstack[cpu] - CONFIG_ARCH_INTERRUPTSTACK;
}
#endif

int up_cpu_idlestack(int cpu, struct tcb_s *tcb, size_t stack_size)
{
  tcb->stack_alloc_ptr = (void *)(g_idle_topstack[cpu] - stack_size);
  tcb->stack_base_ptr  = tcb->stack_alloc_ptr;
  tcb->adj_stack_size  = stack_size;
  return 0;
}
#else

#define g_idle_topstack __USTACK
#define g_intstackalloc __ISTACK_END
#define g_intstacktop   __ISTACK

uintptr_t up_get_intstackbase(int cpu)
{
  return (uintptr_t)g_intstackalloc;
}

int up_cpu_idlestack(int cpu, struct tcb_s *tcb, size_t stack_size)
{
  tcb->stack_alloc_ptr = (void *)(g_idle_topstack - stack_size);
  tcb->stack_base_ptr  = tcb->stack_alloc_ptr;
  tcb->adj_stack_size  = stack_size;
  return 0;
}
#endif
