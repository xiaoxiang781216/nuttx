/****************************************************************************
 * libs/libm/libm/arm/arm_libm.h
 *
 * SPDX-License-Identifier: Apache-2.0
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

#ifndef __LIBS_LIBM_LIBM_ARM_ARM_LIBM_H
#define __LIBS_LIBM_LIBM_ARM_ARM_LIBM_H

/* Single-precision VFP: vabs.f32, vsqrt.f32, etc.
 * Available on ARMv7+ with at least single-precision FP unit.
 * __ARM_FP bit 2 (0x4) indicates single-precision support.
 * Works with both softfp and hard-float ABI since "=t" constraint
 * is always valid when __ARM_FP is defined.
 */

#if defined(__ARM_FP) && (__ARM_FP >= 0x4)
#  define ARM_LIBM_HAVE_SP_VFP 1
#endif

/* Double-precision VFP: vabs.f64, vsqrt.f64, etc.
 * Available on ARMv7+ with double-precision FP unit.
 * __ARM_FP bit 3 (0x8) indicates double-precision support.
 */

#if defined(__ARM_FP) && (__ARM_FP & 0x8)
#  define ARM_LIBM_HAVE_DP_VFP 1
#endif

/* ARMv8 rounding instructions: vrintp, vrintm, vrintr, vrintx,
 * vrinta, vrintz. Available on ARMv8-M, ARMv8-R, ARMv8-A (32-bit)
 * when FP is present.
 */

#if (__ARM_ARCH >= 8) && defined(__ARM_FP)
#  define ARM_LIBM_HAVE_VRINT 1
#endif

/* ARMv8 min/max with IEEE semantics: vminnm, vmaxnm.
 * Same availability as vrint instructions.
 */

#if (__ARM_ARCH >= 8) && defined(__ARM_FP)
#  define ARM_LIBM_HAVE_VMINNM 1
#endif

#endif /* __LIBS_LIBM_LIBM_ARM_ARM_LIBM_H */
