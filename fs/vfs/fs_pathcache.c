/****************************************************************************
 * fs/vfs/fs_pathcache.c
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
#include <errno.h>

#include <nuttx/nuttx.h>
#include <nuttx/lib/lib.h>
#include <nuttx/lru_cache.h>

#include "inode/inode.h"
#include "fs_heap.h"
#include "vfs.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Private Types
 ****************************************************************************/

/****************************************************************************
 * Private Functions Prototypes
 ****************************************************************************/

static void pathcache_get(FAR void *value);
static void pathcache_put(FAR void *value);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct lru_cache_s g_pathcache;
static const struct lru_cache_ops_s g_pathcache_ops =
{
  .get_cb = pathcache_get,
  .put_cb = pathcache_put,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: pathcache_get
 *
 * Description:
 *   Increase reference count of a cached file entry.
 *
 * Input Parameters:
 *   value - Pointer to the cached file structure
 *
 ****************************************************************************/

static void pathcache_get(FAR void *value)
{
  FAR struct file *filep = value;
  atomic_add(&filep->f_refs, 1);
}

/****************************************************************************
 * Name: pathcache_put
 *
 * Description:
 *   Decrease reference count of a cached file entry.
 *
 * Input Parameters:
 *   value - Pointer to the cached file structure
 *
 * Returned Value:
 *   Reference count before decrement
 *
 ****************************************************************************/

static void pathcache_put(FAR void *value)
{
  FAR struct file *filep = value;

  if (atomic_sub(&filep->f_refs, 1) == 1)
    {
      filep->f_inode->u.i_mops->close(filep);
      inode_release(filep->f_inode);
      file_deallocate(filep);
    }
}

/****************************************************************************
 * Name: pathcache_create
 *
 * Description:
 *   Create a cache entry from an opened file.
 *
 * Input Parameters:
 *   filep      - File structure of the opened file
 *   abspath    - Absolute path used as cache key
 *
 * Returned Value:
 *   OK on success; a negated errno value on failure
 *
 ****************************************************************************/

static int pathcache_create(FAR struct file *filep, FAR char *abspath)
{
  FAR struct file *cached_file;
  FAR struct inode *inode;
  int ret;

  inode = filep->f_inode;

  /* Check if dup is supported */

  if (inode->u.i_mops->dup == NULL)
    {
      return -ENOSYS;
    }

  /* Allocate a new file structure for caching */

  cached_file = file_allocate();
  if (cached_file == NULL)
    {
      return -ENOMEM;
    }

  inode_addref(inode);

  /* Initialize cached_file's VFS fields */

  cached_file->f_oflags = O_RDONLY;
  cached_file->f_inode  = inode;

  /* Duplicate the file structure for caching */

  ret = inode->u.i_mops->dup(filep, cached_file);
  if (ret < 0)
    {
      goto errout_with_file;
    }

  /* Insert the entry into cache (internally locked) */

  ret = lru_cache_insert(&g_pathcache, abspath, cached_file);
  if (ret < 0)
    {
      goto errout_with_cached_file;
    }

  return OK;

errout_with_cached_file:
  inode->u.i_mops->close(cached_file);

errout_with_file:
  file_deallocate(cached_file);
  return ret;
}

/****************************************************************************
 * Name: pathcache_is_match
 *
 * Description:
 *   Match callback to check if a cache entry belongs to the unmounting
 *   mountpoint.
 *
 * Input Parameters:
 *   cache - Pointer to the cache structure (unused)
 *   key   - The cache key
 *   value - Pointer to the cached file structure
 *   arg   - Pointer to the unmounting mountpoint inode
 *
 * Returned Value:
 *   1 if entry should be removed; 0 otherwise
 *
 ****************************************************************************/

static int
pathcache_is_match(FAR struct lru_cache_s *cache,
                   FAR const char *key, FAR void *value,
                   FAR void *arg)
{
  /* Check if this file belongs to the unmounting mountpoint */

  return ((FAR struct file *)value)->f_inode == arg;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: pathcache_open
 *
 * Description:
 *   Open a file with caching support. First open stores a snapshot,
 *   subsequent opens duplicate from the cached snapshot.
 *
 * Input Parameters:
 *   filep   - File structure to populate
 *   relpath - Relative path within the mount point
 *   oflags  - Open flags
 *   mode    - File mode
 *   path    - Path passed to open (used as cache key)
 *
 * Returned Value:
 *   OK on success; a negated errno value on failure
 *
 ****************************************************************************/

int pathcache_open(FAR struct file *filep, FAR const char *relpath,
                   int oflags, mode_t mode, FAR const char *path)
{
  FAR struct inode *inode = filep->f_inode;
  FAR struct file *cached_file = NULL;
  FAR char *pathbuffer;
  FAR char *abspath;
  int ret = OK;

  /* Get the canonical absolute path. */

  pathbuffer = lib_get_tempbuffer(PATH_MAX);
  if (pathbuffer == NULL)
    {
      return -ENOMEM;
    }

  abspath = lib_realpath(path, pathbuffer, true);
  if (abspath == NULL)
    {
      goto errout_with_pathbuffer;
    }

  /* Look up for cached entry (internally locked) */

  cached_file = lru_cache_lookup(&g_pathcache, abspath);

  /* Cache miss */

  if (cached_file == NULL)
    {
      /* Open the file by inode */

      ret = inode->u.i_mops->open(filep, relpath, oflags, mode);
      if (ret < 0)
        {
          goto errout_with_pathbuffer;
        }

      /* File opened successfully. Now try to create a cache entry.
       * Only cache files opened in read-only mode here.
       * If caching fails for any reason, we still return success because
       * the file is already opened. Caching is just an optimization.
       */

      if (!(oflags & O_WRONLY))
        {
          pathcache_create(filep, abspath);
        }

      goto errout_with_pathbuffer;
    }
  else
    {
      /* Cache hit.
       * Duplicating the cached file into the provided file pointer.
       */

      inode->u.i_mops->dup(cached_file, filep);

      /* Put the cache entry every time we have finished using it */

      pathcache_put(cached_file);
      if (oflags & O_WRONLY)
        {
          lru_cache_remove(&g_pathcache, abspath);
        }

      /* If the file is opened with O_TRUNC, truncate it */

      if (oflags & O_TRUNC)
        {
          ret = inode->u.i_mops->truncate(filep, 0);
          if (ret < 0)
            {
              goto errout_with_filep;
            }
        }

      /* If the file is opened with O_APPEND, seek to the end */

      if (oflags & O_APPEND)
        {
          ret = inode->u.i_mops->seek(filep, 0, SEEK_END);
          if (ret < 0)
            {
              goto errout_with_filep;
            }
        }

      goto errout_with_pathbuffer;
    }

errout_with_filep:
  inode->u.i_mops->close(filep);

errout_with_pathbuffer:
  lib_put_tempbuffer(pathbuffer);
  return ret;
}

/****************************************************************************
 * Name: pathcache_remove
 *
 * Description:
 *   Remove a cache entry by path.
 *
 * Input Parameters:
 *   path - Path to remove
 *
 * Returned Value:
 *   OK on success; a negated errno value on failure
 *
 ****************************************************************************/

int pathcache_remove(FAR const char *path)
{
  FAR char *pathbuffer;
  FAR char *abspath;
  int ret = OK;

  DEBUGASSERT(path != NULL);

  /* Get the canonical absolute path for removing cache entry */

  pathbuffer = lib_get_tempbuffer(PATH_MAX);
  if (pathbuffer == NULL)
    {
      return -ENOMEM;
    }

  abspath = lib_realpath(path, pathbuffer, true);
  if (abspath == NULL)
    {
      ret = -EINVAL;
      goto errout_with_pathbuffer;
    }

  /* Remove the cache entry */

  lru_cache_remove(&g_pathcache, abspath);

errout_with_pathbuffer:
  lib_put_tempbuffer(pathbuffer);
  return ret;
}

/****************************************************************************
 * Name: pathcache_close
 *
 * Description:
 *   Close a file with caching support. If the file was opened with WRONLY
 *   or TRUNC, update the cache entry.
 *
 * Input Parameters:
 *   filep   - File structure being closed
 *   path    - Absolute path (used as cache key)
 *
 * Returned Value:
 *   OK on success; a negated errno value on failure
 *
 ****************************************************************************/

int pathcache_close(FAR struct file *filep, FAR const char *path)
{
  FAR char *pathbuffer;
  FAR char *abspath;
  int ret;

  DEBUGASSERT(filep != NULL && path != NULL);

  /* Check if the file is opened with WRONLY or TRUNC.
   * If not, no need to update the cache.
   */

  if (!(filep->f_oflags & O_WRONLY))
    {
      return OK;
    }

  /* Get the canonical absolute path for creating new entry */

  pathbuffer = lib_get_tempbuffer(PATH_MAX);
  if (pathbuffer == NULL)
    {
      return -ENOMEM;
    }

  abspath = lib_realpath(path, pathbuffer, true);
  if (abspath == NULL)
    {
      ret = -EINVAL;
      goto errout_with_pathbuffer;
    }

  /* Remove the old cache entry first */

  lru_cache_remove(&g_pathcache, abspath);

  /* Create a new cache entry with the updated file content. */

  pathcache_create(filep, abspath);

errout_with_pathbuffer:
  lib_put_tempbuffer(pathbuffer);
  return ret;
}

/****************************************************************************
 * Name: pathcache_umount
 *
 * Description:
 *   Invalidate all cache entries belonging to a specific mountpoint
 *
 * Input Parameters:
 *   mountpt - Mountpoint inode being unmounted
 *
 ****************************************************************************/

void pathcache_umount(FAR struct inode *mountpt)
{
  lru_cache_remove_if(&g_pathcache, pathcache_is_match, mountpt);
}

/****************************************************************************
 * Name: pathcache_initialize
 *
 * Description:
 *   Initialize the pathcache
 *
 * Input Parameters:
 *   None
 *
 ****************************************************************************/

void pathcache_initialize(void)
{
  lru_cache_init(&g_pathcache, CONFIG_FS_PATHCACHE_MAX_ENTRIES,
                 CONFIG_FS_PATHCACHE_HASH_SIZE, &g_pathcache_ops);
}
