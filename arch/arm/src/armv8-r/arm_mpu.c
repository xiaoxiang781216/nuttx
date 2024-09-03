/****************************************************************************
 * arch/arm/src/armv8-r/arm_mpu.c
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

/* The available region bitmap */

static DEFINE_PER_CPU_BSS_BMP(unsigned int, g_mpu_region);
#define g_mpu_region this_cpu_var_bmp(g_mpu_region)

/****************************************************************************
 * Private Functions
 ****************************************************************************/

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
      CP15_SET(PRSELR, region);
      CP15_SET(PRBAR, 0);
      CP15_SET(PRLAR, 0);
    }

  UP_MB();
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: mpu_allocregion
 *
 * Description:
 *   Allocate the next region
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   The index of the allocated region.
 *
 ****************************************************************************/

unsigned int mpu_allocregion(void)
{
  unsigned int i = ffs(~g_mpu_region) - 1;

  /* There are not enough regions to apply */

  DEBUGASSERT(i < CONFIG_ARM_MPU_NREGIONS);
  g_mpu_region |= 1 << i;
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

  CP15_SET(PRSELR, region);
  CP15_SET(PRBAR, 0);
  CP15_SET(PRLAR, 0);
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

  CP15_SET(MAIR0, (MPU_MAIR_STRONGLY_ORDER <<  0) |
                  (MPU_MAIR_DEVICE         <<  8) |
                  (MPU_MAIR_NONCACHEABLE   << 16) |
                  (MPU_MAIR_WRITE_THROUGH  << 24));

  CP15_SET(MAIR1, (MPU_MAIR_WRITE_BACK     <<  0));

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
 *   flags1 - Additional flags for PBAR.
 *   flags2 - Additional flags for PLAR.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

void mpu_modify_region(unsigned int region, uintptr_t base, size_t size,
                       uint32_t flags1, uint32_t flags2)
{
  uintptr_t limit;
  uintptr_t rbase;

  /* Ensure the base address alignment */

  limit = (base + size - 1) & MPU_RLAR_LIMIT_MASK;
  rbase = base & MPU_RBAR_BASE_MASK;

  /* Select the region */

  CP15_SET(PRSELR, region);

  /* Set the region base, limit and attribute */

  CP15_SET(PRBAR, rbase | flags1);
  CP15_SET(PRLAR, limit | flags2 | MPU_RLAR_ENABLE);

  /* Ensure MPU setting take effects */

  UP_MB();
}

/****************************************************************************
 * Name: mpu_configure_region
 *
 * Description:
 *   Configure a region for privileged, strongly ordered memory
 *
 * Input Parameters:
 *   base   - The base address of the region.
 *   size   - The size of the region.
 *   flags1 - Additional flags for the region.
 *   flags2 - Additional flags for the region.
 *
 * Returned Value:
 *   The region number allocated for the configured region.
 *
 ****************************************************************************/

unsigned int mpu_configure_region(uintptr_t base, size_t size,
                                  uint32_t flags1, uint32_t flags2)
{
  unsigned int region = mpu_allocregion();
  mpu_modify_region(region, base, size, flags1, flags2);
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
      mpu_configure_region(conf->base, conf->size, conf->flags1,
                           conf->flags2);
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
  uint32_t rlar;
  uint32_t rbar;
  uint32_t ctrl;

  /* Get free region */

  ctrl = CP15_GET(SCTLR);
  _info("MPU Enable:%" PRIu32 " Background Region:%" PRIu32 "\n",
        ctrl & SCTLR_M , ctrl & SCTLR_BR);
  for (i = 0; i < CONFIG_ARM_MPU_NREGIONS; i++)
    {
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
