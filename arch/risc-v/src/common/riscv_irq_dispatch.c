/****************************************************************************
 * arch/risc-v/src/common/riscv_irq_dispatch.c
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

#include <nuttx/irq.h>
#include <nuttx/arch.h>

#include "riscv_internal.h"

#ifdef CONFIG_ARCH_RV_HAVE_APLIC
#  include "riscv_aia.h"
#endif

#ifdef CONFIG_ARCH_RV_HAVE_PLIC
#  include "riscv_plic.h"
#endif

#include "chip.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: riscv_dispatch_irq_ext
 *
 * Description:
 *   Claim and dispatch external interrupts from IMSIC, APLIC IDC, or PLIC.
 *
 ****************************************************************************/

#ifdef CONFIG_ARCH_RV_HAVE_IMSIC
static void *riscv_dispatch_irq_ext(uintreg_t irq, uintreg_t *regs)
{
  int extirq;

  while ((extirq = SWAP_CSR(CSR_TOPEI, 0)) != 0)
    {
      extirq = (extirq >> TOPI_IID_SHIFT) + irq;
      regs = riscv_doirq(extirq, regs);
    }

  return regs;
}
#elif defined(CONFIG_ARCH_RV_HAVE_APLIC)
static void *riscv_dispatch_irq_ext(uintreg_t irq, uintreg_t *regs)
{
  int extirq;
  int hartid = riscv_mhartid();
  uintptr_t claimi = RISCV_APLIC_IDC(RISCV_APLIC_BASE, hartid) +
                     RISCV_APLIC_IDC_CLAIMI;

  while ((extirq = getreg32(claimi)) != 0)
    {
      extirq = (extirq >> RISCV_APLIC_IDC_TOPI_ID_SHIFT) + irq;
      regs = riscv_doirq(extirq, regs);
    }

  return regs;
}
#elif defined(CONFIG_ARCH_RV_HAVE_PLIC)
static void *riscv_dispatch_irq_ext(uintreg_t irq, uintreg_t *regs)
{
  int extirq;

  while ((extirq = getreg32(RISCV_PLIC_CLAIM)) != 0)
    {
      regs = riscv_doirq(irq + extirq, regs);
      putreg32(extirq, RISCV_PLIC_CLAIM);
    }

  return regs;
}
#endif

/****************************************************************************
 * Name: riscv_dispatch_async_irq
 *
 * Description:
 *   Top-level async interrupt dispatcher. IMSIC mode uses CSR_TOPI to
 *   iterate pending interrupts; non-IMSIC uses mcause directly.
 *
 ****************************************************************************/

#ifdef CONFIG_ARCH_RV_HAVE_IMSIC
static void *riscv_dispatch_async_irq(uintreg_t irq, uintreg_t *regs)
{
  while ((irq = READ_CSR(CSR_TOPI)) != 0)
    {
      irq = (irq >> TOPI_IID_SHIFT) + RISCV_IRQ_ASYNC;

      if (RISCV_IRQ_EXT == irq)
        {
          regs = riscv_dispatch_irq_ext(irq, regs);
        }
      else
        {
          regs = riscv_doirq(irq, regs);
        }
    }

  return regs;
}
#else
static void *riscv_dispatch_async_irq(uintreg_t irq, uintreg_t *regs)
{
  irq += RISCV_IRQ_ASYNC;

  if (irq == RISCV_IRQ_EXT)
    {
      regs = riscv_dispatch_irq_ext(irq, regs);
    }
  else
    {
      regs = riscv_doirq(irq, regs);
    }

  return regs;
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: riscv_dispatch_irq
 *
 * Description:
 *   Dispatch interrupts and exceptions from the trap vector.
 *
 ****************************************************************************/

void *riscv_dispatch_irq(uintreg_t vector, uintreg_t *regs)
{
  int irq = vector & (~RISCV_IRQ_BIT);

  if ((vector & RISCV_IRQ_BIT) != 0)
    {
      regs = riscv_dispatch_async_irq(irq, regs);
    }
  else
    {
      regs = riscv_doirq(irq, regs);
    }

  return regs;
}
