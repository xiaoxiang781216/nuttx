/****************************************************************************
 * arch/arm/src/common/arm_trustzone_monitor.c
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
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <debug.h>

#include <nuttx/fs/fs.h>
#include <nuttx/fs/procfs.h>
#include <nuttx/kmalloc.h>
#include <nuttx/clock.h>
#include <nuttx/irq.h>

#include "arm_internal.h"

/****************************************************************************
 * Public Data
 ****************************************************************************/

struct trustzone_monitor_s g_trustzone_monitor
locate_data(".trustzone_monitor");

#ifdef CONFIG_FS_PROCFS_REGISTER
/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define TRUSTZONE_PROCFS_LINELEN  128

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct trustzone_monitor_file_s
{
  struct procfs_file_s base;           /* Base open file structure */
  unsigned int linesize;               /* valid characters in line[] */
  char line[TRUSTZONE_PROCFS_LINELEN]; /* Pre-allocated buffer for output */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int trustzone_monitor_open(FAR struct file *filep,
                                  FAR const char *relpath,
                                  int oflags, mode_t mode);
static int trustzone_monitor_close(FAR struct file *filep);
static ssize_t trustzone_monitor_read(FAR struct file *filep,
                                      FAR char *buffer, size_t buflen);
static int trustzone_monitor_stat(FAR const char *relpath,
                                  FAR struct stat *buf);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct procfs_operations g_trustzone_monitor_ops =
{
  trustzone_monitor_open,   /* open */
  trustzone_monitor_close,  /* close */
  trustzone_monitor_read,   /* read */
  NULL,                     /* write */
  NULL,                     /* poll */
  NULL,                     /* opendir */
  NULL,                     /* closedir */
  NULL,                     /* readdir */
  NULL,                     /* rewinddir */
  trustzone_monitor_stat,   /* stat */
};

static const struct procfs_entry_s g_trustzone_monitor_entry =
{
  "trustzone",
  &g_trustzone_monitor_ops,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: trustzone_monitor_open
 ****************************************************************************/

static int trustzone_monitor_open(FAR struct file *filep,
                                  FAR const char *relpath,
                                  int oflags, mode_t mode)
{
  FAR struct trustzone_monitor_file_s *priv;

  priv = kmm_zalloc(sizeof(struct trustzone_monitor_file_s));
  if (priv == NULL)
    {
      return -ENOMEM;
    }

  filep->f_priv = priv;
  return OK;
}

/****************************************************************************
 * Name: trustzone_monitor_close
 ****************************************************************************/

static int trustzone_monitor_close(FAR struct file *filep)
{
  FAR struct trustzone_monitor_file_s *priv;

  priv = (FAR struct trustzone_monitor_file_s *)filep->f_priv;
  DEBUGASSERT(priv);
  kmm_free(priv);
  filep->f_priv = NULL;
  return OK;
}

/****************************************************************************
 * Name: trustzone_monitor_read
 *
 * Description:
 *   Output Secure timing statistics from shared memory.
 *   Format: "count=<N> total=<T>us max=<M>us freq=<F>\n"
 *
 ****************************************************************************/

static ssize_t trustzone_monitor_read(FAR struct file *filep,
                                      FAR char *buffer, size_t buflen)
{
  FAR struct trustzone_monitor_file_s *priv;
  off_t offset;
  ssize_t ret;

  priv = (FAR struct trustzone_monitor_file_s *)filep->f_priv;
  DEBUGASSERT(priv);

  if (g_trustzone_monitor.secure_count > 0)
    {
      struct timespec ts_total;
      struct timespec ts_max;

      perf_convert(g_trustzone_monitor.secure_total, &ts_total);
      perf_convert(g_trustzone_monitor.secure_max, &ts_max);

      uint64_t total_us = ts_total.tv_sec * 1000000ULL +
                          ts_total.tv_nsec / 1000;
      uint64_t max_us = ts_max.tv_sec * 1000000ULL +
                        ts_max.tv_nsec / 1000;

      priv->linesize = snprintf(priv->line, TRUSTZONE_PROCFS_LINELEN,
                                "count=%lu total=%" PRIu64 "us"
                                " max=%" PRIu64 "us\n",
                                g_trustzone_monitor.secure_count,
                                total_us, max_us);
    }
  else
    {
      priv->linesize = snprintf(priv->line, TRUSTZONE_PROCFS_LINELEN,
                                "no data\n");
    }

  offset = filep->f_pos;
  ret = procfs_memcpy(priv->line, priv->linesize, buffer, buflen, &offset);
  filep->f_pos += ret;
  return ret;
}

/****************************************************************************
 * Name: trustzone_monitor_stat
 ****************************************************************************/

static int trustzone_monitor_stat(FAR const char *relpath,
                                  FAR struct stat *buf)
{
  memset(buf, 0, sizeof(struct stat));
  buf->st_mode = S_IFREG | S_IROTH | S_IRGRP | S_IRUSR;
  return OK;
}
#endif /* CONFIG_FS_PROCFS_REGISTER */

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: arm_trustzone_monitor_init
 *
 * Description:
 *   Register the /proc/trustzone procfs entry for Secure timing statistics.
 *
 ****************************************************************************/

int arm_trustzone_monitor_init(void)
{
  int ret = OK;

  memset(&g_trustzone_monitor, 0, sizeof(g_trustzone_monitor));

#ifdef CONFIG_FS_PROCFS_REGISTER
  ret = procfs_register(&g_trustzone_monitor_entry);
#endif

  return ret;
}
