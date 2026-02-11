/****************************************************************************
 * arch/arm64/src/common/arm64_fork.h
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

#ifndef __ARCH_ARM_SRC_COMMON_ARM_FORK_H
#define __ARCH_ARM_SRC_COMMON_ARM_FORK_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <arch/irq.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifdef CONFIG_ARCH_FPU
#define FORK_REGS_SIZE       (32 + FPU_CONTEXT_REGS)
#else
#define FORK_REGS_SIZE       (32)
#endif

#ifndef __ASSEMBLY__

struct fork_s
{
  uint64_t regs[29];        /* REG_X0 to REG_X28 as in irq.h */
  uint64_t fp;              /* REG_X29 in irq.h */
  uint64_t lr;              /* REG_X30 in irq.h */
  uint64_t sp;              /* REG_SP_ELX(31) in irq.h */
#ifdef CONFIG_ARCH_FPU
  uint64_t fpu[FPU_CONTEXT_REGS];
#endif
};

#endif /* __ASSEMBLY__ */

#endif /* __ARCH_ARM_SRC_COMMON_ARM_FORK_H */
