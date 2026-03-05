/****************************************************************************
 * mm/kasan/kasan.h
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

#ifndef __MM_KASAN_KASAN_H
#define __MM_KASAN_KASAN_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdbool.h>
#include <stddef.h>

#include <nuttx/list.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#if defined(CONFIG_MM_KASAN_SW_TAGS) || defined(CONFIG_MM_KASAN_GENERIC)
#  define KASAN_REGISTER(h, i, a, s)  h->mm_kasan[i] = kasan_register(a, s)
#else
#  define KASAN_REGISTER(h, i, a, s)  kasan_register(a, s)
#endif

#ifndef CONFIG_MM_KASAN
#  define kasan_poison2(region, addr, size)
#  define kasan_unpoison2(region, addr, size) addr
#elif defined(CONFIG_MM_KASAN_HW_TAGS) || defined(CONFIG_MM_KASAN_SIM_ASAN)
#  define kasan_poison2(region, addr, size) kasan_poison(addr, size)
#  define kasan_unpoison2(region, addr, size) kasan_unpoison(addr, size)
#else

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct kasan_region_s
{
  uintptr_t begin;
  uintptr_t end;
  struct list_node node;           /* for global linked list */
#ifdef CONFIG_MM_KASAN_GENERIC
  uintptr_t shadow[1];
#endif
#ifdef CONFIG_MM_KASAN_SW_TAGS
  uint8_t   shadow[1];
#endif
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef __cplusplus
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

/****************************************************************************
 * Name: kasan_poison2
 *
 * Description:
 *   Mark memory range as inaccessible with region list
 *
 * Input Parameters:
 *   region - region list of at most CONFIG_MM_REGIONS, ends with NULL
 *   addr - range start address
 *   size - range size
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

void kasan_poison2(FAR struct kasan_region_s *region[],
                   FAR const void *addr, size_t size);

/****************************************************************************
 * Name: kasan_unpoison2
 *
 * Description:
 *   Mark memory range as accessible with region list.
 *
 * Input Parameters:
 *   region - region list of at most CONFIG_MM_REGIONS, ends with NULL
 *   addr - range start address
 *   size - range size
 *
 * Returned Value:
 *   Return tagged address
 *
 ****************************************************************************/

FAR void *kasan_unpoison2(FAR struct kasan_region_s *region[],
                          FAR const void *addr, size_t size);

#undef EXTERN
#ifdef __cplusplus
}
#endif

#endif /* CONFIG_MM_KASAN */

#endif /* __MM_KASAN_KASAN_H */
