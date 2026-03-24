/****************************************************************************
 * arch/arm/src/realview/realview_irq.c
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

#include <nuttx/config.h>

#include <nuttx/arch.h>

#include "arm.h"
#include "arm_internal.h"

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/* GIC functions from armv7-a/arm_gicv2.c */

void arm_gic0_initialize(void);
void arm_gic_initialize(void);

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_irqinitialize
 *
 * Description:
 *   Initialize the GIC interrupt controller and enable interrupts.
 *
 ****************************************************************************/

void up_irqinitialize(void)
{
  /* Initialize the Generic Interrupt Controller */

  arm_gic0_initialize();
  arm_gic_initialize();

  /* And finally, enable interrupts */

#ifndef CONFIG_SUPPRESS_INTERRUPTS
  arm_color_intstack();
  up_irq_restore(PSR_MODE_SYS | PSR_F_BIT);
#endif
}
