/****************************************************************************
 * arch/arm/src/arm_a_r/arm_vectortab.S
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

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

	.file	"arm_vectortab.S"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Public Symbols
 ****************************************************************************/

#ifdef CONFIG_ARM_TOOLCHAIN_ARMCLANG
	.eabi_attribute Tag_ABI_align_preserved, 1
#endif

	.syntax	unified
	.arm

	.globl		_vector_start
	.globl		_vector_end

/****************************************************************************
 * Assembly Macros
 ****************************************************************************/

/****************************************************************************
 * Name: _vector_start
 *
 * Description:
 *   Vector initialization block
 ****************************************************************************/

	.section	.vectors, "ax"
	.globl		_vector_start

/* These will be relocated to VECTOR_BASE. */

_vector_start:
	ldr		pc, .Lresethandler		/* 0x00: Reset */
	ldr		pc, .Lundefinedhandler		/* 0x04: Undefined instruction */
	ldr		pc, .Lsvchandler		/* 0x08: Software interrupt */
	ldr		pc, .Lprefetchaborthandler	/* 0x0c: Prefetch abort */
	ldr		pc, .Ldataaborthandler		/* 0x10: Data abort */
	ldr		pc, .Laddrexcptnhandler		/* 0x14: Address exception (reserved) */
	ldr		pc, .Lirqhandler		/* 0x18: IRQ */
#ifdef CONFIG_ARCH_HIPRI_INTERRUPT
	/* 0x1c: FIQ - inline minimal-context handler directly at vector.
	 * FIQ is the last vector entry, so we can place code here without
	 * colliding with other vectors.  This eliminates one branch/load
	 * from the FIQ entry path.
	 */

	sub		r8, lr, #4			/* r8_fiq = return PC (bl clobbers lr_fiq) */
	stmdb		sp!, {r0-r3}			/* Save caller-saved only */
#ifdef CONFIG_ARCH_FPU
	vmrs		r0, fpscr			/* Save FPSCR */
	stmdb		sp!, {r0}
#ifdef CONFIG_ARM_DPFPU32
	vstmdb.64	sp!, {d16-d31}			/* Save caller-saved d16-d31 */
#endif
	vstmdb.64	sp!, {d0-d7}			/* Save caller-saved d0-d7 (s0-s15) */
#endif
	mov		r9, sp				/* r9_fiq = save unaligned SP */
	bic		sp, sp, #7			/* 8-byte align for C ABI */
	bl		arm_decodefiq			/* Lightweight C handler */
	mov		sp, r9				/* Restore unaligned SP */
#ifdef CONFIG_ARCH_FPU
	vldmia.64	sp!, {d0-d7}			/* Restore caller-saved d0-d7 (s0-s15) */
#ifdef CONFIG_ARM_DPFPU32
	vldmia.64	sp!, {d16-d31}			/* Restore caller-saved d16-d31 */
#endif
	ldmia		sp!, {r0}
	vmsr		fpscr, r0			/* Restore FPSCR */
#endif
	ldmia		sp!, {r0-r3}			/* Restore r0-r3 */
	movs		pc, r8				/* Return: CPSR = SPSR_fiq */
#elif defined(CONFIG_ARCH_TRUSTZONE_SECURE)
	ldr		pc, .Lfiqhandler		/* 0x1c: FIQ */
#else
	subs		pc, lr, #4			/* 0x1c: FIQ - just return */
#endif

	.globl	__start
	.globl	arm_vectorundefinsn
	.globl	arm_vectorsvc
	.globl	arm_vectorprefetch
	.globl	arm_vectordata
	.globl	arm_vectoraddrexcptn
	.globl	arm_vectorirq
#if defined(CONFIG_ARCH_TRUSTZONE_SECURE)
	.globl	arm_vectorfiq
#endif

.Lresethandler:
	.long	__start
.Lundefinedhandler:
	.long	arm_vectorundefinsn
.Lsvchandler:
	.long	arm_vectorsvc
.Lprefetchaborthandler:
	.long	arm_vectorprefetch
.Ldataaborthandler:
	.long	arm_vectordata
.Laddrexcptnhandler:
	.long	arm_vectoraddrexcptn
.Lirqhandler:
	.long	arm_vectorirq
#if defined(CONFIG_ARCH_TRUSTZONE_SECURE)
.Lfiqhandler:
	.long	arm_vectorfiq
#endif

	.globl	_vector_end
_vector_end:
	.size	_vector_start, . - _vector_start

/****************************************************************************
 * Name: _hyp_vector_start
 *
 * Description:
 *   Vector initialization block (ARMv7+ only, requires HYP mode support)
 ****************************************************************************/

#if __ARM_ARCH >= 7

	.section	.hyp_vectors, "ax"
	.globl		_hyp_vector_start

_hyp_vector_start:
	ldr		pc, .Lhypresethandler	/* 0x00: Reset */
	b		$+0x0					/* 0x04: Undefined instruction */
	b		$+0x0					/* 0x08: Software interrupt */
	b		$+0x0					/* 0x0c: Prefetch abort */
	b		$+0x0					/* 0x10: Data abort */
	b		$+0x0					/* 0x14: Address exception (reserved) */
	b		$+0x0					/* 0x18: IRQ */
	b		$+0x0					/* 0x1c: FIQ */

	.globl	__start

.Lhypresethandler:
	.long	__start

_hyp_vector_end:
	.size	_hyp_vector_start, . - _hyp_vector_start

#endif /* __ARM_ARCH >= 7 */
	.end
