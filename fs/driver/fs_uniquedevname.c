/****************************************************************************
 * fs/driver/fs_uniquedevname.c
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

#include <sys/stat.h>

#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <debug.h>

#include <nuttx/fs/fs.h>
#include <nuttx/mutex.h>

#include "driver/driver.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

static uint32_t g_devno;
static mutex_t g_devno_lock = NXMUTEX_INITIALIZER;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: unique_devname
 *
 * Description:
 *   Build a unique temporary device name "/dev/tmp<prefix><6 hex>" in the
 *   pseudo-file system.  mktemp() cannot be used because it would open() the
 *   name.  A single counter serves both flavours; the prefix only keeps the
 *   two device classes visually distinct, and nx_stat() guards against a
 *   name that is somehow already in use.
 *
 * Input Parameters:
 *   prefix - Device-class tag: 'b' for the FTL block device created by
 *            mtd_proxy(), 'c' for the BCH character device created by
 *            block_proxy().
 *   devbuf - Caller-provided buffer receiving the name.
 *   len    - Size of devbuf in bytes.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure.
 *
 ****************************************************************************/

int unique_devname(char prefix, FAR char *devbuf, size_t len)
{
  struct stat statbuf;
  uint32_t devno;
  int ret;

  /* Loop until we get a unique device name */

  for (; ; )
    {
      ret = nxmutex_lock(&g_devno_lock);
      if (ret < 0)
        {
          ferr("ERROR: nxmutex_lock failed: %d\n", ret);
          return ret;
        }

      devno = ++g_devno;
      nxmutex_unlock(&g_devno_lock);

      devno &= 0xffffff;
      snprintf(devbuf, len, "/dev/tmp%c%06lx", prefix,
               (unsigned long)devno);

      /* Make sure that name is not already in use */

      ret = nx_stat(devbuf, &statbuf, 1);
      if (ret < 0)
        {
          DEBUGASSERT(ret == -ENOENT);
          return OK;
        }

      /* It is in use, try again */
    }
}
