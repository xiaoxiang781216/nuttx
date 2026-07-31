/****************************************************************************
 * arch/zsp/src/nanoplus/zsp_head.S
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

#include <nuttx/config.h>
    .segment "text"
    .align  2
    .global _start
_start:

    /* Set up idle thread stack pointer.
     * g_idle_topstack is a C variable (const uintptr_t), so we need to
     * load its address first, then dereference to get the stack top value.
     */

    lda32   a4, _g_idle_topstack
    .endgrp
    ld32    r0, a4, 0
    .endgrp
    mov32   a7, r0

    /* Clear BSS section */

    lda32   r2, _ebss
    lda32   r0, _sbss
    cmp32   r0, r2
        bc  L_bss_done, nt
    add32   r2, -1
    sub32   r30, r2, r0
    mov32   a4, r0
    shrl32  r30, r30, 2
    movl40s r0, 0
    add32   r30, r30, 1
    mov32   %loop0, r30
    lp0addr L_bss_loop, L_bss_done
L_bss_loop:
    stu32   r0, a4, 4
L_bss_done:

#ifdef CONFIG_BOOT_RUNFROMFLASH
    /* Copy .data from flash (load address) to RAM (exec address) */

    lda32   a0, _zsp_data_initialize
    call    a0
#endif

    /* Enable UVT (user vector table) */

    bits    %smode, 7

    /* Call NuttX entry point */

    lda32   a0, _nx_start
    call    a0

    /* Should never return */

_halt_loop:
    br      _halt_loop
