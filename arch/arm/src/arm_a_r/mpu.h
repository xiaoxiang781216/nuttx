/****************************************************************************
 * arch/arm/src/arm_a_r/mpu.h
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

#ifndef __ARCH_ARM_SRC_ARM_A_R_MPU_H
#define __ARCH_ARM_SRC_ARM_A_R_MPU_H

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

#  include "sctlr.h"
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* MPU Type Register Bit Definitions */

#define MPU_TYPE_SEPARATE         (1 << 0) /* Bit 0: 0:unified or 1:separate memory maps */
#define MPU_TYPE_DREGION_SHIFT    (8)      /* Bits 8-15: Number MPU data regions */
#define MPU_TYPE_DREGION_MASK     (0xff << MPU_TYPE_DREGION_SHIFT)
#define MPU_TYPE_IREGION_SHIFT    (16)     /* Bits 16-23: Number MPU instruction regions */
#define MPU_TYPE_IREGION_MASK     (0xff << MPU_TYPE_IREGION_SHIFT)

/* MPU Region Number Register */

#if defined(CONFIG_ARM_MPU_NREGIONS)
#  if CONFIG_ARM_MPU_NREGIONS <= 8
#    define MPU_RNR_MASK          (0x00000007)
#  elif CONFIG_ARM_MPU_NREGIONS <= 16
#    define MPU_RNR_MASK          (0x0000000f)
#  elif CONFIG_ARM_MPU_NREGIONS <= 32
#    define MPU_RNR_MASK          (0x0000001f)
#  else
#    error "FIXME: Unsupported number of MPU regions"
#  endif
#endif

/* Common MPU Region Enable (shared by PMSAv7 DRSR and PMSAv8 PRLAR) */

#define MPU_REGION_ENABLE         (1 << 0)                    /* Bit 0: Region enable */

/* Common MPU Address Mask (shared by PMSAv7 RBAR and PMSAv8 PRBAR/PRLAR) */

#define MPU_ADDR_MASK             0xffffffc0                  /* Bits 6-31: Address mask */

#ifdef CONFIG_ARCH_ARMV8R

/****************************************************************************
 * ARMv8-R PMSAv8 Definitions
 ****************************************************************************/

/* PRBAR Bit Definitions */

#define MPU_RBAR_XN               (1 << 0)                    /* Bit 0: Execute never */
#define MPU_RBAR_AP_SHIFT         (1)                         /* Bits 1-2: Access permission */
#define MPU_RBAR_AP_MASK          (3 << MPU_RBAR_AP_SHIFT)
#  define MPU_RBAR_AP_RWNO        (0 << MPU_RBAR_AP_SHIFT)    /* P:RW U:None */
#  define MPU_RBAR_AP_RWRW        (1 << MPU_RBAR_AP_SHIFT)    /* P:RW U:RW   */
#  define MPU_RBAR_AP_RONO        (2 << MPU_RBAR_AP_SHIFT)    /* P:RO U:None */
#  define MPU_RBAR_AP_RORO        (3 << MPU_RBAR_AP_SHIFT)    /* P:RO U:RO   */
#define MPU_RBAR_SH_SHIFT         (3)                         /* Bits 3-4: Shareability */
#define MPU_RBAR_SH_MASK          (3 << MPU_RBAR_SH_SHIFT)
#  define MPU_RBAR_SH_NO          (0 << MPU_RBAR_SH_SHIFT)    /* Non-shareable */
#  define MPU_RBAR_SH_OUTER       (2 << MPU_RBAR_SH_SHIFT)    /* Outer shareable */
#  define MPU_RBAR_SH_INNER       (3 << MPU_RBAR_SH_SHIFT)    /* Inner shareable */
#define MPU_RBAR_BASE_MASK        MPU_ADDR_MASK               /* Bits 6-31: Region base */

/* PRLAR Bit Definitions */

#define MPU_RLAR_ENABLE           MPU_REGION_ENABLE           /* Bit 0: Region enable */
#define MPU_RLAR_INDX_SHIFT       (1)                         /* Bits 1-3: Attribute index */
#define MPU_RLAR_INDX_MASK        (7 << MPU_RLAR_INDX_SHIFT)
#  define MPU_RLAR_STRONGLY_ORDER (0 << MPU_RLAR_INDX_SHIFT)
#  define MPU_RLAR_DEVICE         (1 << MPU_RLAR_INDX_SHIFT)
#  define MPU_RLAR_NONCACHEABLE   (2 << MPU_RLAR_INDX_SHIFT)
#  define MPU_RLAR_WRITE_THROUGH  (3 << MPU_RLAR_INDX_SHIFT)
#  define MPU_RLAR_WRITE_BACK     (4 << MPU_RLAR_INDX_SHIFT)
#define MPU_RLAR_LIMIT_MASK       MPU_ADDR_MASK               /* Bits 6-31: Region limit */

/* MAIR Register Bit Definitions */

#define MPU_MAIR_INNER_WA         (1 << 0)
#define MPU_MAIR_INNER_RA         (1 << 1)
#define MPU_MAIR_INNER_WB         (1 << 2)
#define MPU_MAIR_INNER_NT         (1 << 3)
#define MPU_MAIR_INNER_NC         (4 << 0)
#define MPU_MAIR_INNER_NGNRNE     (0 << 2)
#define MPU_MAIR_INNER_NGNRE      (1 << 2)
#define MPU_MAIR_INNER_NGRE       (2 << 2)
#define MPU_MAIR_INNER_GRE        (3 << 2)

#define MPU_MAIR_OUTER_WA         (1 << 4)
#define MPU_MAIR_OUTER_RA         (1 << 5)
#define MPU_MAIR_OUTER_WB         (1 << 6)
#define MPU_MAIR_OUTER_NT         (1 << 7)
#define MPU_MAIR_OUTER_DEVICE     (0 << 4)
#define MPU_MAIR_OUTER_NC         (4 << 4)

#define MPU_MAIR_STRONGLY_ORDER   (MPU_MAIR_OUTER_DEVICE | MPU_MAIR_INNER_NGNRNE)
#define MPU_MAIR_DEVICE           (MPU_MAIR_OUTER_DEVICE | MPU_MAIR_INNER_NGNRE)
#define MPU_MAIR_NONCACHEABLE     (MPU_MAIR_OUTER_NC | MPU_MAIR_INNER_NC)
#define MPU_MAIR_WRITE_THROUGH    (MPU_MAIR_OUTER_NT | MPU_MAIR_OUTER_RA | \
                                   MPU_MAIR_OUTER_WA | MPU_MAIR_INNER_NT | \
                                   MPU_MAIR_INNER_RA | MPU_MAIR_INNER_WA)
#define MPU_MAIR_WRITE_BACK       (MPU_MAIR_OUTER_NT | MPU_MAIR_OUTER_WB | \
                                   MPU_MAIR_OUTER_RA | MPU_MAIR_OUTER_WA | \
                                   MPU_MAIR_INNER_NT | MPU_MAIR_INNER_WB | \
                                   MPU_MAIR_INNER_RA | MPU_MAIR_INNER_WA)

#define MPU_ALIGN                 0x40

#else /* !CONFIG_ARCH_ARMV8R -- ARMv7-R PMSAv7 */

/****************************************************************************
 * ARMv7-R PMSAv7 Definitions
 ****************************************************************************/

/* Region Base Address Register Definitions */

#define MPU_RBAR_ADDR_MASK        0xffffffe0

/* Region Size and Enable Register */

#define MPU_RASR_ENABLE           MPU_REGION_ENABLE
#define MPU_RASR_RSIZE_SHIFT      (1)
#define MPU_RASR_RSIZE_MASK       (31 << MPU_RASR_RSIZE_SHIFT)
#  define MPU_RASR_RSIZE_LOG2(n)  ((n-1) << MPU_RASR_RSIZE_SHIFT)

#define MPU_RASR_SRD_SHIFT        (8)
#define MPU_RASR_SRD_MASK         (0xff << MPU_RASR_SRD_SHIFT)

/* Region Access Control Register */

#define MPU_RACR_B                (1 << 0)  /* Bit 0: Bufferable */
#define MPU_RACR_C                (1 << 1)  /* Bit 1: Cacheable */
#define MPU_RACR_S                (1 << 2)  /* Bit 2: Shareable */
#define MPU_RACR_TEX_SHIFT        (3)       /* Bits 3-5: Type extension */
#define MPU_RACR_TEX_MASK         (7 << MPU_RACR_TEX_SHIFT)
#  define MPU_RACR_TEX(n)         ((uint32_t)(n) << MPU_RACR_TEX_SHIFT)
#define MPU_RACR_AP_SHIFT         (8)       /* Bits 8-10: Access permission */
#define MPU_RACR_AP_MASK          (7 << MPU_RACR_AP_SHIFT)
#  define MPU_RACR_AP_NONO        (0 << MPU_RACR_AP_SHIFT) /* PL0:None PL1:None */
#  define MPU_RACR_AP_RWNO        (1 << MPU_RACR_AP_SHIFT) /* PL0:RW   PL1:None */
#  define MPU_RACR_AP_RWRO        (2 << MPU_RACR_AP_SHIFT) /* PL0:RW   PL1:RO   */
#  define MPU_RACR_AP_RWRW        (3 << MPU_RACR_AP_SHIFT) /* PL0:RW   PL1:RW   */
#  define MPU_RACR_AP_RONO        (5 << MPU_RACR_AP_SHIFT) /* PL0:RO   PL1:None */
#  define MPU_RACR_AP_RORO        (6 << MPU_RACR_AP_SHIFT) /* PL0:RO   PL1:RO   */

#define MPU_RACR_XN               (1 << 12) /* Bit 12: Instruction access disable */

#endif /* CONFIG_ARCH_ARMV8R */

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct mpu_region_s
{
  uintptr_t base;
  size_t size;
  uint32_t flags;
#ifdef CONFIG_ARCH_ARMV8R
  uint32_t flags2;
#endif
};

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
 * Name: mpu_reset / mpu_early_reset
 ****************************************************************************/

#if defined(CONFIG_ARM_MPU_RESET)
void mpu_reset(void);
#else
#  define mpu_reset() do { } while (0)
#endif

#if defined(CONFIG_ARM_MPU_EARLY_RESET)
void mpu_early_reset(void);
#else
#  define mpu_early_reset() do { } while (0)
#endif

#ifdef CONFIG_ARM_MPU

/****************************************************************************
 * Name: mpu_allocregion
 ****************************************************************************/

#define mpu_allocregion() mpu_allocregions(1)

/****************************************************************************
 * Name: mpu_allocregions
 ****************************************************************************/

unsigned int mpu_allocregions(unsigned int nregions);

/****************************************************************************
 * Name: mpu_freeregion
 ****************************************************************************/

void mpu_freeregion(unsigned int region);

/****************************************************************************
 * Name: mpu_control
 ****************************************************************************/

void mpu_control(bool enable);

/****************************************************************************
 * Name: mpu_dump_region
 ****************************************************************************/

void mpu_dump_region(void);

/****************************************************************************
 * Name: mpu_modify_region
 ****************************************************************************/

void mpu_modify_region(unsigned int region, uintptr_t base, size_t size,
                       uint32_t flags
#ifdef CONFIG_ARCH_ARMV8R
                       , uint32_t flags2
#endif
                       );

#ifndef CONFIG_ARCH_ARMV8R
/****************************************************************************
 * Name: mpu_modify_regions (PMSAv7 only)
 ****************************************************************************/

void mpu_modify_regions(unsigned int region, uintptr_t base, size_t size,
                        uint32_t flags);
#endif

/****************************************************************************
 * Name: mpu_configure_region
 ****************************************************************************/

unsigned int mpu_configure_region(uintptr_t base, size_t size,
                                  uint32_t flags
#ifdef CONFIG_ARCH_ARMV8R
                                  , uint32_t flags2
#endif
                                  );

/****************************************************************************
 * Name: mpu_initialize
 ****************************************************************************/

void mpu_initialize(const struct mpu_region_s *table, size_t count);

#ifndef CONFIG_ARCH_ARMV8R
/****************************************************************************
 * PMSAv7-only function prototypes
 ****************************************************************************/

uint8_t mpu_log2regionceil(size_t size);
uint8_t mpu_log2regionfloor(size_t size);
uint32_t mpu_subregion(uintptr_t base, size_t size, uint8_t l2size);
#endif

/****************************************************************************
 * Inline Functions
 ****************************************************************************/

/****************************************************************************
 * Name: mpu_showtype
 ****************************************************************************/

#ifdef CONFIG_DEBUG_SCHED_INFO
#  define mpu_showtype() \
    do \
      { \
        uint32_t regval = CP15_GET(MPUIR); \
        sinfo("%s MPU Regions: data=%d instr=%d\n", \
          (regval & MPU_TYPE_SEPARATE) != 0 ? "Separate" : "Unified", \
          (regval & MPU_TYPE_DREGION_MASK) >> MPU_TYPE_DREGION_SHIFT, \
          (regval & MPU_TYPE_IREGION_MASK) >> MPU_TYPE_IREGION_SHIFT); \
      }
    while (0)
#else
#  define mpu_showtype() do { } while (0)
#endif

/****************************************************************************
 * Architecture-specific MPU attribute macros
 *
 * ARMv8-R (PMSAv8): Each macro expands to two arguments (flags, flags2)
 *   matching the two-flag mpu_configure_region() signature.
 * ARMv7-R (PMSAv7): Each macro expands to a single argument (flags)
 *   matching the single-flag mpu_configure_region() signature.
 ****************************************************************************/

#ifdef CONFIG_ARCH_ARMV8R

#  define MPU_PRIV_SO            MPU_RBAR_AP_RWNO, \
                                 MPU_RLAR_STRONGLY_ORDER
#  define MPU_USER_FLASH         MPU_RBAR_AP_RORO, \
                                 MPU_RLAR_WRITE_BACK
#  define MPU_PRIV_FLASH         MPU_RBAR_AP_RONO, \
                                 MPU_RLAR_WRITE_BACK
#  define MPU_USER_INTSRAM       (MPU_RBAR_XN | MPU_RBAR_AP_RWRW), \
                                 MPU_RLAR_NONCACHEABLE
#  define MPU_PRIV_INTSRAM       MPU_RBAR_AP_RWNO, \
                                 MPU_RLAR_NONCACHEABLE
#  define MPU_PRIV_SHMEM         MPU_RBAR_AP_RWNO, \
                                 (MPU_RLAR_NONCACHEABLE | \
                                 MPU_RBAR_SH_INNER)
#  define MPU_USER_EXTSRAM       (MPU_RBAR_XN | MPU_RBAR_AP_RWRW | \
                                 MPU_RBAR_SH_OUTER), \
                                 MPU_RLAR_WRITE_BACK
#  define MPU_PRIV_EXTSRAM       (MPU_RBAR_AP_RWNO | \
                                 MPU_RBAR_SH_OUTER), \
                                 MPU_RLAR_WRITE_BACK
#  define MPU_PRIV_NONCACHE      (MPU_RBAR_XN | MPU_RBAR_AP_RWNO), \
                                 MPU_RLAR_NONCACHEABLE
#  define MPU_PERIPHERAL         MPU_RBAR_AP_RWNO, \
                                 MPU_RLAR_DEVICE
#  define MPU_USER_PERIPH        (MPU_RBAR_XN | MPU_RBAR_AP_RWRW), \
                                 MPU_RLAR_DEVICE
#  define MPU_USER_INTSRAM_WB    MPU_RBAR_AP_RWRW, \
                                 MPU_RLAR_WRITE_BACK

#else /* !CONFIG_ARCH_ARMV8R -- ARMv7-R PMSAv7 */

#  define MPU_PRIV_SO            (MPU_RACR_S | MPU_RACR_AP_RWNO)
#  define MPU_USER_FLASH         (MPU_RACR_C | MPU_RACR_AP_RORO)
#  define MPU_PRIV_FLASH         (MPU_RACR_C | MPU_RACR_AP_RONO)
#  define MPU_USER_INTSRAM       (MPU_RACR_S | MPU_RACR_C | \
                                 MPU_RACR_AP_RWRW)
#  define MPU_PRIV_INTSRAM       (MPU_RACR_S | MPU_RACR_C | \
                                 MPU_RACR_AP_RWNO)
#  define MPU_PRIV_SHMEM         (MPU_RACR_S | MPU_RACR_B | \
                                 MPU_RACR_AP_RWNO | MPU_RACR_XN)
#  define MPU_PRIV_NONCACHE      (MPU_RACR_S | MPU_RACR_TEX(4) | \
                                 MPU_RACR_AP_RWRW | MPU_RACR_XN)
#  define MPU_USER_EXTSRAM       (MPU_RACR_S | MPU_RACR_C | \
                                 MPU_RACR_B | MPU_RACR_AP_RWRW)
#  define MPU_PRIV_EXTSRAM       (MPU_RACR_S | MPU_RACR_C | \
                                 MPU_RACR_B | MPU_RACR_AP_RWNO)
#  define MPU_PERIPHERAL         (MPU_RACR_S | MPU_RACR_B | \
                                 MPU_RACR_AP_RWNO | MPU_RACR_XN)
#  define MPU_USER_PERIPH        (MPU_RACR_S | MPU_RACR_B | \
                                 MPU_RACR_AP_RWRW | MPU_RACR_XN)
#  define MPU_USER_INTSRAM_WB    (MPU_RACR_B | MPU_RACR_TEX(5) | \
                                 MPU_RACR_AP_RWRW)

#endif /* CONFIG_ARCH_ARMV8R */

/****************************************************************************
 * Convenience Macros
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

#define mpu_user_extsram(base, size) \
  mpu_configure_region(base, size, MPU_USER_EXTSRAM)

#define mpu_priv_extsram(base, size) \
  mpu_configure_region(base, size, MPU_PRIV_EXTSRAM)

#define mpu_peripheral(base, size) \
  mpu_configure_region(base, size, MPU_PERIPHERAL)

#define mpu_user_peripheral(base, size) \
  mpu_configure_region(base, size, MPU_USER_PERIPH)

#define mpu_priv_shmem(base, size) \
  mpu_configure_region(base, size, MPU_PRIV_SHMEM)

#define mpu_priv_noncache(base, size) \
  mpu_configure_region(base, size, MPU_PRIV_NONCACHE)

#define mpu_user_intsram_wb(base, size) \
  mpu_configure_region(base, size, MPU_USER_INTSRAM_WB)

#undef EXTERN
#if defined(__cplusplus)
}
#endif

#endif /* __ASSEMBLY__ */
#endif /* CONFIG_ARM_MPU */
#endif /* __ARCH_ARM_SRC_ARM_A_R_MPU_H */
