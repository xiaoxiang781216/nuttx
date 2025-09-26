/****************************************************************************
 * drivers/mtd/mtd_config_nvs2.c
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
 * NVS2: non volatile storage in flash
 *
 * Copyright (c) 2018 Laczen
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <debug.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/poll.h>

#include <nuttx/bits.h>
#include <nuttx/crc8.h>
#include <nuttx/kmalloc.h>
#include <nuttx/mtd/mtd.h>
#include <nuttx/mtd/configdata.h>
#include <nuttx/mutex.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* MASKS AND SHIFT FOR ADDRESSES
 * an address in nvs is an uint64_t where:
 *   high 4 bytes represent the block number
 *   low 4 bytes represent the offset in a block
 */

#define NVS_ADDR_BLOCK_MASK             GENMASK_ULL(63, 32)
#define NVS_ADDR_BLOCK_SHIFT            32
#define NVS_ADDR_OFFSET_MASK            GENMASK_ULL(31, 0)

#define NVS_CACHE_NO_ADDR               GENMASK_ULL(63, 0)

#define NVS_INVALID_BLOCK               GENMASK_ULL(31, 0)

#define NVS_HASH_INITIAL_VALUE          2166136261

#if CONFIG_MTD_CONFIG_BUFFER_SIZE > 0
#  define NVS_BUFFER_SIZE(fs)           CONFIG_MTD_CONFIG_BUFFER_SIZE
#  define NVS_ATE(name, size) \
    char name##_buf[CONFIG_MTD_CONFIG_BUFFER_SIZE]; \
    FAR struct nvs_ate *name = (FAR struct nvs_ate *)name##_buf
#else
#  define NVS_BUFFER_SIZE(fs)           nvs_align_up(fs, 32)
#  define NVS_ATE(name, size) \
    char name##_buf[size]; \
    FAR struct nvs_ate *name = (FAR struct nvs_ate *)name##_buf
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* Non-volatile Storage File system structure */

struct nvs_cache
{
#ifdef CONFIG_MTD_CONFIG_FULL_CACHE
  uint32_t id;
#endif
  uint64_t addr;
};

struct nvs_fs
{
  FAR struct mtd_dev_s *mtd;           /* MTD device */
  uint32_t              blocksize;     /* Size of one nvs block */
  uint32_t              nblocks;       /* Number of nvs blocks */
  uint16_t              progsize;      /* Size of one read/write block */
  uint8_t               erasestate;    /* Erased value */
  uint8_t               cycle_cnt;     /* Current cycle counter of the
                                        * active block
                                        */
  uint64_t              ate_wra;       /* Next alloc table entry
                                        * Write address
                                        */
  uint64_t              data_wra;      /* Next data write address */
  mutex_t               nvs_lock;
#if CONFIG_MTD_CONFIG_CACHE_SIZE > 0
  struct nvs_cache      cache[CONFIG_MTD_CONFIG_CACHE_SIZE];
#endif
#ifdef CONFIG_MTD_CONFIG_FULL_CACHE
  bool                  cache_partial;
#endif
};

/* Allocation Table Entry */

begin_packed_struct struct nvs_ate
{
  uint32_t id;           /* Data id */
  uint32_t offset;       /* Data offset within block */
  uint16_t len;          /* Data len within block */
  uint16_t key_len;      /* Key string len */
  uint8_t  data_crc8;    /* Crc8 check of the data */
  uint8_t  part;         /* Future extension */
  uint8_t  cycle_cnt;    /* cycle counter for non erasable devices */
  uint8_t  crc8;         /* Crc8 check of the ate entry */
} end_packed_struct;

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/* MTD NVS opeation api */

static int     mtdconfig_open(FAR struct file *filep);
static int     mtdconfig_close(FAR struct file *filep);
static ssize_t mtdconfig_read(FAR struct file *filep, FAR char *buffer,
                              size_t buflen);
static int     mtdconfig_ioctl(FAR struct file *filep, int cmd,
                               unsigned long arg);
static int     mtdconfig_poll(FAR struct file *filep, FAR struct pollfd *fds,
                              bool setup);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct file_operations g_mtdconfig_fops =
{
  mtdconfig_open,  /* Open */
  mtdconfig_close, /* Close */
  mtdconfig_read,  /* Read */
  NULL,            /* Write */
  NULL,            /* Seek */
  mtdconfig_ioctl, /* Ioctl */
  NULL,            /* Truncate */
  NULL,            /* Mmap */
  mtdconfig_poll   /* Poll */
};

/****************************************************************************
 * Public Data
 ****************************************************************************/

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: nvs_compare_cache
 ****************************************************************************/

#ifdef CONFIG_MTD_CONFIG_FULL_CACHE
static int nvs_compare_cache(FAR const void *a, FAR const void *b)
{
  FAR const struct nvs_cache *cache_a = (FAR const struct nvs_cache *)a;
  FAR const struct nvs_cache *cache_b = (FAR const struct nvs_cache *)b;

  return (cache_a->id > cache_b->id) ? 1 :
         (cache_a->id < cache_b->id) ? -1 : 0;
}
#endif

/****************************************************************************
 * Name: nvs_invalid_cache
 ****************************************************************************/

#if CONFIG_MTD_CONFIG_CACHE_SIZE > 0
static void nvs_invalid_cache(FAR struct nvs_fs *fs, uint32_t block)
{
  FAR struct nvs_cache *cache_entry = fs->cache;
  FAR const struct nvs_cache *cache_end =
                              &fs->cache[CONFIG_MTD_CONFIG_CACHE_SIZE];

  for (; cache_entry < cache_end; ++cache_entry)
    {
      if (cache_entry->addr >> NVS_ADDR_BLOCK_SHIFT == block)
        {
          memset(cache_entry, 0xff, sizeof(struct nvs_cache));
        }
    }

#  ifdef CONFIG_MTD_CONFIG_FULL_CACHE
  qsort(fs->cache, CONFIG_MTD_CONFIG_CACHE_SIZE, sizeof(struct nvs_cache),
        nvs_compare_cache);
#  endif
}

/****************************************************************************
 * Name: nvs_lookup_cache
 ****************************************************************************/

static FAR struct nvs_cache *nvs_lookup_cache(FAR struct nvs_fs *fs,
                                              uint32_t id)
{
#  if defined(CONFIG_MTD_CONFIG_HASH_TABLE)
  return &fs->cache[id % CONFIG_MTD_CONFIG_CACHE_SIZE];
#  elif defined(CONFIG_MTD_CONFIG_FULL_CACHE)
  struct nvs_cache cache_tmp;

  cache_tmp.id = id;
  return (FAR struct nvs_cache *)bsearch(&cache_tmp, fs->cache,
                                         CONFIG_MTD_CONFIG_CACHE_SIZE,
                                         sizeof(struct nvs_cache),
                                         nvs_compare_cache);
#  endif
}

/****************************************************************************
 * Name: nvs_lookup_addr
 ****************************************************************************/

static uint64_t nvs_lookup_addr(FAR struct nvs_fs *fs, uint32_t id)
{
  FAR struct nvs_cache *cache_entry = nvs_lookup_cache(fs, id);
#  if defined(CONFIG_MTD_CONFIG_HASH_TABLE)
  return cache_entry->addr;
#  elif defined(CONFIG_MTD_CONFIG_FULL_CACHE)
  if (cache_entry != NULL)
    {
      return cache_entry->addr;
    }
  else if (fs->cache_partial)
    {
      return fs->ate_wra;
    }
  else
    {
      return NVS_CACHE_NO_ADDR;
    }
#  endif
}

/****************************************************************************
 * Name: nvs_search_cache
 ****************************************************************************/

static FAR struct nvs_cache *nvs_search_cache(FAR struct nvs_fs *fs,
                                              uint32_t id)
{
#  if defined(CONFIG_MTD_CONFIG_HASH_TABLE)
  return &fs->cache[id % CONFIG_MTD_CONFIG_CACHE_SIZE];
#  elif defined(CONFIG_MTD_CONFIG_FULL_CACHE)
  FAR struct nvs_cache *cache_entry = fs->cache;
  FAR const struct nvs_cache *cache_end =
                             &fs->cache[CONFIG_MTD_CONFIG_CACHE_SIZE];

  for (; cache_entry < cache_end; ++cache_entry)
    {
      if (cache_entry->id == id)
        {
          return cache_entry;
        }
    }

  return NULL;
#  endif
}
#endif

/****************************************************************************
 * Name: nvs_fnv_hash_part
 ****************************************************************************/

static uint32_t nvs_fnv_hash_part(FAR const void *input, size_t len,
                                  uint32_t hval)
{
  FAR const uint8_t *key8 = (FAR const uint8_t *)input;

  /* FNV-1 hash each octet in the buffer */

  while (len-- > 0)
    {
      /* Multiply by the 32 bit FNV magic prime mod 2^32 */

      hval *= 0x01000193;

      /* Xor the bottom with the current octet */

      hval ^= *key8++;
    }

  return hval;
}

/****************************************************************************
 * Name: nvs_fnv_hash
 ****************************************************************************/

static uint32_t nvs_fnv_hash(FAR const void *input, uint32_t len)
{
  return nvs_fnv_hash_part(input, len, NVS_HASH_INITIAL_VALUE);
}

/****************************************************************************
 * Name: nvs_fnv_hash_id
 ****************************************************************************/

static uint32_t nvs_fnv_hash_id(uint32_t hash)
{
  return hash % 0xfffffffd + 1;
}

/****************************************************************************
 * Name: nvs_align_up
 ****************************************************************************/

static inline size_t nvs_align_up(FAR struct nvs_fs *fs, size_t len)
{
  return (len + (fs->progsize - 1)) & ~(fs->progsize - 1);
}

/****************************************************************************
 * Name: nvs_align_down
 ****************************************************************************/

static inline size_t nvs_align_down(FAR struct nvs_fs *fs, size_t len)
{
  return len & ~(fs->progsize - 1);
}

/****************************************************************************
 * Name: nvs_ate_size
 ****************************************************************************/

static inline size_t nvs_ate_size(FAR struct nvs_fs *fs)
{
  return nvs_align_up(fs, sizeof(struct nvs_ate));
}

/****************************************************************************
 * Name: nvs_is_different_block
 ****************************************************************************/

static inline bool nvs_is_different_block(uint64_t addr1, uint64_t addr2)
{
  return addr1 >> NVS_ADDR_BLOCK_SHIFT != addr2 >> NVS_ADDR_BLOCK_SHIFT;
}

/****************************************************************************
 * Name: nvs_close_ate_addr
 ****************************************************************************/

static inline uint64_t nvs_close_ate_addr(FAR struct nvs_fs *fs,
                                          uint64_t addr)
{
  return (addr & NVS_ADDR_BLOCK_MASK) + fs->blocksize - 2 * nvs_ate_size(fs);
}

/****************************************************************************
 * Name: nvs_empty_ate_addr
 ****************************************************************************/

static inline uint64_t nvs_empty_ate_addr(FAR struct nvs_fs *fs,
                                          uint64_t addr)
{
  return (addr & NVS_ADDR_BLOCK_MASK) + fs->blocksize - nvs_ate_size(fs);
}

/****************************************************************************
 * Name: nvs_special_ate_id
 *
 * Description:
 *   Gc done or close ate has the id of 0xffffffff.
 *   We can tell if the ate is special by looking at its id.
 *
 ****************************************************************************/

static inline uint32_t nvs_special_ate_id(FAR struct nvs_fs *fs)
{
  return (fs->erasestate << 24) | (fs->erasestate << 16) |
         (fs->erasestate << 8) | fs->erasestate;
}

/****************************************************************************
 * Name: nvs_flash_wrt
 *
 * Description:
 *   Flash routines, process offset then write.
 *
 ****************************************************************************/

static int nvs_flash_wrt(FAR struct nvs_fs *fs, uint64_t addr,
                         FAR const void *data, size_t len)
{
  off_t offset;
  int rc;

  offset = fs->blocksize * (addr >> NVS_ADDR_BLOCK_SHIFT);
  offset += addr & NVS_ADDR_OFFSET_MASK;

#ifdef CONFIG_MTD_BYTE_WRITE
  rc = MTD_WRITE(fs->mtd, offset, len, data);
#else
  rc = MTD_BWRITE(fs->mtd, offset / fs->progsize, len / fs->progsize, data);
#endif

  return rc < 0 ? rc : 0;
}

/****************************************************************************
 * Name: nvs_flash_brd
 ****************************************************************************/

static int nvs_flash_brd(FAR struct nvs_fs *fs, off_t offset,
                         FAR void *data, size_t len)
{
  int rc;

#ifdef CONFIG_MTD_BYTE_WRITE
  rc = MTD_READ(fs->mtd, offset, len, data);
#else
  rc = MTD_BREAD(fs->mtd, offset / fs->progsize, len / fs->progsize, data);
#endif
  if (rc == -EBADMSG)
    {
      /* ECC fail first time
       * try again to avoid transient electronic interference
       */

#ifdef CONFIG_MTD_BYTE_WRITE
      rc = MTD_READ(fs->mtd, offset, len, data);
#else
      rc = MTD_BREAD(fs->mtd, offset / fs->progsize, len / fs->progsize,
                     data);
#endif
      if (rc == -EBADMSG)
        {
          /* ECC fail second time
           * fill ~erasestate to trigger recovery process
           */

          memset(data, ~fs->erasestate, len);
          rc = 0;
        }
    }

  return rc < 0 ? rc : 0;
}

/****************************************************************************
 * Name: nvs_flash_rd
 *
 * Description:
 *   Basic flash read from nvs address.
 *
 ****************************************************************************/

static int nvs_flash_rd(FAR struct nvs_fs *fs, uint64_t addr,
                        FAR void *data, size_t len)
{
  FAR uint8_t *data8 = (FAR uint8_t *)data;
  off_t offset;

  offset = fs->blocksize * (addr >> NVS_ADDR_BLOCK_SHIFT);
  offset += addr & NVS_ADDR_OFFSET_MASK;

#ifdef CONFIG_MTD_BYTE_WRITE
  return nvs_flash_brd(fs, offset, data8, len);
#else
  uint8_t buf[NVS_BUFFER_SIZE(fs)];
  size_t bytes_to_rd;
  off_t begin_padding;
  int rc;

  begin_padding = offset % fs->progsize;
  if (begin_padding > 0)
    {
      offset -= begin_padding;
      rc = nvs_flash_brd(fs, offset, buf, fs->progsize);
      if (rc < 0)
        {
          return rc;
        }

      bytes_to_rd = MIN(fs->progsize - begin_padding, len);
      memcpy(data8, buf + begin_padding, bytes_to_rd);
      offset += fs->progsize;
      data8 += bytes_to_rd;
      len -= bytes_to_rd;
    }

  if (len >= fs->progsize)
    {
      bytes_to_rd = len / fs->progsize * fs->progsize;
      rc = nvs_flash_brd(fs, offset, data8, bytes_to_rd);
      if (rc < 0)
        {
          return rc;
        }

      offset += bytes_to_rd;
      data8 += bytes_to_rd;
      len -= bytes_to_rd;
    }

  if (len > 0)
    {
      rc = nvs_flash_brd(fs, offset, buf, fs->progsize);
      if (rc < 0)
        {
          return rc;
        }

      memcpy(data8, buf, len);
    }

  return 0;
#endif
}

/****************************************************************************
 * Name: nvs_flash_ate_wrt
 *
 * Description:
 *   Allocation entry write.
 *
 ****************************************************************************/

static inline int nvs_flash_ate_wrt(FAR struct nvs_fs *fs,
                                    FAR const struct nvs_ate *entry)
{
  size_t ate_size = nvs_ate_size(fs);
  int rc;
#if CONFIG_MTD_CONFIG_CACHE_SIZE > 0
  FAR struct nvs_cache *cache_entry;

  /* 0xFFFFFFFF is a special-purpose identifier. Exclude it from the cache */

  if (entry->id != nvs_special_ate_id(fs))
    {
      cache_entry = nvs_lookup_cache(fs, entry->id);
      if (cache_entry != NULL)
        {
          cache_entry->addr = fs->ate_wra;
        }
#  ifdef CONFIG_MTD_CONFIG_FULL_CACHE
      else
        {
          cache_entry = &fs->cache[CONFIG_MTD_CONFIG_CACHE_SIZE - 1];
          if (cache_entry->id == NVS_INVALID_BLOCK)
            {
              cache_entry->addr = fs->ate_wra;
              cache_entry->id = entry->id;
              qsort(fs->cache, CONFIG_MTD_CONFIG_CACHE_SIZE,
                    sizeof(struct nvs_cache), nvs_compare_cache);
            }
          else
            {
              fs->cache_partial = true;
            }
        }
#  endif
    }
#endif

  rc = nvs_flash_wrt(fs, fs->ate_wra, entry, ate_size);
  fs->ate_wra -= ate_size;

  return rc;
}

/****************************************************************************
 * Name: nvs_flash_data_wrt
 ****************************************************************************/

static inline int nvs_flash_data_wrt(FAR struct nvs_fs *fs,
                                     FAR const void *data, size_t len)
{
  int rc;

  rc = nvs_flash_wrt(fs, fs->data_wra, data, len);
  fs->data_wra += len;

  return rc;
}

/****************************************************************************
 * Name: nvs_flash_ate_rd
 ****************************************************************************/

static inline int nvs_flash_ate_rd(FAR struct nvs_fs *fs, uint64_t addr,
                                   FAR struct nvs_ate *entry)
{
  return nvs_flash_rd(fs, addr, entry, nvs_ate_size(fs));
}

/****************************************************************************
 * Name: nvs_flash_block_cmp
 *
 * Description:
 *   Compares the data in flash at addr to data
 *   in blocks of size NVS_BLOCK_SIZE aligned to fs->write_block_size.
 *   Returns 0 if equal, 1 if not equal, errcode if error.
 *
 ****************************************************************************/

static int nvs_flash_block_cmp(FAR struct nvs_fs *fs, uint64_t addr,
                               FAR const void *data, size_t len)
{
  FAR const uint8_t *data8 = (FAR const uint8_t *)data;
  uint8_t buf[NVS_BUFFER_SIZE(fs)];
  size_t buf_size = nvs_align_down(fs, sizeof(buf));
  size_t bytes_to_cmp;
  int rc;

  while (len > 0)
    {
      bytes_to_cmp = MIN(buf_size, len);
      rc = nvs_flash_rd(fs, addr, buf, bytes_to_cmp);
      if (rc)
        {
          return rc;
        }

      rc = memcmp(data8, buf, bytes_to_cmp);
      if (rc)
        {
          return 1;
        }

      len -= bytes_to_cmp;
      addr += bytes_to_cmp;
      data8 += bytes_to_cmp;
    }

  return 0;
}

/****************************************************************************
 * Name: nvs_flash_cmp_direct
 *
 * Description:
 *   Compares the data in flash at addr1 and addr2
 *   of len in blocks of size NVS_BLOCK_SIZE aligned to fs->write_block_size.
 *   Returns 0 if equal, 1 if not equal, errcode if error.
 *
 ****************************************************************************/

static int nvs_flash_cmp_direct(FAR struct nvs_fs *fs, uint64_t addr1,
                                uint64_t addr2, size_t len)
{
  uint8_t buf1[NVS_BUFFER_SIZE(fs)];
  uint8_t buf2[NVS_BUFFER_SIZE(fs)];
  size_t buf_size = nvs_align_down(fs, sizeof(buf1));
  size_t bytes_to_cmp;
  int rc;

  while (len > 0)
    {
      bytes_to_cmp = MIN(buf_size, len);
      rc = nvs_flash_rd(fs, addr1, buf1, bytes_to_cmp);
      if (rc)
        {
          return rc;
        }

      rc = nvs_flash_rd(fs, addr2, buf2, bytes_to_cmp);
      if (rc)
        {
          return rc;
        }

      rc = memcmp(buf1, buf2, bytes_to_cmp);
      if (rc)
        {
          return 1;
        }

      len -= bytes_to_cmp;
      addr1 += bytes_to_cmp;
      addr2 += bytes_to_cmp;
    }

  return 0;
}

/****************************************************************************
 * Name: nvs_flash_cmp_const
 *
 * Description:
 *   Compares the data in flash at addr to a constant
 *   value. returns 0 if all data in flash is equal to value, 1 if not equal,
 *   errcode if error.
 *
 ****************************************************************************/

static int nvs_flash_cmp_const(FAR struct nvs_fs *fs, uint64_t addr,
                               uint8_t value, size_t len)
{
  uint8_t cmp[NVS_BUFFER_SIZE(fs)];
  size_t buf_size = nvs_align_down(fs, sizeof(cmp));
  size_t bytes_to_cmp;
  int rc;

  memset(cmp, value, sizeof(cmp));
  while (len > 0)
    {
      bytes_to_cmp = MIN(buf_size, len);
      rc = nvs_flash_block_cmp(fs, addr, cmp, bytes_to_cmp);
      if (rc)
        {
          return rc;
        }

      len -= bytes_to_cmp;
      addr += bytes_to_cmp;
    }

  return 0;
}

/****************************************************************************
 * Name: nvs_flash_block_move
 *
 * Description:
 *   Move a block at addr to the current data write
 *   location and updates the data write location.
 *
 ****************************************************************************/

static int nvs_flash_block_move(FAR struct nvs_fs *fs,
                                FAR struct nvs_ate *entry, uint64_t addr)
{
  size_t len = nvs_align_up(fs, entry->key_len + entry->len);
  uint64_t data_begin = addr + entry->key_len;
  uint64_t data_end = data_begin + entry->len;
  uint8_t buf[NVS_BUFFER_SIZE(fs)];
  size_t buf_size = nvs_align_down(fs, sizeof(buf));
  uint32_t hash = NVS_HASH_INITIAL_VALUE;
  uint8_t data_crc8 = 0;
  int rc;

  while (len)
    {
      size_t bytes_to_copy = MIN(buf_size, len);
      rc = nvs_flash_rd(fs, addr, buf, bytes_to_copy);
      if (rc)
        {
          return rc;
        }

      if (addr < data_begin)
        {
          hash = nvs_fnv_hash_part(buf, MIN(bytes_to_copy,
                                   data_begin - addr), hash);
        }

      if (addr + bytes_to_copy > data_begin)
        {
          uint64_t end_addr = MIN(data_end, addr + bytes_to_copy);
          uint64_t begin_addr = MAX(data_begin, addr);

          if (nvs_fnv_hash_id(hash) != entry->id)
            {
              return -EBADMSG;
            }

          data_crc8 = crc8part(buf + (begin_addr - addr),
                               end_addr - begin_addr, data_crc8);
        }

      rc = nvs_flash_data_wrt(fs, buf, bytes_to_copy);
      if (rc)
        {
          return rc;
        }

      len -= bytes_to_copy;
      addr += bytes_to_copy;
    }

  return data_crc8 == entry->data_crc8 ? 0 : -EBADMSG;
}

/****************************************************************************
 * Name: nvs_flash_block_erase
 *
 * Description:
 *   Erase a block by first checking it is used and then erasing if required.
 *   Return 0 if OK, errorcode on error.
 *
 ****************************************************************************/

static int nvs_flash_block_erase(FAR struct nvs_fs *fs, uint64_t addr)
{
  int rc;

#if CONFIG_MTD_CONFIG_CACHE_SIZE > 0
  nvs_invalid_cache(fs, addr >> NVS_ADDR_BLOCK_SHIFT);
#endif

  if (fs->mtd->erase == NULL)
    {
      /* RRAM/MRAM that do not require a separate erase operation */

      return 0;
    }

  rc = MTD_ERASE(fs->mtd,
                 CONFIG_MTD_CONFIG_BLOCKSIZE_MULTIPLE *
                 (addr >> NVS_ADDR_BLOCK_SHIFT),
                 CONFIG_MTD_CONFIG_BLOCKSIZE_MULTIPLE);
  if (rc < 0)
    {
      ferr("Erasing failed %d\n", rc);
      return rc;
    }

  return 0;
}

/****************************************************************************
 * Name: nvs_ate_crc8_update
 *
 * Description:
 *   Crc update on allocation entry.
 *
 ****************************************************************************/

static inline void nvs_ate_crc8_update(FAR struct nvs_ate *entry)
{
  entry->crc8 = crc8part((FAR const uint8_t *)entry,
                         offsetof(struct nvs_ate, crc8), 0xff);
}

/****************************************************************************
 * Name: nvs_ate_crc8_check
 *
 * Description:
 *   Crc check on allocation entry.
 *   Returns true if OK, false on crc fail.
 *
 ****************************************************************************/

static inline bool nvs_ate_crc8_check(FAR struct nvs_fs *fs,
                                      FAR const struct nvs_ate *entry)
{
  return entry->crc8 == crc8part((FAR const uint8_t *)entry,
                                 offsetof(struct nvs_ate, crc8), 0xff) &&
         (entry->len > 0 || entry->data_crc8 == 0 ||
          entry->data_crc8 == fs->erasestate);
}

/****************************************************************************
 * Name: nvs_ate_cmp_const
 *
 * Description:
 *   Compares an ATE to a constant value. returns true if
 *   the whole ATE is equal to value, false if not equal.
 *
 ****************************************************************************/

static bool nvs_ate_cmp_const(FAR const struct nvs_ate *entry,
                              uint8_t value, size_t len)
{
  FAR const uint8_t *data8 = (FAR const uint8_t *)entry;
  size_t i;

  for (i = 0; i < len; i++)
    {
      if (data8[i] != value)
        {
          return false;
        }
    }

  return true;
}

/****************************************************************************
 * Name: nvs_ate_valid_different_block
 *
 * Description:
 *   Validates an ate that is in a different block than the active one.
 *   It takes as argument the cycle_cnt of the block where the ATE to
 *   be validated is stored
 *
 * Returned Value:
 *   1 if crc8 and cycle_cnt are valid, 0 otherwise
 *
 ****************************************************************************/

static inline bool
nvs_ate_valid_different_block(FAR struct nvs_fs *fs,
                              FAR const struct nvs_ate *entry,
                              uint8_t cycle_cnt)
{
  return cycle_cnt == entry->cycle_cnt && nvs_ate_crc8_check(fs, entry) &&
         entry->offset < (fs->blocksize - 2 * nvs_ate_size(fs)) &&
         (entry->key_len > 0 || entry->id == nvs_special_ate_id(fs));
}

/****************************************************************************
 * Name: nvs_ate_valid
 *
 * Description:
 *   Return true if crc8 and offset valid, false otherwise
 *
 ****************************************************************************/

static inline bool nvs_ate_valid(FAR struct nvs_fs *fs,
                                 FAR const struct nvs_ate *entry)
{
  return nvs_ate_valid_different_block(fs, entry, fs->cycle_cnt);
}

/****************************************************************************
 * Name: nvs_close_ate_valid
 *
 * Description:
 *   Validates an block close ate:
 *   A valid block close ate:
 *   - Calid ate.
 *   - Len = 0 and id = special_ate_id.
 *   - Offset points to location at ate multiple from block size.
 *   Return true if valid, false otherwise.
 *
 ****************************************************************************/

static inline bool nvs_close_ate_valid(FAR struct nvs_fs *fs,
                                       FAR const struct nvs_ate *entry)
{
  return nvs_ate_valid_different_block(fs, entry, entry->cycle_cnt) &&
         entry->len == 0 && entry->id == nvs_special_ate_id(fs) &&
         (fs->blocksize - entry->offset) % nvs_ate_size(fs) == 0;
}

/****************************************************************************
 * Name: nvs_empty_ate_valid
 ****************************************************************************/

static inline bool nvs_empty_ate_valid(FAR struct nvs_fs *fs,
                                       FAR const struct nvs_ate *entry)
{
  return nvs_ate_valid_different_block(fs, entry, entry->cycle_cnt) &&
         entry->len == 0xffff && entry->id == nvs_special_ate_id(fs);
}

/****************************************************************************
 * Name: nvs_gc_done_ate_valid
 ****************************************************************************/

static inline bool nvs_gc_done_ate_valid(FAR struct nvs_fs *fs,
                                         FAR const struct nvs_ate *entry)
{
  return nvs_ate_valid_different_block(fs, entry, entry->cycle_cnt) &&
         entry->len == 0 && entry->id == nvs_special_ate_id(fs);
}

/****************************************************************************
 * Name: nvs_get_block_cycle
 ****************************************************************************/

static int nvs_get_block_cycle(FAR struct nvs_fs *fs, uint64_t addr,
                               FAR uint8_t *cycle_cnt)
{
  NVS_ATE(empty_ate, nvs_ate_size(fs));
  uint64_t empty_addr;
  int rc;

  empty_addr = nvs_empty_ate_addr(fs, addr);
  rc = nvs_flash_ate_rd(fs, empty_addr, empty_ate);
  if (rc < 0)
    {
      return rc;
    }

  if (nvs_empty_ate_valid(fs, empty_ate))
    {
      *cycle_cnt = empty_ate->cycle_cnt;
      return 0;
    }

  return -ENOENT;
}

/****************************************************************************
 * Name: nvs_get_cycle_on_block_change
 ****************************************************************************/

static inline int nvs_get_cycle_on_block_change(FAR struct nvs_fs *fs,
                                                uint64_t addr,
                                                uint32_t prev_block,
                                                FAR uint8_t *cycle_cnt)
{
  int rc = 0;

  if ((addr >> NVS_ADDR_BLOCK_SHIFT) != prev_block)
    {
      rc = nvs_get_block_cycle(fs, addr, cycle_cnt);
      if (rc == -ENOENT)
        {
          *cycle_cnt = 0;
          rc = 0;
        }
    }

  return rc;
}

/****************************************************************************
 * Name: nvs_flash_write_multi_blk
 *
 * Description:
 *   Store multi align block in flash
 *
 * Returned Value:
 *   number of bytes at the end of addr which is left to write next time.
 *   zero indicates all bytes were written . On error returns -ERRNO code.
 ****************************************************************************/

static int nvs_flash_write_multi_blk(FAR struct nvs_fs *fs,
                                     FAR const void *data, size_t len)
{
  size_t left;
  int rc;

  left = len % fs->progsize;

  if (len > left)
    {
      rc = nvs_flash_data_wrt(fs, data, len - left);
      if (rc)
        {
          ferr("Write multi data value failed, rc=%d\n", rc);
          return rc;
        }
    }

  return left;
}

/****************************************************************************
 * Name: nvs_flash_wrt_entry
 *
 * Description:
 *   Store an entry in flash
 *
 ****************************************************************************/

static int nvs_flash_wrt_entry(FAR struct nvs_fs *fs, uint32_t id,
                               FAR const void *key, size_t key_len,
                               FAR const void *data, size_t len)
{
  FAR const uint8_t *key8 = (FAR const uint8_t *)key;
  FAR const uint8_t *data8 = (FAR const uint8_t *)data;
  size_t ate_size = nvs_ate_size(fs);
  uint8_t buf[NVS_BUFFER_SIZE(fs)];
  NVS_ATE(entry, ate_size);
  size_t copy_len = 0;
  size_t left;
  int rc;

  memset(entry, fs->erasestate, ate_size);
  entry->id = id;
  entry->offset = fs->data_wra & NVS_ADDR_OFFSET_MASK;
  entry->len = len;
  entry->key_len = key_len;
  entry->cycle_cnt = fs->cycle_cnt;
  entry->data_crc8 = crc8(data, len);

  nvs_ate_crc8_update(entry);

  /* Let's save key and data into one, key comes first, then data */

  rc = nvs_flash_write_multi_blk(fs, key8, key_len);
  if (rc < 0)
    {
      return rc;
    }

  if (rc)
    {
      /* Write align block which inlcude part key + part data */

      left = rc;
      memset(buf, fs->erasestate, fs->progsize);

      copy_len = left + len <= fs->progsize ?
                 len : fs->progsize - left;

      memcpy(buf, key8 + key_len - left, left);
      memcpy(buf + left, data8, copy_len);
      rc = nvs_flash_data_wrt(fs, buf, fs->progsize);
      if (rc)
        {
          ferr("Write value failed, rc=%d\n", rc);
          return rc;
        }
    }

  rc = nvs_flash_write_multi_blk(fs, data8 + copy_len, len - copy_len);
  if (rc < 0)
    {
      return rc;
    }

  if (rc)
    {
      /* Add padding at the end of data */

      left = rc;
      memset(buf, fs->erasestate, fs->progsize);
      memcpy(buf, data8 + len - left, left);

      rc = nvs_flash_data_wrt(fs, buf, fs->progsize);
      if (rc)
        {
          ferr("Write value failed, rc=%d\n", rc);
          return rc;
        }
    }

  /* Last, let's save entry to flash */

  rc = nvs_flash_ate_wrt(fs, entry);
  if (rc)
    {
      ferr("Write ate failed, rc=%d\n", rc);
      return rc;
    }

  return 0;
}

/****************************************************************************
 * Name: nvs_recover_last_ate
 *
 * Description:
 *   If the closing ate is invalid, its offset cannot be trusted and
 *   the last valid ate of the block should instead try to be recovered
 *   by going through all ate's.
 *
 *   Addr should point to the faulty closing ate and will be updated to
 *   the last valid ate. If no valid ate is found it will be left untouched.
 *
 ****************************************************************************/

static int nvs_recover_last_ate(FAR struct nvs_fs *fs, FAR uint64_t *addr,
                                FAR uint64_t *data_wra)
{
  size_t ate_size = nvs_ate_size(fs);
  NVS_ATE(end_ate, ate_size);
  uint64_t data_end_addr;
  uint64_t ate_end_addr;
  bool found = false;
  int rc;

  *addr -= ate_size;
  ate_end_addr = *addr;
  data_end_addr = *addr & NVS_ADDR_BLOCK_MASK;
  while (ate_end_addr >= data_end_addr)
    {
      rc = nvs_flash_ate_rd(fs, ate_end_addr, end_ate);
      if (rc)
        {
          return rc;
        }

      if (nvs_ate_valid(fs, end_ate) &&
          end_ate->offset >= (data_end_addr & NVS_ADDR_OFFSET_MASK))
        {
          /* Found a valid ate, update data_end_addr and *addr */

          data_end_addr &= NVS_ADDR_BLOCK_MASK;
          data_end_addr += end_ate->offset +
                           nvs_align_up(fs, end_ate->key_len + end_ate->len);
          *addr = ate_end_addr;
          found = true;
        }

      if (ate_end_addr < ate_size)
        {
          break;
        }

      ate_end_addr -= ate_size;
    }

  if (data_wra != NULL)
    {
      *data_wra = data_end_addr;
    }

  return found;
}

/****************************************************************************
 * Name: nvs_recover_free_ate
 ****************************************************************************/

static int nvs_recover_free_ate(FAR struct nvs_fs *fs, FAR uint64_t *ate_wra,
                                FAR uint64_t *data_wra)
{
  size_t ate_size = nvs_ate_size(fs);
  NVS_ATE(end_ate, ate_size);
  size_t empty_len;
  int rc;

  rc = nvs_recover_last_ate(fs, ate_wra, data_wra);
  if (rc < 0)
    {
      return rc;
    }
  else if (rc)
    {
      *ate_wra -= ate_size;
    }

  if (fs->mtd->erase == NULL)
    {
      return 0;
    }

  while (*ate_wra >= *data_wra)
    {
      rc = nvs_flash_ate_rd(fs, *ate_wra, end_ate);
      if (rc)
        {
          return rc;
        }

      if (nvs_ate_cmp_const(end_ate, fs->erasestate, ate_size))
        {
          break;
        }

      *ate_wra -= ate_size;
    }

  /* Possible data write after last ate write, update data_wra */

  while (*ate_wra >= *data_wra)
    {
      empty_len = *ate_wra - *data_wra;
      rc = nvs_flash_cmp_const(fs, *data_wra, fs->erasestate, empty_len);
      if (rc <= 0)
        {
          return rc;
        }

      *data_wra += fs->progsize;
    }

  return 0;
}

/****************************************************************************
 * Name: nvs_get_block_header
 ****************************************************************************/

static int nvs_get_block_header(FAR struct nvs_fs *fs, uint64_t addr,
                                FAR struct nvs_ate *empty_ate,
                                FAR struct nvs_ate *close_ate)
{
  uint64_t close_addr = nvs_close_ate_addr(fs, addr);
  int rc;

  /* Read the second ate in the block to get the close ATE */

  rc = nvs_flash_ate_rd(fs, close_addr, close_ate);
  if (rc)
    {
      return rc;
    }

  /* Read the first ate in the block to get the empty ATE */

  rc = nvs_flash_ate_rd(fs, close_addr + nvs_ate_size(fs), empty_ate);
  if (rc)
    {
      return rc;
    }

  return 0;
}

/****************************************************************************
 * Name: nvs_validate_closed_block
 ****************************************************************************/

static int nvs_validate_closed_block(FAR struct nvs_fs *fs, uint64_t addr,
                                     FAR struct nvs_ate *empty_ate,
                                     FAR struct nvs_ate *close_ate)
{
  int rc;

  /* Read the header ATEs */

  rc = nvs_get_block_header(fs, addr, empty_ate, close_ate);
  if (rc)
    {
      return rc;
    }

  if (nvs_close_ate_valid(fs, close_ate) &&
      nvs_empty_ate_valid(fs, empty_ate) &&
      empty_ate->cycle_cnt == close_ate->cycle_cnt)
    {
      return 1;
    }
  else if (fs->mtd->erase != NULL &&
           !nvs_ate_cmp_const(close_ate, fs->erasestate, nvs_ate_size(fs)))
    {
      /* The situation which close_ate is not completed in Norflash is
       * considered as a closed state.
       */

      uint64_t last_addr = nvs_close_ate_addr(fs, addr);
      fs->cycle_cnt = empty_ate->cycle_cnt;
      rc = nvs_recover_last_ate(fs, &last_addr, NULL);
      if (rc < 0)
        {
          return rc;
        }

      close_ate->offset = last_addr & NVS_ADDR_OFFSET_MASK;
      return 1;
    }

  return 0;
}

/****************************************************************************
 * Name: nvs_prev_ate
 *
 * Description:
 *   Walking through allocation entry list, from newest to oldest entries.
 *   Read ate from addr, modify addr to the previous ate.
 *
 ****************************************************************************/

static int nvs_prev_ate(FAR struct nvs_fs *fs, FAR uint64_t *addr,
                        FAR struct nvs_ate *ate)
{
  size_t ate_size = nvs_ate_size(fs);
  NVS_ATE(close_ate, ate_size);
  NVS_ATE(empty_ate, ate_size);
  int rc;

  rc = nvs_flash_ate_rd(fs, *addr, ate);
  if (rc)
    {
      return rc;
    }

  *addr += ate_size;
  if (((*addr) & NVS_ADDR_OFFSET_MASK) < fs->blocksize - 2 * ate_size)
    {
      return 0;
    }

  /* Last ate in block, do jump to previous block */

  if (((*addr) >> NVS_ADDR_BLOCK_SHIFT) == 0)
    {
      *addr += (uint64_t)(fs->nblocks - 1) << NVS_ADDR_BLOCK_SHIFT;
    }
  else
    {
      *addr -= (uint64_t)1 << NVS_ADDR_BLOCK_SHIFT;
    }

  rc = nvs_validate_closed_block(fs, *addr, empty_ate, close_ate);
  if (rc < 0)
    {
      return rc;
    }

  /* At the end of filesystem */

  if (!rc)
    {
      *addr = fs->ate_wra;
      return 0;
    }

  /* Update the address if the close ate is valid. */

  *addr &= NVS_ADDR_BLOCK_MASK;
  *addr += close_ate->offset;
  return 0;
}

/****************************************************************************
 * Name: nvs_block_advance
 ****************************************************************************/

static inline void nvs_block_advance(FAR struct nvs_fs *fs,
                                     FAR uint64_t *addr)
{
  *addr += (uint64_t)1 << NVS_ADDR_BLOCK_SHIFT;
  if ((*addr >> NVS_ADDR_BLOCK_SHIFT) == fs->nblocks)
    {
      *addr -= (uint64_t)fs->nblocks << NVS_ADDR_BLOCK_SHIFT;
    }
}

/****************************************************************************
 * Name: nvs_block_close
 *
 * Description:
 *   Allocation entry close (this closes the current block) by writing
 *   offset of last ate to the block end.
 *
 ****************************************************************************/

static int nvs_block_close(FAR struct nvs_fs *fs)
{
  size_t ate_size = nvs_ate_size(fs);
  NVS_ATE(garbage_ate, ate_size);
  NVS_ATE(close_ate, ate_size);
  int rc;

  memset(close_ate, fs->erasestate, ate_size);
  close_ate->id = nvs_special_ate_id(fs);
  close_ate->len = 0;
  close_ate->key_len = 0;
  close_ate->offset = (fs->ate_wra + ate_size) &
                      NVS_ADDR_OFFSET_MASK;
  close_ate->cycle_cnt = fs->cycle_cnt;
  nvs_ate_crc8_update(close_ate);

  memset(garbage_ate, fs->erasestate, ate_size);
  while (fs->ate_wra >= fs->data_wra)
    {
      rc = nvs_flash_ate_wrt(fs, garbage_ate);
      if (rc)
        {
          return rc;
        }
    }

  fs->ate_wra = nvs_close_ate_addr(fs, fs->ate_wra);
  rc = nvs_flash_ate_wrt(fs, close_ate);
  if (rc < 0)
    {
      ferr("Write ate failed, rc=%d\n", rc);
    }

  nvs_block_advance(fs, &fs->ate_wra);
  fs->data_wra = fs->ate_wra & NVS_ADDR_BLOCK_MASK;

  rc = nvs_get_block_cycle(fs, fs->ate_wra, &fs->cycle_cnt);
  if (rc == -ENOENT)
    {
      fs->cycle_cnt = 0;
      rc = 0;
    }

  return 0;
}

/****************************************************************************
 * Name: nvs_add_gc_done_ate
 ****************************************************************************/

static int nvs_add_gc_done_ate(FAR struct nvs_fs *fs)
{
  size_t ate_size = nvs_ate_size(fs);
  NVS_ATE(gc_done_ate, ate_size);

  memset(gc_done_ate, fs->erasestate, ate_size);
  gc_done_ate->id = nvs_special_ate_id(fs);
  gc_done_ate->len = 0;
  gc_done_ate->key_len = 0;
  gc_done_ate->offset = fs->data_wra & NVS_ADDR_OFFSET_MASK;
  gc_done_ate->cycle_cnt = fs->cycle_cnt;
  nvs_ate_crc8_update(gc_done_ate);

  return nvs_flash_ate_wrt(fs, gc_done_ate);
}

/****************************************************************************
 * Name: nvs_verify_and_increment_cycle_cnt
 ****************************************************************************/

static int nvs_verify_and_increment_cycle_cnt(FAR struct nvs_fs *fs,
                                              uint64_t addr,
                                              FAR uint8_t *cycle_cnt)
{
  uint64_t close_addr = nvs_close_ate_addr(fs, addr);
  NVS_ATE(close_ate, nvs_ate_size(fs));
  int rc;

  rc = nvs_flash_ate_rd(fs, close_addr, close_ate);
  if (rc < 0)
    {
      return rc;
    }

  *cycle_cnt = *cycle_cnt + 1;

  /* Verify that the close cycle cnt is not equal to the incremented value.
   * If they are equal, increment it again.
   */

  if (close_ate->cycle_cnt == *cycle_cnt)
    {
      *cycle_cnt = *cycle_cnt + 1;
    }

  return 0;
}

/****************************************************************************
 * Name: nvs_add_empty_ate
 ****************************************************************************/

static int nvs_add_empty_ate(FAR struct nvs_fs *fs, uint64_t addr)
{
  size_t ate_size = nvs_ate_size(fs);
  NVS_ATE(empty_ate, ate_size);
  uint64_t prev_ate_wra;
  uint8_t cycle_cnt;
  int rc;

  memset(empty_ate, fs->erasestate, ate_size);
  empty_ate->id = nvs_special_ate_id(fs);
  empty_ate->len = 0xffff;
  empty_ate->key_len = 0;
  empty_ate->offset = 0;

  rc = nvs_get_block_cycle(fs, addr, &cycle_cnt);
  if (rc == -ENOENT)
    {
      cycle_cnt = 0;
    }
  else if (rc)
    {
      ferr("get empty ATE block cycle failed, rc=%d\n", rc);
      return rc;
    }

  rc = nvs_verify_and_increment_cycle_cnt(fs, addr, &cycle_cnt);
  if (rc < 0)
    {
      ferr("verify and increment empty ATE cycle cnt failed, "
           "rc=%d\n", rc);
      return rc;
    }

  empty_ate->cycle_cnt = cycle_cnt;
  nvs_ate_crc8_update(empty_ate);

  prev_ate_wra = fs->ate_wra;
  fs->ate_wra = nvs_empty_ate_addr(fs, addr);
  rc = nvs_flash_ate_wrt(fs, empty_ate);
  fs->ate_wra = prev_ate_wra;

  if (rc)
    {
      ferr("add empty ATE failed, rc=%d\n", rc);
      return rc;
    }

  return 0;
}

/****************************************************************************
 * Name: nvs_rebuild_cache
 ****************************************************************************/

#if CONFIG_MTD_CONFIG_CACHE_SIZE > 0
static int nvs_rebuild_cache(FAR struct nvs_fs *fs)
{
  uint32_t prev_block = NVS_INVALID_BLOCK;
  FAR struct nvs_cache *cache_entry;
  NVS_ATE(ate, nvs_ate_size(fs));
  uint64_t addr = fs->ate_wra;
  uint32_t block_count = 1;
  uint8_t cycle_cnt = 0;
  uint32_t count = 0;
  uint64_t ate_addr;
  int rc;

  memset(fs->cache, 0xff, sizeof(fs->cache));

  do
    {
      ate_addr = addr;
      rc = nvs_prev_ate(fs, &addr, ate);
      if (rc)
        {
          return rc;
        }

      if (nvs_is_different_block(addr, ate_addr))
        {
          if (++block_count > fs->nblocks)
            {
              return -ERANGE;
            }
        }

      if (ate->id != nvs_special_ate_id(fs) &&
          (!(cache_entry = nvs_search_cache(fs, ate->id)) ||
           cache_entry->addr == NVS_CACHE_NO_ADDR))
        {
          rc = nvs_get_cycle_on_block_change(fs, ate_addr, prev_block,
                                             &cycle_cnt);
          if (rc)
            {
              return rc;
            }

          if (nvs_ate_valid_different_block(fs, ate, cycle_cnt))
            {
#  ifdef CONFIG_MTD_CONFIG_FULL_CACHE
              cache_entry = &fs->cache[count];
              if (count++ == CONFIG_MTD_CONFIG_CACHE_SIZE)
                {
                  fs->cache_partial = true;
                  break;
                }

              cache_entry->id = ate->id;
#  endif
              cache_entry->addr = ate_addr;
#  ifdef CONFIG_MTD_CONFIG_HASH_TABLE
              if (++count == CONFIG_MTD_CONFIG_CACHE_SIZE)
                {
                  break;
                }
#  endif
            }

          prev_block = ate_addr >> NVS_ADDR_BLOCK_SHIFT;
        }
    }
  while (addr != fs->ate_wra);

#  ifdef CONFIG_MTD_CONFIG_FULL_CACHE
  qsort(fs->cache, CONFIG_MTD_CONFIG_CACHE_SIZE, sizeof(struct nvs_cache),
        nvs_compare_cache);
#  endif
  return 0;
}
#endif /* CONFIG_MTD_CONFIG_CACHE_SIZE */

/****************************************************************************
 * Name: nvs_find_ate
 ****************************************************************************/

static int nvs_find_ate(FAR struct nvs_fs *fs,
                        FAR const struct nvs_ate *entry, uint64_t entry_addr,
                        uint64_t start_addr, uint64_t end_addr,
                        FAR struct nvs_ate *ate, FAR uint64_t *ate_addr)
{
  uint32_t prev_block = NVS_INVALID_BLOCK;
  uint32_t block_count = 1;
  bool prev_found = false;
  uint8_t cycle_cnt = 0;
  uint64_t prev_addr;
  int rc;

  do
    {
      prev_addr = start_addr;
      rc = nvs_prev_ate(fs, &start_addr, ate);
      if (rc)
        {
          ferr("Walk to previous ate failed, rc=%d\n", rc);
          return rc;
        }

      if (nvs_is_different_block(start_addr, prev_addr))
        {
          if (++block_count > fs->nblocks)
            {
              return -ERANGE;
            }
        }

      if (ate->id == entry->id && ate->key_len == entry->key_len)
        {
          /* Read the ate cycle only when we change the block or if it is
           * the first read (prev_block == NVS_INVALID_BLOCK).
           */

          rc = nvs_get_cycle_on_block_change(fs, prev_addr, prev_block,
                                             &cycle_cnt);
          if (rc)
            {
              return rc;
            }

          if (nvs_ate_valid_different_block(fs, ate, cycle_cnt) &&
              !nvs_flash_cmp_direct(fs, (prev_addr & NVS_ADDR_BLOCK_MASK) +
                          ate->offset, (entry_addr & NVS_ADDR_BLOCK_MASK) +
                                            entry->offset, entry->key_len))
            {
              prev_found = true;
              break;
            }

          prev_block = prev_addr >> NVS_ADDR_BLOCK_SHIFT;
        }
    }
  while (start_addr != end_addr && start_addr != fs->ate_wra);

  *ate_addr = prev_addr;
  return prev_found;
}

/****************************************************************************
 * Name: nvs_find_ate_with_key
 ****************************************************************************/

static int nvs_find_ate_with_key(FAR struct nvs_fs *fs,
                                 FAR const void *key, size_t key_len,
                                 uint64_t start_addr, uint64_t end_addr,
                                 FAR struct nvs_ate *ate,
                                 FAR uint64_t *ate_addr)
{
  uint32_t hash_id = nvs_fnv_hash_id(nvs_fnv_hash(key, key_len));
  uint32_t prev_block = NVS_INVALID_BLOCK;
  uint32_t block_count = 1;
  bool prev_found = false;
  uint8_t cycle_cnt = 0;
  uint64_t prev_addr;
  int rc;

  do
    {
      prev_addr = start_addr;
      rc = nvs_prev_ate(fs, &start_addr, ate);
      if (rc)
        {
          ferr("Walk to previous ate failed, rc=%d\n", rc);
          return rc;
        }

      if (nvs_is_different_block(start_addr, prev_addr))
        {
          if (++block_count > fs->nblocks)
            {
              return -ERANGE;
            }
        }

      if (ate->id == hash_id && ate->key_len == key_len)
        {
          /* Read the ate cycle only when we change the block or if it is
           * the first read (prev_block == NVS_INVALID_BLOCK).
           */

          rc = nvs_get_cycle_on_block_change(fs, prev_addr, prev_block,
                                             &cycle_cnt);
          if (rc)
            {
              return rc;
            }

          if (nvs_ate_valid_different_block(fs, ate, cycle_cnt) &&
              !nvs_flash_block_cmp(fs, (prev_addr & NVS_ADDR_BLOCK_MASK) +
                                   ate->offset, key, key_len))
            {
              prev_found = true;
              break;
            }

          prev_block = prev_addr >> NVS_ADDR_BLOCK_SHIFT;
        }
    }
  while (start_addr != end_addr && start_addr != fs->ate_wra);

  *ate_addr = prev_addr;
  return prev_found;
}

/****************************************************************************
 * Name: nvs_gc
 *
 * Description:
 *   Garbage collection: the address ate_wra has been updated to the new
 *   block that has just been started. The data to gc is in the block
 *   after this new block.
 *
 ****************************************************************************/

static int nvs_gc(FAR struct nvs_fs *fs)
{
  size_t ate_size = nvs_ate_size(fs);
  NVS_ATE(close_ate, ate_size);
  NVS_ATE(empty_ate, ate_size);
  NVS_ATE(wlk_ate, ate_size);
  NVS_ATE(gc_ate, ate_size);
  uint8_t prev_cycle;
  uint64_t gc_prev_addr;
  uint64_t data_addr;
  uint64_t stop_addr;
  uint64_t sec_addr;
  uint64_t wlk_addr;
  uint64_t gc_addr;
  int rc;

  rc = nvs_get_block_cycle(fs, fs->ate_wra, &fs->cycle_cnt);
  if (rc == -ENOENT)
    {
      rc = nvs_flash_block_erase(fs, fs->ate_wra);
      if (rc)
        {
          return rc;
        }

      /* Block never used */

      rc = nvs_add_empty_ate(fs, fs->ate_wra);
      if (rc)
        {
          return rc;
        }

      /* Make sure that empty ATE exist */

      rc = nvs_get_block_cycle(fs, fs->ate_wra, &fs->cycle_cnt);
      if (rc)
        {
          return rc;
        }
    }
  else if (rc)
    {
      return rc;
    }

  prev_cycle = fs->cycle_cnt;
  sec_addr = (fs->ate_wra & NVS_ADDR_BLOCK_MASK);
  nvs_block_advance(fs, &sec_addr);
  gc_addr = sec_addr + fs->blocksize - 2 * ate_size;

  rc = nvs_validate_closed_block(fs, gc_addr, empty_ate, close_ate);
  if (rc < 0)
    {
      return rc;
    }

  /* If the block is not closed don't do gc */

  if (!rc)
    {
      goto gc_done;
    }

  fs->cycle_cnt = empty_ate->cycle_cnt;

  stop_addr = gc_addr - ate_size;
  gc_addr &= NVS_ADDR_BLOCK_MASK;
  gc_addr += close_ate->offset;

  do
    {
      gc_prev_addr = gc_addr;
      if (nvs_is_different_block(gc_prev_addr, stop_addr))
        {
          return -ERANGE;
        }

      rc = nvs_prev_ate(fs, &gc_addr, gc_ate);
      if (rc)
        {
          return rc;
        }

      if (!nvs_ate_valid(fs, gc_ate) || !gc_ate->len ||
          gc_ate->id == nvs_special_ate_id(fs))
        {
          continue;
        }

#if CONFIG_MTD_CONFIG_CACHE_SIZE > 0
      wlk_addr = nvs_lookup_addr(fs, gc_ate->id);
      if (wlk_addr == NVS_CACHE_NO_ADDR)
#endif
        {
          wlk_addr = fs->ate_wra;
        }

      /* Search for a previous valid ATE with the same ID. If it doesn't
       * exist, then wlk_addr will be equal to gc_prev_addr.
       */

      rc = nvs_find_ate(fs, gc_ate, gc_prev_addr, wlk_addr, gc_prev_addr,
                        wlk_ate, &wlk_addr);
      if (rc < 0)
        {
          return rc;
        }
      else if (rc == 0 || gc_prev_addr == wlk_addr)
        {
          data_addr = gc_prev_addr & NVS_ADDR_BLOCK_MASK;
          data_addr += gc_ate->offset;
          gc_ate->offset = fs->data_wra & NVS_ADDR_OFFSET_MASK;
          rc = nvs_flash_block_move(fs, gc_ate, data_addr);
          if (rc == -EBADMSG)
            {
              continue;
            }
          else if (rc)
            {
              return rc;
            }

          gc_ate->cycle_cnt = prev_cycle;
          nvs_ate_crc8_update(gc_ate);
          rc = nvs_flash_ate_wrt(fs, gc_ate);
          if (rc)
            {
              return rc;
            }
        }
    }
  while (gc_prev_addr != stop_addr);

gc_done:

  /* Restore the previous block_cycle */

  fs->cycle_cnt = prev_cycle;
  rc = nvs_add_gc_done_ate(fs);
  if (rc)
    {
      return rc;
    }

  /* Erase the gc'ed block */

  rc = nvs_flash_block_erase(fs, sec_addr);
  if (rc)
    {
      return rc;
    }

  return nvs_add_empty_ate(fs, sec_addr);
}

/****************************************************************************
 * Name: nvs_clear
 ****************************************************************************/

static int nvs_clear(FAR struct nvs_fs *fs)
{
  uint64_t addr;
  uint32_t i;
  int rc;

  if (fs->mtd->erase != NULL)
    {
      rc = MTD_IOCTL(fs->mtd, MTDIOC_BULKERASE, 0);
      if (rc < 0)
        {
          return rc;
        }
    }
  else
    {
      for (i = 0; i < fs->nblocks; i++)
        {
          addr = nvs_empty_ate_addr(fs, (uint64_t)i << NVS_ADDR_BLOCK_SHIFT);
          rc = nvs_add_empty_ate(fs, addr);
          if (rc)
            {
              return rc;
            }
        }
    }

#if CONFIG_MTD_CONFIG_CACHE_SIZE > 0
  memset(fs->cache, 0xff, sizeof(fs->cache));
#endif

  return 0;
}

/****************************************************************************
 * Name: nvs_mount
 ****************************************************************************/

static int nvs_mount(FAR struct nvs_fs *fs)
{
  struct mtd_geometry_s geo;
  size_t ate_size;
  int rc;

  /* Get the device geometry. (Casting to uintptr_t first eliminates
   * complaints on some architectures where the sizeof long is different
   * from the size of a pointer).
   */

  rc = MTD_IOCTL(fs->mtd, MTDIOC_GEOMETRY, (unsigned long)(uintptr_t)&geo);
  if (rc < 0)
    {
      ferr("ERROR: MTD ioctl(MTDIOC_GEOMETRY) failed: %d\n", rc);
      return rc;
    }

  fs->blocksize = CONFIG_MTD_CONFIG_BLOCKSIZE_MULTIPLE * geo.erasesize;
  fs->nblocks   = geo.neraseblocks / CONFIG_MTD_CONFIG_BLOCKSIZE_MULTIPLE;
  fs->progsize  = geo.blocksize;

  ate_size = nvs_ate_size(fs);
#if CONFIG_MTD_CONFIG_BUFFER_SIZE > 0
  DEBUGASSERT(ate_size <= CONFIG_MTD_CONFIG_BUFFER_SIZE);
#endif

  rc = MTD_IOCTL(fs->mtd, MTDIOC_ERASESTATE,
                 (unsigned long)((uintptr_t)&fs->erasestate));
  if (rc < 0)
    {
      ferr("ERROR: MTD ioctl(MTDIOC_ERASESTATE) failed: %d\n", rc);
      return rc;
    }

  /* Check the number of blocks, it should be at least 2. */

  if (fs->nblocks < 2)
    {
      ferr("Configuration error - block count\n");
      return -EINVAL;
    }

  /* We need at least 5 aligned ATEs size as the minimum sector size
   * 1 close ATE, 1 empty ATE, 1 GC done ATE, 1 Delete ATE, 1 ID/Value ATE
   */

  if (fs->blocksize < 5 * ate_size)
    {
      return -EINVAL;
    }

  return 0;
}

/****************************************************************************
 * Name: nvs_init
 ****************************************************************************/

static int nvs_init(FAR struct nvs_fs *fs)
{
  size_t ate_size = nvs_ate_size(fs);
  uint32_t closed_blocks = 0;
  uint64_t data_wra = 0;
  uint64_t addr = 0;
  uint32_t i;
  int rc;

  NVS_ATE(close_ate, ate_size);
  NVS_ATE(empty_ate, ate_size);
  NVS_ATE(first_ate, ate_size);

  /* Step through the blocks to find a open block following
   * a closed block, this is where NVS can write.
   */

  for (i = 0; i < fs->nblocks; i++)
    {
      addr = nvs_close_ate_addr(fs, (uint64_t)i << NVS_ADDR_BLOCK_SHIFT);
      rc = nvs_validate_closed_block(fs, addr, empty_ate, close_ate);
      if (rc < 0)
        {
          return rc;
        }

      fs->cycle_cnt = empty_ate->cycle_cnt;
      if (rc == 1)
        {
          /* Closed block */

          closed_blocks++;
          nvs_block_advance(fs, &addr);
          rc = nvs_validate_closed_block(fs, addr, empty_ate, close_ate);
          if (rc < 0)
            {
              return rc;
            }

          /* Update block cycle */

          fs->cycle_cnt = empty_ate->cycle_cnt;
          if (!rc)
            {
              /* Open block */

              break;
            }
        }
    }

  /* All blocks are closed, this is not a nvs fs */

  if (closed_blocks == fs->nblocks)
    {
      return -ENOTSUP;
    }

  if (i == fs->nblocks)
    {
      /* None of the blocks were closed, which means that the first
       * block is the one in use, except if there are only 2 blocks.
       * Let's check if the last block has valid ATEs otherwise set
       * the open block to the first one.
       */

      rc = nvs_flash_ate_rd(fs, addr - ate_size, first_ate);
      if (rc)
        {
          return rc;
        }

      if (!nvs_ate_valid(fs, first_ate))
        {
          /* Empty ate */

          nvs_block_advance(fs, &addr);
        }

      rc = nvs_get_block_header(fs, addr, empty_ate, close_ate);
      if (rc)
        {
          return rc;
        }

      if (!nvs_empty_ate_valid(fs, empty_ate))
        {
          rc = nvs_flash_block_erase(fs, addr);
          if (rc)
            {
              return rc;
            }

          rc = nvs_add_empty_ate(fs, addr);
          if (rc)
            {
              return rc;
            }
        }

      rc = nvs_get_block_cycle(fs, addr, &fs->cycle_cnt);
      if (rc == -ENOENT)
        {
          /* Block never used */

          fs->cycle_cnt = 0;
        }
      else if (rc)
        {
          /* Bad flash read */

          return rc;
        }
    }

  /* Addr contains address of closing ate in the most recent block,
   * search for the last free ate using the recover_free_ate routine
   */

  rc = nvs_recover_free_ate(fs, &addr, &data_wra);
  if (rc)
    {
      return rc;
    }

  /* fs->ate_wra should point to the next available entry. This is normally
   * the next position after the one found by the recovery function.
   * Let's verify that it doesn't contain any valid ATE, otherwise search for
   * an empty position.
   */

  fs->ate_wra = addr;
  fs->data_wra = data_wra;

  /* The block after the write block is either empty with a valid empty ATE
   * (regular case) or it has never been used or it is a closed block (GC
   * didn't finish) If it is a closed block we must look for a valid GC done
   * ATE in the current write block, if it is missing, we need to restart gc
   * because it has been interrupted. If no valid empty ATE is found then it
   * has never been used. Just erase it by adding a valid empty ATE.
   * When gc needs to be restarted, first erase the block by adding an empty
   * ATE otherwise the data might not fit into the block.
   */

  addr = nvs_close_ate_addr(fs, fs->ate_wra);
  nvs_block_advance(fs, &addr);
  rc = nvs_validate_closed_block(fs, addr, empty_ate, close_ate);
  if (rc < 0)
    {
      return rc;
    }

  if (rc)
    {
      /* The block after fs->ate_wrt is not empty, look for a marker
       * (gc_done_ate) that indicates that gc was finished.
       */

      bool gc_done_marker = false;
      NVS_ATE(gc_done_ate, ate_size);

      addr = fs->ate_wra + ate_size;
      while ((addr & NVS_ADDR_OFFSET_MASK) < (fs->blocksize - 2 * ate_size))
        {
          rc = nvs_flash_ate_rd(fs, addr, gc_done_ate);
          if (rc)
            {
              return rc;
            }

          if (nvs_gc_done_ate_valid(fs, gc_done_ate) &&
              (gc_done_ate->cycle_cnt == fs->cycle_cnt))
            {
              gc_done_marker = true;
              break;
            }

          addr += ate_size;
        }

      if (gc_done_marker)
        {
          /* Erase the next block */

          fwarn("GC Done marker found\n");
          addr = fs->ate_wra & NVS_ADDR_BLOCK_MASK;
          nvs_block_advance(fs, &addr);
          rc = nvs_flash_block_erase(fs, addr);
          if (rc)
            {
              return rc;
            }

          rc = nvs_add_empty_ate(fs, addr);
          if (rc)
            {
              return rc;
            }
        }
      else
        {
          fwarn("No GC Done marker found: restarting gc\n");
          rc = nvs_flash_block_erase(fs, fs->ate_wra);
          if (rc)
            {
              return rc;
            }

          rc = nvs_add_empty_ate(fs, fs->ate_wra);
          if (rc)
            {
              return rc;
            }

          fs->ate_wra &= NVS_ADDR_BLOCK_MASK;
          fs->ate_wra += (fs->blocksize - 3 * ate_size);
          fs->data_wra = (fs->ate_wra & NVS_ADDR_BLOCK_MASK);
#if CONFIG_MTD_CONFIG_CACHE_SIZE > 0
          memset(fs->cache, 0xff, sizeof(fs->cache));
#endif

          rc = nvs_gc(fs);
          if (rc)
            {
              return rc;
            }
        }
    }

#if CONFIG_MTD_CONFIG_CACHE_SIZE > 0
  rc = nvs_rebuild_cache(fs);
  if (rc)
    {
      return rc;
    }

#endif
  /* If the block is empty, add a gc done ate to avoid having insufficient
   * space when doing gc.
   */

  if ((fs->ate_wra & NVS_ADDR_OFFSET_MASK) == fs->blocksize - 3 * ate_size)
    {
      rc = nvs_add_gc_done_ate(fs);
    }

  finfo("%" PRIu32 " Eraseblocks of %" PRIu32 " bytes\n",
        fs->nblocks, fs->blocksize);
  finfo("alloc wra: %" PRIu64 ", 0x%" PRIx64 "\n",
        fs->ate_wra >> NVS_ADDR_BLOCK_SHIFT,
        fs->ate_wra & NVS_ADDR_OFFSET_MASK);
  finfo("data wra: %" PRIu64 ", 0x%" PRIx64 "\n",
        fs->data_wra >> NVS_ADDR_BLOCK_SHIFT,
        fs->data_wra & NVS_ADDR_OFFSET_MASK);

  return rc;
}

/****************************************************************************
 * Name: nvs_read
 *
 * Description:
 *   Read an entry from the file system.
 *
 * Input Parameters:
 *   fs    - Pointer to file system.
 *   pdata - Pointer to data buffer.
 *
 * Returned Value:
 *   0 on success, -ERRNO errno code if error.
 *
 ****************************************************************************/

static int nvs_read(FAR struct nvs_fs *fs, FAR struct config_data_s *pdata)
{
  NVS_ATE(wlk_ate, nvs_ate_size(fs));
  uint64_t wlk_addr = fs->ate_wra;
  uint8_t data_crc8;
  int rc;

#ifdef CONFIG_MTD_CONFIG_NAMED
  FAR const uint8_t *key = (FAR const uint8_t *)pdata->name;
  size_t key_len = strlen(pdata->name) + 1;
#else
  uint8_t key[sizeof(pdata->id) + sizeof(pdata->instance)];
  memcpy(key, &pdata->id, sizeof(pdata->id));
  memcpy(key + sizeof(pdata->id), &pdata->instance, sizeof(pdata->instance));
  size_t key_len = sizeof(pdata->id) + sizeof(pdata->instance);
#endif

#if CONFIG_MTD_CONFIG_CACHE_SIZE > 0
  uint32_t hash_id = nvs_fnv_hash_id(nvs_fnv_hash(key, key_len));
  wlk_addr = nvs_lookup_addr(fs, hash_id);
  if (wlk_addr == NVS_CACHE_NO_ADDR)
    {
      return -ENOENT;
    }
#endif

  rc = nvs_find_ate_with_key(fs, key, key_len, wlk_addr, fs->ate_wra,
                             wlk_ate, &wlk_addr);
  if (rc < 0)
    {
      return rc;
    }

  if (!rc || wlk_ate->len == 0)
    {
      return -ENOENT;
    }

  if (pdata->configdata && pdata->len)
    {
      wlk_addr &= NVS_ADDR_BLOCK_MASK;
      wlk_addr += wlk_ate->offset + wlk_ate->key_len;
      rc = nvs_flash_rd(fs, wlk_addr, pdata->configdata,
                        MIN(pdata->len, wlk_ate->len));
      if (rc)
        {
          ferr("Data read failed, rc=%d\n", rc);
          return rc;
        }

      if (pdata->len >= wlk_ate->len && wlk_ate->data_crc8 != fs->erasestate)
        {
          /* Do not compute CRC for partial reads as CRC won't match */

          data_crc8 = crc8(pdata->configdata, wlk_ate->len);
          if (wlk_ate->data_crc8 != data_crc8)
            {
              ferr("Invalid data crc: %" PRIx8 ", wlk_ate->data_crc8: "
                    "%" PRIx8 "\n", data_crc8, wlk_ate->data_crc8);
              return -EIO;
            }
        }
    }

  pdata->len = wlk_ate->len;
  return 0;
}

/****************************************************************************
 * Name: nvs_write
 *
 * Description:
 *   Write an entry to the file system.
 *
 * Input Parameters:
 *   fs    - Pointer to file system.
 *   pdata - Pointer to data buffer.
 *
 * Returned Value:
 *   Number of bytes written. On success, it will be equal to the
 *   number of bytes requested to be written. On error returns -ERRNO code.
 *
 ****************************************************************************/

static int nvs_write(FAR struct nvs_fs *fs, FAR struct config_data_s *pdata)
{
  size_t ate_size = nvs_ate_size(fs);
  uint64_t wlk_addr = fs->ate_wra;
  NVS_ATE(wlk_ate, ate_size);
  uint32_t required_space;
  uint32_t gc_count;
  uint32_t hash_id;
  int rc = 0;

#ifdef CONFIG_MTD_CONFIG_NAMED
  FAR const uint8_t *key = (FAR const uint8_t *)pdata->name;
  size_t key_len = strlen(pdata->name) + 1;
#else
  uint8_t key[sizeof(pdata->id) + sizeof(pdata->instance)];
  memcpy(key, &pdata->id, sizeof(pdata->id));
  memcpy(key + sizeof(pdata->id), &pdata->instance, sizeof(pdata->instance));
  size_t key_len = sizeof(pdata->id) + sizeof(pdata->instance);
#endif

  /* Data now contains input data and input key, input key first. */

  required_space = nvs_align_up(fs, key_len + pdata->len);

  /* The maximum required space is block size - 4 ate
   * where: 1 ate for data, 1 ate for block close, 1 ate for empty,
   * 1 ate for gc done, and 1 ate to always allow a delete.
   */

  if (required_space > fs->blocksize - 5 * ate_size)
    {
      return -EINVAL;
    }

  /* Calc hash id of key. */

  hash_id = nvs_fnv_hash_id(nvs_fnv_hash(key, key_len));

  /* Find latest entry with same id. */

#if CONFIG_MTD_CONFIG_CACHE_SIZE > 0
  wlk_addr = nvs_lookup_addr(fs, hash_id);
  if (wlk_addr != NVS_CACHE_NO_ADDR)
#endif
    {
      rc = nvs_find_ate_with_key(fs, key, key_len, wlk_addr, fs->ate_wra,
                                 wlk_ate, &wlk_addr);
    }

  if (rc < 0)
    {
      return rc;
    }

  if (rc)
    {
      /* Previous entry found. */

      if (pdata->len == 0)
        {
          /* If prev ate len is 0, it is deleted. */

          if (wlk_ate->len == 0)
            {
              /* Skip delete entry as it is already the
               * last one.
               */

              return 0;
            }
        }
      else if (pdata->len == wlk_ate->len)
        {
          /* Do not try to compare if lengths are not equal
           * or prev one is deleted.
           * Compare the data and if equal return 0.
           */

          wlk_addr &= NVS_ADDR_BLOCK_MASK;
          wlk_addr += wlk_ate->offset + wlk_ate->key_len;
          rc = nvs_flash_block_cmp(fs, wlk_addr, pdata->configdata,
                                   pdata->len);
          if (rc <= 0)
            {
              return rc;
            }
        }
    }
  else if (pdata->len == 0)
    {
      /* Skip delete entry for non-existing entry. */

      return 0;
    }

  for (gc_count = 0; gc_count < fs->nblocks; gc_count++)
    {
      if (fs->ate_wra >= fs->data_wra + required_space)
        {
          rc = nvs_flash_wrt_entry(fs, hash_id, key, key_len,
                                   pdata->configdata, pdata->len);
          if (rc)
            {
              ferr("Write entry failed\n");
            }

          return rc;
        }

      rc = nvs_block_close(fs);
      if (rc)
        {
          return rc;
        }

      rc = nvs_gc(fs);
      if (rc)
        {
          return rc;
        }
    }

  /* Gc'ed all blocks, no extra space will be created
   * by extra gc.
   */

  return -ENOSPC;
}

/****************************************************************************
 * Name: nvs_delete
 *
 * Description:
 *   Delete an entry from the file system.
 *
 * Input Parameters:
 *   fs    - Pointer to file system.
 *   pdata - Pointer to data buffer.
 *
 * Returned Value:
 *   0 on success, -ERRNO errno code if error.
 *
 ****************************************************************************/

static int nvs_delete(FAR struct nvs_fs *fs, FAR struct config_data_s *pdata)
{
  /* If user wants to operate /dev/config directly.
   * Set len=0 to trigger delete, so that user doesn't need to do that.
   */

  pdata->len = 0;
  return nvs_write(fs, pdata);
}

/****************************************************************************
 * Name: nvs_next
 *
 * Description:
 *   Get the next KV in database.
 *
 * Input Parameters:
 *   fs    - Pointer to file system.
 *   pdata - Pointer to data buffer.
 *   first - true if we are reading the first KV.
 *
 * Returned Value:
 *   0 on success, -ERRNO errno code if error.
 *
 ****************************************************************************/

static int nvs_next(FAR struct nvs_fs *fs, FAR struct file *filep,
                    FAR struct config_data_s *pdata, bool first)
{
  FAR uint64_t *step_addr = (FAR uint64_t *)filep->f_priv;
  uint32_t prev_block = NVS_INVALID_BLOCK;
  NVS_ATE(step_ate, nvs_ate_size(fs));
  NVS_ATE(wlk_ate, nvs_ate_size(fs));
  uint8_t cycle_cnt = 0;
  uint64_t wlk_addr;
  uint64_t rd_addr;
  uint16_t key_len;
  int rc;

#ifdef CONFIG_MTD_CONFIG_NAMED
  FAR uint8_t *key = (FAR uint8_t *)pdata->name;
#else
  uint8_t key[sizeof(pdata->id) + sizeof(pdata->instance)];
#endif

  if (first)
    {
      *step_addr = fs->ate_wra;
    }
  else if (*step_addr == fs->ate_wra)
    {
      return -ENOENT;
    }

  for (; ; )
    {
      rd_addr = *step_addr;
      rc = nvs_prev_ate(fs, step_addr, step_ate);
      if (rc)
        {
          return rc;
        }

      rc = nvs_get_cycle_on_block_change(fs, rd_addr, prev_block,
                                         &cycle_cnt);
      if (rc)
        {
          return rc;
        }

      prev_block = rd_addr >> NVS_ADDR_BLOCK_SHIFT;
      if (step_ate->id != nvs_special_ate_id(fs) && step_ate->len != 0 &&
          nvs_ate_valid_different_block(fs, step_ate, cycle_cnt))
        {
#if CONFIG_MTD_CONFIG_CACHE_SIZE > 0
          wlk_addr = nvs_lookup_addr(fs, step_ate->id);
          if (wlk_addr == NVS_CACHE_NO_ADDR)
#endif
            {
              wlk_addr = fs->ate_wra;
            }

          rc = nvs_find_ate(fs, step_ate, rd_addr, wlk_addr, rd_addr,
                            wlk_ate, &wlk_addr);
          if (rc < 0)
            {
              return rc;
            }
          else if (rc == 0 || rd_addr == wlk_addr)
            {
              break;
            }
        }

      if (*step_addr == fs->ate_wra)
        {
          return -ENOENT;
        }
    }

#ifdef CONFIG_MTD_CONFIG_NAMED
  key_len = MIN(step_ate->key_len, CONFIG_MTD_CONFIG_NAME_LEN);
#else
  key_len = MIN(step_ate->key_len, sizeof(key));
#endif

  rd_addr &= NVS_ADDR_BLOCK_MASK;
  rd_addr += step_ate->offset;
  rc = nvs_flash_rd(fs, rd_addr, key, key_len);
  if (rc)
    {
      ferr("Key read failed, rc=%d\n", rc);
      return rc;
    }

#ifdef CONFIG_MTD_CONFIG_NAMED
  key[CONFIG_MTD_CONFIG_NAME_LEN - 1] = 0;
#else
  memcpy(&pdata->id, key, sizeof(pdata->id));
  memcpy(&pdata->instance, key + sizeof(pdata->id), sizeof(pdata->instance));
#endif

  rc = nvs_flash_rd(fs, rd_addr + step_ate->key_len, pdata->configdata,
                    MIN(pdata->len, step_ate->len));
  if (rc)
    {
      ferr("Value read failed, rc=%d\n", rc);
      return rc;
    }

  pdata->len = step_ate->len;
  return OK;
}

/****************************************************************************
 * Name: mtdconfig_open
 ****************************************************************************/

static int mtdconfig_open(FAR struct file *filep)
{
  FAR uint64_t *step_addr = (FAR uint64_t *)kmm_zalloc(8);
  if (step_addr == NULL)
    {
      ferr("Failed to allocate memory for step_addr\n");
      return -ENOMEM;
    }

  filep->f_priv = (FAR void *)step_addr;
  return OK;
}

/****************************************************************************
 * Name: mtdconfig_close
 ****************************************************************************/

static int mtdconfig_close(FAR struct file *filep)
{
  FAR uint64_t *step_addr = (FAR uint64_t *)filep->f_priv;

  kmm_free(step_addr);
  filep->f_priv = NULL;
  return OK;
}

/****************************************************************************
 * Name: mtdconfig_read
 ****************************************************************************/

static ssize_t mtdconfig_read(FAR struct file *filep, FAR char *buffer,
                              size_t buflen)
{
  return -ENOTSUP;
}

/****************************************************************************
 * Name: mtdconfig_ioctl
 ****************************************************************************/

static int mtdconfig_ioctl(FAR struct file *filep, int cmd,
                           unsigned long arg)
{
  FAR struct inode *inode = filep->f_inode;
  FAR struct nvs_fs *fs = inode->i_private;
  FAR struct config_data_s *pdata = (FAR struct config_data_s *)arg;
  int rc;

  if (cmd >= CFGDIOC_GETCONFIG && cmd <= CFGDIOC_NEXTCONFIG &&
      (pdata == NULL || pdata->len > UINT16_MAX ||
      (pdata->len > 0 && pdata->configdata == NULL
      && cmd != CFGDIOC_DELCONFIG)))
    {
      return -EINVAL;
    }

  rc = nxmutex_lock(&fs->nvs_lock);
  if (rc < 0)
    {
      return rc;
    }

  switch (cmd)
    {
      case CFGDIOC_GETCONFIG:

        /* Read a nvs item. */

        rc = nvs_read(fs, pdata);
        break;

      case CFGDIOC_SETCONFIG:

        /* Write a nvs item. */

        rc = nvs_write(fs, pdata);
        break;

      case CFGDIOC_DELCONFIG:

        /* Delete a nvs item. */

        rc = nvs_delete(fs, pdata);
        break;

      case CFGDIOC_FIRSTCONFIG:

        /* Get the first item. */

        rc = nvs_next(fs, filep, pdata, true);
        break;

      case CFGDIOC_NEXTCONFIG:

        /* Get the next item. */

        rc = nvs_next(fs, filep, pdata, false);
        break;

      case MTDIOC_BULKERASE:

        /* Call the MTD's ioctl for this. */

        rc = nvs_clear(fs);
        if (rc == 0)
          {
            rc = nvs_init(fs);
          }

        break;

      default:
        rc = -ENOTTY;
    }

  nxmutex_unlock(&fs->nvs_lock);
  return rc;
}

/****************************************************************************
 * Name: mtdconfig_poll
 ****************************************************************************/

static int mtdconfig_poll(FAR struct file *filep, FAR struct pollfd *fds,
                          bool setup)
{
  if (setup)
    {
      poll_notify(&fds, 1, POLLIN | POLLOUT);
    }

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: mtdconfig_register_by_path
 *
 * Description:
 *   Register a "path" device backed by an fail-safe NVS.
 *
 ****************************************************************************/

int mtdconfig_register_by_path(FAR struct mtd_dev_s *mtd,
                               FAR const char *path)
{
  FAR struct nvs_fs *fs;
  int rc;

  fs = kmm_zalloc(sizeof(struct nvs_fs));
  if (fs == NULL)
    {
      return -ENOMEM;
    }

  /* Initialize the mtdnvs device structure */

  fs->mtd = mtd;
  rc = nxmutex_init(&fs->nvs_lock);
  if (rc < 0)
    {
      ferr("ERROR: nxmutex_init failed: %d\n", rc);
      goto errout;
    }

  rc = nvs_mount(fs);
  if (rc)
    {
      ferr("ERROR: nvs mount failed: %d\n", rc);
      goto mutex_err;
    }

  rc = nvs_init(fs);
  if (rc < 0)
    {
#ifdef CONFIG_MTD_CONFIG_AUTOFORMAT
      fwarn("WARNING: nvs init failed: %d, autoformat\n", rc);
      rc = nvs_clear(fs);
      if (rc == 0)
        {
          rc = nvs_init(fs);
        }

      if (rc < 0)
#endif
        {
          ferr("ERROR: nvs_init failed: %d\n", rc);
          goto mutex_err;
        }
    }

  rc = register_driver(path, &g_mtdconfig_fops, 0666, fs);
  if (rc < 0)
    {
      ferr("ERROR: register mtd config failed: %d\n", rc);
      goto mutex_err;
    }

  return rc;

mutex_err:
  nxmutex_destroy(&fs->nvs_lock);
errout:
  kmm_free(fs);
  return rc;
}

/****************************************************************************
 * Name: mtdconfig_register
 *
 * Description:
 *   Register a /dev/config device backed by an fail-safe NVS.
 *
 ****************************************************************************/

int mtdconfig_register(FAR struct mtd_dev_s *mtd)
{
  return mtdconfig_register_by_path(mtd, "/dev/config");
}

/****************************************************************************
 * Name: mtdconfig_unregister_by_path
 *
 * Description:
 *   Unregister a MTD device backed by an fail-safe NVS.
 *
 ****************************************************************************/

int mtdconfig_unregister_by_path(FAR const char *path)
{
  FAR struct inode *inode;
  FAR struct nvs_fs *fs;
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
  nxmutex_destroy(&fs->nvs_lock);
  kmm_free(fs);
  file_close(&file);
  unregister_driver(path);

  return OK;
}

/****************************************************************************
 * Name: mtdconfig_unregister
 *
 * Description:
 *   Unregister a /dev/config device backed by an fail-safe NVS.
 *
 ****************************************************************************/

int mtdconfig_unregister(void)
{
  return mtdconfig_unregister_by_path("/dev/config");
}
