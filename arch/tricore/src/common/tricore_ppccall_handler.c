/****************************************************************************
 * arch/tricore/src/common/tricore_ppccall_handler.c
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

#include <arch/syscall.h>
#include <nuttx/arch.h>
#include <nuttx/ppccall.h>

#include "tricore_internal.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_ppccall_handler
 *
 * Description:
 *   This function is both the the user-space and kernel-space ppccall
 *   handler trampoline function. It is called when exit the syscall
 *   SYS_ppccall, this function is called in user-space and kernel-space
 *   depends on the target function located at which space.
 *
 * Input Parameters:
 *   func  - the ppccall function to be called
 *   param - the parameter to be passed to the ppccall function
 *
 * Returned Value:
 *   None.  This function does not return in the normal sense.  It returns
 *   via the SYS_ppccall_return (see syscall.h)
 *
 ****************************************************************************/

void up_ppccall_handler(uintptr_t func, struct ppccall_param_s *param)
{
  __asm__ __volatile__ (
#ifdef CONFIG_TRICORE_TOOLCHAIN_TASKING
    "mov.a     a15, d4\n\t"
    "ld.w      d4, [a4]4\n\t"
    "ld.w      d5, [a4]8\n\t"
    "ld.w      d6, [a4]12\n\t"
    "ld.w      d7, [a4]16\n\t"
    "sub.a     sp, #8\n\t"
    "ld.w      d15, [a4]20\n\t"
    "st.w      [sp]0, d15\n\t"
    "ld.w      d15, [a4]24\n\t"
    "st.w      [sp]4, d15\n\t"
    "calli     a15\n\t"
#else
    "mov.a     %%a15, %%d4\n\t"
    "ld.w      %%d4, [%%a4]4\n\t"
    "ld.w      %%d5, [%%a4]8\n\t"
    "ld.w      %%d6, [%%a4]12\n\t"
    "ld.w      %%d7, [%%a4]16\n\t"
    "sub.a     %%sp, 8\n\t"
    "ld.w      %%d15, [%%a4]20\n\t"
    "st.w      [%%sp]0, %%d15\n\t"
    "ld.w      %%d15, [%%a4]24\n\t"
    "st.w      [%%sp]4, %%d15\n\t"
    "calli     %%a15\n\t"
#endif
    ::: "memory"
  );
  sys_call0(SYS_ppccall_return);
}
