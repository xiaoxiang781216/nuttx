/****************************************************************************
 * arch/risc-v/src/common/riscv_pmp.c
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
#include <stdbool.h>

#include <sys/param.h>

#include <nuttx/compiler.h>
#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <nuttx/lib/math32.h>

#include "riscv_internal.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* PMP register length in bits */

#ifdef CONFIG_ARCH_RV32
#define PMP_XLEN                (32)
#else
#define PMP_XLEN                (64)
#endif

#define PMP_NREGIONS            CONFIG_ARCH_RISCV_MPU_NREGIONS

#define PMP_CFG_BITS_CNT        (8)
#define PMP_CFG_FLAG_MASK       ((uintptr_t)0xFF)

#define PMP_CFG_CNT_IN_REG      (PMP_XLEN / PMP_CFG_BITS_CNT)

#define PMP_MASK_SET_ONE_REGION(region, attr, reg) \
  do { \
      uintptr_t offset = region % PMP_CFG_CNT_IN_REG; \
      reg &= ~(PMP_CFG_FLAG_MASK << (offset * PMP_CFG_BITS_CNT)); \
      reg |= attr << (offset * PMP_CFG_BITS_CNT); \
    } while(0);

#define PMP_READ_REGION_FROM_REG(region, reg) \
  ({ \
    uintptr_t region##_val = READ_CSR(reg); \
    region##_val >>= ((region % PMP_CFG_CNT_IN_REG) * PMP_CFG_BITS_CNT); \
    region##_val &= PMP_CFG_FLAG_MASK; \
    region##_val; \
  })

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* Helper structure for handling a PMP entry */

struct pmp_entry_s
{
  uintptr_t base;   /* Base address of region */
  uintptr_t end;    /* End address of region */
  uintptr_t size;   /* Region size */
  uint8_t   mode;   /* Address matching mode */
  uint8_t   rwx;    /* Access rights */
};

typedef struct pmp_entry_s pmp_entry_t;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: pmp_check_region_attrs
 *
 * Description:
 *   Test if the base address and size of region meet alignment requirements.
 *
 * Input Parameters:
 *   base - The base address of the region.
 *   size - The memory length of the region.
 *   type - Address matching type.
 *
 * Returned Value:
 *   true if it is, false otherwise.
 *
 ****************************************************************************/

static bool pmp_check_region_attrs(uintptr_t base, uintptr_t size,
                                   uintptr_t type)
{
  switch (type)
  {
    case PMPCFG_A_TOR:

      /* For TOR any size is good, but alignment requirement stands */

      if ((base & 0x03) != 0)
        {
          return false;
        }

      break;

    case PMPCFG_A_NA4:

      /* For NA4 only size 4 is good, and base must be aligned */

      if ((base & 0x03) != 0 || size != 4)
        {
          return false;
        }

      break;

    case PMPCFG_A_NAPOT:
      {
        /* Special range for the whole range */

        if (base == 0 && size == 0)
          {
            return true;
          }

        /* For NAPOT, Naturally aligned power-of-two region, >= 8 bytes */

        if ((base & 0x07) != 0 || size < 8 || (size & (size - 1)) != 0)
          {
            return false;
          }

        /* Get the power-of-two for size, rounded up */

        if ((base & ((UINT64_C(1) << log2ceil(size)) - 1)) != 0)
          {
            /* The start address is not properly aligned with size */

            return false;
          }
      }

      break;

    default:
      break;
  }

  return true;
}

/****************************************************************************
 * Name: pmp_read_region_cfg
 *
 * Description:
 *   Read PMP configuration for region
 *
 * Input Parameters:
 *   region - Region number.
 *
 * Returned Value:
 *   Configuration value from pmpcfg+region
 *
 ****************************************************************************/

static uintptr_t pmp_read_region_cfg(uintptr_t region)
{
#if (PMP_XLEN == 32)
  switch (region)
    {
      case 0 ... 3:
        return PMP_READ_REGION_FROM_REG(region, CSR_PMPCFG0);

      case 4 ... 7:
        return PMP_READ_REGION_FROM_REG(region, CSR_PMPCFG1);

      case 8 ... 11:
        return PMP_READ_REGION_FROM_REG(region, CSR_PMPCFG2);

      case 12 ... 15:
        return PMP_READ_REGION_FROM_REG(region, CSR_PMPCFG3);

      case 16 ... 19:
        return PMP_READ_REGION_FROM_REG(region, CSR_PMPCFG4);

      case 20 ... 23:
        return PMP_READ_REGION_FROM_REG(region, CSR_PMPCFG5);

      case 24 ... 27:
        return PMP_READ_REGION_FROM_REG(region, CSR_PMPCFG6);

      case 28 ... 31:
        return PMP_READ_REGION_FROM_REG(region, CSR_PMPCFG7);

      case 32 ... 35:
        return PMP_READ_REGION_FROM_REG(region, CSR_PMPCFG8);

      case 36 ... 39:
        return PMP_READ_REGION_FROM_REG(region, CSR_PMPCFG9);

      case 40 ... 43:
        return PMP_READ_REGION_FROM_REG(region, CSR_PMPCFG10);

      case 44 ... 47:
        return PMP_READ_REGION_FROM_REG(region, CSR_PMPCFG11);

      case 48 ... 51:
        return PMP_READ_REGION_FROM_REG(region, CSR_PMPCFG12);

      case 52 ... 55:
        return PMP_READ_REGION_FROM_REG(region, CSR_PMPCFG13);

      case 56 ... 59:
        return PMP_READ_REGION_FROM_REG(region, CSR_PMPCFG14);

      case 60 ... 63:
        return PMP_READ_REGION_FROM_REG(region, CSR_PMPCFG15);

      default:
        break;
    }
#elif (PMP_XLEN == 64)
  switch (region)
    {
      case 0 ... 7:
        return PMP_READ_REGION_FROM_REG(region, CSR_PMPCFG0);

      case 8 ... 15:
        return PMP_READ_REGION_FROM_REG(region, CSR_PMPCFG2);

      case 16 ... 23:
        return PMP_READ_REGION_FROM_REG(region, CSR_PMPCFG4);

      case 24 ... 31:
        return PMP_READ_REGION_FROM_REG(region, CSR_PMPCFG6);

      case 32 ... 39:
        return PMP_READ_REGION_FROM_REG(region, CSR_PMPCFG8);

      case 40 ... 47:
        return PMP_READ_REGION_FROM_REG(region, CSR_PMPCFG10);

      case 48 ... 55:
        return PMP_READ_REGION_FROM_REG(region, CSR_PMPCFG12);

      case 56 ... 63:
        return PMP_READ_REGION_FROM_REG(region, CSR_PMPCFG14);

      default:
        break;
    }
#endif

  /* Never executed */

  return 0;
}

/****************************************************************************
 * Name: pmp_read_addr
 *
 * Description:
 *   Read address for region
 *
 * Input Parameters:
 *   region - Region number.
 *
 * Returned Value:
 *   Address value from pmpcfg+region
 *
 ****************************************************************************/

static uintptr_t pmp_read_addr(uintptr_t region)
{
  switch (region)
    {
      case 0:
        return READ_CSR(CSR_PMPADDR0);

      case 1:
        return READ_CSR(CSR_PMPADDR1);

      case 2:
        return READ_CSR(CSR_PMPADDR2);

      case 3:
        return READ_CSR(CSR_PMPADDR3);

      case 4:
        return READ_CSR(CSR_PMPADDR4);

      case 5:
        return READ_CSR(CSR_PMPADDR5);

      case 6:
        return READ_CSR(CSR_PMPADDR6);

      case 7:
        return READ_CSR(CSR_PMPADDR7);

      case 8:
        return READ_CSR(CSR_PMPADDR8);

      case 9:
        return READ_CSR(CSR_PMPADDR9);

      case 10:
        return READ_CSR(CSR_PMPADDR10);

      case 11:
        return READ_CSR(CSR_PMPADDR11);

      case 12:
        return READ_CSR(CSR_PMPADDR12);

      case 13:
        return READ_CSR(CSR_PMPADDR13);

      case 14:
        return READ_CSR(CSR_PMPADDR14);

      case 15:
        return READ_CSR(CSR_PMPADDR15);

      case 16:
        return READ_CSR(CSR_PMPADDR16);

      case 17:
        return READ_CSR(CSR_PMPADDR17);

      case 18:
        return READ_CSR(CSR_PMPADDR18);

      case 19:
        return READ_CSR(CSR_PMPADDR19);

      case 20:
        return READ_CSR(CSR_PMPADDR20);

      case 21:
        return READ_CSR(CSR_PMPADDR21);

      case 22:
        return READ_CSR(CSR_PMPADDR22);

      case 23:
        return READ_CSR(CSR_PMPADDR23);

      case 24:
        return READ_CSR(CSR_PMPADDR24);

      case 25:
        return READ_CSR(CSR_PMPADDR25);

      case 26:
        return READ_CSR(CSR_PMPADDR26);

      case 27:
        return READ_CSR(CSR_PMPADDR27);

      case 28:
        return READ_CSR(CSR_PMPADDR28);

      case 29:
        return READ_CSR(CSR_PMPADDR29);

      case 30:
        return READ_CSR(CSR_PMPADDR30);

      case 31:
        return READ_CSR(CSR_PMPADDR31);

      case 32:
        return READ_CSR(CSR_PMPADDR32);

      case 33:
        return READ_CSR(CSR_PMPADDR33);

      case 34:
        return READ_CSR(CSR_PMPADDR34);

      case 35:
        return READ_CSR(CSR_PMPADDR35);

      case 36:
        return READ_CSR(CSR_PMPADDR36);

      case 37:
        return READ_CSR(CSR_PMPADDR37);

      case 38:
        return READ_CSR(CSR_PMPADDR38);

      case 39:
        return READ_CSR(CSR_PMPADDR39);

      case 40:
        return READ_CSR(CSR_PMPADDR40);

      case 41:
        return READ_CSR(CSR_PMPADDR41);

      case 42:
        return READ_CSR(CSR_PMPADDR42);

      case 43:
        return READ_CSR(CSR_PMPADDR43);

      case 44:
        return READ_CSR(CSR_PMPADDR44);

      case 45:
        return READ_CSR(CSR_PMPADDR45);

      case 46:
        return READ_CSR(CSR_PMPADDR46);

      case 47:
        return READ_CSR(CSR_PMPADDR47);

      case 48:
        return READ_CSR(CSR_PMPADDR48);

      case 49:
        return READ_CSR(CSR_PMPADDR49);

      case 50:
        return READ_CSR(CSR_PMPADDR50);

      case 51:
        return READ_CSR(CSR_PMPADDR51);

      case 52:
        return READ_CSR(CSR_PMPADDR52);

      case 53:
        return READ_CSR(CSR_PMPADDR53);

      case 54:
        return READ_CSR(CSR_PMPADDR54);

      case 55:
        return READ_CSR(CSR_PMPADDR55);

      case 56:
        return READ_CSR(CSR_PMPADDR56);

      case 57:
        return READ_CSR(CSR_PMPADDR57);

      case 58:
        return READ_CSR(CSR_PMPADDR58);

      case 59:
        return READ_CSR(CSR_PMPADDR59);

      case 60:
        return READ_CSR(CSR_PMPADDR60);

      case 61:
        return READ_CSR(CSR_PMPADDR61);

      case 62:
        return READ_CSR(CSR_PMPADDR62);

      case 63:
        return READ_CSR(CSR_PMPADDR63);

      default:
        break;
    }

  /* Never executed */

  return 0;
}

/****************************************************************************
 * Name: pmp_napot_decode
 *
 * Description:
 *   Decode base and size from NAPOT value
 *
 * Input Parameters:
 *   val  - Value to decode.
 *   size - Size out.
 *
 * Returned Value:
 *   Base address
 *
 ****************************************************************************/

static uintptr_t pmp_napot_decode(uintptr_t val, uintptr_t * size)
{
  uintptr_t mask = (uintptr_t)(-1) >> 1;
  uintptr_t pot  = PMP_XLEN + 2;

  while (mask)
    {
      if ((val & mask) == mask)
        {
          break;
        }

      pot--;
      mask >>= 1;
    }

  val &= ~mask;
  *size = UINT64_C(1) << pot;
  return (val << 2);
}

/****************************************************************************
 * Name: pmp_read
 *
 * Description:
 *   Read PMP region into PMP entry
 *
 * Input Parameters:
 *   region - Region number.
 *   entry  - Entry out
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void pmp_read(uintptr_t region, pmp_entry_t * entry)
{
  uintptr_t addr = 0;
  uintptr_t size = 0;
  uintptr_t mode = 0;
  uintptr_t rwx  = 0;
  uintptr_t cfg  = 0;

  addr = pmp_read_addr(region);
  cfg  = pmp_read_region_cfg(region);
  mode = cfg & PMPCFG_A_MASK;
  rwx  = cfg & PMPCFG_RWX_MASK;

  switch (mode)
  {
    case PMPCFG_A_TOR:
      addr <<= 2;

      /* TOR region, must peek into prior region for size */

      if (region == 0)
        {
          size = addr;
        }
      else
        {
          size = addr - pmp_read_addr(region - 1);
        }

      break;

    case PMPCFG_A_NA4:
      addr <<= 2;
      size = 4;
      break;

    case PMPCFG_A_NAPOT:
      addr = pmp_napot_decode(addr, &size);
      break;

    default:
      break;
  }

  entry->base = addr;
  entry->end  = addr + size;
  entry->size = size;
  entry->rwx  = rwx;
  entry->mode = mode;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: riscv_config_pmp_region
 *
 * Description:
 *   This function will set the specific PMP region with the desired cfg.
 *
 * Input Parameters:
 *   region - The region index number.
 *   attr - The region configurations.
 *   base - The base address of the region.
 *   size - The memory length of the region.
 *   For the NAPOT mode, the base address must aligned to the size boundary,
 *   and the size must be power-of-two according to the the PMP spec.
 *
 * Returned Value:
 *   0 on succeess; negated error on failure
 *
 ****************************************************************************/

int riscv_config_pmp_region(uintptr_t region, uintptr_t attr,
                            uintptr_t base, uintptr_t size)
{
  uintptr_t addr    = 0;
  uintptr_t cfg     = 0;
  uintptr_t type    = (attr & PMPCFG_A_MASK);

  if (region >= PMP_NREGIONS)
    {
      return -EINVAL;
    }

  /* Check the region attributes */

  if (pmp_check_region_attrs(base, size, type) == false)
    {
      return -EINVAL;
    }

  /* Calculate new base address from type */

  addr = base >> 2;
  if (type == PMPCFG_A_NAPOT)
    {
      addr |= (size - 1) >> 3;
    }

  /* Set the address value */

  switch (region)
    {
      case 0:
        WRITE_CSR(CSR_PMPADDR0, addr);
        break;

      case 1:
        WRITE_CSR(CSR_PMPADDR1, addr);
        break;

      case 2:
        WRITE_CSR(CSR_PMPADDR2, addr);
        break;

      case 3:
        WRITE_CSR(CSR_PMPADDR3, addr);
        break;

      case 4:
        WRITE_CSR(CSR_PMPADDR4, addr);
        break;

      case 5:
        WRITE_CSR(CSR_PMPADDR5, addr);
        break;

      case 6:
        WRITE_CSR(CSR_PMPADDR6, addr);
        break;

      case 7:
        WRITE_CSR(CSR_PMPADDR7, addr);
        break;

      case 8:
        WRITE_CSR(CSR_PMPADDR8, addr);
        break;

      case 9:
        WRITE_CSR(CSR_PMPADDR9, addr);
        break;

      case 10:
        WRITE_CSR(CSR_PMPADDR10, addr);
        break;

      case 11:
        WRITE_CSR(CSR_PMPADDR11, addr);
        break;

      case 12:
        WRITE_CSR(CSR_PMPADDR12, addr);
        break;

      case 13:
        WRITE_CSR(CSR_PMPADDR13, addr);
        break;

      case 14:
        WRITE_CSR(CSR_PMPADDR14, addr);
        break;

      case 15:
        WRITE_CSR(CSR_PMPADDR15, addr);
        break;

      case 16:
        WRITE_CSR(CSR_PMPADDR16, addr);
        break;

      case 17:
        WRITE_CSR(CSR_PMPADDR17, addr);
        break;

      case 18:
        WRITE_CSR(CSR_PMPADDR18, addr);
        break;

      case 19:
        WRITE_CSR(CSR_PMPADDR19, addr);
        break;

      case 20:
        WRITE_CSR(CSR_PMPADDR20, addr);
        break;

      case 21:
        WRITE_CSR(CSR_PMPADDR21, addr);
        break;

      case 22:
        WRITE_CSR(CSR_PMPADDR22, addr);
        break;

      case 23:
        WRITE_CSR(CSR_PMPADDR23, addr);
        break;

      case 24:
        WRITE_CSR(CSR_PMPADDR24, addr);
        break;

      case 25:
        WRITE_CSR(CSR_PMPADDR25, addr);
        break;

      case 26:
        WRITE_CSR(CSR_PMPADDR26, addr);
        break;

      case 27:
        WRITE_CSR(CSR_PMPADDR27, addr);
        break;

      case 28:
        WRITE_CSR(CSR_PMPADDR28, addr);
        break;

      case 29:
        WRITE_CSR(CSR_PMPADDR29, addr);
        break;

      case 30:
        WRITE_CSR(CSR_PMPADDR30, addr);
        break;

      case 31:
        WRITE_CSR(CSR_PMPADDR31, addr);
        break;

      case 32:
        WRITE_CSR(CSR_PMPADDR32, addr);
        break;

      case 33:
        WRITE_CSR(CSR_PMPADDR33, addr);
        break;

      case 34:
        WRITE_CSR(CSR_PMPADDR34, addr);
        break;

      case 35:
        WRITE_CSR(CSR_PMPADDR35, addr);
        break;

      case 36:
        WRITE_CSR(CSR_PMPADDR36, addr);
        break;

      case 37:
        WRITE_CSR(CSR_PMPADDR37, addr);
        break;

      case 38:
        WRITE_CSR(CSR_PMPADDR38, addr);
        break;

      case 39:
        WRITE_CSR(CSR_PMPADDR39, addr);
        break;

      case 40:
        WRITE_CSR(CSR_PMPADDR40, addr);
        break;

      case 41:
        WRITE_CSR(CSR_PMPADDR41, addr);
        break;

      case 42:
        WRITE_CSR(CSR_PMPADDR42, addr);
        break;

      case 43:
        WRITE_CSR(CSR_PMPADDR43, addr);
        break;

      case 44:
        WRITE_CSR(CSR_PMPADDR44, addr);
        break;

      case 45:
        WRITE_CSR(CSR_PMPADDR45, addr);
        break;

      case 46:
        WRITE_CSR(CSR_PMPADDR46, addr);
        break;

      case 47:
        WRITE_CSR(CSR_PMPADDR47, addr);
        break;

      case 48:
        WRITE_CSR(CSR_PMPADDR48, addr);
        break;

      case 49:
        WRITE_CSR(CSR_PMPADDR49, addr);
        break;

      case 50:
        WRITE_CSR(CSR_PMPADDR50, addr);
        break;

      case 51:
        WRITE_CSR(CSR_PMPADDR51, addr);
        break;

      case 52:
        WRITE_CSR(CSR_PMPADDR52, addr);
        break;

      case 53:
        WRITE_CSR(CSR_PMPADDR53, addr);
        break;

      case 54:
        WRITE_CSR(CSR_PMPADDR54, addr);
        break;

      case 55:
        WRITE_CSR(CSR_PMPADDR55, addr);
        break;

      case 56:
        WRITE_CSR(CSR_PMPADDR56, addr);
        break;

      case 57:
        WRITE_CSR(CSR_PMPADDR57, addr);
        break;

      case 58:
        WRITE_CSR(CSR_PMPADDR58, addr);
        break;

      case 59:
        WRITE_CSR(CSR_PMPADDR59, addr);
        break;

      case 60:
        WRITE_CSR(CSR_PMPADDR60, addr);
        break;

      case 61:
        WRITE_CSR(CSR_PMPADDR61, addr);
        break;

      case 62:
        WRITE_CSR(CSR_PMPADDR62, addr);
        break;

      case 63:
        WRITE_CSR(CSR_PMPADDR63, addr);
        break;

      default:
        return -EINVAL;
    }

  /* Set the configuration register value */

#if (PMP_XLEN == 32)
  switch (region)
    {
      case 0 ... 3:
        cfg = READ_CSR(CSR_PMPCFG0);
        PMP_MASK_SET_ONE_REGION(region, attr, cfg);
        WRITE_CSR(CSR_PMPCFG0, cfg);
        break;

      case 4 ... 7:
        cfg = READ_CSR(CSR_PMPCFG1);
        PMP_MASK_SET_ONE_REGION(region, attr, cfg);
        WRITE_CSR(CSR_PMPCFG1, cfg);
        break;

      case 8 ... 11:
        cfg = READ_CSR(CSR_PMPCFG2);
        PMP_MASK_SET_ONE_REGION(region, attr, cfg);
        WRITE_CSR(CSR_PMPCFG2, cfg);
        break;

      case 12 ... 15:
        cfg = READ_CSR(CSR_PMPCFG3);
        PMP_MASK_SET_ONE_REGION(region, attr, cfg);
        WRITE_CSR(CSR_PMPCFG3, cfg);
        break;

      case 16 ... 19:
        cfg = READ_CSR(CSR_PMPCFG4);
        PMP_MASK_SET_ONE_REGION(region, attr, cfg);
        WRITE_CSR(CSR_PMPCFG4, cfg);
        break;

      case 20 ... 23:
        cfg = READ_CSR(CSR_PMPCFG5);
        PMP_MASK_SET_ONE_REGION(region, attr, cfg);
        WRITE_CSR(CSR_PMPCFG5, cfg);
        break;

      case 24 ... 27:
        cfg = READ_CSR(CSR_PMPCFG6);
        PMP_MASK_SET_ONE_REGION(region, attr, cfg);
        WRITE_CSR(CSR_PMPCFG6, cfg);
        break;

      case 28 ... 31:
        cfg = READ_CSR(CSR_PMPCFG7);
        PMP_MASK_SET_ONE_REGION(region, attr, cfg);
        WRITE_CSR(CSR_PMPCFG7, cfg);
        break;

      case 32 ... 35:
        cfg = READ_CSR(CSR_PMPCFG8);
        PMP_MASK_SET_ONE_REGION(region, attr, cfg);
        WRITE_CSR(CSR_PMPCFG8, cfg);
        break;

      case 36 ... 39:
        cfg = READ_CSR(CSR_PMPCFG9);
        PMP_MASK_SET_ONE_REGION(region, attr, cfg);
        WRITE_CSR(CSR_PMPCFG9, cfg);
        break;

      case 40 ... 43:
        cfg = READ_CSR(CSR_PMPCFG10);
        PMP_MASK_SET_ONE_REGION(region, attr, cfg);
        WRITE_CSR(CSR_PMPCFG10, cfg);
        break;

      case 44 ... 47:
        cfg = READ_CSR(CSR_PMPCFG11);
        PMP_MASK_SET_ONE_REGION(region, attr, cfg);
        WRITE_CSR(CSR_PMPCFG11, cfg);
        break;

      case 48 ... 51:
        cfg = READ_CSR(CSR_PMPCFG12);
        PMP_MASK_SET_ONE_REGION(region, attr, cfg);
        WRITE_CSR(CSR_PMPCFG12, cfg);
        break;

      case 52 ... 55:
        cfg = READ_CSR(CSR_PMPCFG13);
        PMP_MASK_SET_ONE_REGION(region, attr, cfg);
        WRITE_CSR(CSR_PMPCFG13, cfg);
        break;

      case 56 ... 59:
        cfg = READ_CSR(CSR_PMPCFG14);
        PMP_MASK_SET_ONE_REGION(region, attr, cfg);
        WRITE_CSR(CSR_PMPCFG14, cfg);
        break;

      case 60 ... 63:
        cfg = READ_CSR(CSR_PMPCFG15);
        PMP_MASK_SET_ONE_REGION(region, attr, cfg);
        WRITE_CSR(CSR_PMPCFG15, cfg);
        break;

      default:
        return -EINVAL;
    }
#elif (PMP_XLEN == 64)
  switch (region)
    {
      case 0 ... 7:
        cfg = READ_CSR(CSR_PMPCFG0);
        PMP_MASK_SET_ONE_REGION(region, attr, cfg);
        WRITE_CSR(CSR_PMPCFG0, cfg);
        break;

      case 8 ... 15:
        cfg = READ_CSR(CSR_PMPCFG2);
        PMP_MASK_SET_ONE_REGION(region, attr, cfg);
        WRITE_CSR(CSR_PMPCFG2, cfg);
        break;

      case 16 ... 23:
        cfg = READ_CSR(CSR_PMPCFG4);
        PMP_MASK_SET_ONE_REGION(region, attr, cfg);
        WRITE_CSR(CSR_PMPCFG4, cfg);
        break;

      case 24 ... 31:
        cfg = READ_CSR(CSR_PMPCFG6);
        PMP_MASK_SET_ONE_REGION(region, attr, cfg);
        WRITE_CSR(CSR_PMPCFG6, cfg);
        break;

      case 32 ... 39:
        cfg = READ_CSR(CSR_PMPCFG8);
        PMP_MASK_SET_ONE_REGION(region, attr, cfg);
        WRITE_CSR(CSR_PMPCFG8, cfg);
        break;

      case 40 ... 47:
        cfg = READ_CSR(CSR_PMPCFG10);
        PMP_MASK_SET_ONE_REGION(region, attr, cfg);
        WRITE_CSR(CSR_PMPCFG10, cfg);
        break;

      case 48 ... 55:
        cfg = READ_CSR(CSR_PMPCFG12);
        PMP_MASK_SET_ONE_REGION(region, attr, cfg);
        WRITE_CSR(CSR_PMPCFG12, cfg);
        break;

      case 56 ... 63:
        cfg = READ_CSR(CSR_PMPCFG14);
        PMP_MASK_SET_ONE_REGION(region, attr, cfg);
        WRITE_CSR(CSR_PMPCFG14, cfg);
        break;

      default:
        return -EINVAL;
    }
#else
#  error "XLEN of risc-v not supported"
#endif
#ifdef CONFIG_ARCH_USE_S_MODE
  /* Fence is needed when page-based virtual memory is implemented.
   * If page-based virtual memory is not implemented, memory accesses check
   * the PMP settings synchronously, so no SFENCE.VMA is needed.
   */

  __asm volatile("sfence.vma x0, x0" : : : "memory");
#endif

  return OK;
}

/****************************************************************************
 * Name: riscv_check_pmp_access
 *
 * Description:
 *   This function will set the specific PMP region with the desired cfg.
 *
 * Input Parameters:
 *   attr - The region configurations.
 *   base - The base address of the region.
 *   size - The memory length of the region.
 *   For the NAPOT mode, the base address must aligned to the size boundary,
 *   and the size must be power-of-two according to the the PMP spec.
 *
 * Returned Value:
 *   0 if access rights are not set at all
 *   < 0 if access rights are set and match match partially
 *   > 0 if access rights are set and match fully
 *
 ****************************************************************************/

int riscv_check_pmp_access(uintptr_t attr, uintptr_t base, uintptr_t size)
{
  pmp_entry_t   entry;
  uintptr_t     end;
  uintptr_t     orgsize;
  unsigned int  region;

  /* Go through every single configured region and test the attributes */

  attr    = (attr & PMPCFG_RWX_MASK);
  end     = base + size;
  orgsize = size;

  for (region = 0; region < PMP_NREGIONS && size > 0; region++)
    {
      /* Find matching configuration first */

      pmp_read(region, &entry);

      /* Check if any configuration at all */

      if (entry.mode == PMPCFG_A_OFF)
        {
          continue;
        }

      /* Does this address range match ? Take partial matches into account.
       *
       * NOTE: The PMP end address itself is not part of the mapping
       *
       * There are four possibilities:
       * 1: Full match; region inside mapped area
       * 2: Partial match; mapped area inside region
       * 3: Partial match; base inside mapped region, end outside
       * 4: Partial match; base outside mapped region, end inside
       */

      if ((base >= entry.base && end  <= entry.end) ||
          (base <  entry.base && end  >  entry.end) ||
          (base >= entry.base && base <  entry.end) ||
          (end  >  entry.base && end  <= entry.end))
        {
          /* Found a matching splice, check rights */

          if ((entry.rwx & attr) == attr)
            {
              /* Found matching region that allows access */

              size -= MIN(end, entry.end) - MAX(base, entry.base);
            }
          else
            {
              /* Found matching region that does not allow access */

              return PMP_ACCESS_DENIED;
            }
        }
    }

  /* Check if nothing configured at all ? */

  if (size == orgsize)
    {
      return PMP_ACCESS_OFF;
    }

  /* If size is non-positive, the requested range is accessible */

  if (size <= 0)
    {
      return PMP_ACCESS_FULL;
    }

  /* The requested range is either fully or partially inaccessible */

  return PMP_ACCESS_DENIED;
}

/****************************************************************************
 * Name: riscv_configured_pmp_regions
 *
 * Description:
 *   Count amount of configured PMP regions, note: is not atomic
 *
 * Input Parameters:
 *
 * Returned Value:
 *   Amount of configured PMP regions
 *
 ****************************************************************************/

int riscv_configured_pmp_regions(void)
{
  pmp_entry_t   entry;
  unsigned int  region;
  int           ret = 0;

  for (region = 0; region < PMP_NREGIONS; region++)
    {
      pmp_read(region, &entry);

      if (entry.mode != PMPCFG_A_OFF)
        {
          ret++;
        }
    }

  return ret;
}

/****************************************************************************
 * Name: riscv_next_free_pmp_region
 *
 * Description:
 *   Returns next free PMP region, note: is not atomic
 *
 * Input Parameters:
 *
 * Returned Value:
 *   Next free PMP region, or -1 if none found
 *
 ****************************************************************************/

int riscv_next_free_pmp_region(void)
{
  pmp_entry_t   entry;
  unsigned int  region;

  for (region = 0; region < PMP_NREGIONS; region++)
    {
      pmp_read(region, &entry);

      if (entry.mode == PMPCFG_A_OFF)
        {
          return region;
        }
    }

  return -1;
}
