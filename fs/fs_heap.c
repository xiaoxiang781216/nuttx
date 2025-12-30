/****************************************************************************
 * fs/fs_heap.c
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

#include <assert.h>

#include "fs_heap.h"

#if CONFIG_FS_HEAPSIZE > 0

/****************************************************************************
 * Private Data
 ****************************************************************************/

static FAR struct mm_heap_s *g_fs_heap;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void fs_heap_initialize(void)
{
  struct mm_heap_config_s config;
  struct mm_pool_config_s poolconfig;

#ifdef FS_HEAPBUF_SECTION
  static uint8_t buf[CONFIG_FS_HEAPSIZE]
    locate_data(CONFIG_FS_HEAPBUF_SECTION);
#else
  FAR void *buf = kmm_malloc(CONFIG_FS_HEAPSIZE);
#endif

  DEBUGASSERT(buf != NULL);
  memset(&config, 0, sizeof(config));
  config.name  = "heapfs";
  config.start = buf;
  config.size  = CONFIG_FS_HEAPSIZE;
  memset(&poolconfig, 0, sizeof(struct mm_pool_config_s));
  poolconfig.init_chunksize = CONFIG_FS_HEAP_MEMPOOL_INIT_CHUNK_SIZE;
  mm_initialize_pool(&config, &poolconfig, &g_fs_heap);
}

struct mallinfo_task fs_heap_mallinfo_task(FAR const struct malltask *task)
{
  return mm_mallinfo_task(g_fs_heap, task);
}

FAR void *fs_large_zalloc(size_t size)
{
  return mm_zalloc(g_fs_heap, size);
}

FAR void *fs_large_malloc(size_t size)
{
  return mm_malloc(g_fs_heap, size);
}

size_t fs_large_malloc_size(FAR void *mem)
{
  return mm_malloc_size(g_fs_heap, mem);
}

FAR void *fs_large_realloc(FAR void *oldmem, size_t size)
{
  return mm_realloc(g_fs_heap, oldmem, size);
}

FAR void *fs_large_memalign(size_t alignment, size_t size)
{
  return mm_memalign(g_fs_heap, alignment, size);
}

void fs_large_free(FAR void *mem)
{
  mm_free(g_fs_heap, mem);
}

FAR char *fs_large_strdup(FAR const char *s)
{
  size_t len = strlen(s) + 1;
  FAR char *copy = fs_large_malloc(len);
  if (copy != NULL)
    {
      memcpy(copy, s, len);
    }

  return copy;
}

FAR char *fs_large_strndup(FAR const char *s, size_t size)
{
  size_t len = strnlen(s, size) + 1;
  FAR char *copy = fs_large_malloc(len);
  if (copy != NULL)
    {
      memcpy(copy, s, len);
      copy[len - 1] = '\0';
    }

  return copy;
}

int fs_large_asprintf(FAR char **strp, FAR const char *fmt, ...)
{
  va_list ap;
  int len;

  /* Calculates the length required to format the string */

  va_start(ap, fmt);
  len = vsnprintf(NULL, 0, fmt, ap);
  va_end(ap);

  if (len < 0)
    {
      *strp = NULL;
      return len;
    }

  *strp = fs_large_malloc(len + 1);
  if (*strp == NULL)
    {
      return -ENOMEM;
    }

  va_start(ap, fmt);
  vsnprintf(*strp, len + 1, fmt, ap);
  va_end(ap);

  return len;
}

#endif
