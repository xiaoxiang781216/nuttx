/****************************************************************************
 * include/nuttx/kconfig.h
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

#ifndef __INCLUDE_NUTTX_KCONFIG_H
#define __INCLUDE_NUTTX_KCONFIG_H

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* NX_IS_ENABLED() - evaluate boolean Kconfig at C expression level.
 *
 * Works for boolean Kconfig only: CONFIG_FOO is either defined to 1
 * or not defined at all.  Does NOT work for int/hex/string Kconfig.
 *
 * This allows using Kconfig symbols in C expressions:
 *   if (NX_IS_ENABLED(CONFIG_FOO)) { ... }
 * instead of:
 *   #ifdef CONFIG_FOO
 *
 * Technique: identical to Linux kernel's IS_ENABLED() in
 * include/linux/kconfig.h — uses token-pasting to detect whether
 * a macro expands to 1 or is left undefined.
 */

#define __NX_ARG_PLACEHOLDER_1                    0,
#define __nx_take_second_arg(__ignored, val, ...) val
#define __nx_is_defined(x)                        ___nx_is_defined(x)
#define ___nx_is_defined(val)                     ____nx_is_defined(__NX_ARG_PLACEHOLDER_##val)
#define ____nx_is_defined(arg1_or_junk)           __nx_take_second_arg(arg1_or_junk 1, 0)
#define NX_IS_ENABLED(option)                     __nx_is_defined(option)

#endif /* __INCLUDE_NUTTX_KCONFIG_H */
