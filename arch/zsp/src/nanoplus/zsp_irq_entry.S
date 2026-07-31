/****************************************************************************
 * arch/zsp/src/nanoplus/zsp_irq_entry.S
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

    .section ".itcm", "ax"
    .align  2
    .global _zsp_irq_entry

_zsp_irq_entry:
    /* Entry state:
     *   a7 = task stack - 172 (frame already allocated in UVT entry)
     *   r0 already saved at offset 0
     *   r0 = IRQ number
     */

    /* Save remaining operand regs to task stack frame */

    st32    r2,  a7, 4
    .endgrp
    st32    r4,  a7, 8
    .endgrp
    st32    r6,  a7, 12
    .endgrp
    st32    r8,  a7, 16
    .endgrp
    st32    r10, a7, 20
    .endgrp
    st32    r12, a7, 24
    .endgrp
    st32    r14, a7, 28
    .endgrp
    st32    r16, a7, 32
    .endgrp
    st32    r18, a7, 36
    .endgrp
    st32    r20, a7, 40
    .endgrp
    st32    r22, a7, 44
    .endgrp
    st32    r24, a7, 48
    .endgrp
    st32    r26, a7, 52
    .endgrp
    st32    r28, a7, 56
    .endgrp
    st32    r30, a7, 60
    .endgrp

    /* Save address regs */

    st32    a0, a7, 64
    .endgrp
    st32    a1, a7, 68
    .endgrp
    st32    a2, a7, 72
    .endgrp
    st32    a3, a7, 76
    .endgrp
    st32    a4, a7, 80
    .endgrp
    st32    a5, a7, 84
    .endgrp
    st32    a6, a7, 88
    .endgrp

    /* Save original SP (a7 before frame allocation = current a7 + 172) */

    mov32   r4, a7
    .endgrp
    add32   r4, r4, 172
    .endgrp
    st32    r4, a7, 92
    .endgrp

    /* Save control regs */

    mov32   r4, %tpc
    .endgrp
    st32    r4, a7, 128
    .endgrp
    mov32   r4, %imask
    .endgrp
    st32    r4, a7, 136
    .endgrp

    /* Context saved on task stack. Now switch to interrupt stack.
     * a4 = regs pointer (for C function argument)
     * r0 = IRQ number (already set)
     */

    mov32   a4, a7
    .endgrp
    lda32   a5, _g_intstacktop
    .endgrp
    ld32    r4, a5, 0
    .endgrp
    mov32   a7, r4
    .endgrp

    /* Call C handler: zsp_doirq(irq, regs)
     * r1r0 = irq (32-bit: r1=0, r0=irq_number)
     * a4 = regs pointer
     */

    mov16   r1, 0
    .endgrp
    lda32   a0, _zsp_doirq
    .endgrp
    call    a0
    .endgrp

    /* Return value in a4 = new regs pointer (may differ after ctx switch) */

    mov32   a7, a4
    .endgrp

    /* Restore control regs from (possibly new) frame */

    ld32    r4, a7, 128
    .endgrp
    mov32   %tpc, r4
    .endgrp
    ld32    r4, a7, 136
    .endgrp
    mov32   %imask, r4
    .endgrp

    /* Restore address regs */

    ld32    a0, a7, 64
    .endgrp
    ld32    a1, a7, 68
    .endgrp
    ld32    a2, a7, 72
    .endgrp
    ld32    a3, a7, 76
    .endgrp
    ld32    a4, a7, 80
    .endgrp
    ld32    a5, a7, 84
    .endgrp
    ld32    a6, a7, 88
    .endgrp

    /* Restore operand regs */

    ld32    r0,  a7, 0
    .endgrp
    ld32    r2,  a7, 4
    .endgrp
    ld32    r4,  a7, 8
    .endgrp
    ld32    r6,  a7, 12
    .endgrp
    ld32    r8,  a7, 16
    .endgrp
    ld32    r10, a7, 20
    .endgrp
    ld32    r12, a7, 24
    .endgrp
    ld32    r14, a7, 28
    .endgrp
    ld32    r16, a7, 32
    .endgrp
    ld32    r18, a7, 36
    .endgrp
    ld32    r20, a7, 40
    .endgrp
    ld32    r22, a7, 44
    .endgrp
    ld32    r24, a7, 48
    .endgrp
    ld32    r26, a7, 52
    .endgrp
    ld32    r28, a7, 56
    .endgrp
    ld32    r30, a7, 60
    .endgrp

    /* Restore SP from frame (original task SP or new task SP) */

    ld32    a7, a7, 92
    .endgrp

    /* Use reti here (not br) because we are returning from an interrupt.
     * The interrupt entry hardware saved: PGIE←GIE, GIE←0, PM2←PM, PM←CM.
     * reti correctly restores: GIE←PGIE, CM←PM, EPL←PEPL, PC←%tpc.
     * This differs from fullcontextrestore which uses br for non-interrupt
     * paths where PGIE/PEPL are not hardware-initialized.
     */

    reti
    nop
    .endgrp
