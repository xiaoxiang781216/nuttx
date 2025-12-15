/****************************************************************************
 * fs/vfs/fs_symlink.c
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

#include <sys/types.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#include <nuttx/lib/lib.h>
#include <nuttx/fs/fs.h>

#include "inode/inode.h"
#include "fs_heap.h"
#include "vfs.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifdef CONFIG_FS_LINKS

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#ifndef CONFIG_DISABLE_MOUNTPOINT

/****************************************************************************
 * Name: get_relative_path
 *
 * Description:
 *   Calculate the relative path from symlink location to target.
 *   Both paths are relative to the same mountpoint.
 *
 * Input Parameters:
 *   t             - Target path relative to mountpoint
 *   s             - Symlink path relative to mountpoint
 *   result        - Buffer to store the resulting relative path
 *   result_size   - Size of the buffer
 *
 * Returned Value:
 *   OK on success, or negative errno on failure.
 *
 * Example:
 *   t             = "dir1/file"
 *   s             = "dir2/link"
 *   result        = "../dir1/file"
 *
 ****************************************************************************/

static int get_relative_path(FAR const char *t, FAR const char *s,
                             FAR char *result, size_t result_size)
{
  FAR char *end = result + result_size;
  int levels_up = 0;

  /* Count directory levels in symlink's path, handling ".." components.
   * Each regular component increases depth, ".." decreases it.
   */

  while (*s != '\0')
    {
      /* Check if current component is ".." */

      if (s[0] == '.' && s[1] == '.' && (s[2] == '/' || s[2] == '\0'))
        {
          levels_up--;
        }
      else
        {
          levels_up++;
        }

      s = inode_nextname(s);
    }

  /* Count out the filename of the symlink */

  if (--levels_up < 0)
    {
      return -EINVAL;
    }

  /* Build result: add "../" for each level */

  while (levels_up--)
    {
      if (result + 3 > end)
        {
          return -ENAMETOOLONG;
        }

      *result++ = '.';
      *result++ = '.';
      *result++ = '/';
    }

  /* Append the complete target path */

  if (result + strlen(t) >= end)
    {
      return -ENAMETOOLONG;
    }

  strlcpy(result, t, end - result);
  return OK;
}
#endif /* CONFIG_DISABLE_MOUNTPOINT */

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: symlink
 *
 * Description:
 *   The symlink() function will create a new link (directory entry) for the
 *   existing file, path2.  This implementation is simplified for use with
 *   NuttX in these ways:
 *
 *   - Links may be created only within the same mountpoint.
 *   - File privileges are ignored.
 *   - c_time is not updated.
 *
 * Input Parameters:
 *   path1 - Points to a pathname naming an existing file.
 *   path2 - Points to a pathname naming the new directory entry to be
 *           created.
 *
 * Returned Value:
 *   On success, zero (OK) is returned.  Otherwise, -1 (ERROR) is returned
 *   the errno variable is set appropriately.
 *
 ****************************************************************************/

int symlink(FAR const char *path1, FAR const char *path2)
{
  struct inode_search_s target_desc;
  struct inode_search_s desc;
  FAR struct inode *inode = NULL;
  int errcode;
  int ret;

  if (path1 == NULL || path2 == NULL)
    {
      errcode = EINVAL;
      goto errout;
    }

  if (*path1 == '\0' || *path2 == '\0')
    {
      errcode = ENOENT;
      goto errout;
    }

  /* Check that no inode exists at the 'path2' and that the path up to
   * 'path2' does not lie on a mounted volume.
   */

  SETUP_SEARCH(&desc, path2, true);

  ret = inode_find(&desc);
  if (ret >= 0)
    {
      /* Something exists at the path2 where we are trying to create the
       * link.
       */

#ifndef CONFIG_DISABLE_MOUNTPOINT
      /* Check if the inode is a mountpoint. */

      DEBUGASSERT(desc.node != NULL);
      if (INODE_IS_MOUNTPT(desc.node))
        {
          if (desc.node->u.i_mops && desc.node->u.i_mops->symlink)
            {
              SETUP_SEARCH(&target_desc, path1, true);
              ret = inode_search(&target_desc);
              if (ret < 0 || target_desc.node != desc.node)
                {
                  /* This file does not exist or is not on the same
                   * mountpoint with the symlink, which is not
                   * supported in our system, so return error.
                   */

                  errcode = ENOSYS;
                  goto errout_with_target_desc;
                }

              /* Now we have:
               * desc.relpath - symlink path relative to mountpoint
               * target_desc.relpath - target path relative to mountpoint
               * If path1 is absolute, convert target_desc.relpath to
               * relative path from symlink's directory.
               * If path1 is relative, use it as-is.
               */

              if (path1[0] == '/')
                {
                  FAR char *target_relpath = lib_get_tempbuffer(PATH_MAX);

                  /* Get relative path from symlink's directory to target */

                  ret = get_relative_path(target_desc.relpath,
                                          desc.relpath,
                                          target_relpath, PATH_MAX);
                  if (ret < 0)
                    {
                      errcode = -ret;
                      lib_put_tempbuffer(target_relpath);
                      goto errout_with_target_desc;
                    }

                  ret = desc.node->u.i_mops->symlink(desc.node,
                                                     target_relpath,
                                                     desc.relpath);
                  lib_put_tempbuffer(target_relpath);
                }
              else
                {
                  ret = desc.node->u.i_mops->symlink(desc.node, path1,
                                                     desc.relpath);
                }

              if (ret < 0)
                {
                  errcode = -ret;
                  goto errout_with_target_desc;
                }

              RELEASE_SEARCH(&target_desc);
            }
          else
            {
              /* Symbolic links within this type of fs are not supported */

              errcode = ENOSYS;
              goto errout_with_inode;
            }
        }
      else
#endif
        {
          /* A node already exists in the pseudofs at 'path1' */

          errcode = EEXIST;
          goto errout_with_inode;
        }
    }
  else if (ret != -ENOENT)
    {
      /* There was an unexpected error during the search */

      errcode = -ret;
      goto errout_with_search;
    }

  /* No inode exists that contains this path.  Create a new inode in the
   * pseudo-filesystem at this location.
   */

  else
    {
      /* Copy path1 */

      FAR char *newpath2 = fs_heap_strdup(path1);
      if (newpath2 == NULL)
        {
          errcode = ENOMEM;
          goto errout_with_search;
        }

      /* Create an inode in the pseudo-filesystem at this path.
       * NOTE that the new inode will be created with a reference
       * count of zero.
       */

      inode_lock();
      ret = inode_reserve(path2, 0777, &inode);

      if (ret >= 0)
        {
          /* Initialize the inode */

          INODE_SET_SOFTLINK(inode);
          inode->u.i_link = newpath2;
        }

      inode_unlock();
      if (ret < 0)
        {
          fs_heap_free(newpath2);
          errcode = -ret;
          goto errout_with_search;
        }
    }

  /* Symbolic link successfully created */

  RELEASE_SEARCH(&desc);
#ifdef CONFIG_FS_NOTIFY
  notify_create(path2);
#endif
  return OK;

errout_with_target_desc:
  RELEASE_SEARCH(&target_desc);

errout_with_inode:
  inode_release(inode);

errout_with_search:
  RELEASE_SEARCH(&desc);

errout:
  set_errno(errcode);
  return ERROR;
}

#endif /* CONFIG_FS_LINKS */
