/****************************************************************************
 * arch/risc-v/src/common/riscv_imsic.c
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

#include "riscv_internal.h"
#include "riscv_aia.h"

#ifdef CONFIG_ARCH_RV_USE_IMSIC_IPI
#  include "chip.h"
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void riscv_imsic_local_eix_update(unsigned long base_id,
                                  unsigned long num_id,
                                  bool pend, bool val)
{
  uintptr_t i, isel, ireg;
  unsigned long id = base_id;
  unsigned long last_id = base_id + num_id;

  while (id < last_id)
    {
      isel = id / __riscv_xlen;
      isel *= __riscv_xlen / RISCV_IMSIC_EIP_BITS;
      isel += (pend) ? ISELECT_EIP0 : ISELECT_EIE0;

      ireg = 0;
      for (i = id & (__riscv_xlen - 1);
          (id < last_id) && (i < __riscv_xlen); i++)
        {
          ireg |= BIT(i);
          id++;
        }

      if (val)
        {
          riscv_imsic_csr_set(isel, ireg);
        }
      else
        {
          riscv_imsic_csr_clear(isel, ireg);
        }
    }
}

/****************************************************************************
 * Name: riscv_imsic_send_ipi
 *
 * Description:
 *   Send an IPI to the target CPU by writing the IPI identity to
 *   the target hart's IMSIC MMIO page (seteipnum_le register).
 *
 ****************************************************************************/

#ifdef CONFIG_ARCH_RV_USE_IMSIC_IPI
void riscv_imsic_send_ipi(int cpu)
{
  uintptr_t hartid = riscv_cpuid_to_hartid(cpu);
  uintptr_t page   = RISCV_IMSIC_BASE +
                     ((uintptr_t)hartid << RISCV_IMSIC_MMIO_PAGE_BIT);

  /* AIA spec Ch.6: A FENCE is needed before sending an MSI to ensure
   * prior memory stores (e.g. task delivery data) are visible to the
   * target hart before the interrupt arrives.
   */

  __asm__ __volatile__ ("fence rw, w" ::: "memory");

  putreg32(RISCV_IMSIC_IPI_ID, page + RISCV_IMSIC_MMIO_PAGE_LE);
}
#endif
