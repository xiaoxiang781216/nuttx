/****************************************************************************
 * arch/arm/src/arm_a_r/arm_boot_image.S
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
 * Description
 *
 * void up_boot_entry(uintptr_t entry, uintptr_t arg);
 *
 * Final hand-off to an already loaded image, matching the AArch32 Linux boot
 * protocol (Documentation/arm/booting.rst): MMU and D-cache off, I-cache
 * invalidated, r0 = 0, r1 = ~0 (device tree instead of a machine type),
 * r2 = boot argument (a DTB physical address), then branch to the image
 * entry.  Any payload following that protocol is accepted, e.g. a Linux or
 * a NuttX kernel.
 *
 * The caller's DRAM is identity mapped (VA == PA, see the qemu
 * g_section_mapping table), so the PC stays valid across the MMU disable and
 * this stub keeps executing from the same physical address until the branch.
 *
 * Never returns.
 ****************************************************************************/

#include <nuttx/config.h>

    .file   "arm_boot_image.S"

    .text
    .syntax unified
    .arm

    .global up_boot_entry
    .type   up_boot_entry, function

up_boot_entry:
    /* r0 = entry, r1 = arg.  Both are needed after the cache maintenance
     * call below, so park them in callee-saved registers.  Nothing has to be
     * preserved for the caller: this never returns.
     */

    mov     r4, r0                  /* r4 = image entry              */
    mov     r5, r1                  /* r5 = boot argument            */

#ifdef CONFIG_ARCH_DCACHE
    /* Write every dirty cache line back before disabling the D-cache.
     * Without a D-cache there is nothing to write back, and
     * up_flush_dcache_all() is an empty macro rather than a symbol, so the
     * call would not link.
     */

    bl      up_flush_dcache_all
#endif

    /* Mask IRQs and FIQs for the hand-off. */

    cpsid   if

    /* Disable the MMU and the caches. */

    mrc     p15, 0, r0, c1, c0, 0   /* r0 = SCTLR                    */
    bic     r0, r0, #(1 << 0)       /* SCTLR_M : MMU off             */
    bic     r0, r0, #(1 << 2)       /* SCTLR_C : D-cache off         */
    bic     r0, r0, #(1 << 12)      /* SCTLR_I : I-cache off         */
    mcr     p15, 0, r0, c1, c0, 0
    isb

    /* Invalidate the I-cache and the unified TLBs. */

    mov     r0, #0
    mcr     p15, 0, r0, c7, c5, 0   /* ICIALLU                       */
    mcr     p15, 0, r0, c8, c7, 0   /* TLBIALL                       */
    dsb
    isb

    /* AArch32 boot protocol registers, then enter the image. */

    mov     r0, #0                  /* r0 = 0                        */
    mvn     r1, #0                  /* r1 = ~0 : DTB, no machine type */
    mov     r2, r5                  /* r2 = boot argument            */

    bx      r4                      /* enter the image, never returns */

    .size   up_boot_entry, . - up_boot_entry
