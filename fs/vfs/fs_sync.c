/****************************************************************************
 * fs/vfs/fs_sync.c
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

#include <unistd.h>

#include <nuttx/sched.h>
#include <nuttx/fs/fs.h>

#include "inode/inode.h"
#include "mount/mount.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: task_fssync
 *
 * Description:
 *   nxsched_foreach() callback that fsyncs every open file of one task.
 *
 ****************************************************************************/

static void task_fssync(FAR struct tcb_s *tcb, FAR void *arg)
{
  FAR struct fdlist *list;
  int count;
  int fd;

  UNUSED(arg);

  list = nxsched_get_fdlist_from_tcb(tcb);
  count = fdlist_count(list);

  for (fd = 0; fd < count; fd++)
    {
      FAR struct file *filep;

      /* fdlist_get() addrefs the file on success and rejects empty or
       * out-of-range slots, so a failure just means "no file here".
       */

      if (fdlist_get(list, fd, &filep) >= 0)
        {
          file_fsync(filep);
          file_put(filep);
        }
    }
}

#ifndef CONFIG_DISABLE_MOUNTPOINT
/****************************************************************************
 * Name: mount_syncfs
 *
 * Description:
 *   foreach_mountpoint() callback that flushes one filesystem via its
 *   syncfs(), which persists state a per-file fsync never reaches (e.g. a
 *   yaffs checkpoint).  The traversal hands the callback only the path, so
 *   the mountpoint inode is re-resolved here.
 *
 ****************************************************************************/

static int mount_syncfs(FAR const char *mountpoint, FAR void *arg)
{
  FAR struct inode *inode;
  struct inode_search_s desc;
  int ret;

  UNUSED(arg);

  SETUP_SEARCH(&desc, mountpoint, false);
  ret = inode_find(&desc);
  if (ret < 0)
    {
      /* Raced with an unmount; nothing to flush. */

      RELEASE_SEARCH(&desc);
      return 0;
    }

  inode = desc.node;
  if (INODE_IS_MOUNTPT(inode) && inode->u.i_mops->syncfs != NULL)
    {
      inode->u.i_mops->syncfs(inode);
    }

  inode_release(inode);
  RELEASE_SEARCH(&desc);
  return 0;
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: sync
 *
 * Description:
 *   sync() causes all pending modifications to filesystem metadata and
 *   cached file data to be written to the underlying filesystems.
 *
 ****************************************************************************/

void sync(void)
{
  nxsched_foreach(task_fssync, NULL);

#ifndef CONFIG_DISABLE_MOUNTPOINT
  foreach_mountpoint(mount_syncfs, NULL, false);
#endif
}
