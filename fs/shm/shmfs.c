/****************************************************************************
 * fs/shm/shmfs.c
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

#include <nuttx/fs/ioctl.h>
#include <nuttx/mm/map.h>
#include <nuttx/arch.h>
#include <nuttx/pgalloc.h>
#include <nuttx/sched.h>

#include "shm/shmfs.h"
#include "inode/inode.h"
#include "sched/sched.h"

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int shmfs_close(FAR struct file *filep);
static ssize_t shmfs_read(FAR struct file *filep, FAR char *buffer,
                          size_t buflen);
static ssize_t shmfs_write(FAR struct file *filep, FAR const char *buffer,
                           size_t buflen);
static int shmfs_truncate(FAR struct file *filep, off_t length);

#ifndef CONFIG_DISABLE_PSEUDOFS_OPERATIONS
static int shmfs_unlink(FAR struct inode *inode);
#endif

#ifdef CONFIG_FS_FILEMAP
static int shmfs_mmap(FAR struct file *filep,
                      FAR struct mm_map_entry_s *entry);
static int shmfs_munmap(FAR struct task_group_s *group,
                        FAR struct mm_map_entry_s *entry,
                        FAR void *start,
                        size_t length);
#endif

/****************************************************************************
 * Public Data
 ****************************************************************************/

const struct file_operations g_shmfs_operations =
{
  NULL,             /* open */
  shmfs_close,      /* close */
  shmfs_read,       /* read */
  shmfs_write,      /* write */
  NULL,             /* seek */
  NULL,             /* ioctl */
  shmfs_truncate,   /* truncate */
  NULL,             /* poll */
  NULL,             /* readv */
  NULL,             /* writev */
#ifdef CONFIG_FS_FILEMAP
  shmfs_mmap,       /* mmap */
#endif
#ifndef CONFIG_DISABLE_PSEUDOFS_OPERATIONS
  shmfs_unlink      /* unlink */
#endif
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: shmfs_read
 ****************************************************************************/

static ssize_t shmfs_read(FAR struct file *filep, FAR char *buffer,
                          size_t buflen)
{
  FAR struct shmfs_object_s *sho;
  ssize_t nread;
  off_t startpos;
  off_t endpos;

  DEBUGASSERT(filep->f_inode->i_private != NULL);

  sho = filep->f_inode->i_private;

  if (filep->f_pos > sho->length)
    {
      return 0;
    }

  /* Handle attempts to read beyond the end of the file. */

  startpos = filep->f_pos;
  nread    = buflen;
  endpos   = startpos + buflen;

  if (endpos > sho->length)
    {
      endpos = sho->length;
      nread  = endpos - startpos;
    }

  /* Copy data from the memory object to the user buffer */

  if (sho->paddr != NULL)
    {
      memcpy(buffer, (FAR char *)sho->paddr + startpos, nread);
      filep->f_pos += nread;
    }
  else
    {
      DEBUGASSERT(sho->length == 0 && nread == 0);
    }

  return nread;
}

/****************************************************************************
 * Name: shmfs_write
 ****************************************************************************/

static ssize_t shmfs_write(FAR struct file *filep, FAR const char *buffer,
                           size_t buflen)
{
  FAR struct shmfs_object_s *sho;
  ssize_t nwritten;
  off_t startpos;
  off_t endpos;

  DEBUGASSERT(filep->f_inode->i_private != NULL);

  sho = filep->f_inode->i_private;

  /* Handle attempts to write beyond the end of the file */

  startpos = filep->f_pos;
  nwritten = buflen;
  endpos   = startpos + buflen;

  /* Desn't support shm auto expand, truncate first */

  if (endpos > sho->length)
    {
      return -EFBIG;
    }

  /* Copy data from the user buffer to the memory object */

  if (sho->paddr != NULL)
    {
      memcpy((FAR char *)sho->paddr + startpos, buffer, nwritten);
      filep->f_pos += nwritten;
    }
  else
    {
      DEBUGASSERT(sho->length == 0 && nwritten == 0);
    }

  return nwritten;
}

/****************************************************************************
 * Name: shmfs_release
 ****************************************************************************/

static int shmfs_release(FAR struct inode *inode)
{
  /* If the file has been unlinked previously, delete the contents.
   * The inode is released after this call, hence checking if i_crefs <= 1.
   */

  if (inode->i_parent == NULL && atomic_read(&inode->i_crefs) <= 1)
    {
      shmfs_free_object(inode->i_private);
      inode->i_private = NULL;
    }

  return OK;
}

/****************************************************************************
 * Name: shmfs_close
 ****************************************************************************/

static int shmfs_close(FAR struct file *filep)
{
  /* Release the shmfs object. The object gets deleted if no-one has
   * reference to it (either mmap or open file) and the object has been
   * unlinked
   */

  return shmfs_release(filep->f_inode);
}

/****************************************************************************
 * Name: shmfs_truncate
 ****************************************************************************/

static int shmfs_truncate(FAR struct file *filep, off_t length)
{
  FAR struct shmfs_object_s *object;
  FAR struct shmfs_object_s *newobj;
  int ret = 0;

  inode_lock();
  object = filep->f_inode->i_private;
  if (!object)
    {
      filep->f_inode->i_private = shmfs_alloc_object(length);
      if (!filep->f_inode->i_private)
        {
          filep->f_inode->i_size = 0;
          ret = -EFAULT;
        }
      else
        {
          filep->f_inode->i_size = length;
        }
    }
  else if (object->length != length)
    {
      /* Re-allocate to the requested length (no in-place resize) */

      newobj = shmfs_alloc_object(length);
      if (newobj != NULL)
        {
          shmfs_free_object(object);
          filep->f_inode->i_private = newobj;
          filep->f_inode->i_size = length;
        }
      else
        {
          ret = -ENOMEM;
        }
    }

  inode_unlock();
  return ret;
}

/****************************************************************************
 * Name: shmfs_unlink
 ****************************************************************************/

#ifndef CONFIG_DISABLE_PSEUDOFS_OPERATIONS
static int shmfs_unlink(FAR struct inode *inode)
{
  if (atomic_read(&inode->i_crefs) <= 1)
    {
      shmfs_free_object(inode->i_private);
      inode->i_private = NULL;
    }

  return OK;
}
#endif

#ifdef CONFIG_FS_FILEMAP
/****************************************************************************
 * Name: shmfs_map_object
 ****************************************************************************/

static int shmfs_map_object(FAR struct shmfs_object_s *object,
                            FAR void **vaddr)
{
  int ret = OK;

#ifdef CONFIG_BUILD_KERNEL
  /* Map the physical pages of the shm object with MMU. */

  FAR struct tcb_s *tcb = this_task();
  FAR struct task_group_s *group = tcb->group;
  FAR uintptr_t *pages = (FAR uintptr_t *)&object->paddr;
  uintptr_t mapaddr;
  unsigned int npages;

  /* Find a free vaddr space that satisfies length */

  mapaddr = (uintptr_t)vm_alloc_region(get_group_mm(group), 0,
                                       object->length);
  if (mapaddr == 0)
    {
      return -ENOMEM;
    }

  /* Convert the region size to pages */

  npages = MM_NPAGES(object->length);

  /* Map the memory to user virtual address space */

  ret = up_shmat(pages, npages, mapaddr);
  if (ret < 0)
    {
      vm_release_region(get_group_mm(group), (FAR void *)mapaddr,
                        object->length);
    }
  else
    {
      *vaddr = (FAR void *)mapaddr;
    }
#else
  /* Use the physical address directly */

  *vaddr = object->paddr;
#endif

  return ret;
}

/****************************************************************************
 * Name: shmfs_add_map
 ****************************************************************************/

static int shmfs_add_map(FAR struct mm_map_entry_s *entry,
                         FAR struct inode *inode)
{
  FAR struct mm_map_s *mm = get_current_mm();

  if (mm_map_find(mm, entry->vaddr, entry->length))
    {
      return OK;
    }

  entry->munmap = shmfs_munmap;
  entry->priv.p = (FAR void *)inode;

  return mm_map_add(mm, entry);
}

/****************************************************************************
 * Name: shmfs_mmap
 ****************************************************************************/

static int shmfs_mmap(FAR struct file *filep,
                      FAR struct mm_map_entry_s *entry)
{
  FAR struct shmfs_object_s *object;
  int ret = -EINVAL;

  /* We don't support offset at the moment, just mapping the whole object
   * object is NULL if it hasn't been truncated yet
   */

  if (entry->offset != 0)
    {
      return ret;
    }

  /* Keep the inode when mmapped, increase refcount */

  inode_addref(filep->f_inode);
  object = filep->f_inode->i_private;
  if (object)
    {
      ret = shmfs_map_object(object, &entry->vaddr);
    }

  if (ret < 0 ||
      (ret = shmfs_add_map(entry, filep->f_inode)) < 0)
    {
      inode_release(filep->f_inode);
    }

  return ret;
}

/****************************************************************************
 * Name: shmfs_unmap_object
 ****************************************************************************/

static int shmfs_unmap_area(FAR struct task_group_s *group,
                            FAR void *vaddr, size_t length)
{
  int ret = OK;

#ifdef CONFIG_BUILD_KERNEL
  unsigned int npages;

  /* Convert the region size to pages */

  if (group)
    {
      npages = MM_NPAGES(length);

      /* Unmap the memory from user virtual address space */

      ret = up_shmdt((uintptr_t)vaddr, npages);
      if (ret < 0)
        {
          return ret;
        }

      /* Free the virtual address space */

      vm_release_region(get_group_mm(group), vaddr, length);
    }
  else
    {
      return -EINVAL;
    }
#endif

  return ret;
}

/****************************************************************************
 * Name: shmfs_munmap_partial
 *
 * Description:
 *   Handle a partial unmap request on a shm object.  The covering mapping
 *   record is shrunk (unmap of the head or tail) or split in two (unmap of
 *   the middle) so that the still-mapped sub-region(s) remain valid.  The
 *   inode reference taken at mmap() time is preserved because the object
 *   stays (partially) mapped; a middle split creates a second mapping and
 *   therefore takes one extra reference so that each resulting mapping owns
 *   exactly one.
 *
 *   On BUILD_FLAT mmap() hands back the physical address of the whole
 *   object and munmap() frees no memory (the object lives until the last
 *   open descriptor is closed and the name is unlinked), so a partial unmap
 *   is pure bookkeeping.  On a BUILD_KERNEL (MMU) build the unmapped
 *   sub-range is additionally torn down from the caller's address space via
 *   vm_unmap_region(); the physical pages stay owned by the object and are
 *   freed only when the last mapping is removed.
 *
 ****************************************************************************/

static int shmfs_munmap_partial(FAR struct mm_map_s *mm,
                                FAR struct mm_map_entry_s *entry,
                                FAR void *start, size_t length)
{
  FAR char *entry_end = (FAR char *)entry->vaddr + entry->length;
  FAR char *unmap_end = (FAR char *)start + length;
  off_t head = (FAR char *)start - (FAR char *)entry->vaddr;
  off_t tail = entry_end - unmap_end;

#ifdef CONFIG_BUILD_KERNEL
  /* Tear down the unmapped sub-range from the caller's address space and
   * release its virtual region.  The physical pages remain owned by the shm
   * object (freed only when the last mapping is removed), so this never
   * frees the backing store.
   */

  int ret = vm_unmap_region(mm, start, length);
  if (ret < 0)
    {
      return ret;
    }
#endif

  if (head > 0 && tail > 0)
    {
      /* Unmapping the middle: keep the current entry as the head part and
       * add a new entry describing the tail part.
       */

      struct mm_map_entry_s tailmap = *entry;

      tailmap.vaddr   = unmap_end;
      tailmap.length  = tail;
      tailmap.offset  = entry->offset + head + length;

      entry->length = head;

      inode_addref((FAR struct inode *)entry->priv.p);
      return mm_map_add(mm, &tailmap);
    }
  else if (tail > 0)
    {
      /* Unmapping the head: advance the start of the mapping */

      entry->vaddr   = unmap_end;
      entry->offset += length;
      entry->length  = tail;
    }
  else
    {
      /* Unmapping the tail: just shrink the length */

      entry->length = head;
    }

  return OK;
}

/****************************************************************************
 * Name: shmfs_munmap
 ****************************************************************************/

static int shmfs_munmap(FAR struct task_group_s *group,
                        FAR struct mm_map_entry_s *entry,
                        FAR void *start,
                        size_t length)
{
  FAR struct inode *inode;
  int ret;

  if (start != entry->vaddr || length != entry->length)
    {
      /* Partial unmap: adjust the mapping bookkeeping and, on a MMU build,
       * tear down the affected sub-range.  No backing memory is freed here;
       * the object lives until its last mapping is removed and it has been
       * unlinked.
       */

      return shmfs_munmap_partial(get_group_mm(group), entry, start, length);
    }

  inode = (FAR struct inode *)entry->priv.p;

  /* Unmap the virtual memory area from the user's address space */

  ret = shmfs_unmap_area(group, entry->vaddr, entry->length);

  /* Release the shmfs object. The object gets deleted if no-one has
   * reference to it (either mmap or open file) and the object has been
   * unlinked
   */

  if (ret == OK)
    {
      ret = shmfs_release(inode);
    }

  /* Unkeep the inode when unmapped, decrease refcount */

  if (ret == OK)
    {
      inode_release(inode);

      /* Remove the mapping. */

      ret = mm_map_remove(get_group_mm(group), entry);
    }

  return ret;
}
#endif /* CONFIG_FS_FILEMAP */
