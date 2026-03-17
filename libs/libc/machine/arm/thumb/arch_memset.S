/****************************************************************************
 * libs/libc/machine/arm/thumb/arch_memset.S
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

/* Optimized memset for ARMv6-M (Thumb-1 only, e.g. Cortex-M0/M0+/M1).
 *
 * Algorithm:
 *   Step 1: Byte fill until dst is word-aligned
 *   Step 2: Splat byte value to word
 *   Step 3: Word fill using STMIA (4 words at a time)
 *   Step 4: Word fill (1 word at a time)
 *   Step 5: Remaining bytes fill
 */

#include "libc.h"

#ifdef LIBC_BUILD_MEMSET

	.syntax unified
	.text
	.align	2
	.global	ARCH_LIBCFUN(memset)
	.thumb
	.thumb_func
	.type	ARCH_LIBCFUN(memset), %function
ARCH_LIBCFUN(memset):
	@ r0: dst
	@ r1: value (byte)
	@ r2: len
	.cfi_sections .debug_frame
	.cfi_startproc
	push	{r0, r4-r7, lr}
	.cfi_def_cfa_offset 24
	.cfi_offset 14, -4

	@ Quick exit for zero length
	cmp	r2, #0
	beq	.Ldone

	@ Mask value to byte
	movs	r3, #0xff
	ands	r1, r3

	@ Align dst to word boundary with byte stores
	movs	r3, #3
	tst	r0, r3
	beq	.Laligned

.Lalign_loop:
	strb	r1, [r0, #0]
	adds	r0, #1
	subs	r2, #1
	beq	.Ldone
	movs	r3, #3
	tst	r0, r3
	bne	.Lalign_loop

.Laligned:
	@ Splat byte value across word: r1 = r1 | (r1 << 8) | (r1 << 16) | (r1 << 24)
	lsls	r3, r1, #8
	orrs	r1, r3
	lsls	r3, r1, #16
	orrs	r1, r3

	@ Prepare registers for block fill
	@ r4 = r5 = r6 = r7 = r1 (fill word)
	movs	r4, r1
	movs	r5, r1
	movs	r6, r1
	movs	r7, r1

	@ 4-word (16-byte) block fill
	cmp	r2, #16
	blo	.Lword_fill

.Lblock_fill:
	stmia	r0!, {r4, r5, r6, r7}
	subs	r2, #16
	cmp	r2, #16
	bhs	.Lblock_fill

.Lword_fill:
	@ Word-by-word fill
	cmp	r2, #4
	blo	.Ltail_bytes

.Lword_loop:
	str	r1, [r0, #0]
	adds	r0, #4
	subs	r2, #4
	cmp	r2, #4
	bhs	.Lword_loop

.Ltail_bytes:
	@ Fill remaining 0-3 bytes
	cmp	r2, #0
	beq	.Ldone

.Ltail_loop:
	strb	r1, [r0, #0]
	adds	r0, #1
	subs	r2, #1
	bne	.Ltail_loop

.Ldone:
	pop	{r0, r4-r7, pc}
	.cfi_endproc
	.size	ARCH_LIBCFUN(memset), . - ARCH_LIBCFUN(memset)

#endif
