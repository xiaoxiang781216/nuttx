/****************************************************************************
 * arch/zsp/src/nanoplus/zsp_fullcontextrestore.S
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
 * Public Symbols
 ****************************************************************************/

    .globl _zsp_fullcontextrestore

/****************************************************************************
 * Name: zsp_fullcontextrestore
 *
 * Description:
 *   Restore the full context from the provided register save area and
 *   jump to the saved PC via br (direct branch), NOT reti.
 *
 *   Why not reti:
 *   Per ZSP Programmers Manual, reti performs four automatic operations:
 *     1. %smode<PM,CM> ← %smode<PM2,PM>  (restore execution mode)
 *     2. %imask<gie>   ← %imask<pgie>    (restore interrupt enable)
 *     3. %ip0<epl>     ← %ip0<pepl>      (restore execution priority)
 *     4. %pc           ← %tpc            (jump to return address)
 *
 *   Operations 1-3 rely on PGIE/PEPL/PM2 having been saved by a prior
 *   interrupt entry. In non-interrupt paths (cooperative context switch
 *   via zsp_switchcontext, up_exit, signal delivery), these fields are
 *   NOT initialized by hardware. Using reti would restore GIE from an
 *   undefined PGIE value, causing unpredictable interrupt state.
 *
 *   The BES 3601P ZSP SDK uses the same approach: vPortStartFirstTask
 *   (port_asm.S) uses "br" for initial task dispatch, while the unified
 *   ISR handler (_zsp_unified_irs_hdl) uses reti for interrupt return
 *   where hardware guarantees correct PGIE/PEPL values.
 *
 *   Our design:
 *   - fullcontextrestore (this function): mov32 %imask + br target_pc
 *   - _zsp_irq_entry (interrupt return):  mov32 %imask + reti
 *
 * Input:
 *   a4 = pointer to uint32_t regs[XCPTCONTEXT_REGS]
 *
 ****************************************************************************/

    .text
    .align 2
_zsp_fullcontextrestore:

    /* Restore data registers r0-r15 (offsets 0-28, pairs as 32-bit) */

    ld32    r0, a4, 0       /* REG_R0R1  (offset 0*4) */
    ld32    r2, a4, 4       /* REG_R2R3  (offset 1*4) */
    ld32    r4, a4, 8       /* REG_R4R5  (offset 2*4) */
    ld32    r6, a4, 12      /* REG_R6R7  (offset 3*4) */
    ld32    r8, a4, 16      /* REG_R8R9  (offset 4*4) */
    ld32    r10, a4, 20     /* REG_R10R11 (offset 5*4) */
    ld32    r12, a4, 24     /* REG_R12R13 (offset 6*4) */
    ld32    r14, a4, 28     /* REG_R14R15 (offset 7*4) */

    /* Restore address registers a0-a3 (offsets 64-76) */

    ld32    a0, a4, 64      /* REG_A0 (offset 16*4) */
    ld32    a1, a4, 68      /* REG_A1 (offset 17*4) */
    ld32    a2, a4, 72      /* REG_A2 (offset 18*4) */
    ld32    a3, a4, 76      /* REG_A3 (offset 19*4) */

    /* Load target PC into a6 (temporary) for later jump */

    ld32    a6, a4, 128     /* REG_PC (offset 32*4) */

    /* Restore SP (offset 92 = 23*4) */

    ld32    a7, a4, 92      /* REG_SP */

    /* Restore a5 (offset 84) */

    ld32    a5, a4, 84      /* REG_A5 (offset 21*4) */

    /* Restore %imask last - this enables GIE, so must be done after
     * all other setup is complete. We use a caller-saved register (r16)
     * which was already restored above from the r16-r30 range.
     * Since we need r16 as scratch here, reload it after imask restore.
     */

    ld32    r16, a4, 136    /* REG_IMASK (offset 34*4) */
    .endgrp
    mov32   %imask, r16     /* Restore full %imask including GIE */
    .endgrp

    /* Jump to target PC. Use br with delay slot to restore a6 and a4.
     * After this point a6 holds the jump target, we need to restore
     * the real a6 from the frame.
     */

    br      a6 {
        ld32    a6, a4, 88  /* REG_A6 (offset 22*4) - restore real a6 */
        ld32    a4, a4, 80  /* REG_A4 (offset 20*4) - restore real a4 */
    }
