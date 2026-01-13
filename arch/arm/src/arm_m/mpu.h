/****************************************************************************
 * arch/arm/src/arm_m/mpu.h
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

#ifndef __ARCH_ARM_SRC_ARM_M_MPU_H
#define __ARCH_ARM_SRC_ARM_M_MPU_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifndef __ASSEMBLY__
#  include <sys/types.h>
#  include <stdint.h>
#  include <stdbool.h>
#  include <assert.h>
#  include <debug.h>
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* MPU Register Addresses - Common across ARMv7-M and ARMv8-M */

#define MPU_TYPE                0xe000ed90 /* MPU Type Register */
#define MPU_CTRL                0xe000ed94 /* MPU Control Register */
#define MPU_RNR                 0xe000ed98 /* MPU Region Number Register */
#define MPU_RBAR                0xe000ed9c /* MPU Region Base Address Register */
#define MPU_ATTR                0xe000eda0 /* MPU Region Attribute Register RASR (v7m) or RLAR (v8m) */

/* MPU Alias Registers */

#define MPU_RBAR_A1             0xe000eda4
#define MPU_ATTR_A1             0xe000eda8
#define MPU_RBAR_A2             0xe000edac
#define MPU_ATTR_A2             0xe000edb0
#define MPU_RBAR_A3             0xe000edb4
#define MPU_ATTR_A3             0xe000edb8

/* MPU Type Register Bit Definitions */

#define MPU_TYPE_SEPARATE       (1 << 0) /* Bit 0: 0:unified or 1:separate memory maps */
#define MPU_TYPE_DREGION_SHIFT  (8)      /* Bits 8-15: Number MPU data regions */
#define MPU_TYPE_DREGION_MASK   (0xff << MPU_TYPE_DREGION_SHIFT)
#define MPU_TYPE_IREGION_SHIFT  (16)     /* Bits 16-23: Number MPU instruction regions */
#define MPU_TYPE_IREGION_MASK   (0xff << MPU_TYPE_IREGION_SHIFT)

/* MPU Control Register Bit Definitions */

#define MPU_CTRL_ENABLE         (1 << 0)  /* Bit 0: Enable the MPU */
#define MPU_CTRL_HFNMIENA       (1 << 1)  /* Bit 1: Enable MPU during hard fault, NMI, and FAULTMAS */
#define MPU_CTRL_PRIVDEFENA     (1 << 2)  /* Bit 2: Enable privileged access to default memory map */

/* MPU Region Number Register Bit Definitions */

#if defined(CONFIG_ARM_MPU_NREGIONS) && defined(CONFIG_ARM_MPU)
#  if CONFIG_ARM_MPU_NREGIONS <= 8
#    define MPU_RNR_MASK        (0x00000007)
#  elif CONFIG_ARM_MPU_NREGIONS <= 16
#    define MPU_RNR_MASK        (0x0000000f)
#  elif CONFIG_ARM_MPU_NREGIONS <= 32
#    define MPU_RNR_MASK        (0x0000001f)
#  else
#    error "FIXME: Unsupported number of MPU regions"
#  endif
#endif

#if defined(CONFIG_ARCH_ARMV8M)

/* Armv8-M MPU requires 32-byte alignment for region base addresses */

#define MPU_ALGNMENT            32

/****************************************************************************
 * ARMv8-M Specific MPU Definitions (PMSAv8)
 ****************************************************************************/

/* MPU Non-Secure Base */

#define MPU_BASE_NS             0xe002ed90

/* Architecture-specific register name aliases */

#define MPU_RLAR                MPU_ATTR
#define MPU_RLAR_A1             MPU_ATTR_A1
#define MPU_RLAR_A2             MPU_ATTR_A2
#define MPU_RLAR_A3             MPU_ATTR_A3

/* MPU Memory Attribute Indirection Registers */

#define MPU_MAIR0               0xe000edc0 /* MPU Memory Attribute Indirection Register 0 */
#define MPU_MAIR1               0xe000edc4 /* MPU Memory Attribute Indirection Register 1 */

/* MPU Region Base Address Register Bit Definitions */

#define MPU_RBAR_XN             (1 << 0)                    /* Bit 0: Execute never */
#define MPU_RBAR_AP_SHIFT       (1)                         /* Bits 1-2: Access permission */
#define MPU_RBAR_AP_MASK        (3 << MPU_RBAR_AP_SHIFT)
#  define MPU_RBAR_AP_RWNO      (0 << MPU_RBAR_AP_SHIFT)    /* P:RW U:None */
#  define MPU_RBAR_AP_RWRW      (1 << MPU_RBAR_AP_SHIFT)    /* P:RW U:RW   */
#  define MPU_RBAR_AP_RONO      (2 << MPU_RBAR_AP_SHIFT)    /* P:RO U:None */
#  define MPU_RBAR_AP_RORO      (3 << MPU_RBAR_AP_SHIFT)    /* P:RO U:RO   */
#define MPU_RBAR_SH_SHIFT       (3)                         /* Bits 3-4: Shareability */
#define MPU_RBAR_SH_MASK        (3 << MPU_RBAR_SH_SHIFT)
#  define MPU_RBAR_SH_NO        (0 << MPU_RBAR_SH_SHIFT)    /* Non-shareable */
#  define MPU_RBAR_SH_OUTER     (2 << MPU_RBAR_SH_SHIFT)    /* Outer shareable */
#  define MPU_RBAR_SH_INNER     (3 << MPU_RBAR_SH_SHIFT)    /* Inner shareable */
#define MPU_RBAR_BASE_MASK      0xffffffe0                  /* Bits 5-31: Region base addrese */

/* MPU Region Limit Address Register Bit Definitions */

#define MPU_RLAR_ENABLE         (1 << 0)                    /* Bit 0: Region enable */
#define MPU_RLAR_INDX_SHIFT     (1)                         /* Bits 1-3: Attribute index */
#define MPU_RLAR_INDX_MASK      (7 << MPU_RLAR_INDX_SHIFT)
#define MPU_RLAR_STRONGLY_ORDER (0 << MPU_RLAR_INDX_SHIFT)
#define MPU_RLAR_DEVICE         (1 << MPU_RLAR_INDX_SHIFT)
#define MPU_RLAR_NONCACHEABLE   (2 << MPU_RLAR_INDX_SHIFT)
#define MPU_RLAR_WRITE_THROUGH  (3 << MPU_RLAR_INDX_SHIFT)
#define MPU_RLAR_WRITE_BACK     (4 << MPU_RLAR_INDX_SHIFT)
#define MPU_RLAR_PXN            (1 << 4)                    /* Bit 4: Privileged execute never */
#define MPU_RLAR_LIMIT_MASK     0xffffffe0                  /* Bits 5-31: Region limit address */

/* MPU Memory Attribute Indirection Register Bit Definitions */

#define MPU_MAIR_INNER_WA       (1 << 0) /* Bit 0: Inner write allocation */
#define MPU_MAIR_INNER_RA       (1 << 1) /* Bit 1: Inner read allocation */
#define MPU_MAIR_INNER_WB       (1 << 2) /* Bit 2: Inner write back */
#define MPU_MAIR_INNER_NT       (1 << 3) /* Bit 3: Inner non-transient */
#define MPU_MAIR_INNER_NC       (4 << 0) /* Bit 0-3: Inner non-cacheable */
#define MPU_MAIR_INNER_NGNRNE   (0 << 2) /* Bit 2-3: Inner nGnRnE */
#define MPU_MAIR_INNER_NGNRE    (1 << 2) /* Bit 2-3: Inner nGnRE */
#define MPU_MAIR_INNER_NGRE     (2 << 2) /* Bit 2-3: Inner nGRE */
#define MPU_MAIR_INNER_GRE      (3 << 2) /* Bit 2-3: Inner GRE */

#define MPU_MAIR_OUTER_WA       (1 << 4) /* Bit 4: Outer write allocation */
#define MPU_MAIR_OUTER_RA       (1 << 5) /* Bit 5: Outer read allocation */
#define MPU_MAIR_OUTER_WB       (1 << 6) /* Bit 6: Outer write back */
#define MPU_MAIR_OUTER_NT       (1 << 7) /* Bit 7: Outer non-transient */
#define MPU_MAIR_OUTER_DEVICE   (0 << 4) /* Bit 4-7: Outer device */
#define MPU_MAIR_OUTER_NC       (4 << 4) /* Bit 4-7: Outer non-cacheable */

#define MPU_MAIR_STRONGLY_ORDER (MPU_MAIR_OUTER_DEVICE | MPU_MAIR_INNER_NGNRNE)
#define MPU_MAIR_DEVICE         (MPU_MAIR_OUTER_DEVICE | MPU_MAIR_INNER_NGNRE)
#define MPU_MAIR_NONCACHEABLE   (MPU_MAIR_OUTER_NC | MPU_MAIR_INNER_NC)
#define MPU_MAIR_WRITE_THROUGH  (MPU_MAIR_OUTER_NT | MPU_MAIR_OUTER_RA | \
                                 MPU_MAIR_OUTER_WA | MPU_MAIR_INNER_NT | \
                                 MPU_MAIR_INNER_RA | MPU_MAIR_INNER_WA)
#define MPU_MAIR_WRITE_BACK     (MPU_MAIR_OUTER_NT | MPU_MAIR_OUTER_WB | \
                                 MPU_MAIR_OUTER_RA | MPU_MAIR_OUTER_WA | \
                                 MPU_MAIR_INNER_NT | MPU_MAIR_INNER_WB | \
                                 MPU_MAIR_INNER_RA | MPU_MAIR_INNER_WA)

/* Architecture-independent enable bit alias */

#define MPU_ATTR_ENABLE         MPU_RLAR_ENABLE

#else /* ARMv7-M / ARMv6-M */

/****************************************************************************
 * ARMv7-M / ARMv6-M Specific MPU Definitions (PMSAv7)
 ****************************************************************************/

/* Architecture-specific register name aliases */

#define MPU_RASR                MPU_ATTR
#define MPU_RASR_A1             MPU_ATTR_A1
#define MPU_RASR_A2             MPU_ATTR_A2
#define MPU_RASR_A3             MPU_ATTR_A3

/* MPU Region Base Address Register Bit Definitions */

#define MPU_RBAR_REGION_SHIFT   (0)                         /* Bits 0-3: MPU region */
#define MPU_RBAR_REGION_MASK    (15 << MPU_RBAR_REGION_SHIFT)
#define MPU_RBAR_VALID          (1 << 4)                    /* Bit 4: MPU Region Number valid */
#define MPU_RBAR_ADDR_MASK      0xffffffe0                  /* Bits N-31:  Region base addrese */

/* MPU Region Attributes and Size Register Bit Definitions */

#define MPU_RASR_ENABLE         (1 << 0)                   /* Bit 0: Region enable */
#define MPU_RASR_SIZE_SHIFT     (1)                        /* Bits 1-5: Size of the MPU protection region */
#define MPU_RASR_SIZE_MASK      (31 << MPU_RASR_SIZE_SHIFT)
#  define MPU_RASR_SIZE_LOG2(n) ((n - 1) << MPU_RASR_SIZE_SHIFT)
#define MPU_RASR_SRD_SHIFT      (8)                        /* Bits 8-15: Subregion disable */
#define MPU_RASR_SRD_MASK       (0xff << MPU_RASR_SRD_SHIFT)
#  define MPU_RASR_SRD_0        (0x01 << MPU_RASR_SRD_SHIFT)
#  define MPU_RASR_SRD_1        (0x02 << MPU_RASR_SRD_SHIFT)
#  define MPU_RASR_SRD_2        (0x04 << MPU_RASR_SRD_SHIFT)
#  define MPU_RASR_SRD_3        (0x08 << MPU_RASR_SRD_SHIFT)
#  define MPU_RASR_SRD_4        (0x10 << MPU_RASR_SRD_SHIFT)
#  define MPU_RASR_SRD_5        (0x20 << MPU_RASR_SRD_SHIFT)
#  define MPU_RASR_SRD_6        (0x40 << MPU_RASR_SRD_SHIFT)
#  define MPU_RASR_SRD_7        (0x80 << MPU_RASR_SRD_SHIFT)
#define MPU_RASR_ATTR_SHIFT     (16)                       /* Bits 16-31: MPU Region Attribute field */
#define MPU_RASR_ATTR_MASK      (0xffff << MPU_RASR_ATTR_SHIFT)
#  define MPU_RASR_B            (1 << 16)                  /* Bit 16: Bufferable */
#  define MPU_RASR_C            (1 << 17)                  /* Bit 17: Cacheable */
#  define MPU_RASR_S            (1 << 18)                  /* Bit 18: Shareable */
#  define MPU_RASR_TEX_SHIFT    (19)                       /* Bits 19-21: TEX Address Permission */
#  define MPU_RASR_TEX_MASK     (7 << MPU_RASR_TEX_SHIFT)
#    define MPU_RASR_TEX_SO     (0 << MPU_RASR_TEX_SHIFT) /* Strongly Ordered */
#    define MPU_RASR_TEX_NOR    (1 << MPU_RASR_TEX_SHIFT) /* Normal           */
#    define MPU_RASR_TEX_DEV    (2 << MPU_RASR_TEX_SHIFT) /* Device           */
#    define MPU_RASR_TEX_BB(bb) ((4 | (bb)) << MPU_RASR_TEX_SHIFT)
#      define MPU_RASR_CP_NC    (0)                       /* Non-cacheable */
#      define MPU_RASR_CP_WBRA  (1)                       /* Write back, write and Read- Allocate */
#      define MPU_RASR_CP_WT    (2)                       /* Write through, no Write-Allocate */
#      define MPU_RASR_CP_WB    (4)                       /* Write back, no Write-Allocate */
#  define MPU_RASR_AP_SHIFT     (24)                      /* Bits 24-26: Access permission */
#  define MPU_RASR_AP_MASK      (7 << MPU_RASR_AP_SHIFT)
#    define MPU_RASR_AP_NONO    (0 << MPU_RASR_AP_SHIFT)  /* P:None U:None */
#    define MPU_RASR_AP_RWNO    (1 << MPU_RASR_AP_SHIFT)  /* P:RW   U:None */
#    define MPU_RASR_AP_RWRO    (2 << MPU_RASR_AP_SHIFT)  /* P:RW   U:RO   */
#    define MPU_RASR_AP_RWRW    (3 << MPU_RASR_AP_SHIFT)  /* P:RW   U:RW   */
#    define MPU_RASR_AP_RONO    (5 << MPU_RASR_AP_SHIFT)  /* P:RO   U:None */
#    define MPU_RASR_AP_RORO    (6 << MPU_RASR_AP_SHIFT)  /* P:RO   U:RO   */
#  define MPU_RASR_XN           (1 << 28)                 /* Bit 28: Instruction access disable */

/* Architecture-independent enable bit alias */

#define MPU_ATTR_ENABLE         MPU_RASR_ENABLE

#endif /* CONFIG_ARCH_ARMV8M */

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct mpu_region_s
{
  /* Region Base Address */

  uintptr_t base;

  /* Region Size */

  size_t size;

  /* Region Attributes */

  uint32_t flags;
#ifdef CONFIG_ARCH_ARMV8M
  uint32_t flags2;
#endif
};

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
void mpu_reset(void);
#else
#  define mpu_reset() do { } while (0)
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
void mpu_early_reset(void);
#else
#  define mpu_early_reset() do { } while (0)
#endif

#ifdef CONFIG_ARM_MPU

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifndef __ASSEMBLY__
#undef EXTERN
#if defined(__cplusplus)
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

/****************************************************************************
 * Name: mpu_allocregions
 *
 * Description:
 *   Allocate the regions
 *
 * Input Parameters:
 *   nregions - The number of regions to be allocated.
 *
 * Returned Value:
 *   The index of the allocated region.
 *
 ****************************************************************************/

unsigned int mpu_allocregions(unsigned int nregions);

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

#define mpu_allocregion() mpu_allocregions(1)

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

void mpu_freeregion(unsigned int region);

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

uint8_t mpu_log2regionceil(size_t size);

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

uint8_t mpu_log2regionfloor(size_t size);

#ifndef CONFIG_ARCH_ARMV8M
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

uint32_t mpu_subregion(uintptr_t base, size_t size, uint8_t l2size);
#endif

/****************************************************************************
 * Name: mpu_control
 *
 * Description:
 *   Configure and enable (or disable) the MPU
 *
 * Input Parameters:
 *   enable     - Flag indicating whether to enable the MPU.
 *   hfnmiena   - Flag indicating whether to enable the MPU during hard
 *                fult, NMI, and FAULTMAS.
 *   privdefena - Flag indicating whether to enable privileged access to
 *                the default memory map.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

void mpu_control(bool enable, bool hfnmiena, bool privdefena);

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

void mpu_dump_region(void);

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
 *   flags  - Flags to configure the region (RASR for v7m, RBAR for v8m).
 *   flags2 - Additional flags for the region (RLAR, ARMv8-M only).
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

void mpu_modify_region(unsigned int region, uintptr_t base, size_t size,
                       uint32_t flags
#ifdef CONFIG_ARCH_ARMV8M
                       , uint32_t flags2
#endif
                       );

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

#ifdef CONFIG_ARCH_ARMV8M
#  define mpu_modify_regions(region, base, size, ...) \
          mpu_modify_region(region, base, size, __VA_ARGS__)
#else
void mpu_modify_regions(unsigned int region, uintptr_t base, size_t size,
                        uint32_t flags);
#endif

/****************************************************************************
 * Name: mpu_enable_region
 *
 * Description:
 *   Control a region enable or disable
 *
 * Input Parameters:
 *   region - The index of the MPU region to modify.
 *   enable - A boolean indicating whether to enable or disable the region.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

void mpu_enable_region(unsigned int region, bool enable);

/****************************************************************************
 * Name: mpu_configure_region
 *
 * Description:
 *   Configure a region for privileged, strongly ordered memory
 *
 * Input Parameters:
 *   base   - The base address of the region.
 *   size   - The size of the region.
 *   flags  - Flags to configure the region (RASR for v7m, RBAR for v8m).
 *   flags2 - Additional flags for the region (RLAR, ARMv8-M only).
 *
 * Returned Value:
 *   The region number allocated for the configured region.
 *
 ****************************************************************************/

unsigned int mpu_configure_region(uintptr_t base, size_t size,
                                  uint32_t flags
#ifdef CONFIG_ARCH_ARMV8M
                                  , uint32_t flags2
#endif
                                  );

/****************************************************************************
 * Name: mpu_initialize
 *
 * Description:
 *   Configure a region for privileged, strongly ordered memory
 *
 * Input Parameters:
 *   table      - MPU initialization table.
 *   count      - Initialize the number of entries in the region table.
 *   hfnmiena   - A boolean indicating whether the MPU should enable the
 *                HFNMIENA bit.
 *   privdefena - A boolean indicating whether the MPU should enable the
 *                PRIVDEFENA bit.
 *
 * Returned Value:
 *   NULL.
 *
 ****************************************************************************/

void mpu_initialize(const struct mpu_region_s *table, size_t count,
                    bool hfnmiena, bool privdefena);

/****************************************************************************
 * Inline Functions
 ****************************************************************************/

/****************************************************************************
 * Name: mpu_showtype
 *
 * Description:
 *   Show the characteristics of the MPU
 *
 ****************************************************************************/

#ifdef CONFIG_DEBUG_SCHED_INFO
#  define mpu_showtype() \
    do \
      { \
        uint32_t regval = getreg32(MPU_TYPE); \
        sinfo("%s MPU Regions: data=%d instr=%d\n", \
              (regval & MPU_TYPE_SEPARATE) != 0 ? "Separate" : "Unified", \
              (regval & MPU_TYPE_DREGION_MASK) >> MPU_TYPE_DREGION_SHIFT, \
              (regval & MPU_TYPE_IREGION_MASK) >> MPU_TYPE_IREGION_SHIFT); \
      } \
    while (0)
#else
#  define mpu_showtype() do { } while (0)
#endif

/****************************************************************************
 * Architecture-specific Region Attribute Flags
 *
 * Each MPU_xxx attribute macro expands to architecture-specific values:
 *   - For PMSAv8 (ARMv8-M): two comma-separated values (RBAR, RLAR attrs)
 *   - For PMSAv7 (ARMv7-M/ARMv6-M): single flags value (RASR attributes)
 *
 * These macros can be used directly as function arguments.
 *
 ****************************************************************************/

#ifdef CONFIG_ARCH_ARMV8M
#  define MPU_PRIV_SO            MPU_RBAR_AP_RWNO, \
                                  (MPU_RLAR_STRONGLY_ORDER | MPU_RLAR_PXN)
#  define MPU_USER_FLASH         MPU_RBAR_AP_RORO, \
                                  MPU_RLAR_WRITE_BACK
#  define MPU_PRIV_FLASH         MPU_RBAR_AP_RONO, \
                                  MPU_RLAR_WRITE_BACK
#  define MPU_USER_INTSRAM       (MPU_RBAR_XN | MPU_RBAR_AP_RWRW), \
                                  (MPU_RLAR_NONCACHEABLE | MPU_RLAR_PXN)
#  define MPU_PRIV_INTSRAM       MPU_RBAR_AP_RWNO, \
                                  (MPU_RLAR_NONCACHEABLE | MPU_RLAR_PXN)
#  define MPU_PRIV_SHMEM         MPU_RBAR_AP_RWNO, \
                                  (MPU_RLAR_NONCACHEABLE | \
                                   MPU_RBAR_SH_INNER | MPU_RLAR_PXN)
#  define MPU_USER_EXTSRAM       (MPU_RBAR_XN | MPU_RBAR_AP_RWRW | \
                                   MPU_RBAR_SH_OUTER), \
                                  (MPU_RLAR_WRITE_BACK | MPU_RLAR_PXN)
#  define MPU_PRIV_EXTSRAM       (MPU_RBAR_AP_RWNO | MPU_RBAR_SH_OUTER), \
                                  (MPU_RLAR_WRITE_BACK | MPU_RLAR_PXN)
#  define MPU_PERIPHERAL         MPU_RBAR_AP_RWNO, \
                                  (MPU_RLAR_DEVICE | MPU_RLAR_PXN)
#  define MPU_USER_PERIPH        (MPU_RBAR_XN | MPU_RBAR_AP_RWRW), \
                                  (MPU_RLAR_DEVICE | MPU_RLAR_PXN)
#  define MPU_ADDRENV_NREGIONS   1
#else /* PMSAv7: ARMv7-M / ARMv6-M */
#  define MPU_PRIV_SO            (MPU_RASR_TEX_SO | MPU_RASR_S | \
                                  MPU_RASR_AP_RWNO)
#  define MPU_USER_FLASH         (MPU_RASR_TEX_SO | MPU_RASR_C | \
                                  MPU_RASR_AP_RORO)
#  define MPU_PRIV_FLASH         (MPU_RASR_TEX_SO | MPU_RASR_C | \
                                  MPU_RASR_AP_RONO)
#  define MPU_USER_INTSRAM       (MPU_RASR_TEX_SO | MPU_RASR_C | \
                                  MPU_RASR_S | MPU_RASR_AP_RWRW)
#  define MPU_PRIV_INTSRAM       (MPU_RASR_TEX_SO | MPU_RASR_C | \
                                  MPU_RASR_S | MPU_RASR_AP_RWNO)
#  define MPU_PRIV_SHMEM         (MPU_RASR_TEX_SO | MPU_RASR_B | \
                                  MPU_RASR_S | MPU_RASR_AP_RWNO | \
                                  MPU_RASR_XN)
#  define MPU_USER_EXTSRAM       (MPU_RASR_TEX_SO | MPU_RASR_C | \
                                  MPU_RASR_B | MPU_RASR_S | \
                                  MPU_RASR_AP_RWRW)
#  define MPU_PRIV_EXTSRAM       (MPU_RASR_TEX_SO | MPU_RASR_C | \
                                  MPU_RASR_B | MPU_RASR_S | \
                                  MPU_RASR_AP_RWNO)
#  define MPU_PERIPHERAL         (MPU_RASR_TEX_DEV | MPU_RASR_B | \
                                  MPU_RASR_S | MPU_RASR_AP_RWNO | \
                                  MPU_RASR_XN)
#  define MPU_USER_PERIPH        (MPU_RASR_TEX_DEV | MPU_RASR_B | \
                                  MPU_RASR_S | MPU_RASR_AP_RWRW | \
                                  MPU_RASR_XN)
#  define MPU_ADDRENV_NREGIONS   2
#endif

/****************************************************************************
 * Convenience Macros
 *
 ****************************************************************************/

#define mpu_priv_stronglyordered(base, size) \
  mpu_configure_region(base, size, MPU_PRIV_SO)

#define mpu_user_flash(base, size) \
  mpu_configure_region(base, size, MPU_USER_FLASH)

#define mpu_priv_flash(base, size) \
  mpu_configure_region(base, size, MPU_PRIV_FLASH)

#define mpu_user_intsram(base, size) \
  mpu_configure_region(base, size, MPU_USER_INTSRAM)

#define mpu_priv_intsram(base, size) \
  mpu_configure_region(base, size, MPU_PRIV_INTSRAM)

#define mpu_priv_shmem(base, size) \
  mpu_configure_region(base, size, MPU_PRIV_SHMEM)

#define mpu_user_extsram(base, size) \
  mpu_configure_region(base, size, MPU_USER_EXTSRAM)

#define mpu_priv_extsram(base, size) \
  mpu_configure_region(base, size, MPU_PRIV_EXTSRAM)

#define mpu_peripheral(base, size) \
  mpu_configure_region(base, size, MPU_PERIPHERAL)

#define mpu_user_peripheral(base, size) \
  mpu_configure_region(base, size, MPU_USER_PERIPH)

#undef EXTERN
#if defined(__cplusplus)
}
#endif

#endif /* __ASSEMBLY__ */
#endif /* CONFIG_ARM_MPU */
#endif /* __ARCH_ARM_SRC_ARM_M_MPU_H */
