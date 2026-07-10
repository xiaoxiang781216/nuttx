/****************************************************************************
 * libs/libc/machine/arm/thumb/arch_memcmp.S
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
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

/* memcmp for ARMv6-M (Thumb-1 only).  Compares 8 bytes per iteration
 * (two aligned word loads) to halve the loop trip count versus a 4-byte
 * loop.  src1 is aligned to a 4-byte boundary; src2 is only word-loaded
 * when it shares src1's alignment (ARMv6-M rejects unaligned ldr).  On a
 * differing word the block is re-scanned byte-by-byte (ARMv6-M has no
 * rev).  Byte data lives in r3/r4 everywhere so .Ldiff_byte is consistent.
 * Returns -1/0/1.
 */

#include "libc.h"

#ifdef LIBC_BUILD_MEMCMP

	.syntax unified
	.text
	.align	2
	.global	ARCH_LIBCFUN(memcmp)
	.thumb
	.thumb_func
	.type	ARCH_LIBCFUN(memcmp), %function
ARCH_LIBCFUN(memcmp):
	.cfi_sections .debug_frame
	.cfi_startproc
	push	{r4, r5, r6, r7, lr}
	cmp	r2, #0
	beq	.Lret0
	movs	r7, #3			/* 4-byte alignment mask */
	/* Align src1, comparing bytes; src2 advances in lockstep.  */
	tst	r0, r7
	beq	.Lcheck2
.Lhead:
	ldrb	r3, [r0, #0]
	ldrb	r4, [r1, #0]
	cmp	r3, r4
	bne	.Ldiff_byte
	adds	r0, #1
	adds	r1, #1
	subs	r2, #1
	beq	.Lret0
	tst	r0, r7
	bne	.Lhead
.Lcheck2:
	/* src1 aligned; only word-compare when src2 is also aligned.  */
	tst	r1, r7
	bne	.Lbytes_all		/* src2 unaligned -> byte loop */
	movs	r3, r2
	lsrs	r3, #3			/* r3 = number of 8-byte blocks */
	beq	.Lbytes_all		/* fewer than 8 bytes -> byte loop */
.Lwl:
	ldr	r4, [r0, #0]
	ldr	r5, [r1, #0]
	cmp	r4, r5
	bne	.Ldiff_scan		/* first word differs (r0 = block base) */
	ldr	r4, [r0, #4]
	ldr	r5, [r1, #4]
	cmp	r4, r5
	bne	.Ldiff_scan4		/* second word differs (base + 4) */
	adds	r0, #8
	adds	r1, #8
	subs	r3, #1
	bne	.Lwl
	/* Tail: n & 7 bytes remain (r2 unchanged by the block loop).  */
	movs	r6, r2
	movs	r3, #7
	ands	r6, r3			/* r6 = n & 7 */
	b	.Lbytes_r6
.Lbytes_all:
	movs	r6, r2			/* r6 = all remaining bytes */
.Lbytes_r6:
	cmp	r6, #0
	beq	.Lret0
.Lbloop:
	ldrb	r3, [r0, #0]
	ldrb	r4, [r1, #0]
	cmp	r3, r4
	bne	.Ldiff_byte
	adds	r0, #1
	adds	r1, #1
	subs	r6, #1
	bne	.Lbloop
	b	.Lret0
.Ldiff_scan4:
	/* Second word (bytes 4-7) differs; rescan from base + 4.  */
	adds	r0, #4
	adds	r1, #4
.Ldiff_scan:
	/* Difference in the word at r0: rescan bytes to locate it.  */
	ldrb	r3, [r0, #0]
	ldrb	r4, [r1, #0]
	cmp	r3, r4
	bne	.Ldiff_byte
	adds	r0, #1
	adds	r1, #1
	b	.Ldiff_scan
.Lret0:
	movs	r0, #0
	pop	{r4, r5, r6, r7, pc}
.Ldiff_byte:
	cmp	r3, r4
	blo	.Lneg
	movs	r0, #1
	pop	{r4, r5, r6, r7, pc}
.Lneg:
	movs	r0, #1
	negs	r0, r0
	pop	{r4, r5, r6, r7, pc}
	.cfi_endproc
	.size ARCH_LIBCFUN(memcmp), . - ARCH_LIBCFUN(memcmp)

#endif
