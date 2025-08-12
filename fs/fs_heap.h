/****************************************************************************
 * fs/fs_heap.h
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

#ifndef __FS_FS_HEAP_H
#define __FS_FS_HEAP_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <nuttx/compiler.h>
#include <nuttx/kmalloc.h>

#include <stdio.h>
#include <string.h>

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#if CONFIG_FS_HEAPSIZE > 0
void      fs_heap_initialize(void);
struct mallinfo_task fs_heap_mallinfo_task(FAR const struct malltask *task);
FAR char *fs_heap_strdup(FAR const char *s) malloc_like;
FAR char *fs_heap_strndup(FAR const char *s, size_t size) malloc_like;
int       fs_heap_asprintf(FAR char **strp, FAR const char *fmt, ...)
          printf_like(2, 3);
FAR void *fs_large_zalloc(size_t size) malloc_like1(1);
FAR void *fs_large_malloc(size_t size) malloc_like1(1);
size_t    fs_large_malloc_size(FAR void *mem);
FAR void *fs_large_realloc(FAR void *oldmem, size_t size)
          realloc_like(2);
FAR void *fs_large_memalign(size_t alignment, size_t size)
          malloc_like1(3);
void      fs_large_free(FAR void *mem);
#else
#  define fs_heap_initialize()
#  define fs_heap_mallinfo_task     kmm_mallinfo_task
#  define fs_heap_strdup            strdup
#  define fs_heap_strndup           strndup
#  define fs_heap_asprintf          asprintf
#  define fs_large_zalloc           kmm_zalloc
#  define fs_large_malloc           kmm_malloc
#  define fs_large_malloc_size      kmm_malloc_size
#  define fs_large_realloc          kmm_realloc
#  define fs_large_memalign         kmm_memalign
#  define fs_large_free             kmm_free
#endif /* CONFIG_FS_HEAPSIZE */

# if defined(CONFIG_FS_HEAPLARGE_ONLY) || CONFIG_FS_HEAPSIZE <= 0
#  define fs_heap_zalloc            kmm_zalloc
#  define fs_heap_malloc            kmm_malloc
#  define fs_heap_malloc_size       kmm_malloc_size
#  define fs_heap_realloc           kmm_realloc
#  define fs_heap_memalign          kmm_memalign
#  define fs_heap_free              kmm_free
# else
#  define fs_heap_zalloc            fs_large_zalloc
#  define fs_heap_malloc            fs_large_malloc
#  define fs_heap_malloc_size       fs_large_malloc_size
#  define fs_heap_realloc           fs_large_realloc
#  define fs_heap_memalign          fs_large_memalign
#  define fs_heap_free              fs_large_free
# endif /* CONFIG_FS_HEAPLARGE_ONLY */

#endif /* __FS_FS_HEAP_H */
