/****************************************************************************
 * boards/arm/qemu/qemu-armv8r-aarch32/src/qemu_boardinit.c
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

#include <nuttx/bits.h>
#include <stdint.h>
#include <nuttx/board.h>
#include "qemu-armv8r.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define PL61_PWR_BASE   0x90b0000
#define PL61_PWR_DIR    (PL61_PWR_BASE + 0x400)
#define PL61_PWR_PIN1   (PL61_PWR_BASE + (BIT(1) << 2))

#define PL61_REG(x)     *(volatile char *)(x)

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: qemu_memory_initialize
 *
 * Description:
 *   All qemu architectures must provide the following entry point.  This
 *   entry point is called early in the initialization before memory has
 *   been configured.  This board-specific function is responsible for
 *   configuring any on-board memories.
 *
 *   Logic in qemu_memory_initialize must be careful to avoid using any
 *   global variables because those will be uninitialized at the time this
 *   function is called.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void qemu_memory_initialize(void)
{
  /* SDRAM was initialized by a bootloader in the supported configurations. */
}

/****************************************************************************
 * Name: qemu_board_initialize
 *
 * Description:
 *   All qemu architectures must provide the following entry point.  This
 *   entry point is called in the initialization phase -- after
 *   qemu_memory_initialize and after all memory has been configured and
 *   mapped but before any devices have been initialized.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void qemu_board_initialize(void)
{
#ifdef CONFIG_ARCH_LEDS
  /* Configure on-board LEDs if LED support has been selected. */

#endif
}

/****************************************************************************
 * Name: board_late_initialize
 *
 * Description:
 *   If CONFIG_BOARD_LATE_INITIALIZE is selected, then an additional
 *   initialization call will be performed in the boot-up sequence to a
 *   function called board_late_initialize(). board_late_initialize() will be
 *   called immediately after up_intitialize() is called and just before the
 *   initial application is started.  This additional initialization phase
 *   may be used, for example, to initialize board-specific device drivers.
 *
 ****************************************************************************/

#ifdef CONFIG_BOARD_LATE_INITIALIZE
void board_late_initialize(void)
{
  /* Perform board initialization */

  qemu_bringup();
}
#endif /* CONFIG_BOARD_LATE_INITIALIZE */

#ifdef CONFIG_BOARDCTL_POWEROFF
int board_power_off(int status)
{
  UNUSED(status);

#ifdef CONFIG_ARM_SEMIHOSTING_POWEROFF
  up_systempoweroff();
#endif

  return 0;
}
#endif

#ifdef CONFIG_BOARDCTL_RESET
int board_reset(int status)
{
  UNUSED(status);

  /* Set PL61 pin1 as output then enable it */

  PL61_REG(PL61_PWR_DIR)  = BIT(1);
  PL61_REG(PL61_PWR_PIN1) = BIT(1);

  return 0;
}
#endif
