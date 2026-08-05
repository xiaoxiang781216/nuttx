/****************************************************************************
 * drivers/mtd/mtd_config_rram.c
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

#include <assert.h>
#include <debug.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/param.h>
#include <sys/types.h>

#include <nuttx/bits.h>
#include <nuttx/crc16.h>
#include <nuttx/crc32.h>
#include <nuttx/fs/fs.h>
#include <nuttx/kmalloc.h>
#include <nuttx/mtd/configdata.h>
#include <nuttx/mtd/mtd.h>
#include <nuttx/mutex.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define RRAM_VALUE_SIZE      CONFIG_MTD_CONFIG_VALUE_LEN

#ifdef CONFIG_MTD_CONFIG_NAMED
#  define RRAM_ATE_IS_ZERO(ate)  ((ate)->name[0] == 0)
#  define RRAM_KEY_MATCH(e, pdata)                  \
     (strncmp((e)->name, (pdata)->name, CONFIG_MTD_CONFIG_NAME_LEN) == 0)
#else
#  define RRAM_ATE_IS_ZERO(ate)                     \
     (((ate)->instance | (ate)->id | (ate)->vlen |  \
       (ate)->data_crc32 | (ate)->ate_crc16) == 0)
#  define RRAM_KEY_MATCH(e, pdata)                  \
     ((e)->id == (pdata)->id && (e)->instance == (pdata)->instance)
#endif

#if CONFIG_MTD_CONFIG_BUFFER_SIZE > 0
#  define RRAM_BUFFER_SIZE(fs)  CONFIG_MTD_CONFIG_BUFFER_SIZE
#  define RRAM_ATE(name, size)                                    \
     uint8_t name##_buf[CONFIG_MTD_CONFIG_BUFFER_SIZE];           \
     FAR struct rram_ate *name = (FAR struct rram_ate *)name##_buf
#else
#  define RRAM_BUFFER_SIZE(fs)  ((fs)->progsize)
#  define RRAM_ATE(name, size)                                    \
     uint8_t name##_buf[size];                                    \
     FAR struct rram_ate *name = (FAR struct rram_ate *)name##_buf
#endif

/* Occupied-slot bitmap size, in bytes, sized in unsigned long words so the
 * nuttx/bits.h helpers (set_bit/clear_bit/find_first_zero_bit) can be used.
 */

#define RRAM_SLOT_BYTES(fs) \
     (BITS_TO_LONGS((fs)->nslots) * sizeof(unsigned long))

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* Allocation Table Entry */

begin_packed_struct struct rram_ate
{
#ifdef CONFIG_MTD_CONFIG_NAMED
  char     name[CONFIG_MTD_CONFIG_NAME_LEN]; /* String key */
#else
  uint32_t instance;                         /* Config instance */
  uint16_t id;                               /* Config id */
#endif
  uint16_t vlen;                             /* Value length */
  uint32_t data_crc32;                       /* crc32 of value */
  uint16_t ate_crc16;                        /* crc16 of ATE header */
} end_packed_struct;

#ifdef CONFIG_MTD_CONFIG_FULL_CACHE
struct rram_cache
{
#ifdef CONFIG_MTD_CONFIG_NAMED
  char     name[CONFIG_MTD_CONFIG_NAME_LEN]; /* String key */
#else
  uint32_t instance;                         /* Config instance */
  uint16_t id;                               /* Config id */
#endif
  uint32_t slot;                             /* Physical slot number */
};
#endif

struct rram_fs
{
  FAR struct mtd_dev_s  *mtd;       /* Backing MTD device */
  mutex_t               lock;       /* Serialize all accesses */
  uint32_t              progsize;   /* Page size (write granularity) */
  uint8_t               erasestate; /* Erased byte value (active mark) */
  uint32_t              slotsize;   /* Bytes per slot (page-aligned) */
  uint32_t              nslots;     /* Number of slots in the partition */
#ifdef CONFIG_MTD_CONFIG_FULL_CACHE
  FAR unsigned long     *slotmap;   /* Occupied-slot bitmap (nslots bits) */
  FAR struct rram_cache *cache;     /* Key-sorted index (up to nslots) */
  uint32_t              ncache;     /* Number of live keys in the index */
#endif
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int mtdconfig_ioctl(FAR struct file *filep, int cmd,
                           unsigned long arg);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct file_operations g_rram_fops =
{
  NULL,            /* Open */
  NULL,            /* Close */
  NULL,            /* Read  (KV store: use ioctl) */
  NULL,            /* Write */
  NULL,            /* Seek */
  mtdconfig_ioctl, /* Ioctl */
  NULL,            /* Truncate */
  NULL             /* Poll */
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: rram_align_up
 ****************************************************************************/

static inline size_t rram_align_up(FAR struct rram_fs *fs, size_t len)
{
  return (len + (fs->progsize - 1)) & ~(fs->progsize - 1);
}

/****************************************************************************
 * Name: rram_ate_size
 ****************************************************************************/

static inline size_t rram_ate_size(FAR struct rram_fs *fs)
{
  return rram_align_up(fs, sizeof(struct rram_ate));
}

/****************************************************************************
 * Name: rram_ate_off
 *
 * Description:
 *   Byte offset of the ATE unit within a slot: the value region rounded up
 *   to a page, so the ATE starts on a page boundary.
 *
 ****************************************************************************/

static inline uint32_t rram_ate_off(FAR struct rram_fs *fs)
{
  return rram_align_up(fs, RRAM_VALUE_SIZE);
}

/****************************************************************************
 * Name: rram_ate_valid
 *
 * Description:
 *   True when the slot holds a live key. A free slot (erasestate 0) or a
 *   deleted slot (its key page zeroed) has an all-zero ATE header and reads
 *   as invalid; a live entry passes even with a zero-length value.
 *
 ****************************************************************************/

static inline bool rram_ate_valid(FAR const struct rram_ate *ate)
{
  return ate->vlen <= RRAM_VALUE_SIZE &&
         !RRAM_ATE_IS_ZERO(ate) &&
         ate->ate_crc16 ==
         crc16part((FAR const uint8_t *)ate,
                   offsetof(struct rram_ate, ate_crc16), 0);
}

/****************************************************************************
 * Name: rram_flash_read
 *
 * Description:
 *   Basic flash read from rram address.
 *
 ****************************************************************************/

static int rram_flash_read(FAR struct rram_fs *fs, off_t off, FAR void *buf,
                           size_t len)
{
  ssize_t ret;

  ret = MTD_READ(fs->mtd, off, len, buf);
  if (ret == -EBADMSG)
    {
      ret = MTD_READ(fs->mtd, off, len, buf);
      if (ret == -EBADMSG)
        {
          memset(buf, ~fs->erasestate, len);
          return 0;
        }
    }

  return ret == len ? 0 : (ret < 0 ? ret : -EIO);
}

/****************************************************************************
 * Name: rram_flash_write
 *
 * Description:
 *   Write len bytes to flash. The granularity is selected at build time via
 *   CONFIG_MTD_BYTE_WRITE: MTD_WRITE when set, else MTD_BWRITE. For the
 *   block path a trailing partial page (len not a multiple of progsize) is
 *   padded out to a full page before being written, so callers may pass an
 *   arbitrary len.
 *
 ****************************************************************************/

static int rram_flash_write(FAR struct rram_fs *fs, off_t off,
                            FAR const void *data, size_t len)
{
  FAR const uint8_t *data8 = (FAR const uint8_t *)data;
  ssize_t ret;

#ifdef CONFIG_MTD_BYTE_WRITE
  ret = MTD_WRITE(fs->mtd, off, len, data8);
  return ret == len ? 0 : (ret < 0 ? ret : -EIO);
#else
  uint8_t buf[RRAM_BUFFER_SIZE(fs)];
  size_t nblk = len / fs->progsize;
  size_t bulk = nblk * fs->progsize;

  DEBUGASSERT(off % fs->progsize == 0);

  if (nblk > 0)
    {
      ret = MTD_BWRITE(fs->mtd, off / fs->progsize, nblk, data8);
      if (ret != nblk)
        {
          return ret < 0 ? ret : -EIO;
        }

      len -= bulk;
      data8 += bulk;
      off += bulk;
    }

  if (len > 0)
    {
      memset(buf, 0, fs->progsize);
      memcpy(buf, data8, len);
      ret = MTD_BWRITE(fs->mtd, off / fs->progsize, 1, buf);
      if (ret != 1)
        {
          return ret < 0 ? ret : -EIO;
        }
    }

  return 0;
#endif
}

/****************************************************************************
 * Name: rram_cache_cmp
 *
 * Description:
 *   Total order over cache entries by key, used by rram_cache_bsearch()
 *   for both lookups and sorted inserts.
 *
 ****************************************************************************/

#ifdef CONFIG_MTD_CONFIG_FULL_CACHE
static inline int rram_cache_cmp(FAR const struct rram_cache *ca,
                                 FAR const struct rram_cache *cb)
{
#ifdef CONFIG_MTD_CONFIG_NAMED
  return strncmp(ca->name, cb->name, CONFIG_MTD_CONFIG_NAME_LEN);
#else
  if (ca->id != cb->id)
    {
      return ca->id > cb->id ? 1 : -1;
    }

  return (ca->instance > cb->instance) - (ca->instance < cb->instance);
#endif
}

/****************************************************************************
 * Name: rram_cache_bsearch
 *
 * Description:
 *   Binary search the sorted index for entry's key. Sets *index to the
 *   lower-bound position, i.e. the first entry whose key is >= entry's key,
 *   which is exactly the sorted insert position when the key is absent.
 *   Returns true when the entry at *index matches, else false.
 *
 ****************************************************************************/

static bool rram_cache_bsearch(FAR struct rram_fs *fs,
                               FAR const struct rram_cache *entry,
                               FAR uint32_t *index)
{
  uint32_t hi = fs->ncache;
  uint32_t lo = 0;
  uint32_t mid;
  int cmp;

  while (lo < hi)
    {
      mid = lo + (hi - lo) / 2;
      cmp = rram_cache_cmp(entry, &fs->cache[mid]);

      if (cmp == 0)
        {
          *index = mid;
          return true;
        }
      else if (cmp < 0)
        {
          hi = mid;
        }
      else
        {
          lo = mid + 1;
        }
    }

  *index = lo;
  return false;
}

/****************************************************************************
 * Name: rram_cache_add
 ****************************************************************************/

static void rram_cache_add(FAR struct rram_fs *fs, uint32_t index,
                           FAR const struct rram_ate *ate, uint32_t slot)
{
  FAR struct rram_cache *entry = &fs->cache[index];

  memmove(entry + 1, entry,
          (fs->ncache - index) * sizeof(struct rram_cache));

#ifdef CONFIG_MTD_CONFIG_NAMED
  memcpy(entry->name, ate->name, CONFIG_MTD_CONFIG_NAME_LEN);
#else
  entry->id       = ate->id;
  entry->instance = ate->instance;
#endif
  entry->slot     = slot;
  set_bit(slot, fs->slotmap);
  fs->ncache++;
}

/****************************************************************************
 * Name: rram_cache_remove
 ****************************************************************************/

static void rram_cache_remove(FAR struct rram_fs *fs, uint32_t index)
{
  clear_bit(fs->cache[index].slot, fs->slotmap);
  memmove(&fs->cache[index], &fs->cache[index + 1],
          (fs->ncache - index - 1) * sizeof(struct rram_cache));
  fs->ncache--;
}

/****************************************************************************
 * Name: rram_build
 *
 * Description:
 *   Scan every slot once at register time and mark each occupied slot (valid
 *   header) in the slotmap. When the full cache is enabled, the key index is
 *   populated from the same pass.
 *
 ****************************************************************************/

static int rram_build(FAR struct rram_fs *fs)
{
  size_t ate_size = rram_ate_size(fs);
  RRAM_ATE(ate, ate_size);
  struct rram_cache entry;
  uint32_t index;
  uint32_t i;
  int rc;

  memset(fs->slotmap, 0, RRAM_SLOT_BYTES(fs));
  fs->ncache = 0;

  for (i = 0; i < fs->nslots; i++)
    {
      rc = rram_flash_read(fs, i * fs->slotsize + rram_ate_off(fs), ate,
                           ate_size);
      if (rc < 0)
        {
          return rc;
        }

      if (rram_ate_valid(ate))
        {
#  ifdef CONFIG_MTD_CONFIG_NAMED
          memcpy(entry.name, ate->name, CONFIG_MTD_CONFIG_NAME_LEN);
#  else
          entry.id       = ate->id;
          entry.instance = ate->instance;
#  endif
          rram_cache_bsearch(fs, &entry, &index);
          rram_cache_add(fs, index, ate, i);
        }
    }

  return OK;
}
#endif

/****************************************************************************
 * Name: rram_find
 *
 * Description:
 *   Locate the slot holding pdata's key and read its ATE. Resolution uses
 *   the in-RAM slot cache when enabled, else a flash scan.
 *
 * Input Parameters:
 *   fs        - The RRAM instance.
 *   pdata     - Carries the key to look up (name in NAMED mode, else
 *               id/instance).
 *   ate       - Output buffer that receives the matched slot's ATE.
 *   index     - Output (may be NULL, FULL_CACHE only): the cache index from
 *               the lookup - the entry position on a hit, else the sorted
 *               insert position. Lets write()/delete() reuse it without a
 *               second search.
 *   slot      - Output: slot number of the match, or fs->nslots if none.
 *   free_slot - Output (may be NULL): slot that write() should target - the
 *               matching slot if the key exists, else the first free slot,
 *               else fs->nslots when the store is full.
 *
 * Returned Value:
 *   OK when the key is found; -ENOENT if not; a negative errno on flash
 *   error.
 *
 ****************************************************************************/

static int rram_find(FAR struct rram_fs *fs,
                     FAR const struct config_data_s *pdata,
                     FAR struct rram_ate *ate, FAR uint32_t *index,
                     FAR uint32_t *slot, FAR uint32_t *free_slot)
{
  size_t ate_size = rram_ate_size(fs);
  uint32_t ate_off = rram_ate_off(fs);
  uint32_t i;
  int rc;

#ifdef CONFIG_MTD_CONFIG_FULL_CACHE
  struct rram_cache entry;
  bool found;

#  ifdef CONFIG_MTD_CONFIG_NAMED
  memcpy(entry.name, pdata->name, CONFIG_MTD_CONFIG_NAME_LEN);
#  else
  entry.id       = pdata->id;
  entry.instance = pdata->instance;
#  endif

  found = rram_cache_bsearch(fs, &entry, &i);

  if (index != NULL)
    {
      *index = i;
    }

  if (found)
    {
      *slot = fs->cache[i].slot;

      rc = rram_flash_read(fs, *slot * fs->slotsize + ate_off, ate,
                           ate_size);
      if (rc < 0)
        {
          return rc;
        }

      if (rram_ate_valid(ate))
        {
          if (free_slot != NULL)
            {
              *free_slot = *slot;
            }

          return OK;
        }

      rram_cache_remove(fs, i);
    }

  /* Not found: return the first free slot (fs->nslots when full). */

  *slot = fs->nslots;
  if (free_slot != NULL)
    {
      *free_slot = find_first_zero_bit(fs->slotmap, fs->nslots);
    }

  return -ENOENT;
#else
  UNUSED(index);
  *slot = fs->nslots;
  if (free_slot != NULL)
    {
      *free_slot = fs->nslots;
    }

  for (i = 0; i < fs->nslots; i++)
    {
      rc = rram_flash_read(fs, i * fs->slotsize + ate_off, ate, ate_size);
      if (rc < 0)
        {
          return rc;
        }

      if (!rram_ate_valid(ate))
        {
          if (free_slot != NULL && *free_slot == fs->nslots)
            {
              *free_slot = i;
            }
        }
      else if (RRAM_KEY_MATCH(ate, pdata))
        {
          *slot = i;
          if (free_slot != NULL)
            {
              *free_slot = i;
            }

          return OK;
        }
    }

  return -ENOENT;
#endif
}

/****************************************************************************
 * Name: rram_read
 *
 * Description:
 *   Locate the KV (rram_find validates the header), read the value and
 *   verify data_crc32. pdata->len always gets the real length; a truncated
 *   read returns -EBADMSG.
 *
 ****************************************************************************/

static int rram_read(FAR struct rram_fs *fs, FAR struct config_data_s *pdata)
{
  RRAM_ATE(ate, rram_ate_size(fs));
  uint32_t slot;
  uint32_t crc;
  off_t base;
  int rc;

  rc = rram_find(fs, pdata, ate, NULL, &slot, NULL);
  if (rc < 0)
    {
      return rc;
    }

  base = slot * fs->slotsize;

  rc = rram_flash_read(fs, base, pdata->configdata,
                       MIN(ate->vlen, pdata->len));
  if (rc < 0)
    {
      return rc;
    }

  crc = crc32part(pdata->configdata, MIN(pdata->len, ate->vlen), 0);

  pdata->len = ate->vlen;
  if (crc != ate->data_crc32)
    {
      return -EBADMSG;
    }

  return OK;
}

/****************************************************************************
 * Name: rram_write
 *
 * Description:
 *   Write a KV into a slot: write value + ATE (ate_crc16 last as the commit
 *   point). Writing the full ATE overwrites any zeroed key left by a prior
 *   delete on a reused slot.
 *
 ****************************************************************************/

static int rram_write(FAR struct rram_fs *fs,
                      FAR struct config_data_s *pdata)
{
  size_t ate_size = rram_ate_size(fs);
  RRAM_ATE(ate, ate_size);
  uint32_t free_slot;
  uint32_t slot;
  uint32_t index;
  off_t base;
  int rc;

  /* Target slot: the one already holding this key, else a reusable slot. */

  rc = rram_find(fs, pdata, ate, &index, &slot, &free_slot);
  if (rc < 0 && rc != -ENOENT)
    {
      return rc;
    }

  if (free_slot >= fs->nslots)
    {
      return -ENOSPC;
    }

  base = free_slot * fs->slotsize;

  memset(ate, fs->erasestate, ate_size);
#ifdef CONFIG_MTD_CONFIG_NAMED
  memcpy(ate->name, pdata->name, CONFIG_MTD_CONFIG_NAME_LEN);
#else
  ate->id       = pdata->id;
  ate->instance = pdata->instance;
#endif
  ate->vlen     = pdata->len;

  ate->data_crc32 = crc32part(pdata->configdata, pdata->len, 0);
  ate->ate_crc16  = crc16part((FAR const uint8_t *)ate,
                              offsetof(struct rram_ate, ate_crc16), 0);

  rc = rram_flash_write(fs, base, pdata->configdata, pdata->len);
  if (rc < 0)
    {
      return rc;
    }

  rc = rram_flash_write(fs, base + rram_ate_off(fs), ate, ate_size);
#ifdef CONFIG_MTD_CONFIG_FULL_CACHE
  if (rc >= 0 && slot >= fs->nslots)
    {
      rram_cache_add(fs, index, ate, free_slot);
    }
#endif

  return rc;
}

/****************************************************************************
 * Name: rram_delete
 ****************************************************************************/

static int rram_delete(FAR struct rram_fs *fs,
                       FAR struct config_data_s *pdata)
{
  RRAM_ATE(ate, rram_ate_size(fs));
  uint32_t index;
  uint32_t slot;
  off_t base;
  int rc;

  rc = rram_find(fs, pdata, ate, &index, &slot, NULL);
  if (rc < 0)
    {
      return rc == -ENOENT ? OK : rc;
    }

  base = slot * fs->slotsize;

  /* Zero the key page to break ate_crc16, marking the slot invalid. */

  memset(ate, 0, fs->progsize);
  rc = rram_flash_write(fs, base + rram_ate_off(fs), ate, fs->progsize);
#ifdef CONFIG_MTD_CONFIG_FULL_CACHE
  if (rc >= 0)
    {
      rram_cache_remove(fs, index);
    }
#endif

  return rc;
}

/****************************************************************************
 * Name: rram_bulkerase
 *
 * Description:
 *   Free every slot by zeroing its key page (same as rram_delete). A slot
 *   with a zeroed key reads as invalid, so the store becomes empty. Only the
 *   one-page key region is written per slot.
 *
 ****************************************************************************/

static int rram_bulkerase(FAR struct rram_fs *fs)
{
  RRAM_ATE(ate, rram_ate_size(fs));
  uint32_t i;
  int rc;

  memset(ate, 0, fs->progsize);
  for (i = 0; i < fs->nslots; i++)
    {
      rc = rram_flash_write(fs, i * fs->slotsize + rram_ate_off(fs), ate,
                            fs->progsize);
      if (rc < 0)
        {
          return rc;
        }
    }

#ifdef CONFIG_MTD_CONFIG_FULL_CACHE
  memset(fs->slotmap, 0, RRAM_SLOT_BYTES(fs));
  fs->ncache = 0;
#endif

  return OK;
}

/****************************************************************************
 * Name: mtdconfig_ioctl
 ****************************************************************************/

static int mtdconfig_ioctl(FAR struct file *filep, int cmd,
                           unsigned long arg)
{
  FAR struct inode *inode = filep->f_inode;
  FAR struct rram_fs *fs = inode->i_private;
  FAR struct config_data_s *pdata = (FAR struct config_data_s *)arg;
  int rc;

  if (cmd >= CFGDIOC_GETCONFIG && cmd <= CFGDIOC_DELCONFIG &&
      (pdata == NULL || pdata->len > RRAM_VALUE_SIZE ||
       (cmd != CFGDIOC_DELCONFIG &&
        (pdata->configdata == NULL || pdata->len == 0))))
    {
      return -EINVAL;
    }

  rc = nxmutex_lock(&fs->lock);
  if (rc < 0)
    {
      return rc;
    }

  switch (cmd)
    {
      case CFGDIOC_GETCONFIG:
        rc = rram_read(fs, pdata);
        break;

      case CFGDIOC_SETCONFIG:
        rc = rram_write(fs, pdata);
        break;

      case CFGDIOC_DELCONFIG:
        rc = rram_delete(fs, pdata);
        break;

      case MTDIOC_BULKERASE:
        rc = rram_bulkerase(fs);
        break;

      default:
        rc = MTD_IOCTL(fs->mtd, cmd, arg);
        break;
    }

  nxmutex_unlock(&fs->lock);
  return rc;
}

/****************************************************************************
 * Name: rram_mount
 *
 * Description:
 *   Query the MTD geometry, lay out a page-aligned, data-first slot (value
 *   region followed by the ATE), and compute the number of slots.
 *
 ****************************************************************************/

static int rram_mount(FAR struct rram_fs *fs)
{
  struct mtd_geometry_s geo;
  uint64_t total;
  int rc;

  rc = MTD_IOCTL(fs->mtd, MTDIOC_GEOMETRY, (unsigned long)(uintptr_t)&geo);
  if (rc < 0)
    {
      ferr("ERROR: MTD ioctl(MTDIOC_GEOMETRY) failed: %d\n", rc);
      return rc;
    }

  rc = MTD_IOCTL(fs->mtd, MTDIOC_ERASESTATE,
                 (unsigned long)(uintptr_t)&fs->erasestate);
  if (rc < 0)
    {
      ferr("ERROR: MTD ioctl(MTDIOC_ERASESTATE) failed: %d\n", rc);
      return rc;
    }

  fs->progsize = geo.blocksize;
  if (fs->progsize == 0)
    {
      ferr("ERROR: invalid page size 0\n");
      return -EINVAL;
    }

#if CONFIG_MTD_CONFIG_BUFFER_SIZE > 0
  DEBUGASSERT(rram_ate_size(fs) <= CONFIG_MTD_CONFIG_BUFFER_SIZE);
#endif

  fs->slotsize = rram_ate_off(fs) + rram_ate_size(fs);

  total = (uint64_t)geo.erasesize * geo.neraseblocks;
  fs->nslots = (uint32_t)(total / fs->slotsize);
  if (fs->nslots == 0)
    {
      ferr("ERROR: partition too small: total=%llu slotsize=%" PRIu32 "\n",
           total, fs->slotsize);
      return -EINVAL;
    }

  finfo("rram: progsize=%" PRIu32 " slotsize=%" PRIu32
        " nslots=%" PRIu32 " valuelen=%d\n",
        fs->progsize, fs->slotsize, fs->nslots, RRAM_VALUE_SIZE);

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: mtdconfig_register_by_path
 ****************************************************************************/

int mtdconfig_register_by_path(FAR struct mtd_dev_s *mtd,
                               FAR const char *path)
{
  FAR struct rram_fs *fs;
  int rc;

  fs = kmm_zalloc(sizeof(struct rram_fs));
  if (fs == NULL)
    {
      return -ENOMEM;
    }

  fs->mtd = mtd;
  rc = nxmutex_init(&fs->lock);
  if (rc < 0)
    {
      goto mutex_err;
    }

  rc = rram_mount(fs);
  if (rc < 0)
    {
      goto mount_err;
    }

#ifdef CONFIG_MTD_CONFIG_FULL_CACHE
  fs->cache = kmm_zalloc((size_t)fs->nslots * sizeof(struct rram_cache));
  if (fs->cache == NULL)
    {
      rc = -ENOMEM;
      goto mount_err;
    }

  fs->slotmap = kmm_zalloc(RRAM_SLOT_BYTES(fs));
  if (fs->slotmap == NULL)
    {
      rc = -ENOMEM;
      goto cache_err;
    }

  rc = rram_build(fs);
  if (rc < 0)
    {
      ferr("ERROR: rram build failed: %d\n", rc);
      goto slotmap_err;
    }
#endif

  rc = register_driver(path, &g_rram_fops, 0666, fs);
  if (rc < 0)
    {
      ferr("ERROR: register mtd config failed: %d\n", rc);
      goto slotmap_err;
    }

  return rc;

slotmap_err:
#ifdef CONFIG_MTD_CONFIG_FULL_CACHE
  kmm_free(fs->slotmap);
cache_err:
  kmm_free(fs->cache);
#endif
mount_err:
  nxmutex_destroy(&fs->lock);
mutex_err:
  kmm_free(fs);
  return rc;
}

/****************************************************************************
 * Name: mtdconfig_register
 ****************************************************************************/

int mtdconfig_register(FAR struct mtd_dev_s *mtd)
{
  return mtdconfig_register_by_path(mtd, "/dev/config");
}

/****************************************************************************
 * Name: mtdconfig_unregister_by_path
 ****************************************************************************/

int mtdconfig_unregister_by_path(FAR const char *path)
{
  FAR struct inode *inode;
  FAR struct rram_fs *fs;
  struct file file;
  int rc;

  rc = file_open(&file, path, O_CLOEXEC);
  if (rc < 0)
    {
      ferr("ERROR: open file %s err: %d\n", path, rc);
      return rc;
    }

  inode = file.f_inode;
  fs = inode->i_private;
  nxmutex_destroy(&fs->lock);
#ifdef CONFIG_MTD_CONFIG_FULL_CACHE
  kmm_free(fs->slotmap);
  kmm_free(fs->cache);
#endif
  kmm_free(fs);
  file_close(&file);
  unregister_driver(path);

  return OK;
}

/****************************************************************************
 * Name: mtdconfig_unregister
 ****************************************************************************/

int mtdconfig_unregister(void)
{
  return mtdconfig_unregister_by_path("/dev/config");
}
