/****************************************************************************
 * libs/libc/machine/arm/thumb/arch_strchr.S
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

/* strchr for ARMv6-M (Thumb-1 only).  Word-at-a-time scan with a byte
 * alignment guard so only aligned ldr is used (ARMv6-M rejects unaligned
 * word access).  Returns NULL when the NUL terminator precedes the byte.
 */

#include "libc.h"

#ifdef LIBC_BUILD_STRCHR

	.syntax unified
	.text
	.align	2
	.global	ARCH_LIBCFUN(strchr)
	.thumb
	.thumb_func
	.type	ARCH_LIBCFUN(strchr), %function
ARCH_LIBCFUN(strchr):
	.cfi_sections .debug_frame
	.cfi_startproc
	push	{r4, r5, r6, r7, lr}
	movs	r3, #0xff
	ands	r1, r3			/* ch = c & 0xff */

	/* const_m1 = 0x01010101 */
	movs	r4, #1
	lsls	r4, r4, #8
	adds	r4, #1
	lsls	r4, r4, #8
	adds	r4, #1
	lsls	r4, r4, #8
	adds	r4, #1
	/* const_h = 0x80808080 */
	movs	r5, #0x80
	lsls	r5, r5, #8
	adds	r5, #0x80
	lsls	r5, r5, #8
	adds	r5, #0x80
	lsls	r5, r5, #8
	adds	r5, #0x80
	/* repc = ch * 0x01010101 (ch in all four bytes) */
	movs	r6, r1
	lsls	r3, r1, #8
	adds	r6, r3
	lsls	r3, r1, #16
	adds	r6, r3
	lsls	r3, r1, #24
	adds	r6, r3

	movs	r2, r0			/* ptr = src */
	movs	r7, #3
	/* Byte-scan the misaligned head until ptr is word-aligned.  */
	tst	r2, r7
	beq	.Lwl
.Lhead:
	ldrb	r3, [r2, #0]
	cmp	r3, r1
	beq	.Lfound
	cmp	r3, #0
	beq	.Lnull
	adds	r2, #1
	tst	r2, r7
	bne	.Lhead
.Lwl:
	ldr	r3, [r2, #0]
	/* NUL test: syndrome = (data - const_m1) & ~data.  */
	subs	r7, r3, r4
	bics	r7, r7, r3
	tst	r7, r5
	bne	.Lscan			/* NUL in this word */
	/* Match test: zero-byte test on data ^ repc.  */
	eors	r3, r3, r6
	subs	r7, r3, r4
	bics	r7, r7, r3
	tst	r7, r5
	bne	.Lscan			/* match in this word */
	adds	r2, #4
	b	.Lwl
.Lscan:
	/* Rescan the word byte-by-byte to locate the match or NUL.  */
	ldrb	r3, [r2, #0]
	cmp	r3, r1
	beq	.Lfound
	cmp	r3, #0
	beq	.Lnull
	adds	r2, #1
	b	.Lscan
.Lfound:
	movs	r0, r2
	pop	{r4, r5, r6, r7, pc}
.Lnull:
	movs	r0, #0
	pop	{r4, r5, r6, r7, pc}
	.cfi_endproc
	.size ARCH_LIBCFUN(strchr), . - ARCH_LIBCFUN(strchr)

#endif
