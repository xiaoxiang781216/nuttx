/****************************************************************************
 * arch/tricore/src/common/tricore_vectors.c
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

#include <nuttx/macro.h>

#include <arch/irq.h>

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/* TriCore Interrupt Vector Table (BIV)
 *
 * Unlike ARM/RISC-V which store function pointers, TriCore's interrupt
 * vector table contains executable code directly. Each entry is a 32-byte
 * (0x20) aligned slot holding a small code sequence that saves context
 * and jumps to the actual handler.
 *
 * Entry layout (SRPN = Service Request Priority Number):
 *   SRPN 1 (lowest priority)  -> tricore_irq_sched (context switch)
 *   SRPN 2 ~ MAX (all others) -> tricore_irq_direct     (fast dispatch)
 */

#ifdef CONFIG_TRICORE_TOOLCHAIN_TASKING

__asm__ (
    ".sdecl '.text.inttable',code,protect\n\t"
    ".sect  '.text.inttable'\n\t"
    ".extern tricore_irq_sched\n\t"
    ".extern tricore_irq_direct\n\t"
    ".align 32\n\t"

    "svlcx\n\t"
    "movh.a a15,#@his(tricore_irq_sched)\n\t"
    "lea    a15,[a15]@los(tricore_irq_sched)\n\t"
    "jli     a15\n\t"
    "rslcx\n\t"
    "rfe\n"
    ".align 32\n\t"

    ".DUP " STRINGIFY(TRICORE_IRQ_MAX_PRIORITY - 1) "\n\t"
    "svlcx\n\t"
    "movh.a a15,#@his(tricore_irq_direct)\n\t"
    "lea    a15,[a15]@los(tricore_irq_direct)\n\t"
    "jli     a15\n\t"
    "rslcx\n\t"
    "rfe\n"
    ".align 32\n\t"
    ".ENDM\n"
);

#else /* GCC */

__asm__ (
    ".pushsection .inttable,\"ax\",@progbits\n\t"
    ".align 5\n\t"

    "svlcx\n\t"
    "movh.a %a15, hi:tricore_irq_sched\n\t"
    "lea    %a15, [%a15]lo:tricore_irq_sched\n\t"
    "jli    %a15\n\t"
    "rslcx\n\t"
    "rfe\n\t"
    ".align 5\n"

    ".rept " STRINGIFY(TRICORE_IRQ_MAX_PRIORITY - 1) "\n\t"
    "svlcx\n\t"
    "movh.a %a15, hi:tricore_irq_direct\n\t"
    "lea    %a15, [%a15]lo:tricore_irq_direct\n\t"
    "jli    %a15\n\t"
    "rslcx\n\t"
    "rfe\n\t"
    ".align 5\n"
    ".endr\n"

    ".popsection\n"
);

#endif
