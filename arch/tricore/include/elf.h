/****************************************************************************
 * arch/tricore/include/elf.h
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

#ifndef __ARCH_TRICORE_INCLUDE_ELF_H
#define __ARCH_TRICORE_INCLUDE_ELF_H

/****************************************************************************
 * Pre-processor Prototypes
 ****************************************************************************/

/* 4.3.1 ELF Identification.  Should have:
 *
 * e_machine         = EM_TRICORE
 * e_ident[EI_CLASS] = ELFCLASS32
 * e_ident[EI_DATA]  = ELFDATA2LSB (little endian) or
 *                     ELFDATA2MSB (big endian)
 */

#define EM_ARCH        EM_TRICORE
#define EF_FLAG        0

/* Table 4-2, TriCore-specific e_flags */

/* 4.7.1 Relocation codes
 *
 * S (when used on its own) is the address of the symbol.
 * A is the addend for the relocation.
 * P is the address of the place being relocated (derived from r_offset).
 * Pa is the adjusted address of the place being relocated, defined as
 *    (P & 0xFFFFFFFC).
 * T is 1 if the target symbol S has type STT_FUNC and the symbol addresses
 *   a Thumb instruction; it is 0 otherwise.
 * B(S) is the addressing origin of the output segment defining the symbol S.
 * GOT_ORG is the addressing origin of the Global Offset Table
 * GOT(S) is the address of the GOT entry for the symbol S.
 */

#define R_TRICORE_NONE        0
#define R_TRICORE_32          1
#define R_TRICORE_PCREL       2
#define R_TRICORE_LO10        3
#define R_TRICORE_HI16        4
#define R_TRICORE_BOL         5
#define R_TRICORE_BOH         6
#define R_TRICORE_CPP32       7
#define R_TRICORE_CPP_PCREL   8
#define R_TRICORE_GOT32       9
#define R_TRICORE_GOTPCREL    10
#define R_TRICORE_PLT32       11
#define R_TRICORE_PLT_PCREL   12

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* ELF register definitions */

/* A0~A15, D0~D15, LCX, FCX, PCX, PSW, PC */

typedef unsigned long elf_gregset_t[37];

/****************************************************************************
 * Public Data
 ****************************************************************************/

#ifdef __cplusplus
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

#undef EXTERN
#ifdef __cplusplus
}
#endif

#endif /* __ARCH_TRICORE_INCLUDE_ELF_H */
