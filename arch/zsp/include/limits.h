/****************************************************************************
 * arch/zsp/include/limits.h
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

#ifndef __ARCH_ZSP_INCLUDE_LIMITS_H
#define __ARCH_ZSP_INCLUDE_LIMITS_H

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* ZSP NanoPlus is ILP32: int=32, long=32, pointer=32, long long=64 */

#define CHAR_BIT    8
#define SCHAR_MIN   (-128)
#define SCHAR_MAX   127
#define UCHAR_MAX   255

#define SHRT_MIN    (-32768)
#define SHRT_MAX    32767
#define USHRT_MAX   65535

#define INT_MIN     (-2147483647 - 1)
#define INT_MAX     2147483647
#define UINT_MAX    4294967295u

#define LONG_MIN    (-2147483647L - 1L)
#define LONG_MAX    2147483647L
#define ULONG_MAX   4294967295uL

#define LLONG_MIN   (-9223372036854775807LL - 1LL)
#define LLONG_MAX   9223372036854775807LL
#define ULLONG_MAX  18446744073709551615uLL

#define LONG_BIT    32
#define PTR_MIN     INT_MIN
#define PTR_MAX     INT_MAX
#define UPTR_MAX    UINT_MAX
#define PTRDIFF_MIN INT_MIN
#define PTRDIFF_MAX INT_MAX
#define SIZE_MAX    UINT_MAX

#endif /* __ARCH_ZSP_INCLUDE_LIMITS_H */
