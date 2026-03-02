/****************************************************************************
 * libs/libc/machine/arm/arm_m/thumb1/arch_memchr.S
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

/* Optimized memchr for ARMv6-M (Thumb-1 only, e.g. Cortex-M0/M0+/M1).
 *
 * Algorithm:
 *   Step 1: Byte scan until src is word-aligned
 *   Step 2: Word-at-a-time scan using zero-byte detection trick
 *   Step 3: Byte scan for remaining bytes
 *
 * The word-at-a-time trick: XOR each word with the splat pattern of the
 * search byte, then use the classic has-zero-byte test:
 *   ((x - 0x01010101) & ~x & 0x80808080) != 0
 */

#include "libc.h"

#ifdef LIBC_BUILD_MEMCHR

	.syntax unified
	.text
	.align	2
	.global	ARCH_LIBCFUN(memchr)
	.thumb
	.thumb_func
	.type	ARCH_LIBCFUN(memchr), %function
ARCH_LIBCFUN(memchr):
	@ r0: src
	@ r1: c (byte to find)
	@ r2: n (length)
	.cfi_sections .debug_frame
	.cfi_startproc
	push	{r4-r7, lr}
	.cfi_def_cfa_offset 20
	.cfi_offset 14, -4

	@ Mask search byte
	movs	r3, #0xff
	ands	r1, r3

	@ Quick exit for zero length
	cmp	r2, #0
	beq	.Lnotfound

	@ Byte scan until word-aligned or length < 4
	cmp	r2, #4
	blo	.Lbyte_scan
	movs	r3, #3
	tst	r0, r3
	beq	.Lword_setup

.Lalign_loop:
	ldrb	r3, [r0, #0]
	cmp	r3, r1
	beq	.Lfound
	adds	r0, #1
	subs	r2, #1
	beq	.Lnotfound
	cmp	r2, #4
	blo	.Lbyte_scan
	movs	r3, #3
	tst	r0, r3
	bne	.Lalign_loop

.Lword_setup:
	@ Splat search byte across word: r3 = c | (c<<8) | (c<<16) | (c<<24)
	movs	r3, r1
	lsls	r4, r3, #8
	orrs	r3, r4
	lsls	r4, r3, #16
	orrs	r3, r4
	movs	r7, r3		@ r7 = splat pattern

	@ Prepare constants
	@ r4 = 0x01010101, r5 = 0x80808080
	movs	r4, #1
	lsls	r3, r4, #8
	orrs	r4, r3
	lsls	r3, r4, #16
	orrs	r4, r3		@ r4 = 0x01010101

	lsls	r5, r4, #7	@ r5 = 0x80808080

.Lword_loop:
	cmp	r2, #4
	blo	.Lbyte_scan

	ldr	r6, [r0, #0]
	eors	r6, r7		@ XOR with splat - matching bytes become 0
	@ has-zero-byte test using bics: ((x - 0x01010101) & ~x & 0x80808080)
	subs	r3, r6, r4	@ r3 = x - 0x01010101
	bics	r3, r6		@ r3 = (x - 0x01010101) & ~x
	tst	r3, r5		@ test & 0x80808080
	bne	.Lword_found

	adds	r0, #4
	subs	r2, #4
	b	.Lword_loop

.Lword_found:
	@ Found a match in this word - scan bytes to find exact position
	@ Fall through to byte scan (at most 4 iterations)

.Lbyte_scan:
	cmp	r2, #0
	beq	.Lnotfound

.Lbyte_loop:
	ldrb	r3, [r0, #0]
	cmp	r3, r1
	beq	.Lfound
	adds	r0, #1
	subs	r2, #1
	bne	.Lbyte_loop

.Lnotfound:
	movs	r0, #0
	pop	{r4-r7, pc}

.Lfound:
	@ r0 already points to the found byte
	pop	{r4-r7, pc}
	.cfi_endproc
	.size	ARCH_LIBCFUN(memchr), . - ARCH_LIBCFUN(memchr)

#endif
