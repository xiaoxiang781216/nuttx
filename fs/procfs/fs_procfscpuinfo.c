/****************************************************************************
 * fs/procfs/fs_procfscpuinfo.c
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

#include <assert.h>
#include <errno.h>
#include <debug.h>

#include <nuttx/arch.h>
#include <nuttx/kmalloc.h>
#include <nuttx/fs/fs.h>
#include <nuttx/fs/procfs.h>

#include "fs_heap.h"

#if defined(CONFIG_ARCH_HAVE_CPUINFO) && !defined(CONFIG_FS_PROCFS_EXCLUDE_CPUINFO)

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* This structure describes one open "file" */

struct cpuinfo_file_s
{
  struct procfs_file_s base;      /* Base open file structure */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/* File system methods */

static int     cpuinfo_open(FAR struct file *filep, FAR const char *relpath,
                            int oflags, mode_t mode);
static int     cpuinfo_close(FAR struct file *filep);
static ssize_t cpuinfo_read(FAR struct file *filep, FAR char *buffer,
                            size_t buflen);

/****************************************************************************
 * Public Data
 ****************************************************************************/

/* See fs_mount.c -- this structure is explicitly externed there.
 * We use the old-fashioned kind of initializers so that this will compile
 * with any compiler.
 */

const struct procfs_operations g_cpuinfo_operations =
{
  cpuinfo_open,   /* open */
  cpuinfo_close,  /* close */
  cpuinfo_read,   /* read */
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: cpuinfo_open
 ****************************************************************************/

static int cpuinfo_open(FAR struct file *filep, FAR const char *relpath,
                        int oflags, mode_t mode)
{
  FAR struct cpuinfo_file_s *procfile;

  finfo("Open '%s'\n", relpath);

  /* Allocate a container to hold the file attributes */

  procfile = fs_heap_zalloc(sizeof(struct cpuinfo_file_s));
  if (procfile == NULL)
    {
      ferr("ERROR: Failed to allocate file attributes\n");
      return -ENOMEM;
    }

  /* Save the attributes as the open-specific state in filep->f_priv */

  filep->f_priv = procfile;
  return OK;
}

/****************************************************************************
 * Name: cpuinfo_close
 ****************************************************************************/

static int cpuinfo_close(FAR struct file *filep)
{
  FAR struct cpuinfo_file_s *procfile;

  /* Recover our private data from the struct file instance */

  procfile = filep->f_priv;
  DEBUGASSERT(procfile);

  /* Release the file attributes structure */

  fs_heap_free(procfile);
  filep->f_priv = NULL;
  return OK;
}

/****************************************************************************
 * Name: cpuinfo_read
 ****************************************************************************/

static ssize_t cpuinfo_read(FAR struct file *filep, FAR char *buffer,
                            size_t buflen)
{
  ssize_t copylen;
  off_t offset;

  finfo("buffer=%p buflen=%zu\n", buffer, buflen);

  DEBUGASSERT(buffer != NULL && buflen > 0);
  offset = filep->f_pos;

  copylen = up_show_cpuinfo(buffer, buflen, offset);
  if (copylen > 0)
    {
      filep->f_pos += copylen;
    }
  else
    {
      copylen = 0;
    }

  return copylen;
}

#endif /* !CONFIG_FS_PROCFS_EXCLUDE_CPUINFO */
