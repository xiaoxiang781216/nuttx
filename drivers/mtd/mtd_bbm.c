/****************************************************************************
 * drivers/mtd/mtd_bbm.c
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

#include <sys/param.h>
#include <sys/types.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <debug.h>
#include <errno.h>
#include <unistd.h>

#include <nuttx/kmalloc.h>
#include <nuttx/mtd/mtd.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct mtd_bbm_s
{
  struct mtd_dev_s      mtd;      /* Exposed MTD interface (must be first) */
  FAR struct mtd_dev_s *lower;    /* Underlying raw MTD device */
  struct mtd_geometry_s geo;      /* Device geometry */
  uint16_t              blkper;   /* R/W blocks per erase block */
  FAR off_t            *lptable;  /* lptable[logical] = physical */
  off_t                 lpcount;  /* Number of good blocks */
  FAR uint8_t          *blkbuf;   /* Block move bounce buffer, on demand */
  char                  name[NAME_MAX + 1];
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int     bbm_erase(FAR struct mtd_dev_s *dev, off_t startblock,
                         size_t nblocks);
static ssize_t bbm_bread(FAR struct mtd_dev_s *dev, off_t startblock,
                         size_t nblocks, FAR uint8_t *buffer);
static ssize_t bbm_bwrite(FAR struct mtd_dev_s *dev, off_t startblock,
                          size_t nblocks, FAR const uint8_t *buffer);
static ssize_t bbm_read(FAR struct mtd_dev_s *dev, off_t offset,
                        size_t nbytes, FAR uint8_t *buffer);
#ifdef CONFIG_MTD_BYTE_WRITE
static ssize_t bbm_write(FAR struct mtd_dev_s *dev, off_t offset,
                         size_t nbytes, FAR const uint8_t *buffer);
#endif
static int     bbm_ioctl(FAR struct mtd_dev_s *dev, int cmd,
                         unsigned long arg);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bbm_init_map
 *
 * Description:
 *   Scan all erase blocks and build logical-to-physical mapping table,
 *   skipping blocks marked as bad.
 *
 ****************************************************************************/

static int bbm_init_map(FAR struct mtd_bbm_s *bbm)
{
  int j = 0;
  int i;

  bbm->lptable = kmm_malloc(bbm->geo.neraseblocks * sizeof(off_t));
  if (bbm->lptable == NULL)
    {
      return -ENOMEM;
    }

  for (i = 0; i < bbm->geo.neraseblocks; i++)
    {
      if (!MTD_ISBAD(bbm->lower, i))
        {
          bbm->lptable[j++] = i;
        }
    }

  bbm->lpcount = j;
  return OK;
}

/****************************************************************************
 * Name: bbm_update_map
 *
 * Description:
 *   Remove a bad block from the mapping table at the specified logical
 *   position and shift all subsequent entries.
 *
 ****************************************************************************/

static inline void bbm_update_map(FAR struct mtd_bbm_s *bbm, off_t start)
{
  DEBUGASSERT(start < bbm->lpcount);
  memmove(&bbm->lptable[start], &bbm->lptable[start + 1],
          (--bbm->lpcount - start) * sizeof(bbm->lptable[0]));
}

/****************************************************************************
 * Name: bbm_get_cblock
 *
 * Description:
 *   Get the number of consecutive physical erase blocks starting from
 *   logical block 'start'.
 *
 ****************************************************************************/

static size_t bbm_get_cblock(FAR struct mtd_bbm_s *bbm, off_t start,
                             size_t count)
{
  off_t i;

  count = MIN(count, bbm->lpcount - start);
  for (i = start; i < start + count - 1; i++)
    {
      if (bbm->lptable[i + 1] - bbm->lptable[i] != 1)
        {
          return i - start + 1;
        }
    }

  return count;
}

/****************************************************************************
 * Name: bbm_is_erasedblock
 ****************************************************************************/

static bool bbm_is_erasedblock(FAR struct mtd_bbm_s *bbm, off_t block,
                               FAR void *blkbuf)
{
  FAR uint64_t *buf = (FAR uint64_t *)blkbuf;
  uint64_t cmpbuf = ~0ull;
  uint8_t erasebyte;
  size_t i;
  size_t j;
  int ret;

  ret = MTD_IOCTL(bbm->lower, MTDIOC_ERASESTATE,
                 (unsigned long)((uintptr_t)&erasebyte));
  if (ret >= 0)
    {
      memset(&cmpbuf, erasebyte, sizeof(cmpbuf));
    }

  for (i = 0; i < bbm->blkper; i++)
    {
      ret = MTD_BREAD(bbm->lower,
                      bbm->lptable[block] * bbm->blkper + i,
                      1, blkbuf);
      if (ret != 1)
        {
          return false;
        }

      for (j = 0; j < bbm->geo.blocksize / sizeof(cmpbuf); j++)
        {
          if (buf[j] != cmpbuf)
            {
              return false;
            }
        }
    }

  return true;
}

/****************************************************************************
 * Name: bbm_get_endblock
 ****************************************************************************/

static off_t bbm_get_endblock(FAR struct mtd_bbm_s *bbm,
                              off_t startblock, FAR void *blkbuf)
{
  off_t endblock = bbm->lpcount - 1;

  while (endblock > startblock)
    {
      if (!bbm_is_erasedblock(bbm, endblock, blkbuf))
        {
          break;
        }

      endblock--;
#if CONFIG_MTD_BBM_USLEEP > 0
      usleep(CONFIG_MTD_BBM_USLEEP);
#endif
    }

  return endblock;
}

/****************************************************************************
 * Name: bbm_move
 *
 * Description:
 *   Move all data from startblock to the last non-empty block one position
 *   forward in the logical address space.
 *
 ****************************************************************************/

static int bbm_move(FAR struct mtd_bbm_s *bbm, off_t startblock)
{
  off_t endblock;
  size_t i;
  int ret = OK;

  /* Reached only when a block has just been retired, which may well never
   * happen on a given device.  The bounce buffer is therefore allocated the
   * first time it is needed and then kept for any later move, so the common
   * case costs nothing and repeated moves do not hit the allocator.
   */

  if (bbm->blkbuf == NULL)
    {
      bbm->blkbuf = kmm_malloc(bbm->geo.blocksize);
      if (bbm->blkbuf == NULL)
        {
          return -ENOMEM;
        }
    }

  endblock = bbm_get_endblock(bbm, startblock, bbm->blkbuf);
  if (endblock <= startblock)
    {
      goto out;
    }

  if (endblock >= bbm->lpcount - 1)
    {
      ret = -ENOSPC;
      goto out;
    }

  fwarn("BBM: Start move block from %" PRIdOFF
        " to %" PRIdOFF "\n", startblock, endblock);

  while (endblock >= startblock)
    {
      fwarn("BBM: Move block %" PRIdOFF
            " to block %" PRIdOFF "\n", endblock, endblock + 1);

      /* Erase destination */

      ret = bbm_erase(&bbm->mtd, endblock + 1, 1);
      if (ret < 0)
        {
          goto out;
        }

      /* Copy block page by page */

      for (i = 0; i < bbm->blkper; i++)
        {
          ret = MTD_BREAD(bbm->lower,
                          bbm->lptable[endblock] * bbm->blkper + i,
                          1, bbm->blkbuf);
          if (ret < 0)
            {
              goto out;
            }

          ret = MTD_BWRITE(bbm->lower,
                           bbm->lptable[endblock + 1] * bbm->blkper + i,
                           1, bbm->blkbuf);
          if (ret < 0)
            {
              MTD_MARKBAD(bbm->lower, bbm->lptable[endblock + 1]);
              bbm_update_map(bbm, endblock + 1);

              ret = bbm_move(bbm, endblock + 1);
              if (ret < 0)
                {
                  goto out;
                }
            }
        }

      endblock--;
#if CONFIG_MTD_BBM_USLEEP > 0
      usleep(CONFIG_MTD_BBM_USLEEP);
#endif
    }

  fwarn("BBM: block move done\n");

out:
  return ret;
}

/****************************************************************************
 * Name: bbm_erase
 *
 * Description:
 *   Erase logical erase blocks with automatic bad block recovery.
 *
 ****************************************************************************/

static int bbm_erase(FAR struct mtd_dev_s *dev, off_t startblock,
                     size_t nblocks)
{
  FAR struct mtd_bbm_s *bbm = (FAR struct mtd_bbm_s *)dev;
  ssize_t ret;
  size_t i;

  for (i = 0; i < nblocks; i++)
    {
      off_t block = startblock + i;

      for (; ; )
        {
          if (block >= bbm->lpcount)
            {
              return -ENOSPC;
            }

          ret = MTD_ERASE(bbm->lower, bbm->lptable[block], 1);
          if (ret >= 0)
            {
              break;
            }

          /* Erase failed — mark bad, update map, move data */

          MTD_MARKBAD(bbm->lower, bbm->lptable[block]);
          bbm_update_map(bbm, block);

          ret = bbm_move(bbm, block);
          if (ret < 0)
            {
              return ret;
            }
        }
    }

  return OK;
}

/****************************************************************************
 * Name: bbm_bread
 *
 * Description:
 *   Read blocks with logical-to-physical mapping.
 *
 ****************************************************************************/

static ssize_t bbm_bread(FAR struct mtd_dev_s *dev, off_t startblock,
                         size_t nblocks, FAR uint8_t *buffer)
{
  FAR struct mtd_bbm_s *bbm = (FAR struct mtd_bbm_s *)dev;
  off_t mask = bbm->blkper - 1;
  size_t nread = nblocks;
  ssize_t ret;

  while (nblocks > 0)
    {
      off_t startphysicalblock;
      off_t starteraseblock;
      off_t offset;
      size_t count;

      starteraseblock = startblock / bbm->blkper;
      if (starteraseblock >= bbm->lpcount)
        {
          ret = -ENOSPC;
          break;
        }

      offset = startblock & mask;
      count = bbm_get_cblock(bbm, starteraseblock,
                             (offset + nblocks + mask) / bbm->blkper);
      count = MIN(count * bbm->blkper - offset, nblocks);
      startphysicalblock = bbm->lptable[starteraseblock] *
                           bbm->blkper + offset;

      ret = MTD_BREAD(bbm->lower, startphysicalblock, count, buffer);
      if (ret == count || ret == -EUCLEAN)
        {
          nblocks -= count;
          startblock += count;
          buffer += count * bbm->geo.blocksize;
        }
      else
        {
          bbm_update_map(bbm, starteraseblock);
          break;
        }
    }

  return nblocks != nread ? nread - nblocks : ret;
}

/****************************************************************************
 * Name: bbm_bwrite
 *
 * Description:
 *   Write the given number of blocks starting at the given logical block.
 *   The request is split at erase block boundaries and each piece is
 *   redirected through the logical to physical map, so writes that are
 *   neither erase block aligned nor a multiple of the erase block size
 *   are supported.
 *
 *   When a write fails the offending block is retired: it is marked bad,
 *   removed from the map, the data behind it is shifted forward and the
 *   piece is retried on the replacement block.
 *
 ****************************************************************************/

static ssize_t bbm_bwrite(FAR struct mtd_dev_s *dev, off_t startblock,
                          size_t nblocks, FAR const uint8_t *buffer)
{
  FAR struct mtd_bbm_s *bbm = (FAR struct mtd_bbm_s *)dev;
  off_t mask = bbm->blkper - 1;
  size_t nwrite = nblocks;
  ssize_t ret = OK;

  while (nblocks > 0)
    {
      off_t starteraseblock = startblock / bbm->blkper;
      off_t offset = startblock & mask;
      size_t count = MIN(bbm->blkper - offset, nblocks);

      if (starteraseblock >= bbm->lpcount)
        {
          ret = -ENOSPC;
          break;
        }

      ret = MTD_BWRITE(bbm->lower,
                       bbm->lptable[starteraseblock] * bbm->blkper + offset,
                       count, buffer);
      if (ret == count)
        {
          nblocks -= count;
          startblock += count;
          buffer += count * bbm->geo.blocksize;
          continue;
        }

      /* Retire the block and shift the data behind it forward.  The same
       * logical block now resolves to a different physical block.
       */

      MTD_MARKBAD(bbm->lower, bbm->lptable[starteraseblock]);
      bbm_update_map(bbm, starteraseblock);

      ret = bbm_move(bbm, starteraseblock);
      if (ret < 0)
        {
          break;
        }

      /* bbm_move() left the replacement block holding the data it carried
       * before the remap, so it has to be erased before the retry.
       */

      ret = bbm_erase(&bbm->mtd, starteraseblock, 1);
      if (ret < 0)
        {
          break;
        }
    }

  return nblocks != nwrite ? nwrite - nblocks : ret;
}

/****************************************************************************
 * Name: bbm_read
 *
 * Description:
 *   Byte-oriented read.  The request is split at erase block boundaries and
 *   each piece is translated through the logical to physical map before it
 *   is handed to the lower device, so no bounce buffer is needed.
 *
 *   Only installed when the lower device implements byte reads.
 *
 ****************************************************************************/

static ssize_t bbm_read(FAR struct mtd_dev_s *dev, off_t offset,
                        size_t nbytes, FAR uint8_t *buffer)
{
  FAR struct mtd_bbm_s *bbm = (FAR struct mtd_bbm_s *)dev;
  size_t erasesize = bbm->geo.erasesize;
  size_t nread = nbytes;
  ssize_t ret = OK;

  while (nbytes > 0)
    {
      off_t starteraseblock = offset / erasesize;
      off_t eraseoffset = offset % erasesize;
      size_t count;

      if (starteraseblock >= bbm->lpcount)
        {
          ret = -ENOSPC;
          break;
        }

      /* Coalesce erase blocks that are physically contiguous as well, so
       * the whole run is served by a single lower level access.
       */

      count = bbm_get_cblock(bbm, starteraseblock,
                             (eraseoffset + nbytes + erasesize - 1) /
                             erasesize);
      count = MIN(count * erasesize - eraseoffset, nbytes);

      ret = MTD_READ(bbm->lower,
                     bbm->lptable[starteraseblock] * erasesize + eraseoffset,
                     count, buffer);
      if (ret != count && ret != -EUCLEAN)
        {
          break;
        }

      nbytes -= count;
      offset += count;
      buffer += count;
    }

  return nbytes != nread ? nread - nbytes : ret;
}

#ifdef CONFIG_MTD_BYTE_WRITE

/****************************************************************************
 * Name: bbm_write
 *
 * Description:
 *   Byte-oriented write.  The request is split at erase block boundaries
 *   and each piece is translated through the logical to physical map before
 *   it is handed to the lower device.
 *
 *   When a write fails the offending block is retired the same way as in
 *   bbm_bwrite(): it is marked bad, removed from the map, the data behind
 *   it is shifted forward and the piece is retried on the replacement.
 *
 *   Only installed when the lower device implements byte writes.
 *
 ****************************************************************************/

static ssize_t bbm_write(FAR struct mtd_dev_s *dev, off_t offset,
                         size_t nbytes, FAR const uint8_t *buffer)
{
  FAR struct mtd_bbm_s *bbm = (FAR struct mtd_bbm_s *)dev;
  size_t erasesize = bbm->geo.erasesize;
  size_t nwrite = nbytes;
  ssize_t ret = OK;

  while (nbytes > 0)
    {
      off_t starteraseblock = offset / erasesize;
      off_t eraseoffset = offset % erasesize;
      size_t count = MIN(erasesize - eraseoffset, nbytes);

      if (starteraseblock >= bbm->lpcount)
        {
          ret = -ENOSPC;
          break;
        }

      ret = MTD_WRITE(bbm->lower,
                      bbm->lptable[starteraseblock] * erasesize +
                      eraseoffset, count, buffer);
      if (ret == count)
        {
          nbytes -= count;
          offset += count;
          buffer += count;
          continue;
        }

      /* Retire the block and shift the data behind it forward.  The same
       * logical block now resolves to a different physical block.
       */

      MTD_MARKBAD(bbm->lower, bbm->lptable[starteraseblock]);
      bbm_update_map(bbm, starteraseblock);

      ret = bbm_move(bbm, starteraseblock);
      if (ret < 0)
        {
          break;
        }

      /* bbm_move() left the replacement block holding the data it carried
       * before the remap, so it has to be erased before the retry.
       */

      ret = bbm_erase(&bbm->mtd, starteraseblock, 1);
      if (ret < 0)
        {
          break;
        }
    }

  return nbytes != nwrite ? nwrite - nbytes : ret;
}
#endif /* CONFIG_MTD_BYTE_WRITE */

/****************************************************************************
 * Name: bbm_ioctl
 *
 * Description:
 *   IOCTL pass-through. GEOMETRY returns logical (good) block count.
 *
 ****************************************************************************/

static int bbm_ioctl(FAR struct mtd_dev_s *dev, int cmd, unsigned long arg)
{
  FAR struct mtd_bbm_s *bbm = (FAR struct mtd_bbm_s *)dev;
  int ret;

  ret = MTD_IOCTL(bbm->lower, cmd, arg);

  /* Override geometry to report only good blocks */

  if (cmd == MTDIOC_GEOMETRY && ret >= 0)
    {
      FAR struct mtd_geometry_s *geo = (FAR struct mtd_geometry_s *)
                                       ((uintptr_t)arg);
      geo->neraseblocks = bbm->lpcount;
    }

  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: mtd_bbm_initialize
 ****************************************************************************/

FAR struct mtd_dev_s *mtd_bbm_initialize(FAR struct mtd_dev_s *mtd)
{
  FAR struct mtd_bbm_s *bbm;
  int ret;

  DEBUGASSERT(mtd != NULL);

  /* Probe bad block support.  A device that cannot report bad blocks (NOR
   * flash returns -ENOSYS) needs no logical to physical translation, so
   * hand back the lower device and keep this layer out of the data path
   * entirely.  Every wrapper that is created therefore always owns a valid
   * mapping table.
   */

  if (MTD_ISBAD(mtd, 0) == -ENOSYS)
    {
      return mtd;
    }

  bbm = kmm_zalloc(sizeof(struct mtd_bbm_s));
  if (bbm == NULL)
    {
      return NULL;
    }

  bbm->lower = mtd;

  /* Get device geometry */

  ret = MTD_IOCTL(mtd, MTDIOC_GEOMETRY,
                  (unsigned long)((uintptr_t)&bbm->geo));
  if (ret < 0)
    {
      ferr("ERROR: MTD ioctl(MTDIOC_GEOMETRY) failed: %d\n", ret);
      kmm_free(bbm);
      return NULL;
    }

  bbm->blkper = bbm->geo.erasesize / bbm->geo.blocksize;
  DEBUGASSERT(bbm->blkper * bbm->geo.blocksize == bbm->geo.erasesize);

  /* Build the logical to physical mapping table */

  ret = bbm_init_map(bbm);
  if (ret < 0)
    {
      kmm_free(bbm);
      return NULL;
    }

  /* Fill in the MTD method table */

  bbm->mtd.erase  = bbm_erase;
  bbm->mtd.bread  = bbm_bread;
  bbm->mtd.bwrite = bbm_bwrite;
  bbm->mtd.ioctl  = bbm_ioctl;

  /* Mirror the byte read capability of the lower device.  Advertising a
   * read method that the lower device cannot serve would make consumers
   * that probe for it (lib_mtdsistream_open() for one) select a path that
   * always fails instead of falling back to bread().
   */

  if (mtd->read != NULL)
    {
      bbm->mtd.read = bbm_read;
    }

#ifdef CONFIG_MTD_BYTE_WRITE
  /* Likewise for byte writes.  lib_mtdoutstream_open() and friends switch
   * to a byte oriented path as soon as this method is present.
   */

  if (mtd->write != NULL)
    {
      bbm->mtd.write = bbm_write;
    }
#endif

  snprintf(bbm->name, sizeof(bbm->name), "bbm-%s",
           mtd->name ? mtd->name : "mtd");
  bbm->mtd.name   = bbm->name;

  return &bbm->mtd;
}

/****************************************************************************
 * Name: mtd_bbm_uninitialize
 ****************************************************************************/

void mtd_bbm_uninitialize(FAR struct mtd_dev_s *dev)
{
  FAR struct mtd_bbm_s *bbm = (FAR struct mtd_bbm_s *)dev;

  /* mtd_bbm_initialize() returns the lower device unchanged when the
   * underlying MTD needs no bad block management.  Such a device is not
   * one of our wrappers and must not be freed here, hence the check on a
   * method that only our instances carry.
   */

  if (dev != NULL && dev->erase == bbm_erase)
    {
      kmm_free(bbm->lptable);
      kmm_free(bbm->blkbuf);
      kmm_free(bbm);
    }
}
