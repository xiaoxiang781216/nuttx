/****************************************************************************
 * arch/zsp/src/common/zsp_irq.c
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
#include <errno.h>
#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <arch/irq.h>

#include "zsp_internal.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: zsp_irqinfo
 *
 * Description:
 *   Given an IRQ number, provide the %imask bit position.
 *
 ****************************************************************************/

static int zsp_irqinfo(int irq, int *bit)
{
  /* Map NuttX IRQ number to %imask bit position:
   *   ZSP_IRQ_TIMER0 (23) -> %imask bit 5
   *   ZSP_IRQ_TIMER1 (22) -> %imask bit 6
   *   ZSP_IRQ_INT0-INT13 (8-21) -> %imask bits 0-13
   */

  if (irq == ZSP_IRQ_TIMER0)
    {
      *bit = 5;
    }
  else if (irq == ZSP_IRQ_TIMER1)
    {
      *bit = 6;
    }
  else if (irq >= ZSP_IRQ_INT0 && irq <= ZSP_IRQ_INT13)
    {
      *bit = irq - ZSP_IRQ_INT0;
    }
  else
    {
      return -EINVAL;
    }

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_irqinitialize
 *
 * Description:
 *   Initialize interrupt subsystem.
 *
 ****************************************************************************/

void up_irqinitialize(void)
{
  /* Disable all interrupts during initialization */

  __asm__ __volatile__("bitc %%imask, 15\n" : : : "memory");

  /* Enable global interrupts (individual sources still masked) */

  up_irq_enable();
}

/****************************************************************************
 * Name: up_disable_irq / up_enable_irq
 *
 * Description:
 *   Enable/disable individual interrupt sources via %imask bits.
 *   Disable the IRQ specified by 'irq' at the interrupt controller level.
 *
 ****************************************************************************/

void up_disable_irq(int irq)
{
  irqstate_t flags;
  uint32_t imask;
  int bit;

  if (zsp_irqinfo(irq, &bit) != OK)
    {
      return;
    }

  flags = up_irq_save();
  __asm__ __volatile__(
    "mov32 %0, %%imask\n"
    : "=r"(imask) : : "memory"
  );
  imask &= ~(1u << bit);
  __asm__ __volatile__(
    "mov32 %%imask, %0\n"
    : : "r"(imask) : "memory"
  );
  up_irq_restore(flags);
}

/****************************************************************************
 * Name: up_enable_irq
 *
 * Description:
 *   Enable the IRQ specified by 'irq' at the interrupt controller level.
 *
 ****************************************************************************/

void up_enable_irq(int irq)
{
  irqstate_t flags;
  uint32_t imask;
  int bit;

  if (zsp_irqinfo(irq, &bit) != OK)
    {
      return;
    }

  flags = up_irq_save();
  __asm__ __volatile__(
    "mov32 %0, %%imask\n"
    : "=r"(imask) : : "memory"
  );
  imask |= (1u << bit);
  __asm__ __volatile__(
    "mov32 %%imask, %0\n"
    : : "r"(imask) : "memory"
  );
  up_irq_restore(flags);
}
