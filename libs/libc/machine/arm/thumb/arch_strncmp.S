/****************************************************************************
 * libs/libc/machine/arm/thumb/arch_strncmp.S
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

/* strncmp for ARMv6-M (Thumb-1 only).  Word comparison with a byte
 * alignment guard on src1; src2 is only word-loaded when it shares src1's
 * alignment (ARMv6-M rejects unaligned ldr), otherwise the byte path runs.
 */

#include "libc.h"

#ifdef LIBC_BUILD_STRNCMP

	.syntax unified
	.text
	.align	2
	.global	ARCH_LIBCFUN(strncmp)
	.thumb
	.thumb_func
	.type	ARCH_LIBCFUN(strncmp), %function
ARCH_LIBCFUN(strncmp):
	.cfi_sections .debug_frame
	.cfi_startproc
	push	{r4, r5, r6, r7, lr}
	cmp	r2, #0
	beq	.Lret0
	/* const_m1 = 0x01010101 */
	movs	r5, #1
	lsls	r5, r5, #8
	adds	r5, #1
	lsls	r5, r5, #8
	adds	r5, #1
	lsls	r5, r5, #8
	adds	r5, #1
	/* const_h = const_m1 << 7 = 0x80808080 */
	lsls	r6, r5, #7

	movs	r7, #3
	/* Align src1, comparing bytes; src2 advances in lockstep.  */
	tst	r0, r7
	beq	.Lcheck2
.Lhead:
	ldrb	r3, [r0, #0]
	ldrb	r4, [r1, #0]
	cmp	r3, r4
	bne	.Ldiff_byte
	cmp	r3, #0
	beq	.Lret0
	adds	r0, #1
	adds	r1, #1
	subs	r2, #1
	beq	.Lret0
	tst	r0, r7
	bne	.Lhead
.Lcheck2:
	/* src1 aligned; only word-compare when src2 is also aligned.  */
	tst	r1, r7
	bne	.Lbyte_tail
.Lwl:
	cmp	r2, #4
	blo	.Lbyte_tail
	ldr	r3, [r0, #0]
	ldr	r4, [r1, #0]
	/* NUL in src1 word: fall back to bytes so a NUL preceding a
	 * difference correctly yields equality.  */
	subs	r7, r3, r5
	bics	r7, r7, r3
	tst	r7, r6
	bne	.Lbyte_tail		/* NUL in this word */
	cmp	r3, r4
	bne	.Ldiff_scan
	adds	r0, #4
	adds	r1, #4
	subs	r2, #4
	bne	.Lwl
	b	.Lret0
.Ldiff_scan:
	/* Difference in this word: rescan bytes to locate it.  */
	ldrb	r3, [r0, #0]
	ldrb	r4, [r1, #0]
	cmp	r3, r4
	bne	.Ldiff_byte
	adds	r0, #1
	adds	r1, #1
	b	.Ldiff_scan
.Lbyte_tail:
	cmp	r2, #0
	beq	.Lret0
.Lbloop:
	ldrb	r3, [r0, #0]
	ldrb	r4, [r1, #0]
	cmp	r3, r4
	bne	.Ldiff_byte
	cmp	r3, #0
	beq	.Lret0
	adds	r0, #1
	adds	r1, #1
	subs	r2, #1
	bne	.Lbloop
.Lret0:
	movs	r0, #0
	pop	{r4, r5, r6, r7, pc}
.Ldiff_byte:
	subs	r0, r3, r4
	pop	{r4, r5, r6, r7, pc}
	.cfi_endproc
	.size ARCH_LIBCFUN(strncmp), . - ARCH_LIBCFUN(strncmp)

#endif
