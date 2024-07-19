/****************************************************************************
 * drivers/devicetree/fdt.c
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

#include <stddef.h>
#include <endian.h>
#include <errno.h>
#include <assert.h>
#include <nuttx/compiler.h>
#include <nuttx/fdt.h>
#include <libfdt.h>

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Location of the fdt data for this system. */

static FAR const char *g_fdt_base = NULL;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: fdt_register
 *
 * Description:
 *   Store the pointer to the flattened device tree and verify that it at
 *   least appears to be valid. This function will not fully parse the FDT.
 *
 * Return:
 *   Return -EINVAL if the fdt header does not have the expected magic value.
 *   otherwise return OK. If OK is not returned the existing entry for FDT
 *   is not modified.
 *
 ****************************************************************************/

int fdt_register(FAR const char *fdt_base)
{
  struct fdt_header_s *fdt_header;

  DEBUGASSERT(fdt_base);

  fdt_header = (struct fdt_header_s *)fdt_base;
  if (fdt_header->magic != be32toh(FDT_MAGIC))
    {
      return -EINVAL; /* Bad magic byte read */
    }

  g_fdt_base = fdt_base;
  return OK;
}

/****************************************************************************
 * Name: fdt_get
 *
 * Description:
 *   Return the pointer to a raw FDT. NULL is returned if no FDT has been
 *   loaded.
 *
 ****************************************************************************/

FAR const char *fdt_get(void)
{
  return g_fdt_base;
}

/****************************************************************************
 * Name: fdt_get_irq
 *
 * Description:
 *   Get the interrupt number of the node
 *
 ****************************************************************************/

int fdt_get_irq(FAR const void *fdt, int offset, int irqbase)
{
  FAR const fdt32_t *pv;
  int irq = -1;

  pv = fdt_getprop(fdt, offset, "interrupts", NULL);
  if (pv != NULL)
    {
      irq = fdt32_ld(pv + 1) + irqbase;
    }

  return irq;
}

/****************************************************************************
 * Name: fdt_get_irq_by_path
 *
 * Description:
 *   Get the interrupt number of the node
 *
 ****************************************************************************/

int fdt_get_irq_by_path(FAR const void *fdt, const char *path, int irqbase)
{
  return fdt_get_irq(fdt, fdt_path_offset(fdt, path), irqbase);
}

/****************************************************************************
 * Name: fdt_get_bankwidth
 *
 * Description:
 *   Get the value of bankwidth
 *
 ****************************************************************************/

uint32_t fdt_get_bankwidth(FAR const void *fdt, int offset)
{
  FAR const void *reg;
  uint32_t bankwidth = 0;

  reg = fdt_getprop(fdt, offset, "bank-width", NULL);
  if (reg != NULL)
    {
      bankwidth = fdt32_ld(reg);
    }

  return bankwidth;
}

/****************************************************************************
 * Name: fdt_get_parent_address_cells
 *
 * Description:
 *   Get the parent address of the register space
 *
 ****************************************************************************/

int fdt_get_parent_address_cells(FAR const void *fdt, int offset)
{
  int parentoff;

  parentoff = fdt_parent_offset(fdt, offset);
  if (parentoff < 0)
    {
      return parentoff;
    }

  return fdt_address_cells(fdt, parentoff);
}

/****************************************************************************
 * Name: fdt_get_parent_size_cells
 *
 * Description:
 *   Get the parent size of the register space
 *
 ****************************************************************************/

int fdt_get_parent_size_cells(FAR const void *fdt, int offset)
{
  int parentoff;

  parentoff = fdt_parent_offset(fdt, offset);
  if (parentoff < 0)
    {
      return parentoff;
    }

  return fdt_size_cells(fdt, parentoff);
}

/****************************************************************************
 * Name: fdt_ld_by_cells
 *
 * Description:
 *   Load a 32-bit or 64-bit value from a buffer, depending on the number
 *   of address cells.
 *
 ****************************************************************************/

uintptr_t fdt_ld_by_cells(FAR const void *value, int cells)
{
  if (cells == 2)
    {
      return fdt64_ld(value);
    }
  else
    {
      return fdt32_ld(value);
    }
}

/****************************************************************************
 * Name: fdt_get_reg_count
 *
 * Description:
 *   Get the count (in bytes) of the register space
 *
 ****************************************************************************/

uint32_t fdt_get_reg_count(FAR const void *fdt, int offset)
{
  FAR const struct fdt_property *reg;
  uint32_t count = 0;

  reg = fdt_get_property(fdt, offset, "reg", NULL);
  if (reg != NULL)
    {
      count = fdt32_ld(&reg->len);
    }

  return count;
}

/****************************************************************************
 * Name: fdt_get_reg_base
 *
 * Description:
 *   Get the base address of the register space
 *
 ****************************************************************************/

uintptr_t fdt_get_reg_base(FAR const void *fdt, int offset)
{
  FAR const void *reg;
  uintptr_t addr = 0;

  reg = fdt_getprop(fdt, offset, "reg", NULL);
  if (reg != NULL)
    {
      addr = fdt_ld_by_cells(reg, fdt_get_parent_address_cells(fdt, offset));
    }

  return addr;
}

/****************************************************************************
 * Name: fdt_get_reg_base_by_index
 *
 * Description:
 *   Get the base address of the register space by index
 *
 ****************************************************************************/

uintptr_t fdt_get_reg_base_by_index(FAR const void *fdt, int offset,
                                    int index)
{
  FAR const void *reg;
  uintptr_t addr = 0;

  reg = fdt_getprop(fdt, offset, "reg", NULL);
  if (reg != NULL)
    {
      int address_cell;
      int size_cell;

      address_cell = fdt_get_parent_address_cells(fdt, offset);
      size_cell = fdt_get_parent_size_cells(fdt, offset);
      addr = fdt_ld_by_cells((FAR fdt32_t *)reg +
                             (address_cell + size_cell) * index,
                             address_cell);
    }

  return addr;
}

/****************************************************************************
 * Name: fdt_get_reg_size
 *
 * Description:
 *   Get the size of the register space
 *
 ****************************************************************************/

size_t fdt_get_reg_size(FAR const void *fdt, int offset)
{
  FAR const void *reg;
  size_t size = 0;

  reg = fdt_getprop(fdt, offset, "reg", NULL);
  if (reg != NULL)
    {
      size = fdt_ld_by_cells((FAR fdt32_t *)reg +
                             fdt_get_parent_address_cells(fdt, offset),
                             fdt_get_parent_size_cells(fdt, offset));
    }

  return size;
}

/****************************************************************************
 * Name: fdt_get_reg_size_by_index
 *
 * Description:
 *   Get the size of the register space by index
 *
 ****************************************************************************/

size_t fdt_get_reg_size_by_index(FAR const void *fdt, int offset, int index)
{
  FAR const void *reg;
  size_t size = 0;

  reg = fdt_getprop(fdt, offset, "reg", NULL);
  if (reg != NULL)
    {
      int address_cell;
      int size_cell;

      address_cell = fdt_get_parent_address_cells(fdt, offset);
      size_cell = fdt_get_parent_size_cells(fdt, offset);
      size = fdt_ld_by_cells((FAR fdt32_t *)reg +
             (address_cell + size_cell) * index + address_cell, size_cell);
    }

  return size;
}

/****************************************************************************
 * Name: fdt_get_reg_base_by_path
 *
 * Description:
 *   Get the base address of the register space
 *
 ****************************************************************************/

uintptr_t fdt_get_reg_base_by_path(FAR const void *fdt, FAR const char *path)
{
  return fdt_get_reg_base(fdt, fdt_path_offset(fdt, path));
}

