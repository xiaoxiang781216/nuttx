/****************************************************************************
 * libs/libc/machine/arm/arm_m/thumb1/arch_strlen.S
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

/* Optimized strlen for ARMv6-M (Thumb-1 only, e.g. Cortex-M0/M0+/M1).
 *
 * Algorithm:
 *   Step 1: Byte scan until src is word-aligned
 *   Step 2: Word-at-a-time scan using zero-byte detection trick
 *   Step 3: Byte scan to find exact NUL position in the last word
 *
 * Zero-byte detection: ((x - 0x01010101) & ~x & 0x80808080) != 0
 * Uses bics (bit clear) to fuse ~x & mask into one instruction.
 */

#include "libc.h"

#ifdef LIBC_BUILD_STRLEN

	.syntax unified
	.text
	.align	2
	.global	ARCH_LIBCFUN(strlen)
	.thumb
	.thumb_func
	.type	ARCH_LIBCFUN(strlen), %function
ARCH_LIBCFUN(strlen):
	@ r0: src
	.cfi_sections .debug_frame
	.cfi_startproc
	push	{r4, r5, lr}
	.cfi_def_cfa_offset 12
	.cfi_offset 14, -4

	movs	r1, r0		@ r1 = current pointer, r0 = original (for result)

	@ Byte scan until word-aligned
	movs	r3, #3
	tst	r1, r3
	beq	.Lword_setup

.Lalign_loop:
	ldrb	r2, [r1, #0]
	cmp	r2, #0
	beq	.Lfound
	adds	r1, #1
	tst	r1, r3
	bne	.Lalign_loop

.Lword_setup:
	@ Prepare constant 0x01010101 in r4
	movs	r4, #1
	lsls	r3, r4, #8
	orrs	r4, r3
	lsls	r3, r4, #16
	orrs	r4, r3		@ r4 = 0x01010101

	lsls	r5, r4, #7	@ r5 = 0x80808080

	@ Word-at-a-time loop: 6 instructions per iteration
	@ Uses bics to combine ~x & mask in one instruction

.Lword_loop:
	ldr	r2, [r1, #0]	@ load word
	adds	r1, #4		@ advance pointer early
	subs	r3, r2, r4	@ r3 = x - 0x01010101
	bics	r3, r2		@ r3 = (x - 0x01010101) & ~x
	tst	r3, r5		@ test & 0x80808080
	beq	.Lword_loop	@ loop if no NUL

	@ NUL found - back up pointer (we advanced early)
	subs	r1, #4

	@ Scan bytes to find exact NUL position
	ldrb	r2, [r1, #0]
	cmp	r2, #0
	beq	.Lfound
	adds	r1, #1

	ldrb	r2, [r1, #0]
	cmp	r2, #0
	beq	.Lfound
	adds	r1, #1

	ldrb	r2, [r1, #0]
	cmp	r2, #0
	beq	.Lfound
	adds	r1, #1

	@ Must be the 4th byte
.Lfound:
	subs	r0, r1, r0	@ result = current - original
	pop	{r4, r5, pc}
	.cfi_endproc
	.size	ARCH_LIBCFUN(strlen), . - ARCH_LIBCFUN(strlen)

#endif
