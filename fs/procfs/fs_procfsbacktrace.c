/****************************************************************************
 * fs/procfs/fs_procfsbacktrace.c
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
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>

#include <execinfo.h>

#include <nuttx/fs/fs.h>
#include <nuttx/fs/procfs.h>

#include "fs_heap.h"

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int     backtrace_procfs_open(FAR struct file *filep,
                                     FAR const char *relpath,
                                     int oflags, mode_t mode);
static int     backtrace_procfs_close(FAR struct file *filep);
static ssize_t backtrace_procfs_write(FAR struct file *filep,
                                      FAR const char *buffer,
                                      size_t buflen);

/****************************************************************************
 * Public Data
 ****************************************************************************/

const struct procfs_operations g_backtrace_operations =
{
  backtrace_procfs_open,   /* open */
  backtrace_procfs_close,  /* close */
  NULL,                    /* read */
  backtrace_procfs_write,  /* write */
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: backtrace_procfs_open
 ****************************************************************************/

static int backtrace_procfs_open(FAR struct file *filep,
                                 FAR const char *relpath,
                                 int oflags, mode_t mode)
{
  FAR struct procfs_file_s *procfile;

  procfile = fs_heap_zalloc(sizeof(struct procfs_file_s));
  if (procfile == NULL)
    {
      return -ENOMEM;
    }

  filep->f_priv = procfile;
  return OK;
}

/****************************************************************************
 * Name: backtrace_procfs_close
 ****************************************************************************/

static int backtrace_procfs_close(FAR struct file *filep)
{
  fs_heap_free(filep->f_priv);
  filep->f_priv = NULL;
  return OK;
}

/****************************************************************************
 * Name: backtrace_procfs_write
 ****************************************************************************/

static ssize_t backtrace_procfs_write(FAR struct file *filep,
                                      FAR const char *buffer,
                                      size_t buflen)
{
  if (buflen >= 4 && strncmp(buffer, "dump", 4) == 0)
    {
      backtrace_dump();
    }

  return buflen;
}
