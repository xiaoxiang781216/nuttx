/****************************************************************************
 * libs/libc/machine/arm/arm_m/thumb1/arch_strcpy.S
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

/* Optimized strcpy for ARMv6-M (Thumb-1 only, e.g. Cortex-M0/M0+/M1).
 *
 * Algorithm:
 *   Step 1: Byte copy until both src and dst are word-aligned
 *   Step 2: Word-at-a-time copy with NUL detection
 *   Step 3: Byte copy for the tail containing NUL
 *
 * When src and dst have different alignment, falls back to byte copy.
 */

#include "libc.h"

#ifdef LIBC_BUILD_STRCPY

	.syntax unified
	.text
	.align	2
	.global	ARCH_LIBCFUN(strcpy)
	.thumb
	.thumb_func
	.type	ARCH_LIBCFUN(strcpy), %function
ARCH_LIBCFUN(strcpy):
	@ r0: dst
	@ r1: src
	.cfi_sections .debug_frame
	.cfi_startproc
	push	{r0, r4, r5, lr}
	.cfi_def_cfa_offset 16
	.cfi_offset 14, -4

	@ Check if both have same alignment
	movs	r2, r0
	eors	r2, r1
	movs	r3, #3
	tst	r2, r3
	bne	.Lbyte_copy

	@ Same alignment - align to word boundary
	tst	r0, r3
	beq	.Lword_setup

.Lalign_loop:
	ldrb	r2, [r1, #0]
	adds	r1, #1
	strb	r2, [r0, #0]
	adds	r0, #1
	cmp	r2, #0
	beq	.Ldone
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
	ldr	r2, [r1, #0]

	@ Check for NUL using bics: ((x - 0x01010101) & ~x & 0x80808080)
	subs	r3, r2, r4	@ r3 = x - 0x01010101
	bics	r3, r2		@ r3 = (x - 0x01010101) & ~x
	tst	r3, r5		@ test & 0x80808080
	bne	.Lword_has_nul

	@ No NUL - store word and advance both pointers
	@ Fall into tight loop: store, load next, check, repeat
	@ Single branch (beq back) avoids extra pipeline flush

.Lword_fast:
	stmia	r0!, {r2}	@ *dst++ (word)
	ldr	r2, [r1, #4]	@ load next src word
	adds	r1, #4		@ advance src
	subs	r3, r2, r4	@ r3 = x - 0x01010101
	bics	r3, r2		@ r3 = (x - 0x01010101) & ~x
	tst	r3, r5		@ test & 0x80808080
	beq	.Lword_fast	@ loop if no NUL

.Lword_has_nul:
	@ NUL found in this word - store the whole word.
	@ Both pointers are word-aligned, so this is safe.
	@ Bytes past NUL are don't-care in the destination.
	str	r2, [r0, #0]
	b	.Ldone

.Lbyte_copy:
	@ Different alignment - byte by byte
	ldrb	r2, [r1, #0]
	adds	r1, #1
	strb	r2, [r0, #0]
	adds	r0, #1
	cmp	r2, #0
	bne	.Lbyte_copy

.Ldone:
	pop	{r0, r4, r5, pc}
	.cfi_endproc
	.size	ARCH_LIBCFUN(strcpy), . - ARCH_LIBCFUN(strcpy)

#endif
