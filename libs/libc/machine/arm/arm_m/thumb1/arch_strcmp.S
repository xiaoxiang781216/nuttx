/****************************************************************************
 * libs/libc/machine/arm/arm_m/thumb1/arch_strcmp.S
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

/* Optimized strcmp for ARMv6-M (Thumb-1 only, e.g. Cortex-M0/M0+/M1).
 *
 * Algorithm:
 *   Step 1: Byte compare until both pointers are word-aligned
 *   Step 2: Word-at-a-time compare using zero-byte detection
 *   Step 3: When a difference or NUL is found, byte compare to get result
 *
 * When both strings have the same alignment, we can use word loads.
 * Otherwise we fall back to byte-by-byte comparison.
 */

#include "libc.h"

#ifdef LIBC_BUILD_STRCMP

	.syntax unified
	.text
	.align	2
	.global	ARCH_LIBCFUN(strcmp)
	.thumb
	.thumb_func
	.type	ARCH_LIBCFUN(strcmp), %function
ARCH_LIBCFUN(strcmp):
	@ r0: s1
	@ r1: s2
	.cfi_sections .debug_frame
	.cfi_startproc
	push	{r4, r5, lr}
	.cfi_def_cfa_offset 12
	.cfi_offset 14, -4

	@ Check if both pointers have the same alignment
	movs	r2, r0
	eors	r2, r1
	movs	r3, #3
	tst	r2, r3
	bne	.Lbyte_compare

	@ Same alignment - align to word boundary
	tst	r0, r3
	beq	.Lword_setup

.Lalign_loop:
	ldrb	r2, [r0, #0]
	ldrb	r3, [r1, #0]
	adds	r0, #1
	adds	r1, #1
	cmp	r2, #0
	beq	.Lreturn_diff
	cmp	r2, r3
	bne	.Lreturn_diff
	movs	r3, #3
	tst	r0, r3
	bne	.Lalign_loop

.Lword_setup:
	@ Prepare constant 0x01010101 in r4
	movs	r4, #1
	lsls	r3, r4, #8
	orrs	r4, r3
	lsls	r3, r4, #16
	orrs	r4, r3		@ r4 = 0x01010101

	lsls	r5, r4, #7	@ r5 = 0x80808080

.Lword_loop:
	ldr	r2, [r0, #0]	@ word from s1
	ldr	r3, [r1, #0]	@ word from s2

	@ Check if words differ first (most common exit)
	cmp	r2, r3
	bne	.Lhas_diff

	@ Check for NUL using bics: ((x - 0x01010101) & ~x & 0x80808080)
	subs	r3, r2, r4	@ r3 = x - 0x01010101
	bics	r3, r2		@ r3 = (x - 0x01010101) & ~x
	tst	r3, r5		@ test & 0x80808080
	bne	.Lhas_nul

	@ Tight loop: single branch (beq) avoids extra pipeline flush
.Lword_fast:
	ldr	r2, [r0, #4]	@ next word from s1
	ldr	r3, [r1, #4]	@ next word from s2
	adds	r0, #4
	adds	r1, #4
	cmp	r2, r3
	bne	.Lhas_diff
	subs	r3, r2, r4	@ r3 = x - 0x01010101
	bics	r3, r2		@ r3 = (x - 0x01010101) & ~x
	tst	r3, r5		@ test & 0x80808080
	beq	.Lword_fast

.Lhas_nul:
	@ Words are equal and contain NUL — return 0
	movs	r0, #0
	pop	{r4, r5, pc}

.Lhas_diff:
	@ Found NUL or difference - compare byte by byte
	ldrb	r2, [r0, #0]
	ldrb	r3, [r1, #0]
	cmp	r2, #0
	beq	.Lreturn_diff
	cmp	r2, r3
	bne	.Lreturn_diff

	ldrb	r2, [r0, #1]
	ldrb	r3, [r1, #1]
	cmp	r2, #0
	beq	.Lreturn_diff
	cmp	r2, r3
	bne	.Lreturn_diff

	ldrb	r2, [r0, #2]
	ldrb	r3, [r1, #2]
	cmp	r2, #0
	beq	.Lreturn_diff
	cmp	r2, r3
	bne	.Lreturn_diff

	ldrb	r2, [r0, #3]
	ldrb	r3, [r1, #3]
	b	.Lreturn_diff

.Lbyte_compare:
	@ Different alignment - byte by byte
	ldrb	r2, [r0, #0]
	ldrb	r3, [r1, #0]
	adds	r0, #1
	adds	r1, #1
	cmp	r2, #0
	beq	.Lreturn_diff
	cmp	r2, r3
	beq	.Lbyte_compare

.Lreturn_diff:
	subs	r0, r2, r3
	pop	{r4, r5, pc}
	.cfi_endproc
	.size	ARCH_LIBCFUN(strcmp), . - ARCH_LIBCFUN(strcmp)

#endif
