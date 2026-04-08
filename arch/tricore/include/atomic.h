/****************************************************************************
 * arch/tricore/include/atomic.h
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

#ifndef __ARCH_TRICORE_INCLUDE_ATOMIC_H
#define __ARCH_TRICORE_INCLUDE_ATOMIC_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/compiler.h>

#include <IfxCpu_Intrinsics.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef ARCH_ATOMIC_SPECIFIER
#  define ARCH_ATOMIC_SPECIFIER static inline_function
#endif

#define ARCH_ATOMIC_STORE(func, t)                                       \
  ARCH_ATOMIC_SPECIFIER                                                  \
  void func(volatile void *ptr, t value, int memorder)                   \
  {                                                                      \
    __swap((void *)ptr, value);                                          \
  }

#define ARCH_ATOMIC_LOAD(func, t)                                        \
  ARCH_ATOMIC_SPECIFIER                                                  \
  t func(const volatile void *ptr, int memorder)                         \
  {                                                                      \
    return __ld32((void *)ptr);                                          \
  }

#define ARCH_ATOMIC_EXCHANGE(func, t)                                    \
  ARCH_ATOMIC_SPECIFIER                                                  \
  t func(volatile void *ptr, t value, int memorder)                      \
  {                                                                      \
    return __swap((void *)ptr, value);                                   \
  }

#define ARCH_ATOMIC_COMPARE_EXCHANGE(func, t)                            \
  ARCH_ATOMIC_SPECIFIER                                                  \
  bool func(volatile void *ptr, volatile void *expect,                   \
            t desired, bool weak, int success, int failure)              \
  {                                                                      \
    t old;                                                               \
                                                                         \
    old = __cmpAndSwap((void *)ptr, desired, *(t *)expect);              \
    if (old == *(t *)expect)                                             \
      {                                                                  \
        return true;                                                     \
      }                                                                  \
                                                                         \
    *(t *)expect = old;                                                  \
                                                                         \
    return false;                                                        \
  }

#define ARCH_ATOMIC_FLAGS_TEST_AND_SET(func, t)                          \
  ARCH_ATOMIC_SPECIFIER                                                  \
  t func(volatile void *ptr, int memorder)                               \
  {                                                                      \
    return __swap((void *)ptr, 1);                                       \
  }

#define ARCH_ATOMIC_FETCH_OP(func, t, n, op)                             \
  ARCH_ATOMIC_SPECIFIER                                                  \
  t func(volatile void *ptr, t value, int memorder)                      \
  {                                                                      \
    t old_val;                                                           \
                                                                         \
    do                                                                   \
      {                                                                  \
        old_val = atomic_load_ ## n(ptr, memorder);                      \
      }                                                                  \
    while (__cmpAndSwap((void *)ptr,                                     \
                        old_val op value, old_val) != old_val);          \
                                                                         \
    return old_val;                                                      \
  }

#define ARCH_ATOMIC_DEFINE(prefix, t, n)                                 \
  ARCH_ATOMIC_STORE(prefix ## _store_ ## n, t)                           \
  ARCH_ATOMIC_LOAD(prefix ## _load_ ## n, t)                             \
  ARCH_ATOMIC_EXCHANGE(prefix ## _exchange_ ## n, t)                     \
  ARCH_ATOMIC_COMPARE_EXCHANGE(prefix ## _compare_exchange_ ## n, t)     \
  ARCH_ATOMIC_FLAGS_TEST_AND_SET(prefix ## _flags_test_and_set_ ## n, t) \
  ARCH_ATOMIC_FETCH_OP(prefix ## _fetch_add_ ## n, t, n, +)              \
  ARCH_ATOMIC_FETCH_OP(prefix ## _fetch_sub_ ## n, t, n, -)              \
  ARCH_ATOMIC_FETCH_OP(prefix ## _fetch_and_ ## n, t, n, &)              \
  ARCH_ATOMIC_FETCH_OP(prefix ## _fetch_or_ ## n, t, n, |)               \
  ARCH_ATOMIC_FETCH_OP(prefix ## _fetch_xor_ ## n, t, n, ^)

#define ARCH_HAVE_ATOMIC_4

/****************************************************************************
 * Inline Functions
 ****************************************************************************/

ARCH_ATOMIC_DEFINE(atomic, int32_t, 4)

#endif /* __ARCH_TRICORE_INCLUDE_ATOMIC_H */
