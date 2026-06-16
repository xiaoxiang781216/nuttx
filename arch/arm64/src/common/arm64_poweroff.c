/***************************************************************************
 * arch/arm64/src/common/arm64_poweroff.c
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
 ***************************************************************************/

/***************************************************************************
 * Included Files
 ***************************************************************************/

#include <nuttx/config.h>
#include <debug.h>

#include "arch/syscall.h"

/***************************************************************************
 * Preprocessor Definitions
 ***************************************************************************/

/* per https://github.com/ARM-software/abi-aa semihosting.pdf 2024Q3 */

#define ARMSMH_OPNUM_SYSEXIT    0x18
#define ARMSMH_PARAM_APPEXIT    0x20026

/***************************************************************************
 * Public Functions
 ***************************************************************************/

/***************************************************************************
 * Name: up_systempoweroff
 *
 * Description:
 *   Semihosting EXIT based poweroff logic.
 *
 ***************************************************************************/

void up_systempoweroff(void)
{
  /* On AArch64 the SYS_EXIT(0x18) operation takes a pointer to a two-field
   * parameter block { reason, subcode } in x1, unlike AArch32 where the
   * reason code is passed directly.
   */

  uintptr_t block[2];

  block[0] = ARMSMH_PARAM_APPEXIT;  /* reason: ADP_Stopped_ApplicationExit */
  block[1] = 0;                     /* exit status code */

  /* Set up for the system poweroff */

  smh_call(ARMSMH_OPNUM_SYSEXIT, block);

  /* Wait for power off */

  for (; ; );
}
