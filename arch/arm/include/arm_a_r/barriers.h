/****************************************************************************
 * arch/arm/include/arm_a_r/barriers.h
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

#ifndef __ARCH_ARM_INCLUDE_ARM_A_R_BARRIERS_H
#define __ARCH_ARM_INCLUDE_ARM_A_R_BARRIERS_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifdef CONFIG_ARCH_ARM_A_R

  /* ARMv7-A/R and ARMv8-R memory barriers */

  #define arm_dsb(n) __asm__ __volatile__ ("dsb " #n : : : "memory")
  #define arm_dmb(n) __asm__ __volatile__ ("dmb " #n : : : "memory")
  #define arm_isb()  __asm__ __volatile__ ("isb " : : : "memory")
  #define arm_nop()  __asm__ __volatile__ ("nop\n")
  #define arm_sev()  __asm__ __volatile__ ("sev\n")

  #define UP_DSB()  arm_dsb(15)
  #define UP_DMB()  arm_dmb(15)
  #define UP_ISB()  arm_isb()
  #define UP_NOP()  arm_nop()
  #define UP_SEV()  arm_sev()

#  ifdef CONFIG_ARCH_ARMV7A
#    define UP_RMB()  arm_dmb(3)
#    define UP_WMB()  arm_dmb(2)
#  else
#    define arm_rmb()  __asm__ __volatile__ ("dmb ishld" : : : "memory")
#    define arm_wmb()  __asm__ __volatile__ ("dmb ishst" : : : "memory")
#    define UP_RMB()  arm_rmb()
#    define UP_WMB()  arm_wmb()
#  endif

#else

  /* ARM memory barriers, ARM926EJ-S, ARMv5TEJ have no
   * barrier support, use compiler barrier only.
   */

  #define arm_mb()  __asm__ __volatile__ ("" : : : "memory")
  #define arm_nop() __asm__ __volatile__ ("nop\n")

  #define UP_DSB() arm_mb()
  #define UP_ISB() arm_mb()
  #define UP_DMB() arm_mb()
  #define UP_RMB() arm_mb()
  #define UP_WMB() arm_mb()
  #define UP_NOP() arm_nop()

#endif

#endif /* __ARCH_ARM_INCLUDE_ARM_A_R_BARRIERS_H */
