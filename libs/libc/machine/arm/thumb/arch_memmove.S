/****************************************************************************
 * libs/libc/machine/arm/thumb/arch_memmove.S
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

/* Optimized memmove for ARMv6-M (Thumb-1 only, e.g. Cortex-M0/M0+/M1).
 *
 * Handles overlapping regions by choosing forward or backward copy.
 * Uses word-at-a-time copy when both src and dst are word-aligned.
 */

#include "libc.h"

#ifdef LIBC_BUILD_MEMMOVE

	.syntax unified
	.text
	.align	2
	.global	ARCH_LIBCFUN(memmove)
	.thumb
	.thumb_func
	.type	ARCH_LIBCFUN(memmove), %function
ARCH_LIBCFUN(memmove):
	@ r0: dst
	@ r1: src
	@ r2: len
	.cfi_sections .debug_frame
	.cfi_startproc
	push	{r0, r4-r7, lr}
	.cfi_def_cfa_offset 24
	.cfi_offset 14, -4

	cmp	r2, #0
	beq	.Ldone

	@ Determine copy direction
	cmp	r0, r1
	bls	.Lforward	@ dst <= src: forward copy
	movs	r3, r1
	adds	r3, r2
	cmp	r0, r3
	bhs	.Lforward	@ dst >= src + len: no overlap, forward

	@ Backward copy (dst > src, overlapping)
	@ Start from the end
	adds	r0, r2		@ dst = dst + len
	adds	r1, r2		@ src = src + len

	@ Check if both are word-aligned at the end
	movs	r3, r0
	eors	r3, r1
	movs	r4, #3
	tst	r3, r4
	bne	.Lback_byte_copy

	@ Align to word boundary (from the end)
	movs	r3, #3
	tst	r0, r3
	beq	.Lback_aligned

.Lback_align_loop:
	subs	r1, #1
	subs	r0, #1
	ldrb	r3, [r1, #0]
	strb	r3, [r0, #0]
	subs	r2, #1
	beq	.Ldone
	movs	r3, #3
	tst	r0, r3
	bne	.Lback_align_loop

.Lback_aligned:
	@ Word-by-word backward copy
	cmp	r2, #16
	blo	.Lback_word

.Lback_block:
	subs	r1, #16
	subs	r0, #16
	ldr	r4, [r1, #0]
	ldr	r5, [r1, #4]
	ldr	r6, [r1, #8]
	ldr	r7, [r1, #12]
	str	r4, [r0, #0]
	str	r5, [r0, #4]
	str	r6, [r0, #8]
	str	r7, [r0, #12]
	subs	r2, #16
	cmp	r2, #16
	bhs	.Lback_block

.Lback_word:
	cmp	r2, #4
	blo	.Lback_tail

.Lback_word_loop:
	subs	r1, #4
	subs	r0, #4
	ldr	r3, [r1, #0]
	str	r3, [r0, #0]
	subs	r2, #4
	cmp	r2, #4
	bhs	.Lback_word_loop

.Lback_tail:
	cmp	r2, #0
	beq	.Ldone

.Lback_tail_loop:
	subs	r1, #1
	subs	r0, #1
	ldrb	r3, [r1, #0]
	strb	r3, [r0, #0]
	subs	r2, #1
	bne	.Lback_tail_loop
	b	.Ldone

.Lback_byte_copy:
	subs	r1, #1
	subs	r0, #1
	ldrb	r3, [r1, #0]
	strb	r3, [r0, #0]
	subs	r2, #1
	bne	.Lback_byte_copy
	b	.Ldone

	@ Forward copy
.Lforward:
	@ Check if both have same alignment
	movs	r3, r0
	eors	r3, r1
	movs	r4, #3
	tst	r3, r4
	bne	.Lfwd_byte_copy

	@ Align to word boundary
	movs	r3, #3
	tst	r0, r3
	beq	.Lfwd_aligned

.Lfwd_align_loop:
	ldrb	r3, [r1, #0]
	adds	r1, #1
	strb	r3, [r0, #0]
	adds	r0, #1
	subs	r2, #1
	beq	.Ldone
	movs	r3, #3
	tst	r0, r3
	bne	.Lfwd_align_loop

.Lfwd_aligned:
	@ 4-word block copy
	cmp	r2, #16
	blo	.Lfwd_word

.Lfwd_block:
	ldmia	r1!, {r4, r5, r6, r7}
	stmia	r0!, {r4, r5, r6, r7}
	subs	r2, #16
	cmp	r2, #16
	bhs	.Lfwd_block

.Lfwd_word:
	cmp	r2, #4
	blo	.Lfwd_tail

.Lfwd_word_loop:
	ldr	r3, [r1, #0]
	adds	r1, #4
	str	r3, [r0, #0]
	adds	r0, #4
	subs	r2, #4
	cmp	r2, #4
	bhs	.Lfwd_word_loop

.Lfwd_tail:
	cmp	r2, #0
	beq	.Ldone

.Lfwd_tail_loop:
	ldrb	r3, [r1, #0]
	adds	r1, #1
	strb	r3, [r0, #0]
	adds	r0, #1
	subs	r2, #1
	bne	.Lfwd_tail_loop
	b	.Ldone

.Lfwd_byte_copy:
	ldrb	r3, [r1, #0]
	adds	r1, #1
	strb	r3, [r0, #0]
	adds	r0, #1
	subs	r2, #1
	bne	.Lfwd_byte_copy

.Ldone:
	pop	{r0, r4-r7, pc}
	.cfi_endproc
	.size	ARCH_LIBCFUN(memmove), . - ARCH_LIBCFUN(memmove)

#endif
