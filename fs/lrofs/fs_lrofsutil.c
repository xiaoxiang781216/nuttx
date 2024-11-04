/****************************************************************************
 * fs/lrofs/fs_lrofsutil.c
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
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <debug.h>
#include <libgen.h>

#include <nuttx/fs/ioctl.h>

#include "fs_lrofs.h"
#include "fs_heap.h"

/****************************************************************************
 * Pre-processor Declarations
 ****************************************************************************/

#define LINK_NOT_FOLLOWED 0
#define LINK_FOLLOWED     1
#define NODEINFO_NINCR    4

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct lrofs_entryname_s
{
  FAR const char *le_name;
  size_t le_len;
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int lrofs_nodeinfo_search(FAR const void *a, FAR const void *b)
{
  FAR struct lrofs_nodeinfo_s *ln = *(FAR struct lrofs_nodeinfo_s **)b;
  FAR const struct lrofs_entryname_s *le = a;
  FAR const char *name = ln->ln_name;
  size_t len = ln->ln_namesize;
  int ret;

  if (len > le->le_len)
    {
      len = le->le_len;
    }

  ret = memcmp(le->le_name, name, len);
  if (ret == 0)
    {
      if (le->le_name[len] == '/' || le->le_name[len] == '\0')
        {
          return name[len] == '\0' ? 0 : -1;
        }
      else
        {
          return 1;
        }
    }

  return ret;
}

static int lrofs_nodeinfo_compare(FAR const void *a, FAR const void *b)
{
  FAR struct lrofs_nodeinfo_s *ln = *(FAR struct lrofs_nodeinfo_s **)a;
  struct lrofs_entryname_s le;

  le.le_name = ln->ln_name;
  le.le_len = ln->ln_namesize;
  return lrofs_nodeinfo_search(&le, b);
}

/****************************************************************************
 * Name: lrofs_devmemcpy
 ****************************************************************************/

static void lrofs_devmemcpy(FAR struct lrofs_mountpt_s *lm,
                            int ndx, FAR const void *buf, size_t len)
{
  memcpy(lm->lm_devbuffer + ndx, buf, len);
}

/****************************************************************************
 * Name: lrofs_devstrcpy
 ****************************************************************************/

static void lrofs_devstrcpy(FAR struct lrofs_mountpt_s *lm,
                            int ndx, FAR const char *buf)
{
  strcpy((FAR char *)lm->lm_devbuffer + ndx, buf);
}

/****************************************************************************
 * Name: lrofs_devload32
 *
 * Description:
 *   Load the big-endian 32-bit value to the mount device buffer
 *
 ****************************************************************************/

static uint32_t lrofs_devload32(FAR struct lrofs_mountpt_s *lm, int ndx)
{
  return ((((uint32_t)lm->lm_devbuffer[ndx]     & 0xff) << 24) |
          (((uint32_t)lm->lm_devbuffer[ndx + 1] & 0xff) << 16) |
          (((uint32_t)lm->lm_devbuffer[ndx + 2] & 0xff) << 8) |
           ((uint32_t)lm->lm_devbuffer[ndx + 3] & 0xff));
}

/****************************************************************************
 * Name: lrofs_devread32
 *
 * Description:
 *   Read the big-endian 32-bit value from the mount device buffer
 *
 * Assumption:
 *   All values are aligned to 32-bit boundaries
 *
 ****************************************************************************/

static uint32_t lrofs_devread32(FAR struct lrofs_mountpt_s *lm, int ndx)
{
  /* This should not read past the end of the sector since the directory
   * entries are aligned at 16-byte boundaries.
   */

  return ((((uint32_t)lm->lm_buffer[ndx]     & 0xff) << 24) |
          (((uint32_t)lm->lm_buffer[ndx + 1] & 0xff) << 16) |
          (((uint32_t)lm->lm_buffer[ndx + 2] & 0xff) << 8) |
           ((uint32_t)lm->lm_buffer[ndx + 3] & 0xff));
}

/****************************************************************************
 * Name: lrofs_devwrite32
 *
 * Description:
 *   Write the big-endian 32-bit value to the mount device buffer
 *
 ****************************************************************************/

static void lrofs_devwrite32(FAR struct lrofs_mountpt_s *lm,
                             int ndx, uint32_t value)
{
  /* Write the 32-bit value to the specified index in the buffer */

  lm->lm_devbuffer[ndx]     = (uint8_t)(value >> 24) & 0xff;
  lm->lm_devbuffer[ndx + 1] = (uint8_t)(value >> 16) & 0xff;
  lm->lm_devbuffer[ndx + 2] = (uint8_t)(value >> 8) & 0xff;
  lm->lm_devbuffer[ndx + 3] = (uint8_t)(value & 0xff);
}

/****************************************************************************
 * Name: lrofs_hwwrite
 *
 * Description:
 *   Write the specified number of sectors to the block device
 *
 ****************************************************************************/

static int lrofs_hwwrite(FAR struct lrofs_mountpt_s *lm, FAR uint8_t *buffer,
                         uint32_t sector, unsigned int nsectors)
{
  FAR struct inode *inode = lm->lm_blkdriver;
  ssize_t ret = -ENODEV;

  if (inode->u.i_bops->write)
    {
      ret = inode->u.i_bops->write(inode, buffer, sector, nsectors);
    }

  if (ret == (ssize_t)nsectors)
    {
      ret = 0;
    }

  return ret;
}

/****************************************************************************
 * Name: lrofs_devcachewrite
 *
 * Description:
 *   Write the specified sector for specified offset into the sector cache.
 *
 ****************************************************************************/

static int lrofs_devcachewrite(FAR struct lrofs_mountpt_s *lm,
                               uint32_t sector)
{
  int ret;

  ret = lrofs_hwwrite(lm, lm->lm_devbuffer, sector, 1);
  if (ret >= 0)
    {
      lm->lm_cachesector = sector;
    }
  else
    {
      lm->lm_cachesector = (uint32_t)-1;
    }

  return ret;
}

/****************************************************************************
 * Name: lrofs_filecachewrite
 *
 * Description:
 *   Write the specified sector for specified offset into the sector cache.
 *
 ****************************************************************************/

static int lrofs_filecachewrite(FAR struct lrofs_mountpt_s *lm,
                                FAR struct lrofs_file_s *lf)
{
  int ret;

  ret = lrofs_hwwrite(lm, lf->lf_buffer, lf->lf_cachesector,
                      lf->lf_ncachesector);
  if (ret < 0)
    {
      ferr("ERROR: lrofs_hwwrite failed: %d\n", ret);
    }

  return ret;
}

/****************************************************************************
 * Name: lrofs_devcacheload
 *
 * Description:
 *   Read the specified sector for specified offset into the sector cache.
 *   Return the index into the sector corresponding to the offset
 *
 ****************************************************************************/

static int16_t lrofs_devcacheload(FAR struct lrofs_mountpt_s *lm,
                                  uint32_t offset)
{
  uint32_t sector;
  int      ret;

  sector = SEC_NSECTORS(lm, offset);
  if (lm->lm_cachesector != sector)
    {
      ret = lrofs_hwread(lm, lm->lm_devbuffer, sector, 1);
      if (ret < 0)
        {
          return (int16_t)ret;
        }

      lm->lm_cachesector = sector;
    }

  return offset & SEC_NDXMASK(lm);
}

/****************************************************************************
 * Name: lrofs_searchdir
 *
 * Description:
 *   This is part of the lrofs_finddirentry.  Search the directory
 *   beginning at nodeinfo->rn_offset for entryname.
 *
 ****************************************************************************/

static FAR struct lrofs_nodeinfo_s *
lrofs_searchdir(FAR struct lrofs_mountpt_s *lm, FAR const char *entryname,
                int entrylen, FAR struct lrofs_nodeinfo_s *ln)
{
  FAR struct lrofs_nodeinfo_s **cnodeinfo;
  struct lrofs_entryname_s le;

  le.le_name = entryname;
  le.le_len = entrylen;
  cnodeinfo = bsearch(&le, ln->ln_child, ln->ln_count,
                      sizeof(*ln->ln_child), lrofs_nodeinfo_search);
  if (cnodeinfo)
    {
      return *cnodeinfo;
    }

  return NULL;
}

/****************************************************************************
 * Name: lrofs_alloc_sparenode
 *
 * Description:
 *   Allocate the spare node
 *
 ****************************************************************************/

static FAR struct lrofs_sparenode_s *
lrofs_alloc_sparenode(uint32_t start, uint32_t end)
{
  FAR struct lrofs_sparenode_s *node;
  node = fs_heap_malloc(sizeof(struct lrofs_sparenode_s));
  if (node == NULL)
    {
      ferr("lrofs_alloc_sparenode: no memory\n");
      return NULL;
    }

  node->start = start;
  node->end = end;
  return node;
}

/****************************************************************************
 * Name: lrofs_init_sparelist
 *
 * Description:
 *   Init the sparelist
 *
 ****************************************************************************/

static int lrofs_init_sparelist(FAR struct lrofs_mountpt_s *lm)
{
  FAR struct lrofs_sparenode_s *node;

  list_initialize(&lm->lm_sparelist);
  node = lrofs_alloc_sparenode(0, lm->lm_hwsectorsize *
                               lm->lm_hwnsectors);
  if (node == NULL)
    {
      return -ENOMEM;
    }

  list_add_head(&lm->lm_sparelist, &node->node);
  lm->lm_volsize = 0;
  return 0;
}

/****************************************************************************
 * Name: lrofs_alloc_spareregion
 *
 * Description:
 *   Allocate the spare region
 *
 ****************************************************************************/

static int lrofs_alloc_spareregion(FAR struct list_node *list,
                                   uint32_t start, uint32_t end)
{
  FAR struct lrofs_sparenode_s *node;

  list_for_every_entry(list, node, struct lrofs_sparenode_s, node)
    {
      /* Find the node that start ~ end
       * is in node->start ~ node->end
       */

      if (start == node->start && end == node->end)
        {
          /* Delete the node */

          list_delete(&node->node);
          fs_heap_free(node);
          return 0;
        }
      else if (start == node->start && end < node->end)
        {
          /* Update the node */

          node->start = end;
          return 0;
        }
      else if (end == node->end && start > node->start)
        {
          /* Update the node */

          node->end = start;
          return 0;
        }
      else if (start > node->start && end < node->end)
        {
          /* Split the node */

          FAR struct lrofs_sparenode_s *new;
          new = lrofs_alloc_sparenode(end, node->end);
          if (new == NULL)
            {
              return -ENOMEM;
            }

          node->end = start;
          list_add_after(&node->node, &new->node);
          return 0;
        }
    }

  /* Not found */

  ferr("No space for start %" PRIu32 ", end %" PRIu32 "\n", start, end);
  return -ENOENT;
}

/****************************************************************************
 * Name: lrofs_free_spareregion
 *
 * Description:
 *   Free the spare region
 *
 ****************************************************************************/

static int lrofs_free_spareregion(FAR struct list_node *list,
                                  uint32_t start, uint32_t end)
{
  FAR struct lrofs_sparenode_s *node;
  FAR struct lrofs_sparenode_s *next;
  FAR struct lrofs_sparenode_s *new;

  list_for_every_entry(list, node, struct lrofs_sparenode_s, node)
    {
      /* Check if the freed space is adjacent to the node */

      if (start == node->end)
        {
          /* Update the node */

          node->end = end;

          /* Check if the freed space is adjacent to the next node */

          if (node->node.next != list)
            {
              next = container_of(node->node.next, struct lrofs_sparenode_s,
                                  node);
              if (end == next->start)
                {
                  /* Merge the node */

                  node->end = next->end;
                  list_delete(&next->node);
                  fs_heap_free(next);
                }
            }

          return 0;
        }
      else if (end == node->start)
        {
          /* Update the node */

          node->start = start;
          return 0;
        }
      else if (end < node->start)
        {
          new = lrofs_alloc_sparenode(start, end);
          if (new == NULL)
            {
              return -ENOMEM;
            }

          list_add_before(&node->node, &new->node);
          return 0;
        }
    }

  ferr("No spare area fixed for start %" PRIu32 " end %" PRIu32 "\n",
       start, end);
  return -ENOENT;
}

/****************************************************************************
 * Name: lrofs_find_alinged_fileaddr
 *
 * Description:
 *   Find the aligned file address
 *
 ****************************************************************************/

static uint32_t lrofs_find_alinged_fileaddr(FAR struct lrofs_mountpt_s *lm,
                                            uint32_t start, uint32_t maxsize,
                                            uint32_t size)
{
  uint32_t offset = SEC_ALIGNUP(lm, start);
  uint32_t head_addr;

  while (offset - start < size)
    {
      offset += lm->lm_hwsectorsize;
      if (offset > start + maxsize)
        {
          return 0;
        }
    }

  head_addr = offset - size;
  if (head_addr >= start && head_addr + size <= start + maxsize)
    {
      return head_addr;
    }

  return 0;
}

/****************************************************************************
 * Name: lrofs_add_sparenode
 *
 * Description:
 *   Add the spare node
 *
 ****************************************************************************/

static uint32_t lrofs_add_sparenode(FAR struct lrofs_mountpt_s *lm,
                                    uint32_t size, bool isdir)
{
  FAR struct list_node *list = &lm->lm_sparelist;
  FAR struct lrofs_sparenode_s *node;
  uint32_t offset = 0;

  if (isdir)
    {
      list_for_every_entry(list, node, struct lrofs_sparenode_s, node)
        {
          if (node->end - node->start >= size)
            {
              uint32_t aligned = SEC_ALIGNUP(lm, node->start);
              offset = ROMFS_ALIGNUP(node->start);
              if (aligned - offset < size)
                {
                  offset = aligned;
                }

              if (offset + size <= node->end)
                {
                  lrofs_alloc_spareregion(list, offset, offset + size);
                  return offset;
                }
            }
        }
    }
  else
    {
      /*  Return the max size spare node for file */

      uint32_t maxsize = 0;
      list_for_every_entry(list, node, struct lrofs_sparenode_s, node)
        {
          if (node->end - node->start > maxsize)
            {
              offset = node->start;
              maxsize = node->end - node->start;
            }
        }

      if (maxsize >= size)
        {
          offset = lrofs_find_alinged_fileaddr(lm, offset, maxsize, size);
          if (offset)
            {
              lrofs_alloc_spareregion(list, offset, offset + size);
              return offset;
            }
        }
    }

  return 0;
}

/****************************************************************************
 * Name: lrofs_update_parentnode
 *
 * Description:
 *   Update the parent node
 *
 ****************************************************************************/

static int lrofs_update_parentnode(FAR struct lrofs_nodeinfo_s *ln_parent,
                                   FAR struct lrofs_nodeinfo_s *ln)
{
  if (ln_parent->ln_child == NULL ||
      ln_parent->ln_count == ln_parent->ln_max - 1)
    {
      FAR void *tmp;
      tmp = fs_heap_realloc(ln_parent->ln_child,
                           (ln_parent->ln_max + NODEINFO_NINCR) *
                            sizeof(*ln_parent->ln_child));
      if (tmp == NULL)
        {
          return -ENOMEM;
        }

      ln_parent->ln_child = tmp;
      memset(ln_parent->ln_child + ln_parent->ln_max, 0, NODEINFO_NINCR *
              sizeof(*ln_parent->ln_child));
      ln_parent->ln_max += NODEINFO_NINCR;
    }

  ln_parent->ln_child[ln_parent->ln_count++] = ln;
  if (ln_parent->ln_count > 1)
    {
      qsort(ln_parent->ln_child, ln_parent->ln_count,
            sizeof(*ln_parent->ln_child), lrofs_nodeinfo_compare);
    }

  return OK;
}

/****************************************************************************
 * Name: lrofs_find_parentnode
 *
 * Description:
 *   Find the parent node
 *
 ****************************************************************************/

static FAR struct lrofs_nodeinfo_s *
lrofs_find_parentnode(FAR struct lrofs_mountpt_s *lm, FAR const char *path)
{
  FAR struct lrofs_nodeinfo_s *ln_parent;
  FAR struct lrofs_nodeinfo_s **pinfo;
  FAR char *parentpath;
  struct lrofs_entryname_s le;

  le.le_name = basename((FAR char *)path);
  le.le_len  = strlen(le.le_name);
  parentpath = dirname((FAR char *)path);
  if (strcmp(parentpath, ".") == 0)
    {
      ln_parent = lm->lm_root;
    }
  else
    {
      ln_parent = lrofs_finddirentry(lm, parentpath);
      if (ln_parent == NULL)
        {
          return NULL;
        }
    }

  pinfo = bsearch(&le, ln_parent->ln_child, ln_parent->ln_count,
                  sizeof(*(ln_parent->ln_child)), lrofs_nodeinfo_search);
  if (pinfo)
    {
      return *pinfo;
    }

  return NULL;
}

/****************************************************************************
 * Name: lrofs_get_prevnode
 *
 * Description:
 *   Get the previous node
 *
 ****************************************************************************/

static FAR struct lrofs_nodeinfo_s *
lrofs_get_prevnode(FAR struct lrofs_nodeinfo_s *ln, FAR bool *firstchild)
{
  FAR struct lrofs_nodeinfo_s *ln_parent = ln->ln_parent;
  FAR struct lrofs_nodeinfo_s *ln_prevnode;
  int i;

  *firstchild = false;
  if (ln_parent == NULL)
    {
      return NULL;
    }

  for (i = 0; i < ln_parent->ln_count; i++)
    {
      ln_prevnode = ln_parent->ln_child[i];
      if ((ln_prevnode->ln_next & RFNEXT_OFFSETMASK) ==
          (ln->ln_origoffset & RFNEXT_OFFSETMASK))
        {
          return ln_prevnode;
        }
    }

  *firstchild = true;
  return ln_parent;
}

/****************************************************************************
 * Name: lrofs_add_disk
 *
 * Description:
 *   Add the node to disk
 *
 ****************************************************************************/

static int lrofs_add_disk(FAR struct lrofs_mountpt_s *lm,
                          FAR struct lrofs_nodeinfo_s *ln_prev,
                          FAR struct lrofs_nodeinfo_s *ln,
                          uint32_t type, bool firstchild)
{
  FAR struct lrofs_nodeinfo_s *ln_parent = ln->ln_parent;
  uint32_t node_info = 0;
  uint32_t pre_next;
  int16_t ndx;
  int ret;

  /* Get the node sector index linkoffset */

  ndx = lrofs_devcacheload(lm, ln->ln_origoffset);
  if (ndx < 0)
    {
      return ndx;
    }

  if (strcmp(ln->ln_name, ".") == 0)
    {
      node_info = ln_parent->ln_origoffset & RFNEXT_OFFSETMASK;
    }
  else if (strcmp(ln->ln_name, "..") == 0)
    {
      node_info = (ln_parent->ln_parent == NULL) ? 32 :
                   ln_parent->ln_origoffset & RFNEXT_OFFSETMASK;
    }

  lrofs_devwrite32(lm, ndx + ROMFS_FHDR_INFO, node_info);
  lrofs_devwrite32(lm, ndx + ROMFS_FHDR_NEXT, type);
  lrofs_devwrite32(lm, ndx + ROMFS_FHDR_SIZE, 0);
  lrofs_devwrite32(lm, ndx + ROMFS_FHDR_CHKSUM, 0);
  memcpy(lm->lm_devbuffer + ndx + ROMFS_FHDR_NAME, ln->ln_name,
         ln->ln_namesize + 1);
  ret = lrofs_devcachewrite(lm, SEC_NSECTORS(lm, ln->ln_origoffset));
  if (ret < 0)
    {
      return ret;
    }

  /* Get the prevnode sector index */

  ndx = lrofs_devcacheload(lm, ln_prev->ln_origoffset);
  if (ndx < 0)
    {
      return ndx;
    }

  /* Update the prevnode next */

  if (firstchild)
    {
      lrofs_devwrite32(lm, ndx + ROMFS_FHDR_INFO,
                      (ln->ln_origoffset & RFNEXT_OFFSETMASK));
      return lrofs_devcachewrite(lm,
                                 SEC_NSECTORS(lm, ln_prev->ln_origoffset));
    }

  pre_next = lrofs_devload32(lm, ndx + ROMFS_FHDR_NEXT);
  lrofs_devwrite32(lm, ndx + ROMFS_FHDR_NEXT,
                   (ln->ln_origoffset & RFNEXT_OFFSETMASK) |
                   (pre_next & RFNEXT_ALLMODEMASK));

  return lrofs_devcachewrite(lm, SEC_NSECTORS(lm, ln_prev->ln_origoffset));
}

/****************************************************************************
 * Name: lrofs_alloc_nodeinfo
 *
 * Description:
 *   Alloc the nodeinfo
 *
 ****************************************************************************/

static FAR struct lrofs_nodeinfo_s *
lrofs_alloc_nodeinfo(uint32_t offset, uint32_t size, uint32_t next,
                     FAR const char *name)
{
  FAR struct lrofs_nodeinfo_s *ln;

  ln = fs_heap_zalloc(sizeof(struct lrofs_nodeinfo_s) + strlen(name));
  if (ln == NULL)
    {
      return NULL;
    }

  ln->ln_origoffset = offset;
  ln->ln_offset     = offset;
  ln->ln_next       = next;
  ln->ln_size       = size;
  ln->ln_namesize   = strlen(name);
  memcpy(ln->ln_name, name, strlen(name) + 1);
  return ln;
}

/****************************************************************************
 * Name: lrofs_remove_disk
 *
 * Description:
 *   Remove the node from disk
 *
 ****************************************************************************/

static int lrofs_remove_disk(FAR struct lrofs_mountpt_s *lm,
                             FAR struct lrofs_nodeinfo_s *ln_prev,
                             FAR struct lrofs_nodeinfo_s *ln,
                             bool firstchild)
{
  uint32_t node_next;
  uint32_t pre_next;
  int16_t ndx;

  /* Get the node sector index */

  ndx = lrofs_devcacheload(lm, ln->ln_origoffset);
  if (ndx < 0)
    {
      return ndx;
    }

  node_next = lrofs_devload32(lm, ndx + ROMFS_FHDR_NEXT);

  /* Get the prevnode sector index */

  ndx = lrofs_devcacheload(lm, ln_prev->ln_origoffset);
  if (ndx < 0)
    {
      return ndx;
    }

  pre_next = lrofs_devload32(lm, ndx + ROMFS_FHDR_NEXT);
  if (firstchild)
    {
      lrofs_devwrite32(lm, ndx + ROMFS_FHDR_INFO,
                       (node_next & RFNEXT_OFFSETMASK));
      return lrofs_devcachewrite(lm,
                                 SEC_NSECTORS(lm, ln_prev->ln_origoffset));
    }

  /* Update the prevnode next */

  lrofs_devwrite32(lm, ndx + ROMFS_FHDR_NEXT,
                   (node_next & RFNEXT_OFFSETMASK) |
                   (pre_next & RFNEXT_ALLMODEMASK));

  return lrofs_devcachewrite(lm, SEC_NSECTORS(lm, ln_prev->ln_origoffset));
}

/****************************************************************************
 * Name: lrofs_cachenode
 *
 * Description:
 *   Alloc all entry node at once when filesystem is mounted
 *
 ****************************************************************************/

static int lrofs_cachenode(FAR struct lrofs_mountpt_s *lm,
                           uint32_t origoffset, uint32_t offset,
                           uint32_t next, uint32_t size,
                           FAR const char *name,
                           FAR struct lrofs_nodeinfo_s **pnodeinfo,
                           FAR struct lrofs_nodeinfo_s *parent)
{
  FAR struct lrofs_nodeinfo_s **child;
  FAR struct lrofs_nodeinfo_s *ln;
  char childname[NAME_MAX + 1];
  uint16_t count = 0;
  uint32_t totalsize;
  uint32_t info;
  size_t nsize;
  int ret;

  nsize = strlen(name);
  ln = fs_heap_zalloc(sizeof(struct lrofs_nodeinfo_s) + nsize);
  if (ln == NULL)
    {
      return -ENOMEM;
    }

  *pnodeinfo        = ln;
  ln->ln_origoffset = origoffset;
  ln->ln_parent     = parent;
  ln->ln_offset     = offset;
  ln->ln_next       = next;
  ln->ln_namesize   = nsize;
  memcpy(ln->ln_name, name, nsize + 1);

  totalsize = ROMFS_ALIGNUP(ROMFS_FHDR_NAME + nsize + 1);
  if (offset == origoffset)
    {
      totalsize += ROMFS_ALIGNUP(size);
    }

  lm->lm_volsize += totalsize;
  ret = lrofs_alloc_spareregion(&lm->lm_sparelist, origoffset,
                                origoffset + totalsize);
  if (ret < 0)
    {
      return ret;
    }

  if (!IS_DIRECTORY(next) || (strcmp(name, ".") == 0) ||
      (strcmp(name, "..") == 0))
    {
      ln->ln_size = size;
      return 0;
    }

  origoffset = offset;
  child = ln->ln_child;

  do
    {
      /* Fetch the directory entry at this offset */

      ret = lrofs_parsedirentry(lm, origoffset, &offset, &next, &info,
                                &size);
      if (ret < 0)
        {
          return ret;
        }

      ret = lrofs_parsefilename(lm, origoffset, childname);
      if (ret < 0)
        {
          return ret;
        }

      if (child == NULL || ln->ln_count == count - 1)
        {
          FAR void *tmp;
          tmp = fs_heap_realloc(ln->ln_child, (count + NODEINFO_NINCR) *
                                sizeof(*ln->ln_child));
          if (tmp == NULL)
            {
              return -ENOMEM;
            }

          ln->ln_child = tmp;
          memset(ln->ln_child + count, 0, NODEINFO_NINCR *
                 sizeof(*ln->ln_child));
          count += NODEINFO_NINCR;
        }

      child = &ln->ln_child[ln->ln_count++];
      if (IS_DIRECTORY(next))
        {
          offset = info;
        }

      ln->ln_max = count;
      ret = lrofs_cachenode(lm, origoffset, offset, next, size,
                            childname, child, ln);
      if (ret < 0)
        {
          ln->ln_count--;
          return ret;
        }

      next &= RFNEXT_OFFSETMASK;
      origoffset = next;
    }
  while (next != 0);

  if (ln->ln_count > 1)
    {
      qsort(ln->ln_child, ln->ln_count, sizeof(*ln->ln_child),
            lrofs_nodeinfo_compare);
    }

  return 0;
}

/****************************************************************************
 * Name: lrofs_update_filesize
 *
 * Description:
 *   Update the file size to lrofs
 *
 ****************************************************************************/

static int lrofs_update_filesize(FAR struct lrofs_mountpt_s *lm,
                                 FAR struct lrofs_nodeinfo_s *ln,
                                 uint32_t size)
{
  int16_t ndx;

  /* Get the node sector index */

  ndx = lrofs_devcacheload(lm, ln->ln_offset);
  if (ndx < 0)
    {
      return ndx;
    }

  /* Update the node size */

  lrofs_devwrite32(lm, ndx + ROMFS_FHDR_SIZE, size);
  return lrofs_devcachewrite(lm,
                             SEC_NSECTORS((FAR struct lrofs_mountpt_s *)lm,
                             ln->ln_origoffset));
}

/****************************************************************************
 * Name: lrofs_do_create
 *
 * Description:
 *   Create the node
 *
 ****************************************************************************/

static FAR struct lrofs_nodeinfo_s *
lrofs_do_create(FAR struct lrofs_mountpt_s *lm,
                FAR struct lrofs_nodeinfo_s *ln_parent,
                uint32_t offset, uint32_t size, uint16_t type,
                FAR const char *name, bool firstchild)
{
  FAR struct lrofs_nodeinfo_s *ln;
  FAR struct lrofs_nodeinfo_s *ln_prev = ln_parent;
  int ret;
  int i;

  /* Init nodeinfo for file */

  ln = lrofs_alloc_nodeinfo(offset, size, type, name);
  if (ln == NULL)
    {
      return NULL;
    }

  /* Update the node parent addr */

  ln->ln_parent = ln_parent;

  /* Wirte the node to disk */

  if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
    {
      type = RFNEXT_HARDLINK;
    }

  if (!firstchild)
    {
      for (i = 0; i < ln_parent->ln_count; i++)
        {
          ln_prev = ln_parent->ln_child[i];
          if ((ln_prev->ln_next & RFNEXT_OFFSETMASK) == 0)
            {
              break;
            }
        }
    }

  ret = lrofs_add_disk(lm, ln_prev, ln, type, firstchild);
  if (ret < 0)
    {
      fs_heap_free(ln);
      return NULL;
    }

  /* Update the prevnode nodeinfo */

  if (!firstchild)
    {
      ln_prev->ln_next = (ln->ln_origoffset & RFNEXT_OFFSETMASK) |
                         (ln_prev->ln_next & RFNEXT_ALLMODEMASK);
    }

  /* Update the parent nodeinfo */

  ret = lrofs_update_parentnode(ln_parent, ln);
  if (ret < 0)
    {
      fs_heap_free(ln);
      return NULL;
    }

  return ln;
}

/****************************************************************************
 * Name: lrofs_devcacheread
 *
 * Description:
 *   Read the specified sector for specified offset into the sector cache.
 *   Return the index into the sector corresponding to the offset
 *
 ****************************************************************************/

static int16_t lrofs_devcacheread(FAR struct lrofs_mountpt_s *lm,
                                  uint32_t offset)
{
  uint32_t sector;
  int      ret;

  /* lm->lm_cachesector holds the current sector that is buffer in or
   * referenced by lm->lm_buffer. If the requested sector is the same as this
   * this then we do nothing.
   */

  sector = SEC_NSECTORS(lm, offset);
  if (lm->lm_cachesector != sector)
    {
      /* Check the access mode */

      if (lm->lm_xipbase)
        {
          /* In XIP mode, rf_buffer is just an offset pointer into the device
           * address space.
           */

          lm->lm_buffer = lm->lm_xipbase + SEC_ALIGN(lm, offset);
        }
      else
        {
          /* In non-XIP mode, we will have to read the new sector. */

          ret = lrofs_hwread(lm, lm->lm_buffer, sector, 1);
          if (ret < 0)
            {
              return (int16_t)ret;
            }
        }

      /* Update the cached sector number */

      lm->lm_cachesector = sector;
    }

  /* Return the offset */

  return offset & SEC_NDXMASK(lm);
}

/****************************************************************************
 * Name: lrofs_followhardlinks
 *
 * Description:
 *   Given the offset to a file header, check if the file is a hardlink.
 *   If so, traverse the hard links until the terminal, non-linked header
 *   so found and return that offset.
 *
 * Return value:
 *   < 0  :  An error occurred
 *     0  :  No link followed
 *     1  :  Link followed, poffset is the new volume offset
 *
 ****************************************************************************/

static int lrofs_followhardlinks(FAR struct lrofs_mountpt_s *lm,
                                 uint32_t offset, FAR uint32_t *poffset)
{
  uint32_t next;
  int16_t  ndx;
  int      i;
  int      ret = LINK_NOT_FOLLOWED;

  /* Loop while we are redirected by hardlinks */

  for (i = 0; i < ROMF_MAX_LINKS; i++)
    {
      /* Read the sector containing the offset into memory */

      ndx = lrofs_devcacheread(lm, offset);
      if (ndx < 0)
        {
          return ndx;
        }

      /* Check if this is a hard link */

      next = lrofs_devread32(lm, ndx + ROMFS_FHDR_NEXT);
      if (!IS_HARDLINK(next))
        {
          *poffset = offset;
          return ret;
        }

      /* Follow the hard-link.  Set return to indicate that we followed a
       * link and that poffset was set to the link offset is valid.
       */

      offset = lrofs_devread32(lm, ndx + ROMFS_FHDR_INFO);
      ret    = LINK_FOLLOWED;
    }

  return -ELOOP;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: lrofs_checkmount
 *
 * Description: Check if the mountpoint is still valid.
 *
 *   The caller should hold the mountpoint semaphore
 *
 ****************************************************************************/

int lrofs_checkmount(FAR struct lrofs_mountpt_s *lm)
{
  FAR struct inode *inode;
  struct geometry geo;
  int ret;

  /* If the rm_mounted flag is false, then we have already handled the loss
   * of the mount.
   */

  DEBUGASSERT(lm && lm->lm_blkdriver);
  if (lm->lm_mounted)
    {
      /* We still think the mount is healthy.  Check an see if this is
       * still the case
       */

      inode = lm->lm_blkdriver;
      if (inode->u.i_bops->geometry)
        {
          ret = inode->u.i_bops->geometry(inode, &geo);
          if (ret >= 0 && geo.geo_available && !geo.geo_mediachanged)
            {
              return 0;
            }
        }

      /* If we get here, the mount is NOT healthy */

      lm->lm_mounted = false;
    }

  return -ENODEV;
}

/****************************************************************************
 * Name: lrofs_freenode
 *
 * Description:
 *   Free the node
 *
 ****************************************************************************/

void lrofs_freenode(FAR struct lrofs_nodeinfo_s *nodeinfo)
{
  int i;

  if (IS_DIRECTORY(nodeinfo->ln_next))
    {
      for (i = 0; i < nodeinfo->ln_count; i++)
        {
          lrofs_freenode(nodeinfo->ln_child[i]);
        }

      fs_heap_free(nodeinfo->ln_child);
    }

  fs_heap_free(nodeinfo);
}

/****************************************************************************
 * Name: lrofs_hwread
 *
 * Description: Read the specified sector into the sector buffer
 *
 ****************************************************************************/

int lrofs_hwread(FAR struct lrofs_mountpt_s *lm, FAR uint8_t *buffer,
                 uint32_t sector, unsigned int nsectors)
{
  int ret = 0;

  /* Check the access mode */

  if (lm->lm_xipbase)
    {
      /* In XIP mode, we just copy the requested data */

      memcpy(buffer,
             lm->lm_xipbase + sector * lm->lm_hwsectorsize,
             nsectors * lm->lm_hwsectorsize);
    }
  else
    {
      /* In non-XIP mode, we have to read the data from the device */

      FAR struct inode *inode = lm->lm_blkdriver;
      ssize_t nsectorsread =
              inode->u.i_bops->read(inode, buffer, sector, nsectors);

      if (nsectorsread < 0)
        {
          ret = nsectorsread;
        }
      else if (nsectorsread != (ssize_t)nsectors)
        {
          ret = -EINVAL;
        }
    }

  return ret;
}

/****************************************************************************
 * Name: lrofs_filecacheread
 *
 * Description:
 *   Read the specified sector into the sector cache
 *
 ****************************************************************************/

int lrofs_filecacheread(FAR struct lrofs_mountpt_s *lm,
                        FAR struct lrofs_file_s *lf, uint32_t sector)
{
  int ret;

  /* lf->lf_cachesector holds the current sector that is buffer in or
   * referenced by lf->lf_buffer. If the requested sector is the same as this
   * sector then we do nothing.
   */

  if (lf->lf_cachesector > sector ||
      lf->lf_cachesector + lf->lf_ncachesector <= sector)
    {
      /* Check the access mode */

      if (lm->lm_xipbase)
        {
          /* In XIP mode, rf_buffer is just an offset pointer into the device
           * address space.
           */

          lf->lf_buffer = lm->lm_xipbase + sector * lm->lm_hwsectorsize;
          finfo("XIP buffer: %p\n", lf->lf_buffer);
        }
      else
        {
          /* In non-XIP mode, we will have to read the new sector. */

          ret = lrofs_hwread(lm, lf->lf_buffer, sector, lf->lf_ncachesector);
          if (ret < 0)
            {
              ferr("ERROR: lrofs_hwread failed: %d\n", ret);
              return ret;
            }
        }

      /* Update the cached sector number */

      lf->lf_cachesector = sector;
    }

  return 0;
}

/****************************************************************************
 * Name: lrofs_free_sparelist
 *
 * Description:
 *   Free the sparelist
 *
 ****************************************************************************/

void lrofs_free_sparelist(FAR struct list_node *list)
{
  FAR struct lrofs_sparenode_s *node;
  FAR struct lrofs_sparenode_s *tmp;

  list_for_every_entry_safe(list, node, tmp, struct lrofs_sparenode_s, node)
    {
      list_delete(&node->node);
      fs_heap_free(node);
    }
}

/****************************************************************************
 * Name: lrofs_finddirentry
 *
 * Description:
 *   Given a path to something that may or may not be in the file system,
 *   return the directory entry of the item.
 *
 ****************************************************************************/

FAR struct lrofs_nodeinfo_s *
lrofs_finddirentry(FAR struct lrofs_mountpt_s *lm,
                   FAR const char *path)
{
  FAR struct lrofs_nodeinfo_s *ln;
  FAR const char *entryname;
  FAR const char *terminator;
  int entrylen;

  /* Start with the first element after the root directory */

  ln = lm->lm_root;

  /* The root directory is a special case */

  if (!path || path[0] == '\0')
    {
      return ln;
    }

  /* Then loop for each directory/file component in the full path */

  entryname  = path;
  terminator = NULL;

  for (; ; )
    {
      /* Find the start of the next path component */

      while (*entryname == '/') entryname++;

      /* Find the end of the next path component */

      terminator = strchr(entryname, '/');
      if (!terminator)
        {
          entrylen = strlen(entryname);
        }
      else
        {
          entrylen = terminator - entryname;
        }

      if (entrylen == 0)
        {
          return ln;
        }

      /* Long path segment names will be truncated to NAME_MAX */

      if (entrylen > NAME_MAX)
        {
          entrylen = NAME_MAX;
        }

      /* Then find the entry in the current directory with the
       * matching name.
       */

      ln = lrofs_searchdir(lm, entryname, entrylen, ln);
      if (ln == NULL)
        {
          return NULL;
        }

      /* Was that the last path component? */

      if (!terminator)
        {
          /* Yes.. return success */

          return ln;
        }

      /* No... If that was not the last path component, then it had
       * better have been a directory
       */

      if (!IS_DIRECTORY(ln->ln_next))
        {
          return NULL;
        }

      /* Setup to search the next directory for the next component
       * of the path
       */

      entryname = terminator;
    }

  return NULL; /* Won't get here */
}

/****************************************************************************
 * Name: lrofs_parsedirentry
 *
 * Description:
 *   Return the directory entry at this offset.  If rf is NULL, then the
 *   mount device resources are used.  Otherwise, file resources are used.
 *
 ****************************************************************************/

int lrofs_parsedirentry(FAR struct lrofs_mountpt_s *lm, uint32_t offset,
                        FAR uint32_t *poffset, uint32_t *pnext,
                        FAR uint32_t *pinfo, FAR uint32_t *psize)
{
  uint32_t save;
  uint32_t next;
  int16_t  ndx;
  int      ret;

  /* Read the sector into memory */

  ndx = lrofs_devcacheread(lm, offset);
  if (ndx < 0)
    {
      return ndx;
    }

  /* Yes.. Save the first 'next' value.  That has the offset needed to
   * traverse the parent directory.  But we may need to change the type
   * after we follow the hard links.
   */

  save = lrofs_devread32(lm, ndx + ROMFS_FHDR_NEXT);

  /* Traverse hardlinks as necessary to get to the real file header */

  ret = lrofs_followhardlinks(lm, offset, poffset);
  if (ret < 0)
    {
      return ret;
    }
  else if (ret > 0)
    {
      /* The link was followed */

      ndx = lrofs_devcacheread(lm, *poffset);
      if (ndx < 0)
        {
          return ndx;
        }
    }

  /* Because everything is chunked and aligned to 16-bit boundaries,
   * we know that most the basic node info fits into the sector.  The
   * associated name may not, however.
   *
   * NOTE:  Since ROMFS directory entries are aligned to 16-byte boundaries,
   * we are assured that ndx + ROMFS_FHDR_INFO/SIZE will lie wholly within
   * the sector buffer.
   */

  next   = lrofs_devread32(lm, ndx + ROMFS_FHDR_NEXT);
  *pnext = (save & RFNEXT_OFFSETMASK) | (next & RFNEXT_ALLMODEMASK);
  *pinfo = lrofs_devread32(lm, ndx + ROMFS_FHDR_INFO);
  *psize = lrofs_devread32(lm, ndx + ROMFS_FHDR_SIZE);

  return 0;
}

/****************************************************************************
 * Name: lrofs_parsefilename
 *
 * Description:
 *   Return the filename from directory entry at this offset
 *
 ****************************************************************************/

int lrofs_parsefilename(FAR struct lrofs_mountpt_s *lm, uint32_t offset,
                        FAR char *pname)
{
  int16_t  ndx;
  uint16_t namelen = 0;
  uint16_t chunklen;
  bool     done = false;

  /* Loop until the whole name is obtained or until NAME_MAX characters
   * of the name have been parsed.
   */

  offset += ROMFS_FHDR_NAME;
  while (namelen < NAME_MAX && !done)
    {
      /* Read the sector into memory */

      ndx = lrofs_devcacheread(lm, offset + namelen);
      if (ndx < 0)
        {
          return ndx;
        }

      /* Is the name terminated in this 16-byte block */

      if (lm->lm_buffer[ndx + 15] == '\0')
        {
          /* Yes.. then this chunk is less than 16 */

          chunklen = strlen((FAR char *)&lm->lm_buffer[ndx]);
          done     = true;
        }
      else
        {
          /* No.. then this chunk is 16 bytes in length */

          chunklen = 16;
        }

      /* Check if we would exceed the NAME_MAX */

      if (namelen + chunklen > NAME_MAX)
        {
          chunklen = NAME_MAX - namelen;
          done     = true;
        }

      /* Copy the chunk */

      memcpy(&pname[namelen], &lm->lm_buffer[ndx], chunklen);
      namelen += chunklen;
    }

  /* Terminate the name (NAME_MAX+1 chars total) and return success */

  pname[namelen] = '\0';
  return 0;
}

/****************************************************************************
 * Name: lrofs_datastart
 *
 * Description:
 *   Given the offset to a file header, return the offset to the start of
 *   the file data
 *
 ****************************************************************************/

int lrofs_datastart(FAR struct lrofs_mountpt_s *lm,
                    FAR struct lrofs_nodeinfo_s *ln,
                    FAR uint32_t *start)
{
  *start = ROMFS_ALIGNUP(ln->ln_offset +
                         ROMFS_FHDR_NAME + ln->ln_namesize + 1);
  return 0;
}

/****************************************************************************
 * Name: lrofs_hwconfigure
 *
 * Description:
 *   This function is called as part of the ROMFS mount operation.
 *   It configures the ROMFS filestem for use on this block driver.  This
 *   include the accounting for the geometry of the device, setting up any
 *   XIP modes of operation, and/or allocating any cache buffers.
 *
 ****************************************************************************/

int lrofs_hwconfigure(FAR struct lrofs_mountpt_s *lm)
{
  FAR struct inode *inode = lm->lm_blkdriver;
  struct geometry geo;
  int ret;

  /* Get the underlying device geometry */

  ret = inode->u.i_bops->geometry(inode, &geo);
  if (ret < 0)
    {
      return ret;
    }

  if (!geo.geo_available)
    {
      return -EBUSY;
    }

  /* Save that information in the mount structure */

  lm->lm_hwsectorsize = geo.geo_sectorsize;
  lm->lm_hwnsectors   = geo.geo_nsectors;
  lm->lm_cachesector  = (uint32_t)-1;

  /* Allocate the device cache buffer for normal sector accesses */

  lm->lm_devbuffer = fs_heap_malloc(lm->lm_hwsectorsize);
  if (!lm->lm_devbuffer)
    {
      return -ENOMEM;
    }

  /* Determine if block driver supports the XIP mode of operation */

  if (inode->u.i_bops->ioctl)
    {
      ret = inode->u.i_bops->ioctl(inode, BIOC_XIPBASE,
                                  (unsigned long)&lm->lm_xipbase);
      if (ret >= 0 && lm->lm_xipbase)
        {
          /* Yes.. Then we will directly access the media (vs.
           * copying into an allocated sector buffer.
           */

          lm->lm_buffer      = lm->lm_xipbase;
          lm->lm_cachesector = 0;
          return 0;
        }
    }

  /* The device cache buffer for normal sector accesses */

  lm->lm_buffer = lm->lm_devbuffer;
  return 0;
}

/****************************************************************************
 * Name: lrofs_fsconfigure
 *
 * Description:
 *   This function is called as part of the ROMFS mount operation   It
 *   sets up the mount structure to include configuration information
 *   contained in the ROMFS header.  This is the place where we actually
 *   determine if the media contains a ROMFS filesystem.
 *
 ****************************************************************************/

int lrofs_fsconfigure(FAR struct lrofs_mountpt_s *lm, FAR const void *data)
{
  FAR const char *name;
  int             ret;
  uint32_t        rootoffset;

  /* Then get information about the ROMFS filesystem on the devices managed
   * by this block driver. Read sector zero which contains the volume header.
   */

  ret = lrofs_devcacheread(lm, 0);
  if (ret < 0)
    {
      return ret;
    }

  /* Verify the magic number at that identifies this as a ROMFS filesystem */

  if (memcmp(lm->lm_buffer, ROMFS_VHDR_MAGIC, ROMFS_VHDR_SIZE) != 0)
    {
      return -EINVAL;
    }

  /* Then extract the values we need from the header and return success */

  lm->lm_volsize = lrofs_devread32(lm, ROMFS_VHDR_SIZE);

  /* The root directory entry begins right after the header */

  name = (FAR const char *)&lm->lm_buffer[ROMFS_VHDR_VOLNAME];
  rootoffset = ROMFS_ALIGNUP(ROMFS_VHDR_VOLNAME + strlen(name) + 1);
  ret = lrofs_init_sparelist(lm);
  if (ret < 0)
    {
      return ret;
    }

  ret = lrofs_cachenode(lm, 0, rootoffset, RFNEXT_DIRECTORY,
                        0, "", &lm->lm_root, NULL);
  if (ret < 0)
    {
      lrofs_free_sparelist(&lm->lm_sparelist);
      lrofs_freenode(lm->lm_root);
      return ret;
    }

  lm->lm_mounted = true;
  return 0;
}

/****************************************************************************
 * Name: lrofs_fileconfigure
 *
 * Description:
 *   This function is called as part of the ROMFS file open operation   It
 *   sets up the file structure to handle buffer appropriately, depending
 *   upon XIP mode or not.
 *
 ****************************************************************************/

int lrofs_fileconfigure(FAR struct lrofs_mountpt_s *lm,
                        FAR struct lrofs_file_s *lf)
{
  /* Check if XIP access mode is supported.  If so, then we do not need
   * to allocate anything.
   */

  if (lm->lm_xipbase)
    {
      /* We'll put a valid address in rf_buffer just in case. */

      lf->lf_cachesector  = 0;
      lf->lf_buffer       = lm->lm_xipbase;
      lf->lf_ncachesector = 1;
    }
  else
    {
      uint32_t startsector;
      uint32_t endoffset;
      uint32_t nsectors;

      endoffset = lf->lf_startoffset + lf->lf_size;
      if (lf->lf_size)
        {
          endoffset--;
        }

      lf->lf_endsector = SEC_NSECTORS(lm, endoffset);
      startsector = SEC_NSECTORS(lm, lf->lf_startoffset);
      nsectors = lf->lf_endsector - startsector + 1;
      if (nsectors > CONFIG_FS_ROMFS_CACHE_FILE_NSECTORS)
        {
          nsectors = CONFIG_FS_ROMFS_CACHE_FILE_NSECTORS;
        }

      /* Nothing in the cache buffer */

      lf->lf_cachesector = (uint32_t)-1;
      lf->lf_ncachesector = nsectors;

      /* Create a file buffer to support partial sector accesses */

      lf->lf_buffer = fs_heap_malloc(lm->lm_hwsectorsize *
                                     lf->lf_ncachesector);
      if (!lf->lf_buffer)
        {
          return -ENOMEM;
        }
    }

  return 0;
}

/****************************************************************************
 * Name: lrofs_create
 *
 * Description:
 *   Create the file or dir to lrofs
 *
 ****************************************************************************/

int lrofs_create(FAR struct lrofs_mountpt_s *lm,
                 FAR struct lrofs_nodeinfo_s **ln,
                 FAR const char *relpath, bool isdir)
{
  FAR struct lrofs_nodeinfo_s *ln_new;
  FAR struct lrofs_nodeinfo_s *ln_p1 = NULL;
  FAR struct lrofs_nodeinfo_s *ln_p2 = NULL;
  FAR struct lrofs_nodeinfo_s *ln_parent;
  FAR char *path = fs_heap_strdup(relpath);
  FAR char *name;
  uint32_t offset;
  uint32_t size;
  int ret = OK;

  /* Get the parent nodeinfo */

  name = basename(path);
  if (name == path)
    {
      ln_parent = lm->lm_root;
    }
  else
    {
      ln_parent = lrofs_find_parentnode(lm, dirname((FAR char *)path));
      if (ln_parent == NULL)
        {
          fs_heap_free(path);
          return -ENOENT;
        }
    }

  /* Alloc the node space from lrofs sparelist */

  size = ROMFS_ALIGNUP(ROMFS_VHDR_VOLNAME + strlen(name) + 1);
  if (isdir)
    {
      /* Add node size of ./.. (64) */

      size += 64;
    }

  offset = lrofs_add_sparenode(lm, size, isdir);
  if (offset == 0)
    {
      fs_heap_free(path);
      return -ENOSPC;
    }

  ln_new = lrofs_do_create(lm, ln_parent, offset, 0,
                           isdir ? RFNEXT_DIRECTORY : RFNEXT_FILE,
                           name, false);
  if (ln_new == NULL)
    {
      fs_heap_free(path);
      return -ENOMEM;
    }

  /* If is dir then add ./.. node
   * type in nodeinfo is dir in phymem is hardlink
   */

  if (isdir)
    {
      ln_p1 = lrofs_do_create(lm, ln_new, offset + size - 64, 0,
                              RFNEXT_DIRECTORY, ".", true);
      if (ln_p1 == NULL)
        {
          ret = -ENOMEM;
          goto error_out;
        }

      ln_p2 = lrofs_do_create(lm, ln_new, offset + size - 32, 0,
                              RFNEXT_DIRECTORY, "..", false);
      if (ln_p2 == NULL)
        {
          ret = -ENOMEM;
          goto error_out;
        }
    }

  lm->lm_volsize += size;
  *ln = ln_new;
  fs_heap_free(path);
  return ret;

error_out:
  if (ln_new != NULL)
    {
      fs_heap_free(ln_new);
    }

  if (isdir)
    {
      if (ln_p1 != NULL)
        {
          fs_heap_free(ln_p1);
        }

      if (ln_p2 != NULL)
        {
          fs_heap_free(ln_p2);
        }
    }

  lrofs_free_spareregion(&lm->lm_sparelist, offset, offset + size);
  fs_heap_free(path);
  return ret;
}

/****************************************************************************
 * Name: lrofs_write_file
 *
 * Description:
 *   Write the file to lrofs
 *
 ****************************************************************************/

int lrofs_write_file(FAR struct file *filep, FAR const char *buffer,
                     size_t buflen)
{
  FAR struct lrofs_mountpt_s *lm = filep->f_inode->i_private;
  FAR struct lrofs_file_s *lf = filep->f_priv;
  FAR uint8_t *userbuffer = (FAR uint8_t *)buffer;
  FAR struct lrofs_nodeinfo_s *ln = lf->lf_ln;
  unsigned int byteswritten;
  unsigned int writesize = 0;
  unsigned int nsectors;
  uint32_t offset;
  uint32_t savedoffset;
  uint32_t sector;
  off_t savedbuflen;
  int sectorndx;
  int ret;

  /* Check if there has enough space on disk */

  offset = lf->lf_startoffset + filep->f_pos;
  savedoffset = lf->lf_startoffset + lf->lf_size;
  if (filep->f_pos < lf->lf_size)
    {
      /* We are writing to an existing file.  Check if we need to extend
       * the file.
       */

      savedbuflen = filep->f_pos + buflen - lf->lf_size;
    }
  else
    {
      savedbuflen = buflen;
    }

  if (savedbuflen > 0 &&
      lrofs_alloc_spareregion(&lm->lm_sparelist,
                              savedoffset,
                              savedoffset + savedbuflen) != 0)
    {
      ferr("ERROR: lrofs_alloc_spareregion failed\n");
      return -ENOSPC;
    }

  /* Loop until either (1) all data has been transferred, or (2) an
   * error occurs.
   */

  while (buflen > 0)
    {
      /* Get the first sector and index to read from. */

      offset    = lf->lf_startoffset + filep->f_pos;
      sector    = SEC_NSECTORS(lm, offset);
      sectorndx = offset & SEC_NDXMASK(lm);
      nsectors  = SEC_NSECTORS(lm, buflen);
      if (nsectors >= lf->lf_ncachesector && sectorndx == 0)
        {
          ret = lrofs_hwwrite(lm, userbuffer, sector, nsectors);
          if (ret < 0)
            {
              ferr("ERROR: lrofs_hwwrite failed: %d\n", ret);
              goto error_out;
            }

          byteswritten = nsectors * lm->lm_hwsectorsize;
        }
      else
        {
          /* We are writing a partial sector.  First, read the whole sector
           * into the file data buffer.  This is a caching buffer so if
           * it is already there then all is well.
           */

          ret = lrofs_filecacheread(lm, lf, sector);
          if (ret < 0)
            {
              ferr("ERROR: lrofs_filecacheread failed: %d\n", ret);
              goto error_out;
            }

          /* Copy the partial sector into the write buffer */

          byteswritten = (lf->lf_cachesector + lf->lf_ncachesector -
                          sector) * lm->lm_hwsectorsize - sectorndx;
          sectorndx = lf->lf_ncachesector * lm->lm_hwsectorsize -
                      byteswritten;
          if (byteswritten > buflen)
            {
              /* We will not write to the end of the buffer */

              byteswritten = buflen;
            }

          memcpy(&lf->lf_buffer[sectorndx], userbuffer, byteswritten);

          /* Write the whole sector into the file data buffer. */

          ret = lrofs_filecachewrite(lm, lf);
          if (ret < 0)
            {
              ferr("ERROR: lrofs_filecachewrite failed: %d\n", ret);
              goto error_out;
            }
        }

      userbuffer   += byteswritten;
      filep->f_pos += byteswritten;
      writesize    += byteswritten;
      buflen       -= byteswritten;
    }

  /* Update the file size */

  if (savedbuflen > 0)
    {
      ln->ln_size = filep->f_pos;
      lf->lf_size = filep->f_pos;
      lf->lf_endsector = SEC_NSECTORS(lm, lf->lf_startoffset + lf->lf_size);
      lm->lm_volsize += writesize;

      ret = lrofs_update_filesize(lm, ln, lf->lf_size);
      if (ret < 0)
        {
          ferr("ERROR: lrofs_update_filesize failed: %d\n", ret);
          goto error_out;
        }
    }

  return writesize;

error_out:
  if (savedbuflen > 0)
    {
      lrofs_free_spareregion(&lm->lm_sparelist, savedoffset,
                             savedoffset + savedbuflen);
    }

  return ret;
}

/****************************************************************************
 * Name: lrofs_truncate_file
 *
 * Description:
 *   Truncate the file to lrofs
 *
 ****************************************************************************/

int lrofs_truncate_file(FAR struct file *filep, off_t length)
{
  FAR struct lrofs_mountpt_s *lm = filep->f_inode->i_private;
  FAR struct lrofs_file_s *lf = filep->f_priv;
  FAR struct lrofs_nodeinfo_s *ln = lf->lf_ln;
  off_t remain = length - lf->lf_size;
  int ret;

  if (length < lf->lf_size)
    {
      /* Free the space on disk */

      ret = lrofs_free_spareregion(&lm->lm_sparelist,
                                   lf->lf_startoffset + length,
                                   lf->lf_startoffset + lf->lf_size);
      if (ret < 0)
        {
          ferr("ERROR: lrofs_free_spareregion failed\n");
          return ret;
        }

      lm->lm_volsize -= lf->lf_size - length;
    }
  else if (length > lf->lf_size)
    {
      /* Alloc the space on disk */

      ret = lrofs_alloc_spareregion(&lm->lm_sparelist,
                                    lf->lf_startoffset + lf->lf_size,
                                    lf->lf_startoffset + length);
      if (ret < 0)
        {
          ferr("ERROR: lrofs_alloc_spareregion failed\n");
          return ret;
        }

      lm->lm_volsize += length - lf->lf_size;
    }

  /* Update the file size */

  ln->ln_size = length;
  lf->lf_size = length;
  lf->lf_endsector = SEC_NSECTORS(lm, lf->lf_startoffset + lf->lf_size);

  /* Update the file size to disk */

  ret = lrofs_update_filesize(lm, ln, lf->lf_size);
  if (ret < 0)
    {
      ferr("ERROR: lrofs_update_filesize failed: %d\n", ret);
      return ret;
    }

  if (remain > 0)
    {
      FAR char *buffer = NULL;
      uint32_t buff_len = lm->lm_hwsectorsize * lf->lf_ncachesector;
      off_t savepos = filep->f_pos;

      buffer = fs_heap_zalloc(buff_len);
      if (buffer == NULL)
        {
          return -ENOMEM;
        }

      while (remain > 0)
        {
          if (remain >= buff_len)
            {
              ret = lrofs_write_file(filep, buffer, buff_len);
            }
          else
            {
              ret = lrofs_write_file(filep, buffer, remain);
            }

          if (ret < 0)
            {
              fs_heap_free(buffer);
              return ret;
            }

          remain -= ret;
        }

      fs_heap_free(buffer);
      filep->f_pos = savepos;
    }

  return OK;
}

/****************************************************************************
 * Name: lrofs_mkfs
 *
 * Description:
 *   Format the lrofs filesystem
 *
 ****************************************************************************/

int lrofs_mkfs(FAR struct lrofs_mountpt_s *lm)
{
  /* Write the magic number at that identifies this as a ROMFS filesystem */

  lrofs_devmemcpy(lm, ROMFS_VHDR_ROM1FS, ROMFS_VHDR_MAGIC, ROMFS_VHDR_SIZE);

  /* Init the ROMFS volume size */

  lrofs_devwrite32(lm, ROMFS_VHDR_SIZE, 0x60);

  /* Write the volume name */

  lrofs_devstrcpy(lm, ROMFS_VHDR_VOLNAME, "lrofs");

  /* Write the root node . */

  lrofs_devwrite32(lm, 0x20 + ROMFS_FHDR_NEXT, 0x40 | RFNEXT_DIRECTORY);
  lrofs_devwrite32(lm, 0x20 + ROMFS_FHDR_INFO, 0x20);
  lrofs_devwrite32(lm, 0x20 + ROMFS_FHDR_SIZE, 0);
  lrofs_devwrite32(lm, 0x20 + ROMFS_FHDR_CHKSUM, 0);
  lrofs_devstrcpy(lm, 0x20 + ROMFS_FHDR_NAME, ".");

  /* Write the root node .. */

  lrofs_devwrite32(lm, 0x40 + ROMFS_FHDR_NEXT, RFNEXT_HARDLINK);
  lrofs_devwrite32(lm, 0x40 + ROMFS_FHDR_INFO, 0x20);
  lrofs_devwrite32(lm, 0x40 + ROMFS_FHDR_SIZE, 0);
  lrofs_devwrite32(lm, 0x40 + ROMFS_FHDR_CHKSUM, 0);
  lrofs_devstrcpy(lm, 0x40 + ROMFS_FHDR_NAME, "..");

  /* Write the buffer to sector zero */

  return lrofs_devcachewrite(lm, 0);
}

/****************************************************************************
 * Name: lrofs_remove
 *
 * Description:
 *   Unlink the dir from lrofs
 *
 ****************************************************************************/

int lrofs_remove(FAR struct lrofs_mountpt_s *lm,
                 FAR struct lrofs_nodeinfo_s *ln_parent,
                 FAR const char *relpath, bool isdir)
{
  FAR struct lrofs_nodeinfo_s *ln_prev;
  FAR struct lrofs_nodeinfo_s **pinfo;
  FAR struct lrofs_nodeinfo_s *ln;
  struct lrofs_entryname_s le;
  uint32_t totalsize;
  bool firstchild;
  int ret;

  if (ln_parent == NULL)
    {
      return -ENOENT;
    }

  /* Get the real nodeinfo addr */

  le.le_name = basename((FAR char *)relpath);
  le.le_len = strlen(le.le_name);
  pinfo = bsearch(&le, ln_parent->ln_child, ln_parent->ln_count,
                  sizeof(*ln_parent->ln_child), lrofs_nodeinfo_search);
  ln = *pinfo;
  if (ln == NULL)
    {
      return -ENOENT;
    }

  /* Get the prev nodeinfo */

  ln_prev = lrofs_get_prevnode(ln, &firstchild);
  if (ln_prev == NULL)
    {
      return -ENOENT;
    }

  /* Remove the node from disk */

  ret = lrofs_remove_disk(lm, ln_prev, ln, firstchild);
  if (ret < 0)
    {
      return ret;
    }

  /* Update the prevnode nodeinfo */

  if (!firstchild)
    {
      ln_prev->ln_next = (ln->ln_next & RFNEXT_OFFSETMASK) |
                         (ln_prev->ln_next & RFNEXT_ALLMODEMASK);
    }

  /* Return the node space to lrofs sparelist  */

  totalsize = ROMFS_ALIGNUP(ROMFS_VHDR_VOLNAME + ln->ln_namesize + 1) +
                            ln->ln_size;
  lm->lm_volsize -= totalsize;
  lrofs_free_spareregion(&lm->lm_sparelist, ln->ln_origoffset,
                         ln->ln_origoffset + totalsize);

  /* Update parent cache node */

  if (ln_parent->ln_count > 1)
    {
      *pinfo = ln_parent->ln_child[ln_parent->ln_count - 1];
      ln_parent->ln_child[ln_parent->ln_count - 1] = NULL;
      ln_parent->ln_count--;
      qsort(ln_parent->ln_child, ln_parent->ln_count,
            sizeof(*ln_parent->ln_child), lrofs_nodeinfo_compare);
    }
  else if (ln_parent->ln_count == 1)
    {
      ln_parent->ln_count = 0;
      if (ln_parent->ln_child != NULL)
        {
          fs_heap_free(ln_parent->ln_child);
          ln_parent->ln_child = NULL;
        }
    }

  /* If is dir then free remain child node ./.. */

  if (isdir)
    {
      FAR struct lrofs_nodeinfo_s *ln_temp;
      for (int i = 0; i < ln->ln_count; i++)
        {
          ln_temp = ln->ln_child[i];
          totalsize = ROMFS_ALIGNUP(ROMFS_VHDR_VOLNAME +
                                    ln_temp->ln_namesize + 1) +
                                    ln_temp->ln_size;
          lm->lm_volsize -= totalsize;
          lrofs_free_spareregion(&lm->lm_sparelist,
                                 ln_temp->ln_origoffset,
                                 ln_temp->ln_origoffset + totalsize);
          fs_heap_free(ln_temp);
        }

      fs_heap_free(ln->ln_child);
    }

  fs_heap_free(ln);
  return OK;
}

/****************************************************************************
 * Name: lrofs_rename_file
 *
 * Description:
 *   Rename the file to lrofs
 *
 ****************************************************************************/

int lrofs_rename_file(FAR struct lrofs_mountpt_s *lm,
                      FAR struct lrofs_nodeinfo_s *ln_old,
                      FAR struct lrofs_nodeinfo_s *ln_newpath,
                      FAR const char *newname)
{
  FAR struct lrofs_nodeinfo_s *ln_prev;
  FAR struct lrofs_nodeinfo_s *ln_parent;
  bool firstchild;
  int ret;

  DEBUGASSERT(ln_old != NULL && ln_newpath != NULL);

  /* Get the old prev nodeinfo */

  ln_prev = lrofs_get_prevnode(ln_old, &firstchild);
  if (ln_prev == NULL)
    {
      return -ENOENT;
    }

  /* Remove the node from disk */

  ret = lrofs_remove_disk(lm, ln_prev, ln_old, firstchild);
  if (ret < 0)
    {
      return ret;
    }

  /* Update the prevnode nodeinfo */

  if (!firstchild)
    {
      ln_prev->ln_next = (ln_old->ln_next & RFNEXT_OFFSETMASK) |
                         (ln_prev->ln_next & RFNEXT_ALLMODEMASK);
    }

  /* Update parent cache node */

  ln_parent = ln_old->ln_parent;
  if (ln_parent->ln_count > 1)
    {
      for (int i = 0; i < ln_parent->ln_count; i++)
        {
          if (ln_parent->ln_child[i] == ln_old)
            {
              ln_parent->ln_child[i] =
                         ln_parent->ln_child[ln_parent->ln_count - 1];
              break;
            }
        }

      ln_parent->ln_child[ln_parent->ln_count - 1] = NULL;
      ln_parent->ln_count--;
      qsort(ln_parent->ln_child, ln_parent->ln_count,
            sizeof(*ln_parent->ln_child), lrofs_nodeinfo_compare);
    }
  else if (ln_parent->ln_count == 1)
    {
      ln_parent->ln_count = 0;
      if (ln_parent->ln_child != NULL)
        {
          fs_heap_free(ln_parent->ln_child);
          ln_parent->ln_child = NULL;
        }
    }

  /* Get new prev nodeinfo */

  for (int i = 0 ; i < ln_newpath->ln_count; i++)
    {
      ln_prev = ln_newpath->ln_child[i];
      if ((ln_prev->ln_next & RFNEXT_OFFSETMASK) == 0)
        {
          break;
        }
    }

  ret = lrofs_add_disk(lm, ln_prev, ln_old,
                       ln_old->ln_next & RFNEXT_ALLMODEMASK, false);
  if (ret < 0)
    {
      ferr("ERROR: lrofs_add_disk failed: %d\n", ret);
      return ret;
    }

  /* Update the new prevnode nodeinfo */

  ln_prev->ln_next = (ln_old->ln_origoffset & RFNEXT_OFFSETMASK) |
                     (ln_prev->ln_next & RFNEXT_ALLMODEMASK);

  if (newname != NULL)
    {
      if (strlen(newname) <= ln_old->ln_namesize)
        {
          ln_old->ln_namesize = strlen(newname);
          memcpy(ln_old->ln_name, newname, strlen(newname) + 1);
        }
      else
        {
          FAR struct lrofs_nodeinfo_s *tmp = lrofs_alloc_nodeinfo(
                                             ln_old->ln_origoffset,
                                             ln_old->ln_size,
                                             ln_old->ln_next,
                                             newname);
          if (tmp == NULL)
            {
              return -ENOMEM;
            }

          tmp->ln_offset = ln_old->ln_offset;
          fs_heap_free(ln_old);
          ln_old = tmp;
        }
    }

  /* Update the parent nodeinfo */

  ret = lrofs_update_parentnode(ln_newpath, ln_old);
  if (ret < 0)
    {
      ferr("ERROR: lrofs_update_parentnode failed: %d\n", ret);
      return ret;
    }

  ln_old->ln_parent = ln_newpath;
  return OK;
}
