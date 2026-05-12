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

#include <fcntl.h>
#include <sys/stat.h>

#include <nuttx/atomic.h>
#include <nuttx/fs/fs.h>
#include <nuttx/kmalloc.h>
#include <nuttx/streams.h>
#include <nuttx/note/note_driver.h>

#include "note_driver.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define NOTESTREAM_IDLE     0
#define NOTESTREAM_OPENING  1
#define NOTESTREAM_READY    2

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct notestream_s
{
  struct note_driver_s driver;
  FAR char *path;
  atomic_t state;
  struct lib_fileoutstream_s filestream;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int notestream_add(FAR struct note_driver_s *drv,
                          FAR const void *note, size_t len,
                          bool noswitches);
static void notestream_flush(FAR struct note_driver_s *drv);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct note_driver_ops_s g_notestream_ops =
{
  .add = notestream_add,
  .flush = notestream_flush,
};

/****************************************************************************
 * Public Data
 ****************************************************************************/

DEFINE_PER_CPU_BMP(struct notestream_s, g_notestream) =
{
  {
#ifdef CONFIG_SCHED_INSTRUMENTATION_FILTER
    "stream",
    {
      {
        CONFIG_DRIVERS_NOTESTREAM_FILTER_DEFAULT_MODE,
#  ifndef CONFIG_UP
        CONFIG_DRIVERS_NOTESTREAM_CPUSET,
#  endif
      },
#  ifdef CONFIG_SCHED_INSTRUMENTATION_DUMP
      {
        CONFIG_DRIVERS_NOTE_DEFAULT_LEVEL,
      },
#  endif
    },
#endif
    &g_notestream_ops,
  },
  (FAR char *)CONFIG_DRIVERS_NOTESTREAM_PATH,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void notestream_flush(FAR struct note_driver_s *drv)
{
  FAR struct notestream_s *stream = (FAR struct notestream_s *)drv;

  if (atomic_read(&stream->state) == NOTESTREAM_READY)
    {
      file_fsync(&stream->filestream.file);
    }
}

static int notestream_add(FAR struct note_driver_s *drv,
                          FAR const void *note, size_t len,
                          bool noswitches)
{
  FAR struct notestream_s *stream = (FAR struct notestream_s *)drv;
  int32_t expected = NOTESTREAM_IDLE;
  FAR char *slash;
  int ret;

  if (atomic_read(&stream->state) != NOTESTREAM_READY)
    {
      if (!note_is_safe(note, len, noswitches) ||
          !atomic_cmpxchg(&stream->state, &expected, NOTESTREAM_OPENING))
        {
          return -EAGAIN;
        }

      ret = lib_fileoutstream_open(&stream->filestream,
                                   stream->path,
                                   O_WRONLY | O_CREAT, 0666);
      if (ret == -ENOENT)
        {
          slash = strrchr(stream->path, '/');
          if (slash > stream->path)
            {
              *slash = '\0';
              mkdir(stream->path, 0777);
              *slash = '/';
              ret = lib_fileoutstream_open(&stream->filestream,
                                           stream->path,
                                           O_WRONLY | O_CREAT,
                                           0666);
            }
        }

      if (ret < 0)
        {
          atomic_set(&stream->state, NOTESTREAM_IDLE);
          return ret;
        }

      atomic_set(&stream->state, NOTESTREAM_READY);
    }

  ret = lib_stream_puts(&stream->filestream, note, len);
  return ret < 0 ? ret : 0;
}

