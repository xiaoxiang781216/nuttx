/****************************************************************************
 * fs/fs_reboot.c
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
#include <nuttx/reboot_notifier.h>
#include <nuttx/percpu.h>
#include <nuttx/irq.h>
#include <nuttx/fs/fs.h>

#include <unistd.h>

#include "mount/mount.h"
#include "fs_reboot.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: fs_reboot_handler
 *
 * Description:
 *   Unified filesystem reboot notifier.  On power-off/restart it first
 *   flushes all open descriptors and pseudo filesystems via sync(), then
 *   force-unmounts every mountpoint.  The unmount step drives each
 *   filesystem's unbind() path (e.g. yaffs flush_whole_cache +
 *   checkpoint_save + deinitialise), which sync() alone does not reach.
 *
 *   The per-mountpoint work is delegated to umount_all(), which drives
 *   foreach_mountpoint() to flush and force-unmount each mountpoint with no
 *   dynamic allocation on the reboot path.
 *
 ****************************************************************************/

static int fs_reboot_handler(FAR struct notifier_block *nb,
                             unsigned long action, FAR void *data)
{
  if (!up_interrupt_context() &&
      (action == SYS_POWER_OFF || action == SYS_RESTART))
    {
      /* Flush all cached file data of open descriptors and pseudo
       * filesystems first.
       */

      sync();

#ifndef CONFIG_DISABLE_MOUNTPOINT
      /* Then flush and force-unmount every mountpoint. */

      umount_all();
#endif
    }

  return 0;
}

/****************************************************************************
 * Private Data
 ****************************************************************************/

/****************************************************************************
 * Name: g_fs_reboot_nb
 ****************************************************************************/

static DEFINE_PER_CPU_BMP(struct notifier_block, g_fs_reboot_nb) =
{
  fs_reboot_handler
};
#define g_fs_reboot_nb this_cpu_var_bmp(g_fs_reboot_nb)

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: fs_reboot_initialize
 *
 * Description:
 *   Register the unified filesystem reboot notifier so that on
 *   power-off/restart all filesystems are synced and unmounted.
 *
 ****************************************************************************/

void fs_reboot_initialize(void)
{
  register_reboot_notifier(&g_fs_reboot_nb);
}
