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

#define LOG_TAG  "BinderAlloc"

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
#include <nuttx/kmalloc.h>

#include "binder_internal.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define PAGE_ALIGN(addr)    ALIGN(addr, PAGE_SIZE)
#define PAGE_ALIGNED(addr)  IS_ALIGNED((unsigned long)(addr), PAGE_SIZE)

#define list_is_last(list, head) ((list)->next == (head))
#define buffer_next(buffer) \
        list_entry((buffer)->entry.next, struct binder_buffer, entry)
#define buffer_prev(buffer) \
        list_entry((buffer)->entry.prev, struct binder_buffer, entry)
#define buffer_start_page(buffer) \
        ((void *)((uintptr_t)(buffer)->user_data & PAGE_MASK))
#define prev_buffer_end_page(buffer) \
        ((void *)(((uintptr_t)(buffer)->user_data - 1) & PAGE_MASK))

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static size_t alloc_buffer_size(FAR struct binder_alloc *alloc,
                                FAR struct binder_buffer *buffer)
{
  if (list_is_last(&buffer->entry, &alloc->buffers_list))
    {
      return (alloc->buffer_data +
             alloc->buffer_data_size -
             buffer->user_data);
    }

  return buffer_next(buffer)->user_data - buffer->user_data;
}

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
 *       (buffer->free == 0 && buffer->allow_user_free == 0)
 *   2) when the buffer is being torn down:
 *       (buffer->free == 0 && buffer->transaction == NULL).
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
  bool      ret;
  size_t    size = alloc_buffer_size(alloc, buffer);

  ret = (size >= bytes && offset <= size - bytes &&
        IS_ALIGNED(offset, sizeof(unsigned int)) &&
        !buffer->free &&
        (!buffer->allow_user_free || !buffer->transaction));

  return ret;
}

static void insert_free_buffer(FAR struct binder_alloc *alloc,
                               FAR struct binder_buffer *new_buffer)
{
  BUG_ON(!new_buffer->free);

  binder_debug(BINDER_DEBUG_ALLOC_BUFFER,
               "alloc->pid=%d add free buffer %p, data %p\n",
               alloc->pid, new_buffer, new_buffer->user_data);

  list_add_tail(&alloc->free_buffers_list, &new_buffer->rb_node);
}

FAR static struct binder_buffer * prepare_to_free_locked(
  FAR struct binder_alloc *alloc, uintptr_t user_ptr)
{
  FAR struct binder_buffer  *buffer = NULL;
  FAR struct binder_buffer  *itr    = NULL;
  FAR void                  *uptr;

  uptr = (FAR void *)user_ptr;

  list_for_every_entry(&alloc->allocated_buffers_list, itr,
                       struct binder_buffer, rb_node)
  {
    BUG_ON(itr->free);
    if (uptr == itr->user_data)
      {
        if (!itr->allow_user_free)
          {
            binder_debug(BINDER_DEBUG_ERROR,
                         "%d: buffer not allow user free\n", alloc->pid);
            BUG_ON(1);
            buffer = NULL;
            break;
          }
        buffer                  = itr;
        buffer->allow_user_free = 0;
        binder_debug(BINDER_DEBUG_ALLOC_BUFFER,
                     "alloc->pid=%d buffer %p, data %p\n",
                     alloc->pid, buffer, buffer->user_data);
        break;
      }
  }

  return buffer;
}

static int binder_update_page_range(FAR struct binder_alloc *alloc,
                                    int allocate, FAR void *start,
                                    FAR void *end)
{
  FAR void                * page_addr;
  FAR struct binder_page  * page;

  binder_debug(BINDER_DEBUG_ALLOC_BUFFER, "alloc->pid=%d %s pages %p-%p\n",
               alloc->pid, allocate ? "allocate" : "free", start, end);

  if (end <= start)
    {
      return 0;
    }

  if (allocate == 0)
    {
      goto free_range;
    }

  for (page_addr = start; page_addr < end; page_addr += PAGE_SIZE)
    {
      size_t index;

      index = (page_addr - alloc->buffer_data) / PAGE_SIZE;
      page  = &alloc->pages_array[index];

      if (page->page_ptr)
        {
          continue;
        }

      page->page_ptr = page_addr;
    }

  return 0;

free_range:
  return 0;
}

static FAR struct binder_buffer *binder_alloc_new_buf_locked(
  FAR struct binder_alloc *alloc, size_t data_size, size_t offsets_size,
  int secctx_sz, int is_async, int pid, FAR int * p_ret)
{
  FAR struct binder_buffer  *buffer = NULL;
  FAR struct binder_buffer  *tmp;
  size_t                     buffer_size = 0;
  FAR void                  *has_page_addr;
  FAR void                  *end_page_addr;
  FAR void                  *page_range;
  size_t                     size;
  size_t                     data_offsets_size;
  int                        ret;

  BUG_ON((alloc->buffer_data_size == 0));

  data_offsets_size = ALIGN(data_size, sizeof(void *));
  data_offsets_size += ALIGN(offsets_size, sizeof(void *));

  if (data_offsets_size < data_size || data_offsets_size < offsets_size)
    {
      binder_debug(BINDER_DEBUG_ALLOC_BUFFER,
                   "alloc->pid=%d: got transaction with "
                   "invalid size %zd-%zd\n",
                   alloc->pid, data_size, offsets_size);
      *p_ret = -EINVAL;
      return NULL;
    }

  /* Pad 0-size buffers so they get assigned unique addresses */

  size      = data_offsets_size + ALIGN(secctx_sz, sizeof(void *));
  size      = max(size, sizeof(void *));
  buffer    = NULL;

  list_for_every_entry(&alloc->free_buffers_list, tmp,
                       struct binder_buffer, rb_node)
  {
    buffer_size = alloc_buffer_size(alloc, tmp);
    if (size <= buffer_size)
      {
        buffer = tmp;
        break;
      }
  }

  if (buffer == NULL)
    {
      binder_debug(BINDER_DEBUG_ERROR,
                   "alloc->pid=%d: binder_alloc_buf size %zd failed, "
                   "no address space\n",
                   alloc->pid, size);
      *p_ret = -ENOSPC;
      return NULL;
    }

  binder_debug(BINDER_DEBUG_ALLOC_BUFFER,
               "alloc buffer begin alloc->pid=%d: alloc size %zd "
               "got buffer %p data %p buffer_size %zd\n",
               alloc->pid, size, buffer, buffer->user_data,
               buffer_size);

  has_page_addr =
    (void  *)(((uintptr_t)buffer->user_data + buffer_size) & PAGE_MASK);
  WARN_ON(buffer == NULL && buffer_size != size);
  end_page_addr = (void  *)PAGE_ALIGN((uintptr_t)buffer->user_data + size);
  if (end_page_addr > has_page_addr)
    {
      end_page_addr = has_page_addr;
    }

  page_range    = (void  *)PAGE_ALIGN((uintptr_t)buffer->user_data);
  ret           =
    binder_update_page_range(alloc, 1, page_range, end_page_addr);

  if (ret)
    {
      *p_ret = -ret;
      return NULL;
    }

  if (buffer_size != size)
    {
      struct binder_buffer *new_buffer;

      new_buffer = kmm_zalloc(sizeof(struct binder_buffer));
      if (!new_buffer)
        {
          binder_debug(BINDER_DEBUG_ERROR,
                       "alloc->pid=%d failed to alloc new buffer struct\n",
                       alloc->pid);
          goto err_alloc_buf_struct_failed;
        }

      list_initialize(&new_buffer->entry);
      new_buffer->user_data = (unsigned char *)buffer->user_data + size;
      list_add_head(&buffer->entry, &new_buffer->entry);
      list_initialize(&new_buffer->rb_node);
      new_buffer->free = 1;
      insert_free_buffer(alloc, new_buffer);
      binder_debug(BINDER_DEBUG_ALLOC_BUFFER,
                   "alloc new buffer alloc->pid=%d buffer %p "
                   "success data %p size %d\n",
                   alloc->pid, new_buffer, new_buffer->user_data,
                   buffer->data_size);
    }

  list_delete_init(&buffer->rb_node);
  buffer->free                  = 0;
  buffer->allow_user_free       = 0;
  buffer->data_size             = data_size;
  buffer->offsets_size          = offsets_size;
  buffer->async_transaction     = is_async;
  buffer->pid                   = pid;
  buffer->oneway_spam_suspect   = false;
  list_add_tail(&alloc->allocated_buffers_list, &buffer->rb_node);

  binder_debug(BINDER_DEBUG_ALLOC_BUFFER,
               "alloc buffer success alloc->pid=%d buffer %p "
               "data %p size %d\n",
               alloc->pid, buffer, buffer->user_data,
               buffer->data_size);

  return buffer;

err_alloc_buf_struct_failed:
  page_range = (void  *)PAGE_ALIGN((uintptr_t)buffer->user_data);
  binder_update_page_range(alloc, 0, page_range, end_page_addr);
  *p_ret = -ENOMEM;
  return NULL;
}

static void delete_free_buffer(FAR struct binder_alloc *alloc,
                               FAR struct binder_buffer *buffer)
{
  FAR struct binder_buffer  *prev;
  FAR struct binder_buffer  *next = NULL;
  bool                  to_free = true;

  BUG_ON(alloc->buffers_list.next == &buffer->entry);
  prev = buffer_prev(buffer);
  BUG_ON(!prev->free);
  if (prev_buffer_end_page(prev) == buffer_start_page(buffer))
    {
      to_free = false;
      binder_debug(BINDER_DEBUG_ALLOC_BUFFER,
                   "%d: merge free, buffer %p share page with %p\n",
                   alloc->pid, buffer->user_data, prev->user_data);
    }

  if (!list_is_last(&buffer->entry, &alloc->buffers_list))
    {
      next = buffer_next(buffer);
      if (buffer_start_page(next) == buffer_start_page(buffer))
        {
          to_free = false;
          binder_debug(BINDER_DEBUG_ALLOC_BUFFER,
                       "%d: merge free, buffer %p share page with %p\n",
                       alloc->pid, buffer->user_data, next->user_data);
        }
    }

  if (PAGE_ALIGNED(buffer->user_data))
    {
      binder_debug(BINDER_DEBUG_ALLOC_BUFFER,
                   "%d: merge free, buffer start %p is page aligned\n",
                   alloc->pid, buffer->user_data);
      to_free = false;
    }

  if (to_free)
    {
      binder_debug(BINDER_DEBUG_ALLOC_BUFFER,
                   "%d: merge free, buffer %p do not share "
                   "page with %p or %p\n",
                   alloc->pid, buffer->user_data, prev->user_data,
                   next ? next->user_data : NULL);
      binder_update_page_range(
        alloc, 0, buffer_start_page(buffer),
        buffer_start_page(buffer) + PAGE_SIZE);
    }

  list_delete(&buffer->entry);
  kmm_free(buffer);
}

static void binder_free_buf_locked(FAR struct binder_alloc *alloc,
                                   FAR struct binder_buffer *buffer)
{
  size_t size;
  size_t buffer_size;

  buffer_size = alloc_buffer_size(alloc, buffer);

  size  = ALIGN(buffer->data_size, sizeof(void *));
  size  += ALIGN(buffer->offsets_size, sizeof(void *));

  binder_debug(BINDER_DEBUG_ALLOC_BUFFER,
               "%d: binder_free_buf %p size %zd buffer_size %zd\n",
               alloc->pid, buffer, size, buffer_size);

  BUG_ON(buffer->free);
  BUG_ON(size > buffer_size);
  BUG_ON(buffer->transaction != NULL);
  BUG_ON(buffer->user_data < alloc->buffer_data);
  BUG_ON(buffer->user_data > alloc->buffer_data + alloc->buffer_data_size);

  binder_update_page_range(alloc, 0,
                           (void  *)PAGE_ALIGN((uintptr_t)buffer->user_data),
                           (void  *)(((uintptr_t)buffer->user_data +
                                      buffer_size) & PAGE_MASK));

  list_delete_init(&buffer->rb_node);
  buffer->free = 1;
  if (!list_is_last(&buffer->entry, &alloc->buffers_list))
    {
      struct binder_buffer *next = buffer_next(buffer);

      if (next->free)
        {
          list_delete_init(&next->rb_node);
          delete_free_buffer(alloc, next);
        }
    }

  if (alloc->buffers_list.next != &buffer->entry)
    {
      struct binder_buffer *prev = buffer_prev(buffer);

      if (prev->free)
        {
          delete_free_buffer(alloc, buffer);
          list_delete_init(&prev->rb_node);
          buffer = prev;
        }
    }

  insert_free_buffer(alloc, buffer);
}

/****************************************************************************
 * Name: binder_alloc_get_page
 *
 * Description:
 *   Lookup the struct page corresponding to the address
 *   at buffer_offset into buffer->user_data. If pgoffp is not
 *   NULL, the byte-offset into the page is written there.
 *
 *   The caller is responsible to ensure that the offset points
 *   to a valid address within the buffer and that buffer is
 *   not freeable by the user. Since it can't be freed, we are
 *   guaranteed that the corresponding elements of alloc->pages[]
 *   cannot change.
 *
 * Input Parameters:
 *   alloc         - binder_alloc for this proc
 *   buffer        - binder buffer to be accessed
 *   buffer_offset - offset into buffer data
 *   pgoffp        - address to copy final page offset to
 *
 * Returned Value:
 *   struct page
 *
 ****************************************************************************/

static void *binder_alloc_get_page(
  FAR struct binder_alloc *alloc, FAR struct binder_buffer *buffer,
  binder_size_t buffer_offset, FAR unsigned long *pgoffp)
{
  binder_size_t              buffer_space_offset;
  unsigned long              pgoff;
  size_t                     index;
  FAR struct binder_page    *lru_page;

  buffer_space_offset = buffer_offset +
                        (buffer->user_data - alloc->buffer_data);
  pgoff = buffer_space_offset & ~PAGE_MASK;
  index = buffer_space_offset >> PAGE_SHIFT;

  lru_page  = &alloc->pages_array[index];
  *pgoffp   = pgoff;

  BUG_ON(lru_page->page_ptr == NULL);

  return lru_page->page_ptr;
}

/****************************************************************************
 * Name: binder_alloc_clear_buf
 *
 * Description:
 *   memset the given buffer to 0
 *
 * Input Parameters:
 *   alloc  - binder_alloc for this proc
 *   buffer - binder buffer to be cleared
 *
 ****************************************************************************/

static void binder_alloc_clear_buf(
  FAR struct binder_alloc *alloc, FAR struct binder_buffer *buffer)
{
  size_t        bytes           = alloc_buffer_size(alloc, buffer);
  binder_size_t buffer_offset   = 0;

  while (bytes)
    {
      unsigned long size;
      void          *page;
      unsigned long pgoff;
      void          *kptr;

      page  = binder_alloc_get_page(alloc, buffer, buffer_offset, &pgoff);
      size  = min(bytes, PAGE_SIZE - pgoff);
      kptr  = page + pgoff;
      memset(kptr, 0, size);
      bytes         -= size;
      buffer_offset += size;
    }
}

static int binder_alloc_do_buffer_copy(
  FAR struct binder_alloc *alloc, bool to_buffer,
  FAR struct binder_buffer *buffer, binder_size_t buffer_offset,
  FAR void *ptr, size_t bytes)
{
  unsigned long      size;
  FAR void          *page;
  unsigned long      pgoff;
  FAR void          *tmpptr;
  FAR void          *base_ptr;

  /* All copies must be 32-bit aligned and 32-bit size */

  if (!check_buffer(alloc, buffer, buffer_offset, bytes))
    {
      return -EINVAL;
    }

  while (bytes)
    {
      page      = binder_alloc_get_page(alloc, buffer,
                                        buffer_offset, &pgoff);
      size      = min(bytes, PAGE_SIZE - pgoff);
      base_ptr  = page;
      tmpptr    = base_ptr + pgoff;
      if (to_buffer)
        {
          memcpy(tmpptr, ptr, size);
        }
      else
        {
          memcpy(ptr, tmpptr, size);
        }

      bytes        -= size;
      pgoff         = 0;
      ptr           = ptr + size;
      buffer_offset += size;
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

  nxmutex_lock(&alloc->alloc_lock);
  buffer = prepare_to_free_locked(alloc, user_ptr);
  nxmutex_unlock(&alloc->alloc_lock);
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
 *   extra_buffers_size - size of extra space for meta-data
 *                        (eg, security context)
 *   is_async           - buffer for async transaction
 *   pid                - pid to attribute allocation to (used for debugging)
 *
 * Returned Value:
 *   The allocated buffer or NULL if error
 *
 ****************************************************************************/

FAR struct binder_buffer *binder_alloc_new_buf(
  FAR struct binder_alloc *alloc, size_t data_size, size_t offsets_size,
  size_t secctx_sz, int is_async, int pid, FAR int *ret)
{
  FAR struct binder_buffer *buffer;

  nxmutex_lock(&alloc->alloc_lock);
  buffer = binder_alloc_new_buf_locked(alloc, data_size, offsets_size,
                                       secctx_sz, is_async, pid, ret);
  nxmutex_unlock(&alloc->alloc_lock);
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

/****************************************************************************
 * Name: binder_alloc_free_buf
 *
 * Description:
 *   Free the buffer allocated via binder_alloc_new_buf()
 *
 * Input Parameters:
 *   alloc  - binder_alloc for this proc
 *   buffer - kernel pointer to buffer
 *
 ****************************************************************************/

void binder_alloc_free_buf(FAR struct binder_alloc *alloc,
                           FAR struct binder_buffer *buffer)
{
  if (buffer->clear_on_free)
    {
      binder_alloc_clear_buf(alloc, buffer);
      buffer->clear_on_free = false;
    }

  nxmutex_lock(&alloc->alloc_lock);
  binder_free_buf_locked(alloc, buffer);
  nxmutex_unlock(&alloc->alloc_lock);
}

void binder_alloc_deferred_release(FAR struct binder_alloc *alloc)
{
  int                        buffers;
  int                        page_count;
  FAR struct binder_buffer  *buffer;
  FAR struct binder_buffer  *buffer_itr;

  if (alloc->buffer_data_size == 0)
    {
      /* Open and close immediately, not do mmap */

      return;
    }

  buffers = 0;
  nxmutex_lock(&alloc->alloc_lock);

  list_for_every_entry_safe(&alloc->allocated_buffers_list, buffer,
                            buffer_itr, struct binder_buffer, rb_node)
    {
      /* Transaction should already have been freed */

      BUG_ON(buffer->transaction != NULL);

      if (buffer->clear_on_free)
        {
          binder_alloc_clear_buf(alloc, buffer);
          buffer->clear_on_free = false;
        }

      binder_free_buf_locked(alloc, buffer);
      buffers++;
    }

  while (!list_is_empty(&alloc->buffers_list))
    {
      buffer = list_first_entry(&alloc->buffers_list, struct binder_buffer,
                                entry);
      WARN_ON(!buffer->free);

      list_delete_init(&buffer->entry);
      WARN_ON(!list_is_empty(&alloc->buffers_list));
      kmm_free(buffer);
    }

  page_count = 0;
  if (alloc->pages_array)
    {
      int i;

      for (i = 0; i < alloc->buffer_data_size / PAGE_SIZE; i++)
        {
          if (!alloc->pages_array[i].page_ptr)
            {
              continue;
            }

          binder_debug(BINDER_DEBUG_ALLOC_BUFFER,
                       "alloc->pid=%d: page %d at %pK %s\n", alloc->pid, i,
                       (alloc->buffer_data + i * PAGE_SIZE), "active");
          alloc->pages_array[i].page_ptr = NULL;
          page_count++;
        }

      kmm_free(alloc->pages_array);
    }

  nxmutex_unlock(&alloc->alloc_lock);

  binder_debug(BINDER_DEBUG_OPEN_CLOSE,
               "alloc->pid=%d buffers %d, pages %d\n", alloc->pid, buffers,
               page_count);
}

/****************************************************************************
 * Name: binder_alloc_unmmap
 *
 * Description:
 *   Description of the operation of the function.
 *
 * Input Parameters:
 *   alloc - alloc structure for this proc
 *   vma   - vma passed to mmap().
 *
 * Returned Value:
 *   0 = success
 *
 ****************************************************************************/

int binder_alloc_unmmap(FAR struct binder_alloc *alloc,
                        FAR struct binder_mmap_area *vma)
{
  kmm_free(vma->area_start);
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
 *   alloc - alloc structure for this proc
 *   vma   - vma passed to mmap()
 *
 * Returned Value:
 *   0       : success
 *   -EBUSY  : address space already mapped
 *   -ENOMEM : failed to map memory to given address space
 *
 ****************************************************************************/

int binder_alloc_mmap(FAR struct binder_alloc *alloc,
                      FAR struct binder_mmap_area *vma)
{
  int                        ret;
  FAR const char            *failure_string;
  FAR struct binder_buffer  *buffer;

  nxmutex_lock(&alloc->alloc_lock);

  if (alloc->buffer_data_size)
    {
      ret               = -EBUSY;
      failure_string    = "already mapped";
      goto err_already_mapped;
    }

  vma->area_start = kmm_memalign(PAGE_SIZE, vma->area_size);
  if (vma->area_start == NULL)
    {
      ret               = -ENOMEM;
      failure_string    = "alloc map area failed";
      goto err_alloc_maparea_failed;
    }

  alloc->buffer_data_size = min(vma->area_size, SZ_4M);
  nxmutex_unlock(&alloc->alloc_lock);

  alloc->buffer_data = vma->area_start;

  alloc->pages_array =
    kmm_calloc(alloc->buffer_data_size / PAGE_SIZE,
               sizeof(alloc->pages_array[0]));

  if (alloc->pages_array == NULL)
    {
      ret               = -ENOMEM;
      failure_string    = "alloc page array";
      goto err_alloc_pages_failed;
    }

  buffer = kmm_zalloc(sizeof(*buffer));
  if (buffer == NULL)
    {
      ret               = -ENOMEM;
      failure_string    = "alloc buffer struct";
      goto err_alloc_buf_struct_failed;
    }

  list_initialize(&buffer->entry);
  list_initialize(&buffer->rb_node);

  buffer->user_data = alloc->buffer_data;
  list_add_tail(&alloc->buffers_list, &buffer->entry);
  buffer->free = 1;
  insert_free_buffer(alloc, buffer);

  binder_debug(BINDER_DEBUG_ALLOC_BUFFER,
               "alloc->pid=%d map area %p-%p success\n", alloc->pid,
               vma->area_start, (void *)(vma->area_start + vma->area_size));

  return 0;

err_alloc_buf_struct_failed:
  kmm_free(alloc->pages_array);
  alloc->pages_array = NULL;
err_alloc_pages_failed:
  alloc->buffer_data = NULL;
  nxmutex_lock(&alloc->alloc_lock);
  alloc->buffer_data_size = 0;
err_already_mapped:
err_alloc_maparea_failed:
  nxmutex_unlock(&alloc->alloc_lock);
  _err("ERROR: %s: %d %lx-%lx %s failed %d\n",
       __func__, alloc->pid, (unsigned long)vma->area_start,
       (unsigned long)(vma->area_start + vma->area_size),
       failure_string, ret);
  return ret;
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

void binder_alloc_init(FAR struct binder_alloc *alloc, pid_t pid)
{
  alloc->pid                = pid;
  alloc->buffer_data        = NULL;
  alloc->buffer_data_size   = 0;

  nxmutex_init(&alloc->alloc_lock);
  list_initialize(&alloc->buffers_list);
  list_initialize(&alloc->free_buffers_list);
  list_initialize(&alloc->allocated_buffers_list);

  alloc->pages_array = NULL;
}
