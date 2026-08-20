/****************************************************************************
 * arch/risc-v/src/common/riscv_imsic_pci.c
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

#include <errno.h>

#include <nuttx/bits.h>
#include <nuttx/kmalloc.h>
#include <nuttx/spinlock.h>

#include "riscv_internal.h"
#include "riscv_aia.h"
#include "chip.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

static spinlock_t g_pci_lock = SP_UNLOCKED;
static int g_pci_id_start;
static int g_pci_id_count;
static unsigned long *g_pci_bitmap;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: riscv_imsic_pci_init
 *
 * Description:
 *   Initialize PCI MSI identity allocator. Called from chip layer during
 *   up_irqinitialize with platform-specific id_start and id_count.
 *
 *   id_start - First IMSIC identity available for PCI MSI allocation.
 *              Must be above the APLIC wire IRQ range to avoid conflicts.
 *   id_count - Number of identities available for PCI MSI.
 *
 ****************************************************************************/

int riscv_imsic_pci_init(int id_start, int id_count)
{
  if (id_start < 1 || id_count <= 0)
    {
      return -EINVAL;
    }

  g_pci_id_start = id_start;
  g_pci_id_count = id_count;
  g_pci_bitmap = kmm_zalloc(BITS_TO_LONGS(id_count) * sizeof(unsigned long));

  return (g_pci_bitmap == NULL) ? -ENOMEM : 0;
}

/****************************************************************************
 * Name: up_alloc_irq_msi
 *
 * Description:
 *   Allocate IMSIC identities for PCI MSI vectors.
 *   Returns NuttX IRQ numbers (RISCV_IRQ_EXT + identity).
 *
 ****************************************************************************/

int up_alloc_irq_msi(uint8_t busno, uint32_t devfn, FAR int *pirq, int num)
{
  irqstate_t flags;
  int offset;
  int i;

  UNUSED(busno);
  UNUSED(devfn);

  /* Find a contiguous free region in the identity bitmap */

  flags = spin_lock_irqsave(&g_pci_lock);
  offset = bitmap_find_free_region(g_pci_bitmap, g_pci_id_count, num);
  spin_unlock_irqrestore(&g_pci_lock, flags);

  if (offset < 0)
    {
      return offset;
    }

  /* Convert bitmap offset to NuttX IRQ numbers */

  for (i = 0; i < num; i++)
    {
      pirq[i] = RISCV_IRQ_EXT + g_pci_id_start + offset + i;
    }

  return num;
}

/****************************************************************************
 * Name: up_release_irq_msi
 *
 * Description:
 *   Release previously allocated IMSIC MSI identities back to the pool.
 *
 ****************************************************************************/

void up_release_irq_msi(FAR int *irq, int num)
{
  irqstate_t flags;
  int offset = (*irq - RISCV_IRQ_EXT) - g_pci_id_start;

  flags = spin_lock_irqsave(&g_pci_lock);
  bitmap_release_region(g_pci_bitmap, offset, num);
  spin_unlock_irqrestore(&g_pci_lock, flags);
}

/****************************************************************************
 * Name: up_connect_irq
 *
 * Description:
 *   Return the MSI address and data for a PCI device to write when
 *   signaling an interrupt. The device writes 'mdr' to address 'mar'.
 *
 *   For IMSIC:
 *     mar = target hart's IMSIC page seteipnum_le register
 *     mdr = IMSIC identity number
 *
 ****************************************************************************/

int up_connect_irq(FAR const int *irq, int num,
                   FAR uintptr_t *mar, FAR uint32_t *mdr)
{
  uintptr_t hartid = riscv_mhartid();

  UNUSED(num);

  /* MSI address: target hart's IMSIC MMIO page + seteipnum_le offset */

  *mar = RISCV_IMSIC_BASE + (hartid << RISCV_IMSIC_MMIO_PAGE_BIT)
         + RISCV_IMSIC_MMIO_PAGE_LE;

  /* MSI data: the IMSIC identity number that becomes pending */

  *mdr = *irq - RISCV_IRQ_EXT;

  return 0;
}
