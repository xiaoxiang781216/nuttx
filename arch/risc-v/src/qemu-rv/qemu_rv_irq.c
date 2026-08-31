/****************************************************************************
 * arch/risc-v/src/qemu-rv/qemu_rv_irq.c
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
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <debug.h>

#include <nuttx/arch.h>
#include <nuttx/irq.h>

#include "riscv_internal.h"
#include "riscv_ipi.h"
#include "riscv_aia.h"
#include "riscv_plic.h"
#include "chip.h"

#ifdef CONFIG_PCI
#  include <nuttx/pci/pci.h>
#endif

#ifdef CONFIG_RPTUN
#include "qemu_rv_rptun.h"
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#ifdef CONFIG_RPTUN
static int qemu_ipi_handler(int mcause, void *regs, void *args)
{
  /* Clear IPI (Inter-Processor-Interrupt) */

  riscv_ipi_clear(this_cpu());

#ifdef CONFIG_SMP
  riscv_pause_handler(mcause, regs, args);
#endif

  qemu_rptun_ipi();
  return OK;
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_irqinitialize
 ****************************************************************************/

void up_irqinitialize(void)
{
  /* Disable Machine interrupts */

  up_irq_save();

  /* Disable all global interrupts */

#ifndef CONFIG_ARCH_RV_HAVE_APLIC
  riscv_plic_init();
#else
  riscv_aplic_init();
#endif

  /* Colorize the interrupt stack for debug purposes */

#if defined(CONFIG_STACK_COLORATION) && CONFIG_ARCH_INTERRUPTSTACK > 15
  size_t intstack_size = (CONFIG_ARCH_INTERRUPTSTACK & ~15);
  riscv_stack_color(g_intstackalloc, intstack_size);
#endif

  /* Attach the common interrupt handler */

  riscv_exception_attach();

#ifdef CONFIG_RPTUN
  /* Replace default IRQ_SOFT handler */

  irq_attach(RISCV_IRQ_SOFT, qemu_ipi_handler, NULL);
#endif

#if defined(CONFIG_SMP) || defined(CONFIG_RPTUN)
  /* Clear IPI for CPU0 */

  riscv_ipi_clear(0);

#ifdef CONFIG_ARCH_RV_IMSIC_IPI
  /* IMSIC IPI: attach handler to external interrupt path (EIID=1),
   * and enable the IPI identity in this core's IMSIC.
   */

  irq_attach(RISCV_IRQ_EXT + RISCV_IMSIC_IPI_ID,
             riscv_cpu_call_handler, NULL);
  riscv_imsic_local_eie_enable(RISCV_IMSIC_IPI_ID);
#else
  up_enable_irq(RISCV_IRQ_SOFT);
#endif
#endif

#ifndef CONFIG_SUPPRESS_INTERRUPTS

  /* And finally, enable interrupts */

  riscv_color_intstack();
  up_irq_enable();
#endif
}

/****************************************************************************
 * Name: up_get_legacy_irq
 *
 * Description:
 *   Get legacy PCI INTx interrupt number by applying the standard PCI
 *   swizzle to the platform-defined INTx base IRQ.
 *
 ****************************************************************************/

#ifdef CONFIG_PCI
int up_get_legacy_irq(uint32_t devfn, uint8_t line, uint8_t pin)
{
  UNUSED(line);

  /* Standard PCI interrupt swizzle (PCI Local Bus Spec 2.2.6):
   * Each device's INTx pin is rotated by its slot number so that
   * adjacent slots don't share the same interrupt line.
   * Formula: irq = base + (slot + pin - 1) % 4
   */

  if (pin >= PCI_INTERRUPT_INTA && pin <= PCI_INTERRUPT_INTD)
    {
      return RISCV_IRQ_EXT + QEMU_RV_PCIE_INTX_BASE +
             (PCI_SLOT(devfn) + pin - 1) % 4;
    }

  return -ENOTSUP;
}
#endif
