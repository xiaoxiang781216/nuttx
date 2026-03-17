/****************************************************************************
 * libs/libc/machine/arm/thumb/arch_memcpy.S
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

/* Optimized memcpy for ARMv6-M (Thumb-1 only, e.g. Cortex-M0/M0+/M1).
 *
 * Algorithm:
 *   Step 1: Byte copy until both src and dst are word-aligned
 *   Step 2: Word copy using LDMIA/STMIA (4 words at a time)
 *   Step 3: Word copy (1 word at a time)
 *   Step 4: Remaining bytes copy
 *
 * ARMv6-M constraints:
 *   - No IT blocks, no post-index addressing
 *   - No three-operand ALU, no RSB with immediate != 0
 *   - Only low registers (r0-r7) for most ALU operations
 *   - No unaligned word access
 */

#include "libc.h"

#ifdef LIBC_BUILD_MEMCPY

	.syntax unified
	.text
	.align	2
	.global	ARCH_LIBCFUN(memcpy)
	.thumb
	.thumb_func
	.type	ARCH_LIBCFUN(memcpy), %function
ARCH_LIBCFUN(memcpy):
	@ r0: dst
	@ r1: src
	@ r2: len
	.cfi_sections .debug_frame
	.cfi_startproc
	push	{r0, r4-r7, lr}
	.cfi_def_cfa_offset 24
	.cfi_offset 14, -4

	@ Quick exit for zero length
	cmp	r2, #0
	beq	.Ldone

	@ Check if both src and dst have the same alignment
	movs	r3, r0
	eors	r3, r1
	movs	r4, #3
	tst	r3, r4
	bne	.Lbyte_copy

	@ Both have same alignment - align to word boundary
	movs	r3, #3
	tst	r0, r3
	beq	.Laligned

.Lalign_loop:
	ldrb	r3, [r1, #0]
	adds	r1, #1
	strb	r3, [r0, #0]
	adds	r0, #1
	subs	r2, #1
	beq	.Ldone
	movs	r3, #3
	tst	r0, r3
	bne	.Lalign_loop

.Laligned:
	@ Now both src and dst are word-aligned
	@ Try 4-word (16-byte) block copy
	cmp	r2, #16
	blo	.Lword_copy

.Lblock_copy:
	ldmia	r1!, {r4, r5, r6, r7}
	stmia	r0!, {r4, r5, r6, r7}
	subs	r2, #16
	cmp	r2, #16
	bhs	.Lblock_copy

.Lword_copy:
	@ Word-by-word copy
	cmp	r2, #4
	blo	.Ltail_bytes

.Lword_loop:
	ldr	r3, [r1, #0]
	adds	r1, #4
	str	r3, [r0, #0]
	adds	r0, #4
	subs	r2, #4
	cmp	r2, #4
	bhs	.Lword_loop

.Ltail_bytes:
	@ Copy remaining 0-3 bytes
	cmp	r2, #0
	beq	.Ldone

.Ltail_loop:
	ldrb	r3, [r1, #0]
	adds	r1, #1
	strb	r3, [r0, #0]
	adds	r0, #1
	subs	r2, #1
	bne	.Ltail_loop
	b	.Ldone

.Lbyte_copy:
	@ src and dst have different alignment.
	@ Strategy: align dst, then use word loads from aligned src
	@ with shift/combine to produce word stores to dst.

	@ Align dst first (byte copy up to 3 bytes)
	movs	r3, #3
	tst	r0, r3
	beq	.Lmisalign_setup

.Lbyte_align_dst:
	ldrb	r3, [r1, #0]
	adds	r1, #1
	strb	r3, [r0, #0]
	adds	r0, #1
	subs	r2, #1
	beq	.Ldone
	movs	r3, #3
	tst	r0, r3
	bne	.Lbyte_align_dst

.Lmisalign_setup:
	@ dst is now word-aligned, src is not.
	@ Compute src misalignment and align src down.
	@ r4 = shift_right (bits), r5 = shift_left (bits)

	cmp	r2, #4
	blo	.Lmisalign_tail

	movs	r3, #3
	movs	r4, r1
	ands	r4, r3		@ r4 = src & 3 (1, 2, or 3)
	bics	r1, r3		@ r1 = src aligned down

	lsls	r4, #3		@ r4 = misalign * 8 = shift_right
	movs	r5, #32
	subs	r5, r4		@ r5 = 32 - shift_right = shift_left

	@ Load first aligned word, extract upper partial
	ldr	r6, [r1, #0]
	adds	r1, #4
	lsrs	r6, r4		@ r6 = prev >> shift_right

.Lmisalign_word:
	@ Combine prev partial with next word
	ldr	r7, [r1, #0]	@ next aligned word
	adds	r1, #4
	movs	r3, r7
	lsls	r3, r5		@ r3 = next << shift_left
	orrs	r3, r6		@ r3 = combined word
	str	r3, [r0, #0]
	adds	r0, #4
	movs	r6, r7
	lsrs	r6, r4		@ r6 = next >> shift_right (new prev)
	subs	r2, #4
	cmp	r2, #4
	bhs	.Lmisalign_word

	@ Adjust src back: we over-read from aligned src.
	@ Actual src = aligned_src - (4 - misalign) = r1 - r5/8
	@ Simpler: src = r1 - 4 + (r4 / 8)

	lsrs	r3, r4, #3	@ r3 = original misalign
	subs	r1, #4
	adds	r1, r3		@ r1 = actual src position

.Lmisalign_tail:
	@ Copy remaining 0-3 bytes
	cmp	r2, #0
	beq	.Ldone

.Lmisalign_tail_loop:
	ldrb	r3, [r1, #0]
	adds	r1, #1
	strb	r3, [r0, #0]
	adds	r0, #1
	subs	r2, #1
	bne	.Lmisalign_tail_loop

.Ldone:
	pop	{r0, r4-r7, pc}
	.cfi_endproc
	.size	ARCH_LIBCFUN(memcpy), .-ARCH_LIBCFUN(memcpy)

#endif
