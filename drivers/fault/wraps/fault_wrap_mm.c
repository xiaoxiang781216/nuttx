/****************************************************************************
 * drivers/fault/wraps/fault_wrap_mm.c
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

#include <nuttx/config.h>

#include <sys/types.h>
#include <stddef.h>

#include <nuttx/mm/mm.h>

#include <nuttx/fault_inject.h>

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/* The real mm_* implementations.  When the wrapsymbol tool rewrites these
 * translation units, every undefined reference to mm_malloc() etc. in the
 * rest of the kernel image is redirected to __wrap_mm_malloc() below; the
 * original definitions are renamed to __real_mm_*().
 */

FAR void *__real_mm_malloc(FAR struct mm_heap_s *heap, size_t size);
void      __real_mm_free(FAR struct mm_heap_s *heap, FAR void *mem);
FAR void *__real_mm_realloc(FAR struct mm_heap_s *heap, FAR void *oldmem,
                            size_t size);
FAR void *__real_mm_calloc(FAR struct mm_heap_s *heap, size_t n,
                           size_t elem_size);
FAR void *__real_mm_zalloc(FAR struct mm_heap_s *heap, size_t size);
FAR void *__real_mm_memalign(FAR struct mm_heap_s *heap, size_t alignment,
                             size_t size);

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: __wrap_mm_malloc
 ****************************************************************************/

FAR void *__wrap_mm_malloc(FAR struct mm_heap_s *heap, size_t size)
{
  FAR void *ret;
  FAULT_INJECT_WRAP_RETVAL_NULL(mm, mm_malloc,
                                __real_mm_malloc(heap, size), &ret);
  return ret;
}

/****************************************************************************
 * Name: __wrap_mm_free
 ****************************************************************************/

void __wrap_mm_free(FAR struct mm_heap_s *heap, FAR void *mem)
{
  FAULT_INJECT_WRAP_RETVAL_VOID(mm, mm_free, __real_mm_free(heap, mem));
}

/****************************************************************************
 * Name: __wrap_mm_realloc
 ****************************************************************************/

FAR void *__wrap_mm_realloc(FAR struct mm_heap_s *heap, FAR void *oldmem,
                            size_t size)
{
  FAR void *ret;
  FAULT_INJECT_WRAP_RETVAL_NULL(mm, mm_realloc,
                                __real_mm_realloc(heap, oldmem, size), &ret);
  return ret;
}

/****************************************************************************
 * Name: __wrap_mm_calloc
 ****************************************************************************/

FAR void *__wrap_mm_calloc(FAR struct mm_heap_s *heap, size_t n,
                           size_t elem_size)
{
  FAR void *ret;
  FAULT_INJECT_WRAP_RETVAL_NULL(mm, mm_calloc,
                                __real_mm_calloc(heap, n, elem_size), &ret);
  return ret;
}

/****************************************************************************
 * Name: __wrap_mm_zalloc
 ****************************************************************************/

FAR void *__wrap_mm_zalloc(FAR struct mm_heap_s *heap, size_t size)
{
  FAR void *ret;
  FAULT_INJECT_WRAP_RETVAL_NULL(mm, mm_zalloc,
                                __real_mm_zalloc(heap, size), &ret);
  return ret;
}

/****************************************************************************
 * Name: __wrap_mm_memalign
 ****************************************************************************/

FAR void *__wrap_mm_memalign(FAR struct mm_heap_s *heap, size_t alignment,
                             size_t size)
{
  FAR void *ret;
  FAULT_INJECT_WRAP_RETVAL_NULL(mm, mm_memalign,
                                __real_mm_memalign(heap, alignment, size),
                                &ret);
  return ret;
}
