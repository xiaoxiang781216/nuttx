/****************************************************************************
 * drivers/note/notelowout_driver.c
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

#include <nuttx/arch.h>
#include <nuttx/note/note_driver.h>
#include <nuttx/percpu.h>

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int notelowout_add(FAR struct note_driver_s *drv,
                          FAR const void *note, size_t len,
                          bool noswitches);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct note_driver_ops_s g_notelowout_ops =
{
  notelowout_add
};

/****************************************************************************
 * Public Data
 ****************************************************************************/

DEFINE_PER_CPU_BMP(struct note_driver_s, g_notelowout) =
{
#ifdef CONFIG_SCHED_INSTRUMENTATION_FILTER
  "lowerout",
  {
    {
      CONFIG_DRIVERS_NOTELOWEROUT_FILTER_DEFAULT_MODE,
#  ifndef CONFIG_UP
      CONFIG_DRIVERS_NOTELOWEROUT_CPUSET
#  endif
    },

#  ifdef CONFIG_SCHED_INSTRUMENTATION_DUMP
    {
      CONFIG_DRIVERS_NOTE_DEFAULT_LEVEL,
    },
#  endif
  },
#endif
  &g_notelowout_ops
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int notelowout_add(FAR struct note_driver_s *drv,
                          FAR const void *note, size_t len,
                          bool noswitches)
{
  FAR const unsigned char *p = (FAR const unsigned char *)note;
  size_t i;

  for (i = 0; i < len; i++)
    {
      up_lowputc(p[i]);
    }

  return 0;
}
