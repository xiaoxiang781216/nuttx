/****************************************************************************
 * libs/libc/elf/tasking-elf.lsl
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
#ifdef CONFIG_ARCH_TC1V8
#  include "tc1v1_8.lsl"
#  define DERIVATIVE tc4
#elif CONFIG_ARCH_TC1V6
#  include "tc1v1_6.lsl"
#  define DERIVATIVE tc3
#endif

#ifndef TEXT
#  define TEXT 0x0
#endif

#ifndef TEXT_SIZE
#  define TEXT_SIZE 64M
#endif

#ifndef DATA
#  define DATA 0x8000000
#endif

#ifndef DATA_SIZE
#  define DATA_SIZE 64M
#endif

#ifndef SECTIONS_ALIGN
#  define SECTIONS_ALIGN CONFIG_LIBC_ELF_SECTION_ALIGNMENT
#endif

#ifndef EXTERN_SYMBOLS
#  define EXTERN_SYMBOLS
#endif

derivative DERIVATIVE
{
    core tc0
    {
#ifdef CONFIG_ARCH_TC1V8
        architecture = TC1V1.8;
#else
        architecture = TC1V1.6;
#endif
        space_id_offset = 100;
        copytable_space = vtc:linear;
    }
    core vtc
    {
#ifdef CONFIG_ARCH_TC1V8
        architecture = TC1V1.8;
#else
        architecture = TC1V1.6;
#endif
        import tc0;
    }

    memory RAM
    {
        mau = 8;
        size = DATA_SIZE;
        type = ram;
        map (dest = bus:tc0:fpi_bus, dest_offset = DATA, size = DATA_SIZE);
        map (dest = bus:vtc:fpi_bus, dest_offset = DATA, size = DATA_SIZE);
    }

    memory ROM
    {
        mau = 8;
        size = TEXT_SIZE;
        type = rom;
        map (dest = bus:tc0:fpi_bus, dest_offset = TEXT, size = TEXT_SIZE);
        map (dest = bus:vtc:fpi_bus, dest_offset = TEXT, size = TEXT_SIZE);
    }

    section_layout :vtc:linear
    {
        group (align = SECTIONS_ALIGN, ordered, contiguous, run_addr = mem:ROM)
        {
            section ".text" (blocksize = 4, attributes = rx, fill=0x0)
            {
              select "(.text.*)";
              select "(.rodata*)";
            }
        }

        group data (align = SECTIONS_ALIGN, ordered, contiguous, run_addr = mem:RAM)
        {
            "_sdata"="_lc_gb_data";
            "_edata"="_lc_ge_data";
            section ".data" (blocksize = 4, attributes = rw);
            {
              select "(.data.*)";
            }
        }

        group bss (align = SECTIONS_ALIGN, ordered, contiguous, run_addr = mem:RAM, fill=0x0)
        {
            section ".bss" (blocksize = 4, attributes = rw, fill=0x0)
            {
              select "(.bss.*)";
            }

#ifdef HEAPSIZE
            "_eheap"="_lc_ge_bss";
            "_sheap"="_eheap" - HEAPSIZE;
            section ".heap" (blocksize = HEAPSIZE, attributes = rw, fill=0x0)
            {
            }
#endif
        }

      EXTERN_SYMBOLS
    }

    section_setup :vtc:linear
    {
        start_address
        (
          symbol = "__start"
        );
        copytable
        (
            align = 4,
            dest = linear,
            table
            {
                symbol = "_lc_ub_table";
                space = :tc0:linear;
            }
        );
    }
}

