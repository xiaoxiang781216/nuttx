/****************************************************************************
 * arch/arm/src/arm_a_r/arm_sm_vector.S
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
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2016-2020, Linaro Limited
 * Copyright (c) 2014, STMicroelectronics International N.V.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <arch/irq.h>

#include "arm.h"
#include "sctlr.h"

	.file	"arm_vectorsm.S"

/****************************************************************************
 * Configuration
 ****************************************************************************/

#define SM_EXIT_TO_NON_SECURE       0

/* Layout of struct arm_sm_ctx (mirrored from sm.h).  Under
 * CONFIG_ARCH_FPU the VFP register file sits between the banked set and the
 * general-purpose registers, so the r8/r0 offsets and the struct size
 * differ between the FPU and non-FPU builds.  The banked save/restore loop
 * leaves its advancing pointer exactly at the VFP file, so no separate VFP
 * offset (and no add) is needed.  The r8/r0 offsets serve both worlds (the
 * secure and non-secure per-world layouts are identical): the secure
 * context sits at offset 0, the non-secure context at offset SM_CTX_SIZE
 * (one whole context above sec), so adding SM_CTX_SIZE picks the nsec
 * world.  The C side guards every value here with static_assert in
 * arm_sm_runtime.c.
 */

#ifdef CONFIG_ARCH_FPU
#define SM_CTX_SIZE               392 /* sizeof(struct arm_sm_ctx); nsec offset */
#define SM_CPU_CTX_R8             332 /* offsetof(struct arm_sm_ctx, r8) */
#define SM_CPU_CTX_R0             352 /* offsetof(struct arm_sm_ctx, r0) */
#else
#define SM_CTX_SIZE               128 /* sizeof(struct arm_sm_ctx); nsec offset */
#define SM_CPU_CTX_R8             68  /* offsetof(struct arm_sm_ctx, r8) */
#define SM_CPU_CTX_R0             88  /* offsetof(struct arm_sm_ctx, r0) */
#endif

#define TEESMC_RETURN_ENTRY_DONE             0

/* Issued when returning from "std_smc" or "fast_smc" vector */
#define TEESMC_FUNCID_RETURN_CALL_DONE       1

/* Scheme-B TEE/REE world-switch SMC function IDs.  These MUST match the
 * ARM_SMCCC_NUTTX_* definitions in arch/arm/include/syscall.h.  They are
 * duplicated here as plain hex literals because the C macros expand to
 * expressions containing "UL"-suffixed constants that the GNU assembler
 * cannot evaluate.
 *
 *   value = STD_CALL(0<<31) | SMC32(0<<30) | OWNER_TRUSTED_OS_END(0x3f<<24)
 *           | func_num
 */
#define SM_SMC_TEE_IDLE                      0x3f000001
#define SM_SMC_FOREIGN_INTR                  0x3f000003

/****************************************************************************
 * Assembly Macros
 ****************************************************************************/

.macro save_regs mode
	cps	\mode
	mrs	r2, spsr
	str	r2, [r0], #4
	str	sp, [r0], #4
	str	lr, [r0], #4
.endm

.macro restore_regs mode
	cps	\mode
	ldr	r2, [r0], #4
	ldr	sp, [r0], #4
	ldr	lr, [r0], #4
	msr	spsr_fsxc, r2
.endm

/****************************************************************************
 * Name: mov_imm
 *
 * Parameter:
 *   reg - the dest register to contains the immediate value
 *   val - the immediate value
 *
 * Description:
 *   Move the immediate value to the register, but this macro can handle the
 *   case that when the immediate value is larger than 2^16
 *
 ****************************************************************************/

.macro mov_imm reg, val
	.if ((\val) & 0xffff0000) == 0
		movw	\reg, #(\val)
	.else
		movw	\reg, #((\val) & 0xffff)
		movt	\reg, #((\val) >> 16)
	.endif
.endm

/****************************************************************************
 * Public Functions
 ****************************************************************************/

	.text
	.syntax	unified
	.arm

/****************************************************************************
 * Name: arm_sm_vect_start
 *
 * Description:
 *   Secure monitor vector initialization block
 ****************************************************************************/

	.local	arm_sm_vect_start
	.balign 32

arm_sm_vect_start:
	ldr		pc, .			/* 0x00: Reset */
	ldr		pc, .			/* 0x04: Undefined instruction */
	ldr		pc, .Lsm_smchandler	/* 0x08: Secure monitor call */
	ldr		pc, .			/* 0x0c: Prefetch abort */
	ldr		pc, .			/* 0x10: Data abort */
	ldr		pc, .			/* 0x14: Reserved */
	ldr		pc, .Lsm_irqhandler	/* 0x18: IRQ */
	ldr		pc, .Lsm_fiqhandler	/* 0x1c: FIQ */

	.local	arm_sm_vectorsmc
	.local	arm_sm_vectorirq
	.local	arm_sm_vectorfiq

.Lsm_smchandler:
	.long	arm_sm_vectorsmc
.Lsm_irqhandler:
	.long	arm_sm_vectorirq
.Lsm_fiqhandler:
	.long	arm_sm_vectorfiq

	.size	arm_sm_vect_start, . - arm_sm_vect_start
	.align	4

/****************************************************************************
 * Name: arm_sm_save_ctx
 ****************************************************************************/

/* void arm_sm_save_ctx(struct arm_sm_ctx *ctx);
 *
 * "ctx" points at struct arm_sm_ctx, whose banked-mode registers are the
 * first members, so r0 doubles as the per-world context base.  Under
 * CONFIG_ARCH_FPU this function ALSO swaps the VFP/NEON register file: the
 * VFP file is a shared hardware resource that is NOT banked by security
 * state, so it must be saved on every world switch alongside the banked
 * integer registers (mirroring what arm64 TFA does via simd_ctx_save under
 * CTX_INCLUDE_FPREGS).  The VFP file follows the banked set directly, so the
 * banked save loop leaves r0 pointing right at ctx->vfp and it is streamed
 * out from there with no offset.  It is per-CPU because the whole arm_sm_ctx
 * lives on the per-CPU monitor stack.
 */

	.globl	arm_sm_save_ctx
	.type	arm_sm_save_ctx, %function

arm_sm_save_ctx:
	/* User mode registers has to be saved from system mode */

	cps	#PSR_MODE_SYS
	str	sp, [r0], #4
	str	lr, [r0], #4

	save_regs	#PSR_MODE_IRQ
	save_regs	#PSR_MODE_FIQ
	save_regs	#PSR_MODE_SVC
	save_regs	#PSR_MODE_ABT
	save_regs	#PSR_MODE_UND

	cps	#PSR_MODE_MON

#ifdef CONFIG_ARCH_FPU
	/* Save the live VFP register file into ctx->vfp.  The banked loop above
	 * left r0 pointing exactly at ctx->vfp (it immediately follows the
	 * banked set), so no offset recomputation is needed.  Executed in
	 * monitor mode, where FPEXC.EN is left set by arm_fpuconfig so VFP
	 * access is permitted.  Clobbers r0, r1.
	 */

#ifdef CONFIG_ARM_DPFPU32
	vstmia.64	r0!, {d0-d15}
	vstmia.64	r0!, {d16-d31}
#else
	vstmia		r0!, {s0-s31}
#endif
	vmrs		r1, fpscr
	str		r1, [r0]
#endif

	bx	lr

	.size	arm_sm_save_ctx, . - arm_sm_save_ctx
	.align	4

/****************************************************************************
 * Name: arm_sm_restore_ctx
 ****************************************************************************/

/* void arm_sm_restore_ctx(struct arm_sm_ctx *ctx);
 *
 * Restores the mode specific registers, and (under CONFIG_ARCH_FPU) the
 * VFP/NEON register file from ctx->vfp, mirroring arm_sm_save_ctx.
 * The monitor C dispatcher emits no VFP instructions, so overwriting the
 * AAPCS callee-saved d8-d15 here is harmless.
 */

	.globl	arm_sm_restore_ctx
	.type	arm_sm_restore_ctx, #function

arm_sm_restore_ctx:
	/* User mode registers has to be saved from system mode */

	cps	#PSR_MODE_SYS
	ldr	sp, [r0], #4
	ldr	lr, [r0], #4

	restore_regs	#PSR_MODE_IRQ
	restore_regs	#PSR_MODE_FIQ
	restore_regs	#PSR_MODE_SVC
	restore_regs	#PSR_MODE_ABT
	restore_regs	#PSR_MODE_UND

	cps	#PSR_MODE_MON

#ifdef CONFIG_ARCH_FPU
	/* Restore the VFP register file from ctx->vfp.  As in the save above,
	 * the banked loop already advanced r0 to ctx->vfp, so no offset is
	 * needed.  See the matching save for the FPEXC/clobber notes.
	 */

#ifdef CONFIG_ARM_DPFPU32
	vldmia.64	r0!, {d0-d15}
	vldmia.64	r0!, {d16-d31}
#else
	vldmia		r0!, {s0-s31}
#endif
	ldr		r1, [r0]
	vmsr		fpscr, r1
#endif

	bx	lr

	.size	arm_sm_restore_ctx, . - arm_sm_restore_ctx
	.align	4

/****************************************************************************
 * Name: arm_sm_vectorsmc
 ****************************************************************************/

/* the arm_sm_vectorsmc is called at "smc #0" is invoked. As each world can
 * trigger the smc call, so this method internal will detect which world
 * we come from:
 * if come from non-secure world, will save the non-secure
 * context, and restore the secure world context, and then call the
 * "arm_sm_from_nsec" to handle the following work, and after "arm_sm_from_nsec"
 * method is processing finished, will take the responsibility to return
 * to the non-secure world by calling "smc #0";
 * if come from secure world, will save the secure world context, and then
 * restore the non-secure world context, and then just perform "rfefd" to
 * exit the smc exception, and the non-secure world will resume, continue
 * running.
 * The per-CPU g_monitor buffer holds the sec/nsec context pair at its top
 * (the monitor SP is initialised to the top of that pair) and the C
 * dispatcher's stack below it. As several different CPU modes are using the
 * same stack
 * it's important that switch of CPU mode isn't done until one mode is done.
 * This means FIQ, IRQ and Async abort has to be masked while using it.
 */

	.local	arm_sm_vectorsmc
	.type	arm_sm_vectorsmc, %function

arm_sm_vectorsmc:
	srsdb	sp!, #PSR_MODE_MON
	push	{r0-r7}

	/* Clear the exclusive monitor */

	clrex

	/* Find out if we're doing an secure or non-secure entry */

	mrc	CP15_SCR(r1)
	tst	r1, #SCR_NS
	bne	.smc_from_nsec

	/* As we're coming from secure world (NS bit cleared) the stack
	 * pointer points to arm_sm_ctx.sec.r0 at this stage. After the
	 * instruction below the stack pointer points to arm_sm_ctx.
	 */

	sub	sp, sp, #SM_CPU_CTX_R0

	/* Save secure r8-r12 (not covered by push {r0-r7}) so a secure
	 * context can be fully preserved and later resumed at its exact
	 * interruption point.
	 */

	add	r0, sp, #SM_CPU_CTX_R8
	stm	r0, {r8-r12}

	/* Decode the SMC function id in secure r0.  The scheme-B TEE-side
	 * world-switch SMCs (TEE_IDLE / FOREIGN_INTR) are serviced by the
	 * C dispatcher arm_sm_from_sec.  The only other SMC the TEE issues to
	 * the monitor is CALL_DONE: the TEE has finished servicing an REE
	 * request (forwarded by arm_sm_from_nsec, re-entered via arm_vectorsmc)
	 * and returns the result to the REE through the inline path below.
	 */

	ldr	r0, [sp, #SM_CPU_CTX_R0]
	mov_imm	r9, SM_SMC_TEE_IDLE
	cmp	r0, r9
	beq	.smc_from_sec
	mov_imm	r9, SM_SMC_FOREIGN_INTR
	cmp	r0, r9
	beq	.smc_from_sec

	/* CALL_DONE return path: the TEE finished servicing an REE SMC. */

	/* Save secure context */

	mov	r0, sp

	/* the arm_sm_save_ctx() function will execute "cps #PSR_MODE_MON"
	 * and help switch to monitor mode.  Under CONFIG_ARCH_FPU it also saves
	 * the secure VFP register file into ctx->sec.vfp, keeping the VFP swap
	 * paired with the banked-register swap.
	 */
	bl	arm_sm_save_ctx

	/* Shift r1-r4 from the secure context into r0-r3 in the non-secure
	 * context: these are the SMC return values handed back to the REE
	 * caller.
	 */

	add	r8, sp, #SM_CPU_CTX_R0
	ldm	r8, {r0-r4}
	add	r8, sp, #(SM_CTX_SIZE + SM_CPU_CTX_R0)
	stm	r8, {r1-r4}

	/* Restore non-secure context (also restores ctx->nsec.vfp under
	 * CONFIG_ARCH_FPU).
	 */

	add	r0, sp, #SM_CTX_SIZE
	bl	arm_sm_restore_ctx

	b	.sm_ret_to_nsec

.smc_from_sec:
	/* Scheme-B TEE-side SMC (TEE_IDLE / FOREIGN_INTR).  The C dispatcher
	 * performs the banked-register swap for the non-secure exit; on a
	 * secure exit (TEE_IDLE with no preempted REE) it leaves banked
	 * registers untouched so the TEE can WFI.
	 */

	bl	arm_sm_from_sec
	cmp	r0, #SM_EXIT_TO_NON_SECURE
	beq	.sm_ret_to_nsec

	/* Stay in the secure world: restore secure r8-r12 and return to the
	 * TEE (which will WFI).  ctx->sec.r0 already holds the return value.
	 */

	add	r0, sp, #SM_CPU_CTX_R8
	ldm	r0, {r8-r12}
	add	sp, sp, #SM_CPU_CTX_R0
	b	.sm_exit

.sm_ret_to_nsec:
	/* Return to non-secure world */

	add	r0, sp, #(SM_CTX_SIZE + SM_CPU_CTX_R8)
	ldm	r0, {r8-r12}

	/* Update SCR */

	mrc	CP15_SCR(r0)
	bic	r0, r0, #SCR_IRQ /* Clear IRQ bit in SCR */
	orr	r0, r0, #(SCR_NS | SCR_FIQ) /* Set NS and FIQ bit in SCR */
	mcr	CP15_SCR(r0)

	/* isb not needed since we're doing an exception return below
	 * without dependency to the changes in SCR before that.
	 */

	add	sp, sp, #(SM_CTX_SIZE + SM_CPU_CTX_R0)
	b	.sm_exit

.smc_from_nsec:
	/* As we're coming from non-secure world (NS bit set) the stack
	 * pointer points to arm_sm_ctx.nsec.r0 at this stage. After the
	 * instruction below the stack pointer points to arm_sm_ctx.
	 */

	sub	sp, sp, #(SM_CTX_SIZE + SM_CPU_CTX_R0)

	/* Update SCR for the secure world we may continue into.  Scheme B:
	 * the secure world takes every interrupt locally, so both FIQ (its
	 * own Group 0 interrupts) and IRQ (foreign Group 1 interrupts) stay
	 * in the secure world.  A foreign IRQ is then routed to the TEE's own
	 * vector, which cooperatively yields via ARM_SMCCC_NUTTX_FOREIGN_INTR.
	 */

	bic	r1, r1, #(SCR_NS | SCR_FIQ | SCR_IRQ)
	mcr	CP15_SCR(r1)
	isb

	add	r0, sp, #(SM_CTX_SIZE + SM_CPU_CTX_R8)
	stm	r0, {r8-r12}

	bl	arm_sm_from_nsec
	cmp	r0, #SM_EXIT_TO_NON_SECURE
	beq	.sm_ret_to_nsec

	/* Continue into secure world.  Restore secure r8-r12 (saved when the
	 * TEE was preempted) so it resumes at its exact interruption point.
	 */

	add	r0, sp, #SM_CPU_CTX_R8
	ldm	r0, {r8-r12}

	add	sp, sp, #SM_CPU_CTX_R0

.sm_exit:
	pop	{r0-r7}
	rfefd	sp!

	.size	arm_sm_vectorsmc, . - arm_sm_vectorsmc
	.align	4

/****************************************************************************
 * Name: arm_sm_vectorfiq
 ****************************************************************************/

/* FIQ handling
 *
 * Saves CPU context in the same way as arm_sm_vectorsmc() above. The CPU
 * context will later be restored by arm_sm_vectorsmc() when handling a return
 * from FIQ.
 *
 * When cpu is running in non-secure mode, then there are fiq is triggered
 * in secure mode, then the non-secure mode will need to transfer this fiq
 * to the secure mode, in order to implement this, this fiq will trapped into
 * monitor mode, and the monitor mode will invoke the arm_sm_vectorfiq func to
 * handle this fiq. The arm_sm_vectorfiq will save the non-secure context, and
 * then restore the non-secure context, and then exit current monitor mode,
 * and then will running the secure mode, then the secure mode will handle
 * this fiq
 */

	.local	arm_sm_vectorfiq
	.type	arm_sm_vectorfiq, %function

arm_sm_vectorfiq:
	/* FIQ has a +4 offset for lr compared to preferred return address */

	sub	lr, lr, #4
	/* sp points just past struct arm_sm_ctx */

	srsdb	sp!, #PSR_MODE_MON
	push	{r0-r7}

	clrex /* Clear the exclusive monitor */

	/*
	 * As we're coming from non-secure world the stack pointer points
	 * to arm_sm_ctx.nsec.r0 at this stage. After the instruction below the
	 * stack pointer points to arm_sm_ctx.
	 */

	sub	sp, sp, #(SM_CTX_SIZE + SM_CPU_CTX_R0)

	/* Update SCR for the secure world: all interrupts local (Scheme B).
	 * The secure world takes both its own FIQs (Group 0) and foreign
	 * IRQs (Group 1) locally.
	 */

	mrc	CP15_SCR(r1)
	bic	r1, r1, #(SCR_NS | SCR_FIQ | SCR_IRQ)
	mcr	CP15_SCR(r1)
	isb

	/* Save non-secure context (the preempted REE).  Under CONFIG_ARCH_FPU
	 * this also saves the preempted REE's VFP register file into
	 * ctx->nsec.vfp, keeping the VFP swap paired with the banked-register
	 * swap on every world switch.
	 */

	add	r0, sp, #SM_CTX_SIZE
	bl	arm_sm_save_ctx
	add	r0, sp, #(SM_CTX_SIZE + SM_CPU_CTX_R8)
	stm	r0, {r8-r12}

	/* A secure FIQ (Group 0) preempted the REE; switch to the TEE to
	 * handle it.  No preemption flag is needed for this direction: the
	 * TEE is not the sleeper, so once it goes idle ARM_SMCCC_NUTTX_TEE_IDLE
	 * unconditionally switches back to this saved REE context.
	 */

	/* Restore secure context and resume the TEE at its real PC (also
	 * restores ctx->sec.vfp under CONFIG_ARCH_FPU).
	 */

	mov	r0, sp
	bl	arm_sm_restore_ctx

	add	r0, sp, #SM_CPU_CTX_R8
	ldm	r0, {r8-r12}

	/* Return to the secure world at the TEE's interrupted PC.  The
	 * pending secure FIQ fires through the TEE's own vector once
	 * interrupts are re-enabled by the rfefd below.
	 */

	add	sp, sp, #SM_CPU_CTX_R0
	pop	{r0-r7}
	rfefd	sp!

	.size	arm_sm_vectorfiq, . - arm_sm_vectorfiq
	.align	4

/****************************************************************************
 * Name: arm_sm_vectorirq
 ****************************************************************************/

/* IRQ handling
 *
 * Saves CPU context in the same way as arm_sm_vectorsmc() above. The CPU
 * context will be saved and then restore non-secure context to jump into non-secure
 * mode to handle IRQ.
 *
 * When cpu is running in secure mode, then there are non-secure interrupt
 * irq is triggered in secure mode, then the secure mode will need to transfer
 * this irq to the non-secure mode. In order to implement this, this irq will first
 * trapped into monitor mode, and the monitor mode will save the secure context.
 * And then restore the non-secure context, exit current monitor mode, Finially cpu is
 * running the non-secure mode, and this irq will be handled in non-secure mode.
 */

	.local  arm_sm_vectorirq
	.type	arm_sm_vectorirq, %function
arm_sm_vectorirq:
	/* IRQ has a +4 offset for lr compared to preferred return address */

	sub	lr, lr, #4

	srsdb	sp!, #PSR_MODE_MON
	push	{r0-r7}

	clrex /* Clear the exclusive monitor */

	/* As we're coming from secure world (NS bit cleared) the stack
	 * pointer points to arm_sm_ctx.sec.r0 at this stage. After the
	 * instruction below the stack pointer points to arm_sm_ctx.
	 */

	sub	sp, sp, #SM_CPU_CTX_R0

	/* Save secure context */

	mov	r0, sp

	/* the arm_sm_save_ctx() function will execute "cps #PSR_MODE_MON"
	 * and help switch to monitor mode
	 */
	bl	arm_sm_save_ctx

	/* Restore non-secure context */

	add	r0, sp, #SM_CTX_SIZE
	bl	arm_sm_restore_ctx

	/* Return to non-secure world */

	add	r0, sp, #(SM_CTX_SIZE + SM_CPU_CTX_R8)
	ldm	r0, {r8-r12}

	/* Update SCR */

	mrc	CP15_SCR(r0)
	orr	r0, r0, #(SCR_NS | SCR_FIQ) /* Set NS and FIQ bit in SCR */
	bic r0, r0, #SCR_IRQ /* Clear IRQ bit in SCR. */
	mcr	CP15_SCR(r0)

	/* isb not needed since we're doing an exception return below
	 * without dependency to the changes in SCR before that.
	 */

	add	sp, sp, #(SM_CTX_SIZE + SM_CPU_CTX_R0)
	pop	{r0-r7}
	rfefd	sp!

    .size   arm_sm_vectorirq, . - arm_sm_vectorirq
    .align  4

/****************************************************************************
 * Name: arm_vectorsmc
 ****************************************************************************/

/* this function is declared in sm.h:
 * void arm_vectorsmc(uint32_t a0, uint32_t a1, uint32_t a2,
 *                    uint32_t a3, uint32_t a4, uint32_t a5,
 *                    uint32_t a6, uint32_t a7);
 * There are two ways to switch from REE to TEE, SMC exception and FIQ exception,
 * if we use FIQ exception to trap into the MONITOR mode, then before enter to
 * TEE mode, need to setup the ctx->sec.mon_lr as "arm_vectorfiq_entry", then the
 * "arm_vectorfiq_entry" is using as the entry to the TEE mode;
 * if we use SMC exception to trap into the MONITOR mode, then before enter to
 * TEE mode, need to setup the ctx->sec.mon_lr as "arm_vectorsmc", then
 * the "arm_vectorsmc" is using as the entry to the TEE mode;
 * the "arm_vectorsmc" is working same as "arm_vectorfiq_entry"
 */

	.global	arm_vectorsmc
	.type	arm_vectorsmc, %function

arm_vectorsmc:
	mov	r1, r0
	ldr	r0, =TEESMC_FUNCID_RETURN_CALL_DONE
	smc	#0
	/* SMC should not return */

	b	.

	.size	arm_vectorsmc, . - arm_vectorsmc
	.align	4

/****************************************************************************
 * Name: arm_vectorfiq_entry
 ****************************************************************************/

/* this function is declared in sm.h:
 * void arm_vectorfiq_entry(uint32_t a0, uint32_t a1, uint32_t a2,
 *                          uint32_t a3, uint32_t a4, uint32_t a5,
 *                          uint32_t a6, uint32_t a7);
 *
 * Legacy FIQ trampoline.  Under Scheme B the monitor no longer redirects a
 * preempted TEE through this stub (arm_sm_vectorfiq resumes the TEE at its
 * real PC instead), so this is effectively dead code.  Kept as a safe
 * no-op that yields to the non-secure world should it ever be entered.
 */

	.local	arm_vectorfiq_entry
	.type	arm_vectorfiq_entry, %function

arm_vectorfiq_entry:
	mov_imm	r0, SM_SMC_FOREIGN_INTR
	smc	#0
	bx	lr

	.size	arm_vectorfiq_entry, . - arm_vectorfiq_entry
	.align	4

/****************************************************************************
 * Name: arm_sm_init_stack
 ****************************************************************************/

/* this function is declared in sm.h
 * void arm_sm_init_stack(int stack);
 *
 * this function is using to init the monitor runtime
 * this function will write the adress of arm_sm_vect_start
 * to the mvbar register
 */

	.globl	arm_sm_init_stack
	.type	arm_sm_init_stack, %function

arm_sm_init_stack:
	/* Set monitor stack */

	mrs	r1, cpsr
	cps	#PSR_MODE_MON

	/* Point just beyond arm_sm_ctx.sec */

	sub	sp, r0, #0
	msr	cpsr, r1

	/* Set monitor vector (MVBAR) */

1:
	ldr	r0, =arm_sm_vect_start
2:
	mcr	CP15_MVBAR(r0)

	bx	lr

	.size	arm_sm_init_stack, . - arm_sm_init_stack
	.align	4
