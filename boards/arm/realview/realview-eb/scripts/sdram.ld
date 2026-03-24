/****************************************************************************
 * boards/arm/realview/realview-eb/scripts/sdram.ld
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

OUTPUT_ARCH(arm)
EXTERN(__start)
SECTIONS
{
    /* Low vector table at address 0x00000000.
     * On MMU-capable cores the L1 page table occupies 0x00004000-0x00007fff.
     * QEMU loads the kernel at 0x00010000 for -M realview-eb.
     */

    . = 0x00000000;
    .vectors : {
        KEEP(*(.vectors))
    }

    . = 0x00010000;
    .text : {
        _stext = ABSOLUTE(.);
        *(.text .text.*)
        *(.fixup)
        *(.gnu.warning)
        *(.rodata .rodata.*)
        *(.glue_7)
        *(.glue_7t)
        *(.got)
        _etext = ABSOLUTE(.);
    }

    _eronly = ABSOLUTE(.);
    . = ALIGN(4096);

    .data : {
        _sdata = ABSOLUTE(.);
        *(.data .data.*)
        CONSTRUCTORS
        . = ALIGN(4);
        _edata = ABSOLUTE(.);
    }

    .bss : {
        _sbss = ABSOLUTE(.);
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = ABSOLUTE(.);
    }

    /* Stabs debugging sections. */

    .stab 0 : { *(.stab) }
    .stabstr 0 : { *(.stabstr) }
    .stab.excl 0 : { *(.stab.excl) }
    .stab.exclstr 0 : { *(.stab.exclstr) }
    .stab.index 0 : { *(.stab.index) }
    .stab.indexstr 0 : { *(.stab.indexstr) }
    .comment 0 : { *(.comment) }
    .debug_abbrev 0 : { *(.debug_abbrev) }
    .debug_info 0 : { *(.debug_info) }
    .debug_line 0 : { *(.debug_line) }
    .debug_pubnames 0 : { *(.debug_pubnames) }
    .debug_aranges 0 : { *(.debug_aranges) }
}
