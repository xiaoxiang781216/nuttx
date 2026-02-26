/****************************************************************************
 * mm/iob/iob_initialize.c
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

#include <nuttx/mm/iob.h>
#include <nuttx/mm/mempool.h>

#include "iob.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifdef IOB_SECTION
#  define IOBPOOL_DEFINE(name, blksize, blkcnt) \
  MEMPOOL_DEFINE_WITH_THROTTLE_ALIGN_SECTION(name, IOB_ALIGN_SIZE(blksize), \
                                             blkcnt, 0, 0, \
                                             CONFIG_IOB_THROTTLE, \
                                             IOB_ALIGNMENT, IOB_SECTION);
#else
#  define IOBPOOL_DEFINE(name, blksize, blkcnt) \
  MEMPOOL_DEFINE_WITH_THROTTLE_ALIGN_SECTION(name, IOB_ALIGN_SIZE(blksize), \
                                             blkcnt, 0, 0, \
                                             CONFIG_IOB_THROTTLE, \
                                             IOB_ALIGNMENT);
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Static buffer for IOB mempool. */

IOBPOOL_DEFINE(g_iob1, CONFIG_IOB_BUFSIZE, CONFIG_IOB_NBUFFERS);
#if CONFIG_IOB_NPOOLS > 1
IOBPOOL_DEFINE(g_iob2, CONFIG_IOB_BUFSIZE2, CONFIG_IOB_NBUFFERS2);
#endif
#if CONFIG_IOB_NPOOLS > 2
IOBPOOL_DEFINE(g_iob3, CONFIG_IOB_BUFSIZE3, CONFIG_IOB_NBUFFERS3);
#endif

/****************************************************************************
 * Public Data
 ****************************************************************************/

/* Multiple mempool for I/O buffers */

FAR struct mempool_s *g_iob_pool[CONFIG_IOB_NPOOLS] =
{
  &g_iob1,
#if CONFIG_IOB_NPOOLS > 1
  &g_iob2,
#endif
#if CONFIG_IOB_NPOOLS > 2
  &g_iob3,
#endif
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/
