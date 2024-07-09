/****************************************************************************
 * arch/x86_64/src/common/multiboot1.S
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

/* Multiboot1 NuttX Naive Loader */

.set MB_FLAG_ALIGNED,   1        /* All boot modules must be loaded aligned */
.set MB_FLAG_MEMINFO,   2        /* Boot with memory maps passing  */
.set MB_FLAG_VIDEO,     4        /* Enable video mode  */

.set MB_FLAGS,          MB_FLAG_ALIGNED | MB_FLAG_MEMINFO
.set MB_MAGIC,          0x1BADB002
.set MB_CHECKSUM,       -(MB_MAGIC + MB_FLAGS)

.section                ".multiboot", "a"
header:
.align 4
    .long               MB_MAGIC
    .long               MB_FLAGS
    .long               MB_CHECKSUM

    .long               0       /* header_addr  */
    .long               0       /* load_addr */
    .long               0       /* load_end_addr */
    .long               0       /* bss_end_addr */
    .long               0       /* entry_addr */

    .long               0       /* Video mode type */
    .long               1024    /* Video width */
    .long               768     /* Video height */
    .long               32      /* Video depth */

/* NuttX bin */

.section                ".bin" , "ax"
.globl bin_start
bin_start:
    .incbin             "../../../nuttx.bin"
