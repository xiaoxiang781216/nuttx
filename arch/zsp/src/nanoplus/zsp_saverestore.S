/****************************************************************************
 * arch/zsp/src/nanoplus/zsp_saverestore.S
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

    .global _zsp_switchcontext
_zsp_switchcontext:
    /* Save the ORIGINAL SP (before frame allocation) for later restore */
    mov32   a6, a7
    .endgrp

    /* Allocate context frame on stack (172 bytes = 43 regs * 4) */
    sub32   a7, a7, 172
    .endgrp

    /* Save callee-saved operand regs to frame */
    st32    r8,  a7, 16
    .endgrp
    st32    r10, a7, 20
    .endgrp
    st32    r12, a7, 24
    .endgrp
    st32    r14, a7, 28
    .endgrp

    /* Save callee-saved address regs */
    st32    a0, a7, 64
    .endgrp
    st32    a1, a7, 68
    .endgrp
    st32    a2, a7, 72
    .endgrp
    st32    a3, a7, 76
    .endgrp

    /* Save ORIGINAL SP (the value before we allocated the frame) */
    st32    a6, a7, 92
    .endgrp

    /* Save %rpc (return address from call instruction) */
    mov32   r4, %rpc
    .endgrp
    st32    r4, a7, 128
    .endgrp

    /* Save %imask */
    mov32   r4, %imask
    .endgrp
    st32    r4, a7, 136
    .endgrp

    /* Store frame pointer into *saveregs (a4 points to &xcp.regs) */
    st32    a7, a4, 0
    .endgrp

    /* Restore new context from a5 */
    mov32   a4, a5
    .endgrp
    br      _zsp_fullcontextrestore
    nop
    .endgrp
