/****************************************************************************
 * arch/risc-v/src/common/riscv_plic.c
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
#include <assert.h>

#include <nuttx/arch.h>

#include "riscv_internal.h"
#include "riscv_plic.h"
#include "chip.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: riscv_plic_init
 *
 * Description:
 *   Initialize the PLIC: disable all sources, complete pending claim,
 *   set priorities to 1, threshold to 0.
 *
 ****************************************************************************/

void riscv_plic_init(void)
{
  int i;
  uint32_t val;

  for (i = 0; i < (RISCV_NR_EXT_IRQS + 31) / 32; i++)
    {
      putreg32(0, RISCV_PLIC_ENABLE_BASE + 4 * i);
    }

  val = getreg32(RISCV_PLIC_CLAIM);
  if (val)
    {
      putreg32(val, RISCV_PLIC_CLAIM);
    }

  for (i = 1; i <= RISCV_NR_EXT_IRQS; i++)
    {
      putreg32(1, RISCV_PLIC_PRIORITY + 4 * i);
    }

  putreg32(0, RISCV_PLIC_THRESHOLD);
}

/****************************************************************************
 * Name: up_disable_irq
 *
 * Description:
 *   Disable the IRQ specified by 'irq'
 *
 ****************************************************************************/

void up_disable_irq(int irq)
{
  int extirq;

  if (irq == RISCV_IRQ_SOFT)
    {
      CLEAR_CSR(CSR_IE, IE_SIE);
    }
  else if (irq == RISCV_IRQ_TIMER)
    {
      CLEAR_CSR(CSR_IE, IE_TIE);
    }
  else if (irq == RISCV_IRQ_EXT)
    {
      CLEAR_CSR(CSR_IE, IE_EIE);
    }
  else if (irq > RISCV_PLIC_IRQ_OFFSET)
    {
      extirq = irq - RISCV_PLIC_IRQ_OFFSET;
      if (extirq < RISCV_NR_EXT_IRQS)
        {
          modifyreg32(RISCV_PLIC_ENABLE_BASE + 4 * (extirq / 32),
                      1 << (extirq % 32), 0);
        }
      else
        {
          PANIC();
        }
    }
}

/****************************************************************************
 * Name: up_enable_irq
 *
 * Description:
 *   Enable the IRQ specified by 'irq'
 *
 ****************************************************************************/

void up_enable_irq(int irq)
{
  int extirq;

  if (irq == RISCV_IRQ_SOFT)
    {
      SET_CSR(CSR_IE, IE_SIE);
    }
  else if (irq == RISCV_IRQ_TIMER)
    {
      SET_CSR(CSR_IE, IE_TIE);
    }
  else if (irq == RISCV_IRQ_EXT)
    {
      SET_CSR(CSR_IE, IE_EIE);
    }
  else if (irq > RISCV_PLIC_IRQ_OFFSET)
    {
      extirq = irq - RISCV_PLIC_IRQ_OFFSET;
      if (extirq < RISCV_NR_EXT_IRQS)
        {
          modifyreg32(RISCV_PLIC_ENABLE_BASE + 4 * (extirq / 32),
                      0, 1 << (extirq % 32));
        }
      else
        {
          PANIC();
        }
    }
}

/****************************************************************************
 * Name: up_irq_enable
 *
 * Description:
 *   Enable external interrupts and return previous state.
 *
 ****************************************************************************/

irqstate_t up_irq_enable(void)
{
  SET_CSR(CSR_IE, IE_EIE);
  return READ_AND_SET_CSR(CSR_STATUS, STATUS_IE);
}

/****************************************************************************
 * Name: up_prioritize_irq
 *
 * Description:
 *   Set the priority of an IRQ in the PLIC.
 *
 ****************************************************************************/

#ifdef CONFIG_ARCH_IRQPRIO
int up_prioritize_irq(int irq, int priority)
{
  if (RISCV_IRQ_EXT < irq && irq < NR_IRQS)
    {
      irq -= RISCV_IRQ_EXT;
      putreg32(priority, (uintptr_t)(RISCV_PLIC_PRIORITY + 4 * irq));
      return OK;
    }

  return -EINVAL;
}
#endif

/****************************************************************************
 * Name: up_trigger_irq
 *
 * Description:
 *   Trigger an IRQ by writing to the PLIC pending register.
 *
 ****************************************************************************/

#ifdef CONFIG_ARCH_HAVE_IRQTRIGGER
void up_trigger_irq(int irq, cpu_set_t cpuset)
{
  if (RISCV_IRQ_EXT < irq && irq < NR_IRQS)
    {
      irq -= RISCV_IRQ_EXT;
      modifyreg32(RISCV_PLIC_PENDING + (irq >> 5) * 4,
                  0, 1ul << (irq & 31));
    }

  UNUSED(cpuset);
}
#endif
