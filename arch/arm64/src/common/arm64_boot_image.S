/****************************************************************************
 * arch/arm64/src/common/arm64_boot_image.S
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
 * Final hand-off to an already loaded image, matching the arm64 boot protocol
 * (Documentation/arm64/booting.rst): MMU and D-cache off, I-cache
 * invalidated, x0 = boot argument (a DTB physical address), x1..x3 = 0, then
 * branch to the image entry. Any payload following that protocol is accepted,
 * e.g. a Linux or a NuttX kernel. The caller's DRAM is identity mapped
 * (VA == PA), so the PC stays valid across the MMU disable and this stub
 * keeps executing from the same physical address until the branch.
 *
 * Never returns.
 ****************************************************************************/

#include <nuttx/config.h>

    .file   "arm64_boot_image.S"
    .text
    .globl  up_boot_entry
    .type   up_boot_entry, %function

up_boot_entry:
    /* x0 = entry, x1 = arg. */

    mov     x19, x0                 /* x19 = image entry             */
    mov     x20, x1                 /* x20 = boot argument           */

    /* Write every dirty cache line back before disabling the D-cache. */

    bl      up_flush_dcache_all

    /* Mask all exceptions/interrupts for the hand-off. */

    msr     DAIFSet, #0xf
    isb

    /* Disable the EL1 MMU and caches, then invalidate the EL1 TLBs. */

    mrs     x0, sctlr_el1
    bic     x0, x0, #(1 << 0)       /* SCTLR_M_BIT : MMU off          */
    bic     x0, x0, #(1 << 2)       /* SCTLR_C_BIT : D-cache off      */
    bic     x0, x0, #(1 << 12)      /* SCTLR_I_BIT : I-cache off      */
    msr     sctlr_el1, x0
    isb
    ic      iallu
    tlbi    vmalle1is               /* EL1&0 stage-1 TLBs             */
    dsb     sy
    isb

    /* arm64 boot protocol registers, then enter the image. */
    mov     x0, x20                 /* x0 = boot argument             */
    mov     x1, xzr
    mov     x2, xzr
    mov     x3, xzr

    br      x19                     /* enter the image, never returns */

    .size   up_boot_entry, . - up_boot_entry
