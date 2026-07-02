/****************************************************************************
 * libs/libc/machine/arm/thumb/arch_strnlen.S
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

/* strnlen for ARMv6-M (Thumb-1 only).  Word-at-a-time NUL scan with a
 * byte alignment guard, capped by maxlen.
 */

#include "libc.h"

#ifdef LIBC_BUILD_STRNLEN

	.syntax unified
	.text
	.align	2
	.global	ARCH_LIBCFUN(strnlen)
	.thumb
	.thumb_func
	.type	ARCH_LIBCFUN(strnlen), %function
ARCH_LIBCFUN(strnlen):
	.cfi_sections .debug_frame
	.cfi_startproc
	push	{r2, r4, r5, r6, r7, lr}
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

	movs	r6, r0			/* ptr = src */
	cmp	r1, #0
	beq	.Lzero			/* maxlen = 0 */
	movs	r2, #3
	/* Byte-scan the misaligned head until ptr is word-aligned.  */
	tst	r6, r2
	beq	.Lcheck4
.Lhead:
	ldrb	r3, [r6, #0]
	cmp	r3, #0
	beq	.Ldone			/* NUL found */
	adds	r6, #1
	subs	r1, #1
	beq	.Ldone			/* maxlen exhausted */
	tst	r6, r2
	bne	.Lhead
.Lcheck4:
	cmp	r1, #4
	blo	.Lbtail
.Lwl:
	ldr	r3, [r6, #0]
	subs	r7, r3, r4
	bics	r7, r7, r3
	tst	r7, r5
	bne	.Lnulword		/* NUL in this word */
	adds	r6, #4
	subs	r1, #4
	cmp	r1, #4
	bhs	.Lwl
.Lbtail:
	cmp	r1, #0
	beq	.Ldone
.Lbloop:
	ldrb	r3, [r6, #0]
	cmp	r3, #0
	beq	.Ldone
	adds	r6, #1
	subs	r1, #1
	bne	.Lbloop
.Ldone:
	subs	r0, r6, r0		/* len = ptr - src */
	pop	{r2, r4, r5, r6, r7, pc}
.Lnulword:
	ldrb	r3, [r6, #0]
	cmp	r3, #0
	beq	.Ldone
	adds	r6, #1
	b	.Lnulword
.Lzero:
	movs	r0, #0
	pop	{r2, r4, r5, r6, r7, pc}
	.cfi_endproc
	.size ARCH_LIBCFUN(strnlen), . - ARCH_LIBCFUN(strnlen)

#endif
