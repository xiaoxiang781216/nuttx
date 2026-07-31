/****************************************************************************
 * arch/zsp/src/nanoplus/zsp_uvt.S
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

    .section ".uvt", "ax"
    .align  2
    .global __zsp_uvt

/****************************************************************************
 * UVT vector table for ZSP NanoPlus.
 * Each entry is 16 bytes. All programmable interrupts forward to the
 * unified _zsp_irq_entry handler with the IRQ number in r0.
 ****************************************************************************/

    .org    0x0
__zsp_uvt:
    mov32   a0, 0x40000000
    .endgrp
    br      a0
    nop
    .endgrp

    .org    0x10
_uvt_nmi:
    reti

    .org    0x20
_uvt_dei:
    reti

    .org    0x30
_uvt_int13:
    .endgrp
    br      _zsp_irq_entry {
        sub32   a7, a7, 172
        st32    r0, a7, 0
        mov16   r0, 21
    }

    .org    0x40
_uvt_int12:
    .endgrp
    br      _zsp_irq_entry {
        sub32   a7, a7, 172
        st32    r0, a7, 0
        mov16   r0, 20
    }

    .org    0x50
_uvt_int11:
    .endgrp
    br      _zsp_irq_entry {
        sub32   a7, a7, 172
        st32    r0, a7, 0
        mov16   r0, 19
    }

    .org    0x60
_uvt_int10:
    .endgrp
    br      _zsp_irq_entry {
        sub32   a7, a7, 172
        st32    r0, a7, 0
        mov16   r0, 18
    }

    .org    0x70
_uvt_int9:
    .endgrp
    br      _zsp_irq_entry {
        sub32   a7, a7, 172
        st32    r0, a7, 0
        mov16   r0, 17
    }

    .org    0x80
_uvt_int8:
    .endgrp
    br      _zsp_irq_entry {
        sub32   a7, a7, 172
        st32    r0, a7, 0
        mov16   r0, 16
    }

    .org    0x90
_uvt_int7:
    .endgrp
    br      _zsp_irq_entry {
        sub32   a7, a7, 172
        st32    r0, a7, 0
        mov16   r0, 15
    }

    .org    0xa0
_uvt_timer1:
    .endgrp
    br      _zsp_irq_entry {
        sub32   a7, a7, 172
        st32    r0, a7, 0
        mov16   r0, 22
    }

    .org    0xb0
_uvt_timer0:
    .endgrp
    br      _zsp_irq_entry {
        sub32   a7, a7, 172
        st32    r0, a7, 0
        mov16   r0, 23
    }

    .org    0xc0
_uvt_int6:
    .endgrp
    br      _zsp_irq_entry {
        sub32   a7, a7, 172
        st32    r0, a7, 0
        mov16   r0, 14
    }

    .org    0xd0
_uvt_int5:
    .endgrp
    br      _zsp_irq_entry {
        sub32   a7, a7, 172
        st32    r0, a7, 0
        mov16   r0, 13
    }

    .org    0xe0
_uvt_int4:
    .endgrp
    br      _zsp_irq_entry {
        sub32   a7, a7, 172
        st32    r0, a7, 0
        mov16   r0, 12
    }

    .org    0xf0
_uvt_int3:
    .endgrp
    br      _zsp_irq_entry {
        sub32   a7, a7, 172
        st32    r0, a7, 0
        mov16   r0, 11
    }

    .org    0x100
_uvt_int2:
    .endgrp
    br      _zsp_irq_entry {
        sub32   a7, a7, 172
        st32    r0, a7, 0
        mov16   r0, 10
    }

    .org    0x110
_uvt_int1:
    .endgrp
    br      _zsp_irq_entry {
        sub32   a7, a7, 172
        st32    r0, a7, 0
        mov16   r0, 9
    }

    .org    0x120
_uvt_int0:
    .endgrp
    br      _zsp_irq_entry {
        sub32   a7, a7, 172
        st32    r0, a7, 0
        mov16   r0, 8
    }

    .org    0x130
_uvt_busfault:
    .endgrp
    br      _zsp_irq_entry {
        sub32   a7, a7, 172
        st32    r0, a7, 0
        mov16   r0, 24
    }

    .org    0x140
_uvt_mpufault:
    .endgrp
    br      _zsp_irq_entry {
        sub32   a7, a7, 172
        st32    r0, a7, 0
        mov16   r0, 25
    }

    .org    0x150
_uvt_illfault:
    .endgrp
    br      _zsp_irq_entry {
        sub32   a7, a7, 172
        st32    r0, a7, 0
        mov16   r0, 26
    }

    .org    0x160
_uvt_prifault:
    .endgrp
    br      _zsp_irq_entry {
        sub32   a7, a7, 172
        st32    r0, a7, 0
        mov16   r0, 27
    }

    .org    0x170
_uvt_trap:
    reti
