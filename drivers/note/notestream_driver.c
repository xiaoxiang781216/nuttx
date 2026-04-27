/****************************************************************************
 * drivers/note/notestream_driver.c
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

#include <stdint.h>
#include <fcntl.h>

#include <nuttx/kmalloc.h>
#include <nuttx/streams.h>
#include <nuttx/note/notestream_driver.h>

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct notestream_s
{
  struct note_driver_s driver;
  struct lib_fileoutstream_s filestream;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int notestream_add(FAR struct note_driver_s *drv,
                          FAR const void *note, size_t len,
                          bool noswitches);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct note_driver_ops_s g_notestream_ops =
{
  notestream_add
};

/****************************************************************************
 * Public Data
 ****************************************************************************/

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int notestream_add(FAR struct note_driver_s *drv,
                          FAR const void *note, size_t len,
                          bool noswitches)
{
  FAR struct notestream_s *notestream =
      (FAR struct notestream_s *)drv;
  int ret;

  ret = lib_stream_puts(&notestream->filestream, note, len);
  return ret < 0 ? ret : 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int notestream_register_file(FAR const char *path)
{
  FAR struct notestream_s *notestream;
#ifdef CONFIG_SCHED_INSTRUMENTATION_FILTER
  size_t len = strlen(path) + 1;
#else
  size_t len = 0;
#endif
  int ret;

  notestream = kmm_zalloc(sizeof(struct notestream_s) + len);
  if (notestream == NULL)
    {
      return -ENOMEM;
    }

#ifdef CONFIG_SCHED_INSTRUMENTATION_FILTER
  memcpy(notestream + 1, path, len);
  notestream->driver.name = (FAR const char *)(notestream + 1);
  notestream->driver.filter.mode.type_mask =
                      CONFIG_DRIVERS_NOTESTREAM_FILTER_DEFAULT_MODE;

#  ifndef CONFIG_UP
  notestream->driver.filter.mode.cpuset =
                      CONFIG_DRIVERS_NOTESTREAM_CPUSET;
#  endif
#endif

  notestream->driver.ops = &g_notestream_ops;
  ret = lib_fileoutstream_open(&notestream->filestream,
                               path, O_WRONLY, 0666);
  if (ret < 0)
    {
      kmm_free(notestream);
      return ret;
    }

  ret = note_driver_register(&notestream->driver);
  if (ret < 0)
    {
      lib_fileoutstream_close(&notestream->filestream);
      kmm_free(notestream);
    }

  return ret;
}

