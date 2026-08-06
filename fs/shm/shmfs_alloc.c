/****************************************************************************
 * fs/shm/shmfs_alloc.c
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

#include <stdbool.h>

#include <nuttx/arch.h>
#include <nuttx/cache.h>
#include <nuttx/nuttx.h>
#include <nuttx/kmalloc.h>
#include <nuttx/pgalloc.h>

#include "shm/shmfs.h"
#include "fs_heap.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

FAR struct shmfs_object_s *shmfs_alloc_object(size_t length)
{
  FAR struct shmfs_object_s *object;

  object = fs_heap_zalloc(sizeof(struct shmfs_object_s));
  if (object == NULL)
    {
      return NULL;
    }

  if (length > 0)
    {
      object->paddr = shmfs_alloc_buffer(length);
      if (object->paddr == NULL)
        {
          shmfs_free_object(object);
          return NULL;
        }
    }

  nxmutex_init(&object->lock);
  object->length = length;

  return object;
}

FAR void *shmfs_alloc_buffer(size_t length)
{
  FAR void *paddr = NULL;

#if defined(CONFIG_BUILD_KERNEL)
  size_t n_pages = MM_NPAGES(length);
  FAR void **pages = fs_heap_malloc(n_pages * sizeof(FAR void *));
  if (pages)
    {
      size_t i;
      for (i = 0; i < n_pages; i++)
        {
          pages[i] = (FAR void *)mm_pgalloc(1);
          if (!pages[i])
            {
              break;
            }

          up_addrenv_page_wipe((uintptr_t)pages[i]);
        }

      if (i == n_pages)
        {
          paddr = pages;
        }
      else
        {
          while (i-- > 0)
            {
              mm_pgfree((uintptr_t)pages[i], 1);
            }

          fs_heap_free(pages);
        }
    }
#else
  size_t alloc_size = length;
  size_t cachesize = up_get_dcache_linesize();
  if (cachesize > 0)
    {
      alloc_size = ALIGN_UP(alloc_size, cachesize);
#  if defined(CONFIG_BUILD_FLAT)
      paddr = fs_large_memalign(cachesize, alloc_size);
#  else
      paddr = kumm_memalign(cachesize, alloc_size);
#  endif
    }
  else
    {
#  if defined(CONFIG_BUILD_FLAT)
      paddr = fs_large_malloc(alloc_size);
#  else
      paddr = kumm_malloc(alloc_size);
#  endif
    }

  if (paddr)
    {
      memset(paddr, 0, alloc_size);
    }
#endif

  return paddr;
}

void shmfs_free_object(FAR struct shmfs_object_s *object)
{
  if (object)
    {
      nxmutex_destroy(&object->lock);
      shmfs_free_buffer(object);
      fs_heap_free(object);
    }
}

void shmfs_free_buffer(FAR struct shmfs_object_s *object)
{
  if (object && object->paddr)
    {
#if defined(CONFIG_BUILD_FLAT)
      fs_large_free(object->paddr);
#elif defined(CONFIG_BUILD_PROTECTED)
      kumm_free(object->paddr);
#elif defined(CONFIG_BUILD_KERNEL)
      size_t i;
      size_t n_pages = MM_NPAGES(object->length);
      FAR void **pages = object->paddr;
      for (i = 0; i < n_pages; i++)
        {
          if (pages[i])
            {
              mm_pgfree((uintptr_t)pages[i], 1);
            }
        }

      fs_heap_free(pages);
#endif

      object->paddr = NULL;
      object->length = 0;
    }
}
