/****************************************************************************
 * arch/risc-v/src/common/riscv_aplic.c
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
#include "riscv_aia.h"
#include "chip.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: riscv_aplic_init
 *
 * Description:
 *   Initialize APLIC. In IMSIC mode: disable all sources, configure
 *   IRQ-to-EIID mapping, program MSI address registers, and init PCI
 *   MSI allocator. In direct mode: disable all sources, configure IRQ
 *   priority/target, and set IDC delivery/threshold per hart.
 *
 ****************************************************************************/

void riscv_aplic_init(void)
{
  int id;
#ifdef CONFIG_ARCH_RV_HAVE_IMSIC
  uint32_t tmp;
#endif

  /* Disable all interrupt sources */

  riscv_aplic_disable_irqs(RISCV_APLIC_BASE, RISCV_APLIC_NR_IRQS);

#ifdef CONFIG_ARCH_RV_HAVE_IMSIC
  /* IMSIC mode: configure each source with hart=0, EIID=id+1 */

  for (id = 0; id < RISCV_APLIC_NR_IRQS; id++)
    {
      riscv_aplic_configure_irq(RISCV_APLIC_BASE, id + 1, 0, id + 1);
    }

  /* Program MSI target address registers */

  tmp = RISCV_IMSIC_BASE >> RISCV_IMSIC_MMIO_PAGE_BIT;
  putreg32(tmp, RISCV_APLIC_BASE + RISCV_APLIC_MMSICFGADDR);

  tmp = (uint32_t)((uint64_t)RISCV_IMSIC_BASE >> 32) |
        (RISCV_APLIC_MSI_LHXW << RISCV_APLIC_MSICFGADDRH_LHXW_SHIFT) |
        (RISCV_APLIC_MSI_LHXS << RISCV_APLIC_MSICFGADDRH_LHXS_SHIFT) |
        (RISCV_APLIC_MSI_HHXW << RISCV_APLIC_MSICFGADDRH_HHXW_SHIFT) |
        (RISCV_APLIC_MSI_HHXS << RISCV_APLIC_MSICFGADDRH_HHXS_SHIFT);
  putreg32(tmp, RISCV_APLIC_BASE + RISCV_APLIC_MMSICFGADDRH);

#ifdef CONFIG_ARCH_RV_IMSIC_PCI
  /* Initialize PCI MSI allocator for identities above APLIC wire range */

  riscv_imsic_pci_init(RISCV_APLIC_IRQ_END + 1,
                       RISCV_NR_EXT_IRQS - RISCV_APLIC_IRQ_END - 1);
#endif

#else /* Direct mode */
  /* Configure each source with default priority and hart=0 */

  for (id = 0; id < RISCV_APLIC_NR_IRQS; id++)
    {
      riscv_aplic_configure_irq(RISCV_APLIC_BASE, id + 1,
                                RISCV_APLIC_DEFAULT_PRIORITY, 0);
    }

  /* Set IDC delivery and threshold for each hart */

  for (id = 0; id < CPU_SETSIZE; id++)
    {
      uintptr_t idc = RISCV_APLIC_IDC(RISCV_APLIC_BASE, id);

      putreg32(RISCV_APLIC_ENABLE_IDELIVERY,
               idc + RISCV_APLIC_IDC_IDELIVERY);
      putreg32(0, idc + RISCV_APLIC_IDC_IFORCE);
      putreg32(RISCV_APLIC_ENABLE_ITHRESHOLD,
               idc + RISCV_APLIC_IDC_ITHRESHOLD);
    }
#endif
}

/****************************************************************************
 * Name: up_disable_irq
 *
 * Description:
 *   Disable the IRQ specified by 'irq'. For external IRQs, disables
 *   both APLIC source and IMSIC EIE bit (if IMSIC present).
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
#ifdef CONFIG_ARCH_RV_HAVE_IMSIC
      riscv_imsic_csr_write(ISELECT_EITHRESHOLD,
                            RISCV_IMSIC_DISABLE_EITHRESHOLD);
      riscv_imsic_csr_write(ISELECT_EIDELIVERY,
                            RISCV_IMSIC_DISABLE_EIDELIVERY);
#endif
    }
  else if (irq > RISCV_IRQ_EXT)
    {
      extirq = irq - RISCV_IRQ_EXT;
      if (extirq < RISCV_NR_EXT_IRQS)
        {
          if (extirq <= RISCV_APLIC_NR_IRQS)
            {
              riscv_aplic_disable_irq(RISCV_APLIC_BASE, extirq);
            }

#ifdef CONFIG_ARCH_RV_HAVE_IMSIC
          riscv_imsic_local_eie_disable(extirq);
#endif
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
 *   Enable the IRQ specified by 'irq'. For external IRQs, configures
 *   APLIC source and enables IMSIC EIE (if present).
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
#ifdef CONFIG_ARCH_RV_HAVE_IMSIC
      riscv_imsic_csr_write(ISELECT_EITHRESHOLD,
                            RISCV_IMSIC_ENABLE_EITHRESHOLD);
      riscv_imsic_csr_write(ISELECT_EIDELIVERY,
                            RISCV_IMSIC_ENABLE_EIDELIVERY);
#endif
    }
  else if (irq > RISCV_IRQ_EXT)
    {
      extirq = irq - RISCV_IRQ_EXT;
      if (extirq < RISCV_NR_EXT_IRQS)
        {
          if (extirq <= RISCV_APLIC_NR_IRQS)
            {
              riscv_aplic_configure_irq(RISCV_APLIC_BASE, extirq,
                                        RISCV_APLIC_SOURCECFG_SM_EDGE_RISE,
                                        riscv_mhartid());
              riscv_aplic_enable_irq(RISCV_APLIC_BASE, extirq);
            }

#ifdef CONFIG_ARCH_RV_HAVE_IMSIC
          riscv_imsic_local_eie_enable(extirq);
#endif
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
 *   Enable external interrupts, activate APLIC domain, return old state.
 *
 ****************************************************************************/

irqstate_t up_irq_enable(void)
{
  irqstate_t oldstat;

  up_enable_irq(RISCV_IRQ_EXT);
  oldstat = READ_AND_SET_CSR(CSR_STATUS, STATUS_IE);

#ifdef CONFIG_ARCH_RV_HAVE_IMSIC
  modifyreg32(RISCV_APLIC_BASE + RISCV_APLIC_DOMAINCFG, 0x0,
              RISCV_APLIC_DOMAINCFG_IE | RISCV_APLIC_DOMAINCFG_DM);
#else
  modifyreg32(RISCV_APLIC_BASE + RISCV_APLIC_DOMAINCFG, 0x0,
              RISCV_APLIC_DOMAINCFG_IE);
#endif

  return oldstat;
}
