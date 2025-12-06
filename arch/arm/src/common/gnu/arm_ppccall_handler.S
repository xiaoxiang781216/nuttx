/****************************************************************************
 * arch/arm/src/common/gnu/arm_ppccall_handler.S
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

#include <arch/syscall.h>

/****************************************************************************
 * File info
 ****************************************************************************/

	.file		"arm_ppccall_handler.S"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_ppccall_handler
 *
 * Description:
 *   This function is both the the user-space and kernel-space ppccall
 *   handler trampoline function. It is called when exit the syscall
 *   SYS_ppccall, this function is called in user-space and kernel-space
 *   depends on the target function located at which space.
 *
 *     R0-R3, R11 - volatile registers need not be preserved.
 *     R4-R10 - static registers must be preserved
 *     R12-R14 - LR and SP must be preserved
 *
 * Input Parameters:
 *   R0 = uintptr_t func
 *   R1 = struct ppccall_parm_s *param
 *
 * Returned Value:
 *   None.  This function does not return in the normal sense.  It returns
 *   via the SYS_ppccall_return (see syscall.h)
 *
 ****************************************************************************/

	.text
#ifdef CONFIG_ARM_THUMB
#  ifdef __ghs__
	.thumb
#  else
	.thumb_func
#  endif
#endif
	.globl	up_ppccall_handler
#ifdef __ghs__
	.type	up_ppccall_handler, $function
#else
	.type	up_ppccall_handler, function
#endif
	.cfi_sections	.debug_frame
	.cfi_startproc
up_ppccall_handler:
	mov		r2, ip
	push		{r2, lr}                     /* Save the pc and lr to stack */
	.cfi_def_cfa_offset 8
	.cfi_offset lr, -4
	.cfi_offset pc, -8

	mov		ip, r0                       /* Save the ppccall function pointer to ip */

	ldr		r2, [r1, #20]                /* Get parm5 from param->parm5 */
	ldr		r3, [r1, #24]                /* Get parm6 from param->parm6 */
	push		{r2, r3}
	.cfi_adjust_cfa_offset 8

	ldr		r3, [r1, #16]                /* Get parm4 from param->parm4 */
	ldr		r2, [r1, #12]                /* Get parm3 from param->parm3 */
	ldr		r0, [r1, #4]                 /* Get parm1 from param->parm1 */
	ldr		r1, [r1, #8]                 /* Get parm2 from param->parm2 */

	blx		ip                           /* Call the stub (modifies lr) */

	pop		{r2, r3}
	.cfi_adjust_cfa_offset -8
	pop		{r2, r3}                     /* Pop pc, lr, r2 = pc, r3 = lr */
	.cfi_adjust_cfa_offset -8
	mov		ip, r2                       /* Restore pc to ip */
	mov		lr, r3                       /* Restore lr */

	mov		r2, r0                       /* R2=Save return value in R2 */
	ldr		r0, =SYS_ppccall_return      /* R0=SYS_syscall_return */
	svc		SYS_syscall                  /* Return from the SYSCALL */

	.cfi_endproc
	.size	up_ppccall_handler, .-up_ppccall_handler
	.end
