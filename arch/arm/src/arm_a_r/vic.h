/****************************************************************************
 * arch/arm/src/arm_a_r/vic.h
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

#ifndef __ARCH_ARM_SRC_ARM_A_R_VIC_H
#define __ARCH_ARM_SRC_ARM_A_R_VIC_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifndef __ASSEMBLY__
#  include <stdint.h>
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifndef __ASSEMBLY__

/****************************************************************************
 * Name: arm_vic_initialize
 *
 * Description:
 *   Initialize the VIC(s). Disable all interrupts, clear all pending
 *   software interrupts, and set all interrupts to IRQ mode.
 *
 ****************************************************************************/

void arm_vic_initialize(void);

/****************************************************************************
 * Name: arm_decodeirq
 *
 * Description:
 *   This function is called from the IRQ vector handler in
 *   arm_vectors.S. At this point, the interrupt has been taken and
 *   the registers have been saved on the stack. This function
 *   simply needs to determine the irq number of the interrupt and
 *   then to call arm_doirq to dispatch the interrupt.
 *
 *  Input Parameters:
 *   regs - A pointer to the register save area on the stack.
 *
 ****************************************************************************/

uint32_t *arm_decodeirq(uint32_t *regs);

/****************************************************************************
 * Name: arm_decodefiq
 *
 * Description:
 *   This function is called from the FIQ vector handler in
 *   arm_vectors.S. At this point, the interrupt has been taken and
 *   the registers have been saved on the stack. This function
 *   simply needs to determine the irq number of the interrupt and
 *   then to call arm_doirq to dispatch the interrupt.
 *
 *  Input Parameters:
 *   regs - A pointer to the register save area on the stack.
 *
 ****************************************************************************/

uint32_t *arm_decodefiq(uint32_t *regs);

#endif /* __ASSEMBLY__ */
#endif /* __ARCH_ARM_SRC_ARM_A_R_VIC_H */
