/****************************************************************************
 * drivers/binder/binder_alloc.c
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

#define LOG_TAG "BinderAlloc"

#include <nuttx/config.h>
#include <sys/types.h>
#include <string.h>
#include <poll.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <debug.h>
#include <sched.h>
#include <nuttx/fs/fs.h>
#include <nuttx/android/binder.h>

#include "binder_internal.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: check_buffer
 *
 * Description:
 *   verify that buffer/offset is safe to access.
 *
 *   Check that the offset/bytes are within the size of the given
 *   buffer and that the buffer is currently active and not freeable.
 *   Offsets must also be multiples of sizeof(u32). The kernel is
 *   allowed to touch the buffer in two cases:
 *
 *   1) when the buffer is being created:
 *       (buffer->allow_user_free == 0)
 *   2) when the buffer is being torn down:
 *       (buffer->transaction == NULL).
 *
 * Input Parameters:
 *   alloc  - binder_alloc for this proc
 *   buffer - binder buffer to be accessed
 *   offset - offset into buffer data
 *   bytes  - bytes to access from offset
 *
 * Returned Value:
 *   true if the buffer is safe to access
 ****************************************************************************/

static bool check_buffer(FAR struct binder_alloc *alloc,
                         FAR struct binder_buffer *buffer,
                         binder_size_t offset, size_t bytes)
{
  return IS_ALIGNED(offset, sizeof(unsigned int)) &&
         (!buffer->allow_user_free || !buffer->transaction);
}

/* Calculate the sanitized total size, returns 0 for invalid request */

static inline size_t sanitized_size(size_t data_size,
                                    size_t offsets_size,
                                    size_t secctx_sz)
{
  size_t total;
  size_t tmp;

  /* Align to pointer size and check for overflows */

  tmp = ALIGN(data_size, sizeof(void *)) +
        ALIGN(offsets_size, sizeof(void *));
  if (tmp < data_size || tmp < offsets_size)
    {
      return 0;
    }

  total = tmp + ALIGN(secctx_sz, sizeof(void *));
  if (total < tmp || total < secctx_sz)
    {
      return 0;
    }

  /* Pad 0-sized buffers so they get a unique address */

  total = MAX(total, sizeof(void *));

  return total;
}

static int binder_alloc_do_buffer_copy(
  FAR struct binder_alloc *alloc, bool to_buffer,
  FAR struct binder_buffer *buffer, binder_size_t buffer_offset,
  FAR void *ptr, size_t bytes)
{
  if (!check_buffer(alloc, buffer, buffer_offset, bytes))
    {
      return -EINVAL;
    }

  if (bytes)
    {
      if (to_buffer)
        {
          memcpy(buffer->user_data + buffer_offset - alloc->kbuf_ubuf_offset,
                 ptr, bytes);
        }
      else
        {
          memcpy(ptr,
                 buffer->user_data + buffer_offset - alloc->kbuf_ubuf_offset,
                 bytes);
        }
    }

  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: binder_alloc_prepare_to_free
 *
 * Description:
 *   Get buffer given user ptr. Validate userspace pointer to buffer data and
 *   return buffer corresponding to that user pointer. Search the rb tree for
 *   buffer that matches user data pointer.
 *
 * Input Parameters:
 *   alloc    - binder_alloc for this proc
 *   user_ptr - user pointer to buffer data
 *
 * Returned Value:
 *   Pointer to buffer or NULL
 *
 ****************************************************************************/

FAR struct binder_buffer *binder_alloc_prepare_to_free(
  FAR struct binder_alloc *alloc, uintptr_t user_ptr)
{
  FAR struct binder_buffer *buffer;

  if (!user_ptr)
    {
      return NULL;
    }

  buffer = (struct binder_buffer *)(user_ptr -
           (uintptr_t)alloc->kbuf_ubuf_offset -
           sizeof(struct binder_buffer));

  if (!buffer->allow_user_free)
    {
      binder_debug(BINDER_DEBUG_ERROR,
                   "%d: buffer not allow user free\n", alloc->pid);
      BUG_ON(1);
      return NULL;
    }

  buffer->allow_user_free = 0;

  binder_debug(BINDER_DEBUG_ALLOC_BUFFER,
               "alloc->pid=%d buffer %p, data %p\n",
               alloc->pid, buffer, buffer->user_data);

  return buffer;
}

/****************************************************************************
 * Name: binder_alloc_new_buf
 *
 * Description:
 *   allocate a new buffer given the requested sizes. Returns the kernel.
 *
 *   version of the buffer pointer. The size allocated is the sum of the
 *   three given sizes (each rounded up to pointer-sized boundary)
 *
 * Input Parameters:
 *   alloc              - binder_alloc for this proc
 *   data_size          - size of user data buffer
 *   offsets_size       - user specified buffer offset
 *   secctx_sz          - size of extra space for meta-data
 *                        (eg, security context)
 *   is_async           - buffer for async transaction
 *
 * Returned Value:
 *   The allocated buffer or NULL if error
 *
 ****************************************************************************/

FAR struct binder_buffer *binder_alloc_new_buf(
  FAR struct binder_alloc *alloc, size_t data_size, size_t offsets_size,
  size_t secctx_sz, int is_async, FAR int *ret)
{
  FAR struct binder_buffer *buffer;
  size_t size;
  struct mm_memdump_s dump =
    {
      alloc->pid,
#ifdef CONFIG_MM_RECORD_SEQNO
      0,
      ULONG_MAX
#endif
    };

  size = sanitized_size(data_size, offsets_size, secctx_sz);
  if (!size)
    {
      binder_debug(BINDER_DEBUG_ALLOC_BUFFER,
                   "alloc->pid=%d: got transaction with "
                   "invalid size %zd-%zd\n",
                   alloc->pid, data_size, offsets_size);
      *ret = -EINVAL;
      return NULL;
    }

  buffer = mm_zalloc(alloc->heap, sizeof(struct binder_buffer) + size);
  if (!buffer)
    {
      binder_debug(BINDER_DEBUG_ERROR,
                    "alloc->pid=%d failed to alloc new buffer struct,"
                    "size=%zu\n", alloc->pid, size);
      *ret = -ENOMEM;
      mm_memdump(alloc->heap, &dump);
      return NULL;
    }

  buffer->async_transaction = is_async;
  buffer->data_size = data_size;
  buffer->offsets_size = offsets_size;
  buffer->user_data = (FAR void *)((uintptr_t)buffer +
                      (uintptr_t)alloc->kbuf_ubuf_offset +
                      sizeof(struct binder_buffer));

  return buffer;
}

int binder_alloc_copy_to_buffer(FAR struct binder_alloc *alloc,
                                FAR struct binder_buffer *buffer,
                                binder_size_t buffer_offset,
                                FAR void *src, size_t bytes)
{
  return binder_alloc_do_buffer_copy(alloc, true, buffer, buffer_offset, src,
                                     bytes);
}

int binder_alloc_copy_from_buffer(FAR struct binder_alloc *alloc,
                                  FAR void *dest,
                                  FAR struct binder_buffer *buffer,
                                  binder_size_t buffer_offset, size_t bytes)
{
  return binder_alloc_do_buffer_copy(alloc, false, buffer, buffer_offset,
                                     dest, bytes);
}

void binder_alloc_deferred_release(FAR struct binder_alloc *alloc)
{
  mm_uninitialize(alloc->heap);
  kmm_free(alloc->base);
}

/****************************************************************************
 * Name: binder_alloc_unmmap
 *
 * Description:
 *   Description of the operation of the function.
 *
 * Input Parameters:
 *   mm    - A reference to the process mm_map struct
 *   alloc - alloc structure for this proc
 *   vma   - vma passed to mmap().
 *
 * Returned Value:
 *   0 = success
 *
 ****************************************************************************/

int binder_alloc_unmmap(FAR struct mm_map_s *mm,
                        FAR struct binder_mmap_area *vma)
{
#ifdef CONFIG_BUILD_KERNEL
  vm_unmap_region(mm, vma->area_start, vma->area_size);
#endif
  return 0;
}

/****************************************************************************
 * Name: binder_alloc_mmap
 *
 * Description:
 *   Map address space for proc. Called by binder_mmap() to initialize the
 *   space specified in vma for allocating binder buffers
 *
 * Input Parameters:
 *   mm    - A reference to the process mm_map struct
 *   alloc - alloc structure for this proc
 *   vma   - vma passed to mmap()
 *
 * Returned Value:
 *   0       : success
 *   -EBUSY  : address space already mapped
 *   -ENOMEM : failed to map memory to given address space
 *
 ****************************************************************************/

int binder_alloc_mmap(FAR struct mm_map_s *mm,
                      FAR struct binder_alloc *alloc,
                      FAR struct binder_mmap_area *vma)
{
#ifdef CONFIG_BUILD_KERNEL
  vma->area_start = vm_map_region(mm, (uintptr_t)alloc->base,
                                  ALIGN(vma->area_size, PAGE_SIZE));
#else
  vma->area_start = alloc->base;
#endif
  alloc->kbuf_ubuf_offset = vma->area_start - alloc->base;
  vma->area_size = CONFIG_BINDER_HEAP_SIZE;

  return 0;
}

/****************************************************************************
 * Name: binder_alloc_init
 *
 * Description:
 *   Called by binder_open() for per-proc initialization
 *
 * Input Parameters:
 *   alloc - binder_alloc for this proc
 *   pid   - Process ID for this proc
 *
 ****************************************************************************/

int binder_alloc_init(FAR struct binder_alloc *alloc, pid_t pid)
{
  alloc->pid = pid;

  alloc->base = kmm_malloc(CONFIG_BINDER_HEAP_SIZE);
  if (!alloc->base)
    {
      return -ENOMEM;
    }

  snprintf(alloc->name, sizeof(alloc->name), "binder_heap_%d", pid);
  alloc->size = CONFIG_BINDER_HEAP_SIZE;
  alloc->heap = mm_initialize(alloc->name,
                              alloc->base,
                              alloc->size);
  if (!alloc->heap)
    {
      kmm_free(alloc->base);
      return -ENOMEM;
    }

  return 0;
}
