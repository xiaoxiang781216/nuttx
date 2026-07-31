/****************************************************************************
 * arch/zsp/src/common/zsp_cpuidlestack.c
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
#include <nuttx/irq.h>
#include <stdint.h>

#include "zsp_internal.h"

/****************************************************************************
 * Public Data
 ****************************************************************************/

/* ZSP requires a dedicated interrupt stack. Fail at compile time if not
 * configured (user must set CONFIG_ARCH_INTERRUPTSTACK in defconfig).
 */

static_assert(CONFIG_ARCH_INTERRUPTSTACK > 0,
              "ZSP requires CONFIG_ARCH_INTERRUPTSTACK > 0");

/* Memory layout (flat build, all computed from Kconfig values):
 *
 *   _ebss                                    <- idle stack base
 *   _ebss + CONFIG_IDLETHREAD_STACKSIZE      <- g_idle_topstack / heap start
 *   ...                                      <- heap
 *   RAM_END - CONFIG_ARCH_INTERRUPTSTACK     <- interrupt stack base
 *   RAM_END                                  <- interrupt stack top
 */

const uintptr_t g_idle_basestack = (uintptr_t)_ebss;
const uintptr_t g_idle_topstack  = (uintptr_t)_ebss +
                                   CONFIG_IDLETHREAD_STACKSIZE;
const uintptr_t g_intstackalloc  = CONFIG_RAM_END -
                                   CONFIG_ARCH_INTERRUPTSTACK;
const uintptr_t g_intstacktop    = STACK_ALIGN_DOWN(CONFIG_RAM_END);

/****************************************************************************
 * Public Functions
 ****************************************************************************/
