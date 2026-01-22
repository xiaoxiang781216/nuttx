/****************************************************************************
 * libs/libc/machine/arm/arm_tls.c
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

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/tls.h>

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: arm_read_tp
 *
 * Description:
 *   Helper function for __aeabi_read_tp.
 *
 ****************************************************************************/

void *arm_read_tp(void)
{
  return (void *)(tls_get_info() + 1);
}

/****************************************************************************
 * Name: __aeabi_read_tp
 *
 * Description:
 *   Read thread local storage region pointer.
 *
 *   This function follows the ARM EABI specification for TLS helper
 *   functions:
 *   - Only r0 is modified (return value)
 *   - r1-r3 must be preserved for the caller
 *
 *   The naked attribute ensures strict register usage compliance.
 *
 ****************************************************************************/

__attribute__((naked))
void *__aeabi_read_tp(void)
{
  __asm__ __volatile__
    (
      "push {r1-r3, lr}\n\t"
      "bl arm_read_tp\n\t"
      "pop {r1-r3, pc}\n\t"
    );
}
