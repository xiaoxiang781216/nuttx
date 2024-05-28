/****************************************************************************
 * arch/xtensa/src/iss-hifi3z/hifi3z_timerisr.c
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

#include <nuttx/arch.h>
#include <arch/chip/irq.h>
#include <arch/xtensa/xtensa_specregs.h>

#include "xtensa_counter.h"

/****************************************************************************
 * Private data
 ****************************************************************************/

static const uint32_t g_tick_divisor = 48000;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int hifi3z_timerisr(int irq, void *regs, void *arg)
{
  uint32_t divisor;
  uint32_t compare;
  uint32_t diff;

  divisor = g_tick_divisor;
  do
    {
      compare = xtensa_getcompare();
      xtensa_setcompare(compare + divisor);

      /* Process one timer tick */

      nxsched_process_timer();

      diff = xtensa_getcount() - compare;
    }
  while (diff >= divisor);

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void up_timer_initialize(void)
{
  uint32_t count;

  count = xtensa_getcount();
  xtensa_setcompare(count + g_tick_divisor);

  /* Attach the timer interrupt */

  irq_attach(HIFI3Z_IRQ_TIMER0, hifi3z_timerisr, NULL);

  /* Enable the timer interrupt. */

  up_enable_irq(HIFI3Z_IRQ_TIMER0);
}
