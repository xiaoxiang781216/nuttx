/****************************************************************************
 * arch/arm/src/common/arm_getintstack.c
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

#include "arm_internal.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

#if !defined(CONFIG_UP) && CONFIG_ARCH_INTERRUPTSTACK > 7
#  ifdef ARCH_INTERRUPTSTACK_SECTION
static uint64_t g_cpu_intstackalloc[(CONFIG_NCPUS * INTSTACK_SIZE) >> 3]
  locate_data(ARCH_INTERRUPTSTACK_SECTION);
#  else
static uint64_t g_cpu_intstackalloc[(CONFIG_NCPUS * INTSTACK_SIZE) >> 3];
#  endif
#endif

/****************************************************************************
 * Public Data
 ****************************************************************************/

#if !defined(CONFIG_UP) && CONFIG_ARCH_INTERRUPTSTACK > 7
uintptr_t g_cpu_intstack_top[CONFIG_NCPUS] =
{
  (uintptr_t)g_cpu_intstackalloc + INTSTACK_SIZE,
#if CONFIG_NCPUS > 1
  (uintptr_t)g_cpu_intstackalloc + (2 * INTSTACK_SIZE),
#endif
#if CONFIG_NCPUS > 2
  (uintptr_t)g_cpu_intstackalloc + (3 * INTSTACK_SIZE),
#endif
#if CONFIG_NCPUS > 3
  (uintptr_t)g_cpu_intstackalloc + (4 * INTSTACK_SIZE),
#endif
#if CONFIG_NCPUS > 4
  (uintptr_t)g_cpu_intstackalloc + (5 * INTSTACK_SIZE),
#endif
#if CONFIG_NCPUS > 5
  (uintptr_t)g_cpu_intstackalloc + (6 * INTSTACK_SIZE),
#endif
#if CONFIG_NCPUS > 6
  (uintptr_t)g_cpu_intstackalloc + (7 * INTSTACK_SIZE),
#endif
#if CONFIG_NCPUS > 7
  (uintptr_t)g_cpu_intstackalloc + (8 * INTSTACK_SIZE),
#endif
};
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

#if CONFIG_ARCH_INTERRUPTSTACK > 7
uintptr_t up_get_intstackbase(int cpu)
{
#ifdef CONFIG_UP
  return (uintptr_t)g_intstackalloc;
#else
  return g_cpu_intstack_top[cpu] - INTSTACK_SIZE;
#endif
}
#endif
