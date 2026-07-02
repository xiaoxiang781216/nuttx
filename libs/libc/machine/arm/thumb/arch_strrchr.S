/****************************************************************************
 * libs/libc/machine/arm/thumb/arch_strrchr.S
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

/* strrchr for ARMv6-M (Thumb-1 only).  Word-at-a-time scan with a byte
 * alignment guard, tracking the last match position.
 */

#include "libc.h"

#ifdef LIBC_BUILD_STRRCHR

	.syntax unified
	.text
	.align	2
	.global	ARCH_LIBCFUN(strrchr)
	.thumb
	.thumb_func
	.type	ARCH_LIBCFUN(strrchr), %function
ARCH_LIBCFUN(strrchr):
	.cfi_sections .debug_frame
	.cfi_startproc
	push	{r2, r4, r5, r6, r7, lr}
	movs	r6, r0			/* ptr = src */
	movs	r3, #0xff
	ands	r1, r3			/* ch = c & 0xff */
	movs	r0, #0			/* last = NULL */

	/* const_m1 = 0x01010101 */
	movs	r4, #1
	lsls	r4, r4, #8
	adds	r4, #1
	lsls	r4, r4, #8
	adds	r4, #1
	lsls	r4, r4, #8
	adds	r4, #1
	/* const_h = const_m1 << 7 = 0x80808080 */
	lsls	r5, r4, #7
	/* repc = ch * 0x01010101 (ch in all four bytes) */
	movs	r2, r1
	lsls	r3, r1, #8
	adds	r2, r3
	lsls	r3, r1, #16
	adds	r2, r3
	lsls	r3, r1, #24
	adds	r2, r3

	movs	r7, #3
	/* Byte-scan the misaligned head until ptr is word-aligned.  */
	tst	r6, r7
	beq	.Lwl
.Lhead:
	ldrb	r3, [r6, #0]
	cmp	r3, r1
	bne	.Lh1
	movs	r0, r6			/* last = ptr */
.Lh1:
	cmp	r3, #0
	beq	.Lret			/* NUL terminator */
	adds	r6, #1
	tst	r6, r7
	bne	.Lhead
.Lwl:
	ldr	r3, [r6, #0]
	/* NUL test: syndrome = (data - const_m1) & ~data.  */
	subs	r7, r3, r4
	bics	r7, r7, r3
	tst	r7, r5
	bne	.Lterm			/* NUL in this word */
	/* Match test: zero-byte test on data ^ repc.  */
	eors	r3, r3, r2
	subs	r7, r3, r4
	bics	r7, r7, r3
	tst	r7, r5
	bne	.Lmatch_word		/* match in this word */
	adds	r6, #4
	b	.Lwl
.Lmatch_word:
	/* Match (no NUL): rescan the 4 bytes to update last.  r6 points
	 * at the word start; the loop advances r6 to the next word.  */
	movs	r7, #4
.Lmw_scan:
	ldrb	r3, [r6, #0]
	cmp	r3, r1
	bne	.Lmw1
	movs	r0, r6			/* last = ptr */
.Lmw1:
	adds	r6, #1
	subs	r7, #1
	bne	.Lmw_scan
	b	.Lwl
.Lterm:
	/* NUL in this word: rescan bytes tracking last match until NUL.  */
.Lterm_scan:
	ldrb	r3, [r6, #0]
	cmp	r3, r1
	bne	.Lt1
	movs	r0, r6			/* last = ptr */
.Lt1:
	cmp	r3, #0
	beq	.Lret
	adds	r6, #1
	b	.Lterm_scan
.Lret:
	pop	{r2, r4, r5, r6, r7, pc}
	.cfi_endproc
	.size ARCH_LIBCFUN(strrchr), . - ARCH_LIBCFUN(strrchr)

#endif
