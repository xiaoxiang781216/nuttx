/****************************************************************************
 * mm/kasan/kasan_dummy.c
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

#include <stddef.h>

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/* Exported functions called from the compiler generated code */

void __sanitizer_annotate_contiguous_container(FAR const void *beg,
                                               FAR const void *end,
                                               FAR const void *old_mid,
                                               FAR const void *new_mid)
{
  /* Shut up compiler complaints */
}

void __asan_before_dynamic_init(FAR const void *module_name)
{
  /* Shut up compiler complaints */
}

void __asan_after_dynamic_init(void)
{
  /* Shut up compiler complaints */
}

void __asan_handle_no_return(void)
{
  /* Shut up compiler complaints */
}

void __asan_report_load_n_noabort(FAR void *addr, size_t size)
{
}

void __asan_report_store_n_noabort(FAR void *addr, size_t size)
{
}

void __asan_loadN_noabort(FAR void *addr, size_t size)
{
}

void __asan_storeN_noabort(FAR void * addr, size_t size)
{
}

void __asan_loadN(FAR void *addr, size_t size)
{
}

void __asan_storeN(FAR void *addr, size_t size)
{
}

#define DEFINE_ASAN_LOAD_STORE(size) \
  void __asan_report_load##size##_noabort(FAR void *addr) \
  { \
  } \
  void __asan_report_store##size##_noabort(FAR void *addr) \
  { \
  } \
  void __asan_load##size##_noabort(FAR void *addr) \
  { \
  } \
  void __asan_store##size##_noabort(FAR void *addr) \
  { \
  } \
  void __asan_load##size(FAR void *addr) \
  { \
  } \
  void __asan_store##size(FAR void *addr) \
  { \
  }

DEFINE_ASAN_LOAD_STORE(1)
DEFINE_ASAN_LOAD_STORE(2)
DEFINE_ASAN_LOAD_STORE(4)
DEFINE_ASAN_LOAD_STORE(8)
DEFINE_ASAN_LOAD_STORE(16)
