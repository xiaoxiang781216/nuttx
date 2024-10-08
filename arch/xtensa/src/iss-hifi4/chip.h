/****************************************************************************
 * arch/xtensa/src/iss-hifi4/chip.h
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

#ifndef __ARCH_XTENSA_SRC_ISS_HIFI4_CHIP_H
#define __ARCH_XTENSA_SRC_ISS_HIFI4_CHIP_H

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* This is the name of the section containing the Xtensa low level handlers
 * that is used by the board linker scripts.
 */

#define HANDLER_SECTION .iram

/****************************************************************************
 * Inline Functions
 ****************************************************************************/

#ifndef __ASSEMBLY__

#if defined(__cplusplus)
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

static inline bool xtensa_ptr_exec(const void *p)
{
  return p != NULL;
}

static inline bool xtensa_sp_sane(uint32_t sp)
{
  return true;
}

#undef EXTERN
#if defined(__cplusplus)
}
#endif

#endif /* __ASSEMBLY__ */
#endif /* __ARCH_XTENSA_SRC_ISS_HIFI4_CHIP_H */
