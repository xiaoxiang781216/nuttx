/****************************************************************************
 * arch/arm/src/arm_a_r/arm_mpu.c
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
#include <debug.h>
#include <sys/param.h>
#include <arch/barriers.h>

#include "mpu.h"
#include "arm_internal.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Configuration ************************************************************/

#ifndef CONFIG_ARM_MPU_NREGIONS
#  define CONFIG_ARM_MPU_NREGIONS 8
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

#ifndef CONFIG_ARCH_ARMV8R
/* These sets represent the set of disabled memory sub-regions.  A bit set
 * corresponds to a disabled sub-region; the LS bit corresponds to the first
 * region.
 *
 * The g_ms_regionmask array is indexed by the number of subregions at the
 * end of the region:  0 means no sub-regions are available(0xff) and 8 means
 * all subregions are available (0x00).
 */

static const uint8_t g_ms_regionmask[9] =
{
  0xff, 0xfe, 0xfc, 0xf8, 0xf0, 0xe0, 0xc0, 0x80, 0x00
};

/* The g_ls_regionmask array is indexed by the number of subregions at the
 * beginning of the region:  0 means no sub-regions need be disabled (0x00)
 * and 8 means all subregions must be disabled (0xff).
 */

static const uint8_t g_ls_regionmask[9] =
{
  0x00, 0x01, 0x03, 0x07, 0x0f, 0x1f, 0x3f, 0x7f, 0xff
};
#endif /* !CONFIG_ARCH_ARMV8R */

/* The available region bitmap */

static DEFINE_PER_CPU_BSS_BMP(unsigned int, g_mpu_region);
#define g_mpu_region this_cpu_var_bmp(g_mpu_region)

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#ifndef CONFIG_ARCH_ARMV8R
/****************************************************************************
 * Name: mpu_subregion_ms
 *
 * Description:
 *   Given (1) the size of the memory to be mapped and (2) the log2 size
 *   of the mapping to use, determine the minimal sub-region set at the
 *   to be disabled at the higher end of the region.
 *
 * Assumption:
 *   l2size has the same properties as the return value from
 *   mpu_log2regionceil()
 *
 * Input Parameters:
 *   size: The size of the region.
 *   l2size: The L2 size of the region.
 *
 * Returned Value:
 *   The sub-region bitmask.
 *
 ****************************************************************************/

static inline uint32_t mpu_subregion_ms(size_t size, uint8_t l2size)
{
  unsigned int nsrs;
  uint32_t     asize;
  uint32_t     mask;

  /* Examples with l2size = 12:
   *
   *         Shifted Adjusted        Number      Sub-Region
   * Size    Mask    Size      Shift Sub-Regions Bitset
   * 0x1000  0x01ff  0x1000    9     8           0x00
   * 0x0c00  0x01ff  0x0c00    9     6           0xc0
   * 0x0c40  0x01ff  0x0e00    9     7           0x80
   */

  if (l2size < 32)
    {
      mask  = ((1 << l2size) - 1) >> 3; /* Shifted mask */
    }

  /* The 4Gb region size is a special case */

  else
    {
      /* NOTE: There is no way to represent a 4Gb region size in the 32-bit
       * input.
       */

      mask = 0x1fffffff;         /* Shifted mask */
    }

  asize = (size + mask) & ~mask; /* Adjusted size */
  nsrs  = asize >> (l2size - 3); /* Number of subregions */
  return g_ms_regionmask[nsrs];
}

/****************************************************************************
 * Name: mpu_subregion_ls
 *
 * Description:
 *   Given (1) the offset to the beginning of data in the region and (2) the
 *   log2 size of the mapping to use, determine the minimal sub-region set
 *   to span that memory region sub-region set at the to be disabled at the
 *   lower end of the region
 *
 * Assumption:
 *   l2size has the same properties as the return value from
 *   mpu_log2regionceil()
 *
 * Input Parameters:
 *   offset: The offset of the region.
 *   l2size: The L2 size of the region.
 *
 * Returned Value:
 *   The sub-region bitmask.
 *
 ****************************************************************************/

static inline uint32_t mpu_subregion_ls(size_t offset, uint8_t l2size)
{
  unsigned int nsrs;
  uint32_t     aoffset;
  uint32_t     mask;

  /* Examples with l2size = 12:
   *
   *         Shifted Adjusted        Number      Sub-Region
   * Offset  Mask    Offset    Shift Sub-Regions Bitset
   * 0x0000  0x01ff  0x0000    9     8           0x00
   * 0x0400  0x01ff  0x0400    9     6           0x03
   * 0x02c0  0x01ff  0x0200    9     7           0x01
   */

  if (l2size < 32)
    {
      mask  = ((1 << l2size) - 1) >> 3; /* Shifted mask */
    }

  /* The 4Gb region size is a special case */

  else
    {
      /* NOTE: There is no way to represent a 4Gb region size in the 32-bit
       * input.
       */

      mask = 0x1fffffff;             /* Shifted mask */
    }

  aoffset = offset & ~mask;          /* Adjusted offset */
  nsrs    = aoffset >> (l2size - 3); /* Number of subregions */
  return g_ls_regionmask[nsrs];
}
#endif /* !CONFIG_ARCH_ARMV8R */

/****************************************************************************
 * Name: mpu_reset_internal
 *
 * Description:
 *   Resets the MPU to disabled.
 *
 * Input Parameters:
 *   None.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

#if defined(CONFIG_ARM_MPU_RESET) || defined(CONFIG_ARM_MPU_EARLY_RESET)
static void mpu_reset_internal(void)
{
  int region;
  int regions;
  regions = (CP15_GET(MPUIR) & MPU_TYPE_DREGION_MASK)
                                  >> MPU_TYPE_DREGION_SHIFT;

  for (region = 0; region < regions; region++)
    {
#ifdef CONFIG_ARCH_ARMV8R
      CP15_SET(PRSELR, region);
      CP15_SET(PRBAR, 0);
      CP15_SET(PRLAR, 0);
#else
      CP15_SET(RGNR, region);
      CP15_SET(DRBAR, 0);
      CP15_SET(DRACR, 0);
      CP15_SET(DRSR, 0);
#endif
    }

  UP_MB();
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: mpu_allocregions
 *
 * Description:
 *   Allocate the next region(s)
 *
 * Input Parameters:
 *   nregions - The number of regions to be allocated.
 *
 * Returned Value:
 *   The index of the allocated region.
 *
 ****************************************************************************/

unsigned int mpu_allocregions(unsigned int nregions)
{
  unsigned int i = ffs(~g_mpu_region) - 1;

  /* There are not enough regions to apply */

  DEBUGASSERT((i + nregions - 1) < CONFIG_ARM_MPU_NREGIONS);
  DEBUGASSERT(((g_mpu_region >> i) & ((1 << nregions) - 1)) == 0);
  g_mpu_region |= ((1 << nregions) - 1) << i;
  return i;
}

/****************************************************************************
 * Name: mpu_freeregion
 *
 * Description:
 *   Free target region.
 *
 * Input Parameters:
 *  region - The index of the region to be freed.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void mpu_freeregion(unsigned int region)
{
  DEBUGASSERT(region < CONFIG_ARM_MPU_NREGIONS);

  /* Clear and disable the given MPU Region */

#ifdef CONFIG_ARCH_ARMV8R
  CP15_SET(PRSELR, region);
  CP15_SET(PRBAR, 0);
  CP15_SET(PRLAR, 0);
#else
  CP15_SET(RGNR, region);
  CP15_SET(DRBAR, 0);
  CP15_SET(DRACR, 0);
  CP15_SET(DRSR, 0);
#endif
  g_mpu_region &= ~(1 << region);
  UP_MB();
}

/****************************************************************************
 * Name: mpu_control
 *
 * Description:
 *   Configure and enable (or disable) the MPU
 *
 * Input Parameters:
 *   enable     - Flag indicating whether to enable the MPU.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

void mpu_control(bool enable)
{
  uint32_t regval = 0;

#ifdef CONFIG_ARCH_ARMV8R
  CP15_SET(MAIR0, (MPU_MAIR_STRONGLY_ORDER <<  0) |
                  (MPU_MAIR_DEVICE         <<  8) |
                  (MPU_MAIR_NONCACHEABLE   << 16) |
                  (MPU_MAIR_WRITE_THROUGH  << 24));

  CP15_SET(MAIR1, (MPU_MAIR_WRITE_BACK     <<  0));
#endif

  /* Set/clear the following bits in the SCTLR:
   *
   * SCTLR_M   Bit 0:  MPU enable bit
   * SCTLR_BR  Bit 17: Background Region bit (not cleared)
   */

  regval = CP15_GET(SCTLR);
  if (enable)
    {
      regval |= (SCTLR_M | SCTLR_BR);
    }
  else
    {
      regval &= ~SCTLR_M;
    }

  CP15_SET(SCTLR, regval);

  /* Ensure MPU setting take effects */

  UP_MB();
}

#ifndef CONFIG_ARCH_ARMV8R
/****************************************************************************
 * Name: mpu_log2regionceil
 *
 * Description:
 *   Determine the smallest value of l2size (log base 2 size) such that the
 *   following is true:
 *
 *   size <= (1 << l2size)
 *
 * Input Parameters:
 *   size - The size of the region.
 *
 * Returned Value:
 *   The logarithm base 2 of the ceiling value for the MPU region size.
 *
 ****************************************************************************/

uint8_t mpu_log2regionceil(size_t size)
{
  uint8_t l2size;

  /* The minimum permitted region size is 32 bytes (log2(32) = 5. */

  for (l2size = 5; l2size < 32 && size > (1 << l2size); l2size++);
  return l2size;
}

/****************************************************************************
 * Name: mpu_log2regionfloor
 *
 * Description:
 *   Determine the largest value of l2size (log base 2 size) such that the
 *   following is true:
 *
 *   size >= (1 << l2size)
 *
 * Input Parameters:
 *   size - The size of the region.
 *
 * Returned Value:
 *   The logarithm base 2 of the floor value for the MPU region size.
 *
 ****************************************************************************/

uint8_t mpu_log2regionfloor(size_t size)
{
  uint8_t l2size = mpu_log2regionceil(size);

  if (l2size > 4 && size < (1 << l2size))
    {
      l2size--;
    }

  return l2size;
}

/****************************************************************************
 * Name: mpu_subregion
 *
 * Description:
 *   Given the size of the (1) memory to be mapped and (2) the log2 size
 *   of the mapping to use, determine the minimal sub-region set to span
 *   that memory region.
 *
 * Assumption:
 *   l2size has the same properties as the return value from
 *   mpu_log2regionceil()
 *
 * Input Parameters:
 *   base   - The base address of the region.
 *   size   - The size of the region.
 *   l2size - Log2 of the actual region size is <= (1 << l2size).
 *
 * Returned Value:
 *   The subregion settings as a 32-bit value.
 *
 ****************************************************************************/

uint32_t mpu_subregion(uintptr_t base, size_t size, uint8_t l2size)
{
  uint32_t mask;
  size_t offset;
  uint32_t ret;

  /* Eight subregions are supported.  The representation is as an 8-bit
   * value with the LS bit corresponding to subregion 0.  A bit is set
   * to disable the sub-region.
   *
   * l2size: Log2 of the actual region size is <= (1 << l2size);
   */

  DEBUGASSERT(l2size > 4 && size <= (1 << l2size));

  /* For region sizes of 32, 64, and 128 bytes, the effect of setting
   * one or more bits of the SRD field to 1 is UNPREDICTABLE.
   */

  if (l2size < 8)
    {
      return 0;
    }

  /* Calculate the offset of the base address into the aligned region. */

  mask   = (1 << l2size) - 1;
  offset = base & mask;

  /* Calculate the mask need to handle disabled subregions at the end of the
   * region
   */

  ret = mpu_subregion_ms(size + offset, l2size);

  /* Then OR in the mask need to handle disabled subregions at the beginning
   * of the region.
   */

  ret |= mpu_subregion_ls(offset, l2size);
  return ret;
}
#endif /* !CONFIG_ARCH_ARMV8R */

/****************************************************************************
 * Name: mpu_modify_region
 *
 * Description:
 *   Modify a region for privileged, strongly ordered memory
 *
 * Input Parameters:
 *   region - The index of the MPU region to modify.
 *   base   - The base address of the region.
 *   size   - The size of the region.
 *   flags  - Additional flags for PRBAR (v8r) or DRACR (v7r).
 *   flags2 - Additional flags for PRLAR (v8r only, ignored on v7r).
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

void mpu_modify_region(unsigned int region, uintptr_t base, size_t size,
                       uint32_t flags
#ifdef CONFIG_ARCH_ARMV8R
                       , uint32_t flags2
#endif
                       )
{
#ifdef CONFIG_ARCH_ARMV8R
  uintptr_t limit;
  uintptr_t rbase;

  /* Ensure the base address alignment */

  limit = (base + size - 1) & MPU_RLAR_LIMIT_MASK;
  rbase = base & MPU_RBAR_BASE_MASK;

  /* Select the region */

  CP15_SET(PRSELR, region);

  /* Set the region base, limit and attribute */

  CP15_SET(PRBAR, rbase | flags);
  CP15_SET(PRLAR, limit | flags2 | MPU_RLAR_ENABLE);
#else /* ARMv7-R */
  uint32_t     regval;
  uint8_t      l2size;
  uint8_t      subregions;
  uintptr_t    alignedbase;

  /* Check that the region is valid */

  DEBUGASSERT(g_mpu_region & (1 << region));

  alignedbase  = base & MPU_RBAR_ADDR_MASK;
  l2size       = mpu_log2regionceil(size + base - alignedbase);
  alignedbase &= ~((1 << l2size) - 1);
  l2size       = mpu_log2regionceil(size + base - alignedbase);

  DEBUGASSERT(alignedbase + (1 << l2size) >= base + size);
  DEBUGASSERT(l2size == 5 ||
              alignedbase + (1 << (l2size - 1)) < base + size);
  DEBUGASSERT((alignedbase & MPU_RBAR_ADDR_MASK) == alignedbase);
  DEBUGASSERT((alignedbase & ((1 << l2size) - 1)) == 0);

  /* Select the region */

  CP15_SET(RGNR, region);

  /* Select the region base address */

  CP15_SET(DRBAR, alignedbase & MPU_RBAR_ADDR_MASK);

  /* Select the region size and the sub-region map */

  l2size = mpu_log2regionceil(size);
  subregions = mpu_subregion(base, size, l2size);

  /* Configure the Region */

  CP15_SET(DRACR, flags);
  regval = MPU_RASR_ENABLE                              | /* Enable region */
           MPU_RASR_RSIZE_LOG2(l2size)                  | /* Region size   */
           (subregions << MPU_RASR_SRD_SHIFT);            /* Sub-regions   */
  CP15_SET(DRSR, regval);
#endif

  /* Ensure MPU setting take effects */

  UP_MB();
}

#ifndef CONFIG_ARCH_ARMV8R
/****************************************************************************
 * Name: mpu_modify_regions
 *
 * Description:
 *   Configure multi regions for privileged, strongly ordered memory.
 *   This may use up to 2 MPU regions due to power-of-2
 *   alignment constraints.
 *
 * Input Parameters:
 *   region  - First region number to modify.
 *   base    - Base address of the region.
 *   size    - Size of the region.
 *   flags   - Flags to configure the region.
 *
 ****************************************************************************/

void mpu_modify_regions(unsigned int region, uintptr_t base, size_t size,
                        uint32_t flags)
{
  uintptr_t    alignedbase;
  uint8_t      l2size;
  size_t       region1_size;
  size_t       region2_size;

  alignedbase  = base & MPU_RBAR_ADDR_MASK;
  l2size       = mpu_log2regionceil(size + base - alignedbase);
  alignedbase &= ~((1 << l2size) - 1);
  l2size       = mpu_log2regionceil(size + base - alignedbase);
  alignedbase &= ~((1 << l2size) - 1);

  if (alignedbase + (1 << l2size) < base + size)
    {
      region1_size = alignedbase + (1 << l2size) - base;
      region2_size = size - region1_size;
    }
  else
    {
      region1_size = size;
      region2_size = 0;
    }

  mpu_modify_region(region, base, region1_size, flags);

  if (region2_size != 0)
    {
      mpu_modify_region(region + 1, base + region1_size,
                        region2_size, flags);
    }
  else
    {
      CP15_SET(RGNR, region + 1);
      CP15_SET(DRBAR, 0);
      CP15_SET(DRACR, 0);
      CP15_SET(DRSR, 0);
    }
}
#endif /* !CONFIG_ARCH_ARMV8R */

/****************************************************************************
 * Name: mpu_configure_region
 *
 * Description:
 *   Configure a region for privileged, strongly ordered memory
 *
 * Input Parameters:
 *   base   - The base address of the region.
 *   size   - The size of the region.
 *   flags  - Additional flags for the region.
 *   flags2 - Additional flags for the region (v8r only).
 *
 * Returned Value:
 *   The region number allocated for the configured region.
 *
 ****************************************************************************/

unsigned int mpu_configure_region(uintptr_t base, size_t size,
                                  uint32_t flags
#ifdef CONFIG_ARCH_ARMV8R
                                  , uint32_t flags2
#endif
                                  )
{
  unsigned int region = mpu_allocregion();
  mpu_modify_region(region, base, size, flags
#ifdef CONFIG_ARCH_ARMV8R
                    , flags2
#endif
                    );
  return region;
}

/****************************************************************************
 * Name: mpu_initialize
 *
 * Description:
 *   Configure a region for privileged, strongly ordered memory
 *
 * Input Parameters:
 *   table      - MPU initialization table.
 *   count      - Initialize the number of entries in the region table.
 *
 * Returned Value:
 *   NULL.
 *
 ****************************************************************************/

void mpu_initialize(const struct mpu_region_s *table, size_t count)
{
  const struct mpu_region_s *conf;
  size_t index;

  mpu_control(false);
  for (index = 0; index < count; index++)
    {
      conf = &table[index];
      mpu_configure_region(conf->base, conf->size, conf->flags
#ifdef CONFIG_ARCH_ARMV8R
                           , conf->flags2
#endif
                           );
    }

  mpu_control(true);
}

/****************************************************************************
 * Name: mpu_dump_region
 *
 * Description:
 *   Dump the region that has been used.
 *
 * Input Parameters:
 *   None.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

void mpu_dump_region(void)
{
  int i;
  int count = 0;
  uint32_t ctrl;

  /* Get free region */

  ctrl = CP15_GET(SCTLR);
  _info("MPU Enable:%" PRIu32 " Background Region:%" PRIu32 "\n",
        ctrl & SCTLR_M, ctrl & SCTLR_BR);
  for (i = 0; i < CONFIG_ARM_MPU_NREGIONS; i++)
    {
#ifdef CONFIG_ARCH_ARMV8R
      uint32_t rbar;
      uint32_t rlar;

      CP15_SET(PRSELR, i);
      rlar = CP15_GET(PRLAR);
      rbar = CP15_GET(PRBAR);
      _info("MPU-%d, 0x%08"PRIx32"-0x%08"PRIx32" SH=%"PRIx32" AP=%"PRIx32""
            "XN=%"PRIu32"\n", i, rbar & MPU_RBAR_BASE_MASK,
            rlar & MPU_RLAR_LIMIT_MASK, rbar & MPU_RBAR_SH_MASK,
            rbar & MPU_RBAR_AP_MASK, rbar & MPU_RBAR_XN);
      if (rlar & MPU_RLAR_ENABLE)
        {
          count++;
        }
#else /* ARMv7-R */
      uint32_t dracr;
      uint32_t drbar;
      uint32_t drsr;

      CP15_SET(RGNR, i);
      drbar = CP15_GET(DRBAR);
      dracr = CP15_GET(DRACR);
      drsr = CP15_GET(DRSR);
      _info("MPU-%d, base=0%08X l2size=%"PRIu32" bufferable=%u"
            "cacheable=%u shareable=%u\n", i, drbar & MPU_RBAR_ADDR_MASK,
            drsr & MPU_RASR_RSIZE_MASK, dracr & MPU_RACR_B,
            dracr & MPU_RACR_C, dracr & MPU_RACR_S);
      if (drsr & MPU_RASR_ENABLE)
        {
          count++;
        }
#endif
    }

  _info("Total Use Region:%d, Remaining Available:%d\n", count,
        CONFIG_ARM_MPU_NREGIONS - count);
}

/****************************************************************************
 * Name: mpu_reset
 *
 * Description:
 *   Conditional public interface that resets the MPU to disabled during
 *   MPU initialization.
 *
 * Input Parameters:
 *   None.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/
#if defined(CONFIG_ARM_MPU_RESET)
void mpu_reset(void)
{
  mpu_reset_internal();
}
#endif

/****************************************************************************
 * Name: mpu_early_reset
 *
 * Description:
 *   Conditional public interface that resets the MPU to disabled immediately
 *   after reset.
 *
 * Input Parameters:
 *   None.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/
#if defined(CONFIG_ARM_MPU_EARLY_RESET)
void mpu_early_reset(void)
{
  mpu_reset_internal();
}
#endif
