/***************************************************************************
 * libs/libc/machine/arm/arm/arch_strcpy.S
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
 ***************************************************************************/

#include "libc.h"

#ifdef LIBC_BUILD_STRCPY

/* This strcpy borrowed some ideas from arch_strcmp.S(). */

/* Parameters and result. */
#define dst     r0
#define src     r1
#define result  r0

/* Internal variables, or callee saved registers */
#define tmp1          r4
#define tmp2          r5
#define tmp3          r6
#define src_offset    r7
#define const_m1      r8
#define const_h       r9

#ifdef __ARM_BIG_ENDIAN
#  define MASK_0          0xff000000
#  define MASK_1          0xff0000
#  define MASK_2          0xff00
#  define MASK_3          0xff
#  define BYTE_0_SHIFT    24
#  define BYTE_1_SHIFT    16
#  define BYTE_2_SHIFT    8
#  define BYTE_3_SHIFT    0
#else
#  define MASK_0          0xff
#  define MASK_1          0xff00
#  define MASK_2          0xff0000
#  define MASK_3          0xff000000
#  define BYTE_0_SHIFT    0
#  define BYTE_1_SHIFT    8
#  define BYTE_2_SHIFT    16
#  define BYTE_3_SHIFT    24
#endif

	.syntax	unified
	.text
	.align	2
	.global	ARCH_LIBCFUN(strcpy)
	.arm
	.type	ARCH_LIBCFUN(strcpy), %function

ARCH_LIBCFUN(strcpy):
	push	{result, tmp1, tmp2, tmp3, src_offset, const_m1, const_h}
	movw	const_m1, #:lower16:0x01010101
	movt	const_m1, #:upper16:0x01010101
	movw	const_h, #:lower16:0x80808080
	movt	const_h, #:upper16:0x80808080
	eor	tmp1, dst, src
	tst	tmp1, #3
	/* If dst and src not at same byte offset from a word boundary */
	bne	.Lstrs_diff_offset
	/* Process same byte offset then, get the offset */
	ands	tmp1, src, #3
	beq	.Ldst_src_aligned
	/* get number of bytes unaligned */
	rsb	tmp1, tmp1, #4

.Lbyte_copy_until_dsr_src_aligned:
	ldrb	tmp2, [src], #1
	cmp	tmp2, #0
	beq	.Lcopy_done
	strb	tmp2, [dst], #1
	subs	tmp1, tmp1, #1
	bne	.Lbyte_copy_until_dsr_src_aligned

.Ldst_src_aligned:
	/* Now dst and src are aligned */
	ldr	tmp1, [src], #4
	sub	tmp2, tmp1, const_m1
	bic	tmp2, tmp2, tmp1
	tst	tmp2, const_h
	/* All zero means no zero byte is detected */
	it	eq
	streq	tmp1, [dst], #4
	beq	.Ldst_src_aligned

	/* There is a zero in the word, copy until zero */
	sub	src, src, #4
.Lbyte_copy_until_zero:
	ldrb	tmp2, [src], #1
	cmp	tmp2, #0
	beq	.Lcopy_done
	strb	tmp2, [dst], #1
	b	.Lbyte_copy_until_zero

/* Make dst aligned, so we won't write anything before dst.
 * If we attempt to write before dst, atomic read-write must
 * be ensured. Atomic operation complicates things.
 * So the solution here is byte by byte copy until dst aligned.
 */
.Lstrs_diff_offset:
	ands	tmp1, dst, #3
	beq	.Ldiff_offset_loop_begin
	/* get number of dst bytes unaligned */
	rsb	tmp1, tmp1, #4

.Lbyte_copy_until_dst_aligned:
	ldrb	tmp2, [src], #1
	cmp	tmp2, #0
	beq	.Lcopy_done
	strb	tmp2, [dst], #1
	subs	tmp1, tmp1, #1
	bne	.Lbyte_copy_until_dst_aligned

.Ldiff_offset_loop_begin:
	/* src_offset mustn't be 0 here */
	and	src_offset, src, #3
	lsls	src_offset, src_offset, #3
	bic	src, src, #3
/* first word logic
 * prepend 0xff to make the algorithm simpler
 * only the first word needs to be prepended
 */
	ldr	tmp1, [src], #4
	mvn	tmp2, #0
	rsb	tmp3, src_offset, #32

#ifdef __ARM_BIG_ENDIAN
	lsl	tmp2, tmp2, tmp3
#else
	lsr	tmp2, tmp2, tmp3
#endif
	orr	tmp1, tmp1, tmp2
	/* Test if the first word contains zero */
	sub	tmp3, tmp1, const_m1
	bic	tmp3, tmp3, tmp1
	tst	tmp3, const_h
	/* non-zero means zero byte is detected */
	bne	.Ltail_copy

	/* before loop, set tmp2=tmp1 to simplify the logic in the loop */
	mov	tmp2, tmp1
.Ldiff_offset_loop:
	mov	tmp1, tmp2
	ldr	tmp2, [src], #4
	/* Test if  contains zero */
	sub	tmp3, tmp2, const_m1
	bic	tmp3, tmp3, tmp2
	tst	tmp3, const_h
	/* non-zero means zero byte is detected */
	bne	.Ltail_copy
	/* Now let's fill dst */
#ifdef __ARM_BIG_ENDIAN
	lsl	tmp1, tmp1, src_offset
	rsb	tmp3, src_offset, #32
	lsr	tmp3, tmp2, tmp3
	orr	tmp1, tmp1, tmp3
#else
	lsr	tmp1, tmp1, src_offset
	rsb	tmp3, src_offset, #32
	lsl	tmp3, tmp2, tmp3
	orr	tmp1, tmp1, tmp3
#endif
	str	tmp1, [dst], #4
	b	.Ldiff_offset_loop

.Ltail_copy:
	cmp	src_offset, #24
	beq	.Loffset_3
	cmp	src_offset, #16
	beq	.Loffset_2
	/*  src_offset == 8 here */
	ands	tmp3, tmp1, MASK_1
	beq	.Lcopy_done
	lsr	tmp3, tmp3, BYTE_1_SHIFT
	strb	tmp3, [dst], #1
.Loffset_2:
	ands	tmp3, tmp1, MASK_2
	beq	.Lcopy_done
	lsr	tmp3, tmp3, BYTE_2_SHIFT
	strb	tmp3, [dst], #1
.Loffset_3:
	ands	tmp3, tmp1, MASK_3
	beq	.Lcopy_done
	lsr	tmp3, tmp3, BYTE_3_SHIFT
	strb	tmp3, [dst], #1
	ands	tmp3, tmp2, MASK_0
	beq	.Lcopy_done
	lsr	tmp3, tmp3, BYTE_0_SHIFT
	strb	tmp3, [dst], #1
	ands	tmp3, tmp2, MASK_1
	beq	.Lcopy_done
	lsr	tmp3, tmp3, BYTE_1_SHIFT
	strb	tmp3, [dst], #1
	ands	tmp3, tmp2, MASK_2
	beq	.Lcopy_done
	lsr	tmp3, tmp3, BYTE_2_SHIFT
	strb	tmp3, [dst], #1
.Lcopy_done:
	mov	tmp3, #0
	strb	tmp3, [dst]
	pop	{result, tmp1, tmp2, tmp3, src_offset, const_m1, const_h}
	bx	lr

#endif
