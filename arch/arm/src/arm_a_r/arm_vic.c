/****************************************************************************
 * arch/arm/src/arm_a_r/arm_vic.c
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

#include <stdint.h>
#include <strings.h>

#include <nuttx/arch.h>
#include <nuttx/irq.h>

#include "arm_internal.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* PL190 VIC register offsets */

#define VIC_IRQSTATUS       0x000 /* Active and enabled as IRQ */
#define VIC_FIQSTATUS       0x004 /* Active and enabled as FIQ */
#define VIC_RAWINTR         0x008 /* Raw status before masking */
#define VIC_INTSELECT       0x00c /* IRQ(0) or FIQ(1) per src */
#define VIC_INTENABLE       0x010 /* Write 1 to enable source */
#define VIC_INTENCLEAR      0x014 /* Write 1 to disable source */
#define VIC_SOFTINT         0x018 /* Write 1 to trigger soft IRQ */
#define VIC_SOFTINTCLEAR    0x01c /* Write 1 to clear soft IRQ */
#define VIC_PROTECTION      0x020 /* Privileged-only access ctrl */
#define VIC_VECTADDR        0x030 /* Active IRQ vector address */
#define VIC_DEFVECTADDR     0x034 /* Default handler address */

/* Each VIC handles 32 interrupts */

#define VIC_NUM_IRQS        32

/* Helper macros for primary VIC */

#define vic_getreg(n, o) \
  getreg32(CONFIG_ARM_VIC_BASE + (n) * CONFIG_ARM_VIC_OFFSET + (o))
#define vic_putreg(v, n, o) \
  putreg32((v), CONFIG_ARM_VIC_BASE + \
           (n) * CONFIG_ARM_VIC_OFFSET + (o))

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: arm_vic_initialize
 *
 * Description:
 *   Initialize the VIC(s). Disable all interrupts, clear all pending
 *   software interrupts, and set all interrupts to IRQ mode.
 *
 ****************************************************************************/

void arm_vic_initialize(void)
{
  int i;

  for (i = 0; i < CONFIG_ARM_VIC_NLINES / VIC_NUM_IRQS; i++)
    {
      vic_putreg(0xffffffff, i, VIC_INTENCLEAR);
      vic_putreg(0xffffffff, i, VIC_SOFTINTCLEAR);
      vic_putreg(0, i, VIC_INTSELECT);
    }
}

/****************************************************************************
 * Name: up_enable_irq
 *
 * Description:
 *   Enable the IRQ specified by 'irq' at the VIC level.
 *
 ****************************************************************************/

void up_enable_irq(int irq)
{
  if (irq >= 0 && irq < CONFIG_ARM_VIC_NLINES)
    {
      int n = irq / VIC_NUM_IRQS;

      vic_putreg(1 << (irq % VIC_NUM_IRQS), n, VIC_INTENABLE);
    }
}

/****************************************************************************
 * Name: up_disable_irq
 *
 * Description:
 *   Disable the IRQ specified by 'irq' at the VIC level.
 *
 ****************************************************************************/

void up_disable_irq(int irq)
{
  if (irq >= 0 && irq < CONFIG_ARM_VIC_NLINES)
    {
      int n = irq / VIC_NUM_IRQS;

      vic_putreg(1 << (irq % VIC_NUM_IRQS), n, VIC_INTENCLEAR);
    }
}

/****************************************************************************
 * Name: up_trigger_irq
 *
 * Description:
 *   Trigger an IRQ by asserting a software interrupt.
 *
 ****************************************************************************/

void up_trigger_irq(int irq, cpu_set_t cpuset)
{
  UNUSED(cpuset);

  if (irq >= 0 && irq < CONFIG_ARM_VIC_NLINES)
    {
      int n = irq / VIC_NUM_IRQS;

      vic_putreg(1 << (irq % VIC_NUM_IRQS), n, VIC_SOFTINT);
    }
}

/****************************************************************************
 * Name: up_secure_irq
 *
 * Description:
 *   Secure an IRQ. On the PL190 VIC this selects FIQ (secure=true)
 *   or IRQ (secure=false) routing via the VIC_INTSELECT register.
 *
 ****************************************************************************/

#ifdef CONFIG_ARCH_HIPRI_INTERRUPT
void up_secure_irq(int irq, bool secure)
{
  if (irq >= 0 && irq < CONFIG_ARM_VIC_NLINES)
    {
      int n = irq / VIC_NUM_IRQS;
      uint32_t bit = 1 << (irq % VIC_NUM_IRQS);
      uint32_t regval;

      regval = vic_getreg(n, VIC_INTSELECT);
      if (secure)
        {
          regval |= bit;
        }
      else
        {
          regval &= ~bit;
        }

      vic_putreg(regval, n, VIC_INTSELECT);
    }
}

/****************************************************************************
 * Name: up_secure_irq_all
 *
 * Description:
 *   Secure all IRQs. On the PL190 VIC this sets all interrupts
 *   to FIQ (secure=true) or IRQ (secure=false) routing.
 *
 ****************************************************************************/

void up_secure_irq_all(bool secure)
{
  uint32_t val = secure ? 0xffffffff : 0;
  int i;

  for (i = 0; i < CONFIG_ARM_VIC_NLINES / VIC_NUM_IRQS; i++)
    {
      vic_putreg(val, i, VIC_INTSELECT);
    }
}
#endif

/****************************************************************************
 * Name: arm_decodeirq
 *
 * Description:
 *   This function is called from the IRQ vector handler in arm_vectors.S.
 *   At this point, the interrupt has been taken and the registers have
 *   been saved on the stack. This function simply needs to determine the
 *   irq number of the interrupt and then to call arm_doirq to dispatch
 *   the interrupt.
 *
 *  Input Parameters:
 *   regs - A pointer to the register save area on the stack.
 *
 ****************************************************************************/

uint32_t *arm_decodeirq(uint32_t *regs)
{
  uint32_t status;
  int i;

  for (i = 0; i < CONFIG_ARM_VIC_NLINES / VIC_NUM_IRQS; i++)
    {
      status = vic_getreg(i, VIC_IRQSTATUS);
      while (status != 0)
        {
          int irq = i * VIC_NUM_IRQS + ffs(status) - 1;

          regs = arm_doirq(irq, regs);
          status &= status - 1;
        }
    }

  return regs;
}

/****************************************************************************
 * Name: arm_decodefiq
 *
 * Description:
 *   This function is called from the FIQ vector handler in arm_vectors.S.
 *   At this point, the interrupt has been taken and the registers have
 *   been saved on the stack. This function simply needs to determine the
 *   irq number of the interrupt and then to call arm_doirq to dispatch
 *   the interrupt.
 *
 *  Input Parameters:
 *   regs - A pointer to the register save area on the stack.
 *
 ****************************************************************************/

uint32_t *arm_decodefiq(uint32_t *regs)
{
  uint32_t status;
  int i;

  for (i = 0; i < CONFIG_ARM_VIC_NLINES / VIC_NUM_IRQS; i++)
    {
      status = vic_getreg(i, VIC_FIQSTATUS);
      while (status != 0)
        {
          int irq = i * VIC_NUM_IRQS + ffs(status) - 1;

          regs = arm_doirq(irq, regs);
          status &= status - 1;
        }
    }

  return regs;
}
