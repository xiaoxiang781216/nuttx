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

#include <stdbool.h>

#include <nuttx/nuttx.h>
#include <nuttx/mm/iob.h>
#include <nuttx/mm/mempool.h>

#include "iob.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Fix the I/O Buffer size with specified alignment size */

#ifdef CONFIG_IOB_ALLOC
#  define IOB_ALIGN_SIZE(blksize)  ALIGN_UP(ALIGN_UP(sizeof(struct iob_s), \
                                            IOB_ALIGNMENT) + blksize, \
                                            IOB_ALIGNMENT)
#else
#  define IOB_ALIGN_SIZE(blksize)  ALIGN_UP(offsetof(struct iob_s, io_data) + \
                                            blksize, IOB_ALIGNMENT)
#endif

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

/* Following raw buffer will be divided into iob_s instances, the initial
 * procedure will ensure that the member io_data of each iob_s is aligned
 * to the IOB_ALIGNMENT memory boundary.
 */

IOBPOOL_DEFINE(g_iob1, CONFIG_IOB_BUFSIZE, CONFIG_IOB_NBUFFERS);

/****************************************************************************
 * Public Data
 ****************************************************************************/

/* Multiple mempool for I/O buffers */

FAR struct mempool_s *g_iob_pool[1] =
{
  &g_iob1,
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/
