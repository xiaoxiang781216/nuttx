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

#define IOBPOOL_DEFINE(name, blksize, blkcnt, ...) \
  MEMPOOL_DEFINE_WITH_THROTTLE_ALIGN_SECTION(name, \
                                             (FAR void *)(uintptr_t)blksize, \
                                             IOB_ALIGN_SIZE(blksize), \
                                             blkcnt, 0, 0, \
                                             CONFIG_IOB_THROTTLE, \
                                             IOB_ALIGNMENT, \
                                             NULL, NULL, ##__VA_ARGS__)

#if CONFIG_IOB_EXTRA_HEADS > 0
/* Head pools carry no payload-size tag (priv = NULL) and no throttle.
 * The variadic last argument is forwarded as the section name so each
 * CPU's head pool can live in the same memory section as its level-1
 * data pool when CONFIG_IOB_SECTION1_CPUn (or CONFIG_IOB_SECTION) is
 * configured.
 */

#  define IOBPOOL_HEAD_DEFINE(name, blkcnt, ...) \
  MEMPOOL_DEFINE_WITH_THROTTLE_ALIGN_SECTION(name, NULL, \
                               IOB_ALIGN_SIZE(sizeof(struct iob_s)), \
                               blkcnt, 0, 0, 0, IOB_ALIGNMENT, ##__VA_ARGS__)
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

#ifdef CONFIG_BMP

/* One static mempool per (cpu, level). Each is sized/sectioned per-CPU
 * via Kconfig CONFIG_IOB_{BUFSIZE,NBUFFERS,SECTION}_CPUn[_2|_3].
 */

/* IOB_SECTION_CPUn[_x] is defined by Make.defs/CMakeLists.txt only when
 * CONFIG_IOB_SECTION_CPUn[_x] is non-empty, mirroring the non-BMP
 * IOB_SECTION handling. The #  ifdef guards below pick the with-section or
 * no-section form of IOBPOOL_DEFINE accordingly.
 */

#  if CONFIG_NCPUS > 0
#    ifdef IOB_SECTION1_CPU0
IOBPOOL_DEFINE(g_iob1_cpu0, CONFIG_IOB_BUFSIZE1_CPU0,
               CONFIG_IOB_NBUFFERS1_CPU0, IOB_SECTION1_CPU0);
#    else
IOBPOOL_DEFINE(g_iob1_cpu0, CONFIG_IOB_BUFSIZE1_CPU0,
               CONFIG_IOB_NBUFFERS1_CPU0);
#    endif
#    if CONFIG_IOB_NPOOLS_CPU0 > 1
#      ifdef IOB_SECTION2_CPU0
IOBPOOL_DEFINE(g_iob2_cpu0, CONFIG_IOB_BUFSIZE2_CPU0,
               CONFIG_IOB_NBUFFERS2_CPU0, IOB_SECTION2_CPU0);
#      else
IOBPOOL_DEFINE(g_iob2_cpu0, CONFIG_IOB_BUFSIZE2_CPU0,
               CONFIG_IOB_NBUFFERS2_CPU0);
#      endif
#    endif
#    if CONFIG_IOB_NPOOLS_CPU0 > 2
#      ifdef IOB_SECTION3_CPU0
IOBPOOL_DEFINE(g_iob3_cpu0, CONFIG_IOB_BUFSIZE3_CPU0,
               CONFIG_IOB_NBUFFERS3_CPU0, IOB_SECTION3_CPU0);
#      else
IOBPOOL_DEFINE(g_iob3_cpu0, CONFIG_IOB_BUFSIZE3_CPU0,
               CONFIG_IOB_NBUFFERS3_CPU0);
#      endif
#    endif
#  endif /* CONFIG_NCPUS > 0 */

#  if CONFIG_IOB_EXTRA_HEADS_CPU0 > 0
#    ifdef IOB_SECTION1_CPU0
IOBPOOL_HEAD_DEFINE(g_iob_head_cpu0, CONFIG_IOB_EXTRA_HEADS_CPU0,
                    IOB_SECTION1_CPU0);
#    else
IOBPOOL_HEAD_DEFINE(g_iob_head_cpu0, CONFIG_IOB_EXTRA_HEADS_CPU0);
#    endif
#  endif

#  if CONFIG_NCPUS > 1 && CONFIG_IOB_NPOOLS_CPU1 > 0
#    ifdef IOB_SECTION1_CPU1
IOBPOOL_DEFINE(g_iob1_cpu1, CONFIG_IOB_BUFSIZE1_CPU1,
               CONFIG_IOB_NBUFFERS1_CPU1, IOB_SECTION1_CPU1);
#    else
IOBPOOL_DEFINE(g_iob1_cpu1, CONFIG_IOB_BUFSIZE1_CPU1,
               CONFIG_IOB_NBUFFERS1_CPU1);
#    endif
#    if CONFIG_IOB_NPOOLS_CPU1 > 1
#      ifdef IOB_SECTION2_CPU1
IOBPOOL_DEFINE(g_iob2_cpu1, CONFIG_IOB_BUFSIZE2_CPU1,
               CONFIG_IOB_NBUFFERS2_CPU1, IOB_SECTION2_CPU1);
#      else
IOBPOOL_DEFINE(g_iob2_cpu1, CONFIG_IOB_BUFSIZE2_CPU1,
               CONFIG_IOB_NBUFFERS2_CPU1);
#      endif
#    endif
#    if CONFIG_IOB_NPOOLS_CPU1 > 2
#      ifdef IOB_SECTION3_CPU1
IOBPOOL_DEFINE(g_iob3_cpu1, CONFIG_IOB_BUFSIZE3_CPU1,
               CONFIG_IOB_NBUFFERS3_CPU1, IOB_SECTION3_CPU1);
#      else
IOBPOOL_DEFINE(g_iob3_cpu1, CONFIG_IOB_BUFSIZE3_CPU1,
               CONFIG_IOB_NBUFFERS3_CPU1);
#      endif
#    endif
#  endif /* CONFIG_NCPUS > 1 && CONFIG_IOB_NPOOLS_CPU1 > 0 */

#  if CONFIG_NCPUS > 1 && CONFIG_IOB_EXTRA_HEADS_CPU1 > 0
#    ifdef IOB_SECTION1_CPU1
IOBPOOL_HEAD_DEFINE(g_iob_head_cpu1, CONFIG_IOB_EXTRA_HEADS_CPU1,
                    IOB_SECTION1_CPU1);
#    else
IOBPOOL_HEAD_DEFINE(g_iob_head_cpu1, CONFIG_IOB_EXTRA_HEADS_CPU1);
#    endif
#  endif

#  if CONFIG_NCPUS > 2 && CONFIG_IOB_NPOOLS_CPU2 > 0
#    ifdef IOB_SECTION1_CPU2
IOBPOOL_DEFINE(g_iob1_cpu2, CONFIG_IOB_BUFSIZE1_CPU2,
               CONFIG_IOB_NBUFFERS1_CPU2, IOB_SECTION1_CPU2);
#    else
IOBPOOL_DEFINE(g_iob1_cpu2, CONFIG_IOB_BUFSIZE1_CPU2,
               CONFIG_IOB_NBUFFERS1_CPU2);
#    endif
#    if CONFIG_IOB_NPOOLS_CPU2 > 1
#      ifdef IOB_SECTION2_CPU2
IOBPOOL_DEFINE(g_iob2_cpu2, CONFIG_IOB_BUFSIZE2_CPU2,
               CONFIG_IOB_NBUFFERS2_CPU2, IOB_SECTION2_CPU2);
#      else
IOBPOOL_DEFINE(g_iob2_cpu2, CONFIG_IOB_BUFSIZE2_CPU2,
               CONFIG_IOB_NBUFFERS2_CPU2);
#      endif
#    endif
#    if CONFIG_IOB_NPOOLS_CPU2 > 2
#      ifdef IOB_SECTION3_CPU2
IOBPOOL_DEFINE(g_iob3_cpu2, CONFIG_IOB_BUFSIZE3_CPU2,
               CONFIG_IOB_NBUFFERS3_CPU2, IOB_SECTION3_CPU2);
#      else
IOBPOOL_DEFINE(g_iob3_cpu2, CONFIG_IOB_BUFSIZE3_CPU2,
               CONFIG_IOB_NBUFFERS3_CPU2);
#      endif
#    endif
#  endif /* CONFIG_NCPUS > 2 && CONFIG_IOB_NPOOLS_CPU2 > 0 */

#  if CONFIG_NCPUS > 2 && CONFIG_IOB_EXTRA_HEADS_CPU2 > 0
#    ifdef IOB_SECTION1_CPU2
IOBPOOL_HEAD_DEFINE(g_iob_head_cpu2, CONFIG_IOB_EXTRA_HEADS_CPU2,
                    IOB_SECTION1_CPU2);
#    else
IOBPOOL_HEAD_DEFINE(g_iob_head_cpu2, CONFIG_IOB_EXTRA_HEADS_CPU2);
#    endif
#  endif

#  if CONFIG_NCPUS > 3 && CONFIG_IOB_NPOOLS_CPU3 > 0
#    ifdef IOB_SECTION1_CPU3
IOBPOOL_DEFINE(g_iob1_cpu3, CONFIG_IOB_BUFSIZE1_CPU3,
               CONFIG_IOB_NBUFFERS1_CPU3, IOB_SECTION1_CPU3);
#    else
IOBPOOL_DEFINE(g_iob1_cpu3, CONFIG_IOB_BUFSIZE1_CPU3,
               CONFIG_IOB_NBUFFERS1_CPU3);
#    endif
#    if CONFIG_IOB_NPOOLS_CPU3 > 1
#      ifdef IOB_SECTION2_CPU3
IOBPOOL_DEFINE(g_iob2_cpu3, CONFIG_IOB_BUFSIZE2_CPU3,
               CONFIG_IOB_NBUFFERS2_CPU3, IOB_SECTION2_CPU3);
#      else
IOBPOOL_DEFINE(g_iob2_cpu3, CONFIG_IOB_BUFSIZE2_CPU3,
               CONFIG_IOB_NBUFFERS2_CPU3);
#      endif
#    endif
#    if CONFIG_IOB_NPOOLS_CPU3 > 2
#      ifdef IOB_SECTION3_CPU3
IOBPOOL_DEFINE(g_iob3_cpu3, CONFIG_IOB_BUFSIZE3_CPU3,
               CONFIG_IOB_NBUFFERS3_CPU3, IOB_SECTION3_CPU3);
#      else
IOBPOOL_DEFINE(g_iob3_cpu3, CONFIG_IOB_BUFSIZE3_CPU3,
               CONFIG_IOB_NBUFFERS3_CPU3);
#      endif
#    endif
#  endif /* CONFIG_NCPUS > 3 && CONFIG_IOB_NPOOLS_CPU3 > 0 */

#  if CONFIG_NCPUS > 3 && CONFIG_IOB_EXTRA_HEADS_CPU3 > 0
#    ifdef IOB_SECTION1_CPU3
IOBPOOL_HEAD_DEFINE(g_iob_head_cpu3, CONFIG_IOB_EXTRA_HEADS_CPU3,
                    IOB_SECTION1_CPU3);
#    else
IOBPOOL_HEAD_DEFINE(g_iob_head_cpu3, CONFIG_IOB_EXTRA_HEADS_CPU3);
#    endif
#  endif

#  if CONFIG_NCPUS > 4 && CONFIG_IOB_NPOOLS_CPU4 > 0
#    ifdef IOB_SECTION1_CPU4
IOBPOOL_DEFINE(g_iob1_cpu4, CONFIG_IOB_BUFSIZE1_CPU4,
               CONFIG_IOB_NBUFFERS1_CPU4, IOB_SECTION1_CPU4);
#    else
IOBPOOL_DEFINE(g_iob1_cpu4, CONFIG_IOB_BUFSIZE1_CPU4,
               CONFIG_IOB_NBUFFERS1_CPU4);
#    endif
#    if CONFIG_IOB_NPOOLS_CPU4 > 1
#      ifdef IOB_SECTION2_CPU4
IOBPOOL_DEFINE(g_iob2_cpu4, CONFIG_IOB_BUFSIZE2_CPU4,
               CONFIG_IOB_NBUFFERS2_CPU4, IOB_SECTION2_CPU4);
#      else
IOBPOOL_DEFINE(g_iob2_cpu4, CONFIG_IOB_BUFSIZE2_CPU4,
               CONFIG_IOB_NBUFFERS2_CPU4);
#      endif
#    endif
#    if CONFIG_IOB_NPOOLS_CPU4 > 2
#      ifdef IOB_SECTION3_CPU4
IOBPOOL_DEFINE(g_iob3_cpu4, CONFIG_IOB_BUFSIZE3_CPU4,
               CONFIG_IOB_NBUFFERS3_CPU4, IOB_SECTION3_CPU4);
#      else
IOBPOOL_DEFINE(g_iob3_cpu4, CONFIG_IOB_BUFSIZE3_CPU4,
               CONFIG_IOB_NBUFFERS3_CPU4);
#      endif
#    endif
#  endif /* CONFIG_NCPUS > 4 && CONFIG_IOB_NPOOLS_CPU4 > 0 */

#  if CONFIG_NCPUS > 4 && CONFIG_IOB_EXTRA_HEADS_CPU4 > 0
#    ifdef IOB_SECTION1_CPU4
IOBPOOL_HEAD_DEFINE(g_iob_head_cpu4, CONFIG_IOB_EXTRA_HEADS_CPU4,
                    IOB_SECTION1_CPU4);
#    else
IOBPOOL_HEAD_DEFINE(g_iob_head_cpu4, CONFIG_IOB_EXTRA_HEADS_CPU4);
#    endif
#  endif

#  if CONFIG_NCPUS > 5 && CONFIG_IOB_NPOOLS_CPU5 > 0
#    ifdef IOB_SECTION1_CPU5
IOBPOOL_DEFINE(g_iob1_cpu5, CONFIG_IOB_BUFSIZE1_CPU5,
               CONFIG_IOB_NBUFFERS1_CPU5, IOB_SECTION1_CPU5);
#    else
IOBPOOL_DEFINE(g_iob1_cpu5, CONFIG_IOB_BUFSIZE1_CPU5,
               CONFIG_IOB_NBUFFERS1_CPU5);
#    endif
#    if CONFIG_IOB_NPOOLS_CPU5 > 1
#      ifdef IOB_SECTION2_CPU5
IOBPOOL_DEFINE(g_iob2_cpu5, CONFIG_IOB_BUFSIZE2_CPU5,
               CONFIG_IOB_NBUFFERS2_CPU5, IOB_SECTION2_CPU5);
#      else
IOBPOOL_DEFINE(g_iob2_cpu5, CONFIG_IOB_BUFSIZE2_CPU5,
               CONFIG_IOB_NBUFFERS2_CPU5);
#      endif
#    endif
#    if CONFIG_IOB_NPOOLS_CPU5 > 2
#      ifdef IOB_SECTION3_CPU5
IOBPOOL_DEFINE(g_iob3_cpu5, CONFIG_IOB_BUFSIZE3_CPU5,
               CONFIG_IOB_NBUFFERS3_CPU5, IOB_SECTION3_CPU5);
#      else
IOBPOOL_DEFINE(g_iob3_cpu5, CONFIG_IOB_BUFSIZE3_CPU5,
               CONFIG_IOB_NBUFFERS3_CPU5);
#      endif
#    endif
#  endif /* CONFIG_NCPUS > 5 && CONFIG_IOB_NPOOLS_CPU5 > 0 */

#  if CONFIG_NCPUS > 5 && CONFIG_IOB_EXTRA_HEADS_CPU5 > 0
#    ifdef IOB_SECTION1_CPU5
IOBPOOL_HEAD_DEFINE(g_iob_head_cpu5, CONFIG_IOB_EXTRA_HEADS_CPU5,
                    IOB_SECTION1_CPU5);
#    else
IOBPOOL_HEAD_DEFINE(g_iob_head_cpu5, CONFIG_IOB_EXTRA_HEADS_CPU5);
#    endif
#  endif

#else /* !CONFIG_BMP */

/* Non-BMP: original single-pool-per-level definitions */

#  ifdef IOB_SECTION
IOBPOOL_DEFINE(g_iob1, CONFIG_IOB_BUFSIZE, CONFIG_IOB_NBUFFERS,
               IOB_SECTION);
#  else
IOBPOOL_DEFINE(g_iob1, CONFIG_IOB_BUFSIZE, CONFIG_IOB_NBUFFERS);
#  endif

#  if CONFIG_IOB_NPOOLS > 1
#    ifdef IOB_SECTION2
IOBPOOL_DEFINE(g_iob2, CONFIG_IOB_BUFSIZE2, CONFIG_IOB_NBUFFERS2,
               IOB_SECTION2);
#    else
IOBPOOL_DEFINE(g_iob2, CONFIG_IOB_BUFSIZE2, CONFIG_IOB_NBUFFERS2);
#    endif
#  endif

#  if CONFIG_IOB_NPOOLS > 2
#    ifdef IOB_SECTION3
IOBPOOL_DEFINE(g_iob3, CONFIG_IOB_BUFSIZE3, CONFIG_IOB_NBUFFERS3,
               IOB_SECTION3);
#    else
IOBPOOL_DEFINE(g_iob3, CONFIG_IOB_BUFSIZE3, CONFIG_IOB_NBUFFERS3);
#    endif
#  endif

#  if CONFIG_IOB_EXTRA_HEADS > 0
#    ifdef IOB_SECTION
IOBPOOL_HEAD_DEFINE(g_iob_head, CONFIG_IOB_EXTRA_HEADS, IOB_SECTION);
#    else
IOBPOOL_HEAD_DEFINE(g_iob_head, CONFIG_IOB_EXTRA_HEADS);
#    endif
#  endif

#endif /* CONFIG_BMP */

/****************************************************************************
 * Public Data
 ****************************************************************************/

#ifdef CONFIG_BMP

const int g_iob_nbuffers[CONFIG_NCPUS][CONFIG_IOB_NPOOLS] =
{
  {
    CONFIG_IOB_NBUFFERS1_CPU0,
#  if CONFIG_IOB_NPOOLS >= 2
    CONFIG_IOB_NBUFFERS2_CPU0,
#  endif
#  if CONFIG_IOB_NPOOLS >= 3
    CONFIG_IOB_NBUFFERS3_CPU0,
#  endif
  },
#  if CONFIG_NCPUS > 1
  {
#    if CONFIG_IOB_NPOOLS_CPU1 > 0
    CONFIG_IOB_NBUFFERS1_CPU1,
#      if CONFIG_IOB_NPOOLS >= 2
    CONFIG_IOB_NBUFFERS2_CPU1,
#      endif
#      if CONFIG_IOB_NPOOLS >= 3
    CONFIG_IOB_NBUFFERS3_CPU1,
#      endif
#    endif
  },
#  endif
#  if CONFIG_NCPUS > 2
  {
#    if CONFIG_IOB_NPOOLS_CPU2 > 0
    CONFIG_IOB_NBUFFERS1_CPU2,
#      if CONFIG_IOB_NPOOLS >= 2
    CONFIG_IOB_NBUFFERS2_CPU2,
#      endif
#      if CONFIG_IOB_NPOOLS >= 3
    CONFIG_IOB_NBUFFERS3_CPU2,
#      endif
#    endif
  },
#  endif
#  if CONFIG_NCPUS > 3
  {
#    if CONFIG_IOB_NPOOLS_CPU3 > 0
    CONFIG_IOB_NBUFFERS1_CPU3,
#      if CONFIG_IOB_NPOOLS >= 2
    CONFIG_IOB_NBUFFERS2_CPU3,
#      endif
#      if CONFIG_IOB_NPOOLS >= 3
    CONFIG_IOB_NBUFFERS3_CPU3,
#      endif
#    endif
  },
#  endif
#  if CONFIG_NCPUS > 4
  {
#    if CONFIG_IOB_NPOOLS_CPU4 > 0
    CONFIG_IOB_NBUFFERS1_CPU4,
#      if CONFIG_IOB_NPOOLS >= 2
    CONFIG_IOB_NBUFFERS2_CPU4,
#      endif
#      if CONFIG_IOB_NPOOLS >= 3
    CONFIG_IOB_NBUFFERS3_CPU4,
#      endif
#    endif
  },
#  endif
#  if CONFIG_NCPUS > 5
  {
#    if CONFIG_IOB_NPOOLS_CPU5 > 0
    CONFIG_IOB_NBUFFERS1_CPU5,
#      if CONFIG_IOB_NPOOLS >= 2
    CONFIG_IOB_NBUFFERS2_CPU5,
#      endif
#      if CONFIG_IOB_NPOOLS >= 3
    CONFIG_IOB_NBUFFERS3_CPU5,
#      endif
#    endif
  },
#  endif
};

const int g_iob_bufsize[CONFIG_NCPUS][CONFIG_IOB_NPOOLS] =
{
  {
    CONFIG_IOB_BUFSIZE1_CPU0,
#  if CONFIG_IOB_NPOOLS >= 2
    CONFIG_IOB_BUFSIZE2_CPU0,
#  endif
#  if CONFIG_IOB_NPOOLS >= 3
    CONFIG_IOB_BUFSIZE3_CPU0,
#  endif
  },
#  if CONFIG_NCPUS > 1
  {
#    if CONFIG_IOB_NPOOLS_CPU1 > 0
    CONFIG_IOB_BUFSIZE1_CPU1,
#      if CONFIG_IOB_NPOOLS >= 2
    CONFIG_IOB_BUFSIZE2_CPU1,
#      endif
#      if CONFIG_IOB_NPOOLS >= 3
    CONFIG_IOB_BUFSIZE3_CPU1,
#      endif
#    endif
  },
#  endif
#  if CONFIG_NCPUS > 2
  {
#    if CONFIG_IOB_NPOOLS_CPU2 > 0
    CONFIG_IOB_BUFSIZE1_CPU2,
#      if CONFIG_IOB_NPOOLS >= 2
    CONFIG_IOB_BUFSIZE2_CPU2,
#      endif
#      if CONFIG_IOB_NPOOLS >= 3
    CONFIG_IOB_BUFSIZE3_CPU2,
#      endif
#    endif
  },
#  endif
#  if CONFIG_NCPUS > 3
  {
#    if CONFIG_IOB_NPOOLS_CPU3 > 0
    CONFIG_IOB_BUFSIZE1_CPU3,
#      if CONFIG_IOB_NPOOLS >= 2
    CONFIG_IOB_BUFSIZE2_CPU3,
#      endif
#      if CONFIG_IOB_NPOOLS >= 3
    CONFIG_IOB_BUFSIZE3_CPU3,
#      endif
#    endif
  },
#  endif
#  if CONFIG_NCPUS > 4
  {
#    if CONFIG_IOB_NPOOLS_CPU4 > 0
    CONFIG_IOB_BUFSIZE1_CPU4,
#      if CONFIG_IOB_NPOOLS >= 2
    CONFIG_IOB_BUFSIZE2_CPU4,
#      endif
#      if CONFIG_IOB_NPOOLS >= 3
    CONFIG_IOB_BUFSIZE3_CPU4,
#      endif
#    endif
  },
#  endif
#  if CONFIG_NCPUS > 5
  {
#    if CONFIG_IOB_NPOOLS_CPU5 > 0
    CONFIG_IOB_BUFSIZE1_CPU5,
#      if CONFIG_IOB_NPOOLS >= 2
    CONFIG_IOB_BUFSIZE2_CPU5,
#      endif
#      if CONFIG_IOB_NPOOLS >= 3
    CONFIG_IOB_BUFSIZE3_CPU5,
#      endif
#    endif
  },
#  endif
};

/* Pool pointer table indexed by [cpu][level]. Missing levels are NULL.
 * iob_find_mempool() skips NULL and blocksize==0 entries so unused slots
 * are transparent to the allocator.
 */

FAR struct mempool_s *g_iob_cpu_pool[CONFIG_NCPUS][CONFIG_IOB_NPOOLS] =
{
#  if CONFIG_NCPUS > 0
    {
      &g_iob1_cpu0,
#    if CONFIG_IOB_NPOOLS_CPU0 > 1
      &g_iob2_cpu0,
#    endif
#    if CONFIG_IOB_NPOOLS_CPU0 > 2
      &g_iob3_cpu0,
#    endif
    },
#  endif
#  if CONFIG_NCPUS > 1
    {
#    if CONFIG_IOB_NPOOLS_CPU1 > 0
      &g_iob1_cpu1,
#      if CONFIG_IOB_NPOOLS_CPU1 > 1
      &g_iob2_cpu1,
#      endif
#      if CONFIG_IOB_NPOOLS_CPU1 > 2
      &g_iob3_cpu1,
#      endif
#    endif
    },
#  endif
#  if CONFIG_NCPUS > 2
    {
#    if CONFIG_IOB_NPOOLS_CPU2 > 0
      &g_iob1_cpu2,
#      if CONFIG_IOB_NPOOLS_CPU2 > 1
      &g_iob2_cpu2,
#      endif
#      if CONFIG_IOB_NPOOLS_CPU2 > 2
      &g_iob3_cpu2,
#      endif
#    endif
    },
#  endif
#  if CONFIG_NCPUS > 3
    {
#    if CONFIG_IOB_NPOOLS_CPU3 > 0
      &g_iob1_cpu3,
#      if CONFIG_IOB_NPOOLS_CPU3 > 1
      &g_iob2_cpu3,
#      endif
#      if CONFIG_IOB_NPOOLS_CPU3 > 2
      &g_iob3_cpu3,
#      endif
#    endif
    },
#  endif
#  if CONFIG_NCPUS > 4
    {
#    if CONFIG_IOB_NPOOLS_CPU4 > 0
      &g_iob1_cpu4,
#      if CONFIG_IOB_NPOOLS_CPU4 > 1
      &g_iob2_cpu4,
#      endif
#      if CONFIG_IOB_NPOOLS_CPU4 > 2
      &g_iob3_cpu4,
#      endif
#    endif
    },
#  endif
#  if CONFIG_NCPUS > 5
    {
#    if CONFIG_IOB_NPOOLS_CPU5 > 0
      &g_iob1_cpu5,
#      if CONFIG_IOB_NPOOLS_CPU5 > 1
      &g_iob2_cpu5,
#      endif
#      if CONFIG_IOB_NPOOLS_CPU5 > 2
      &g_iob3_cpu5,
#      endif
#    endif
    },
#  endif
};

#else /* !CONFIG_BMP */

FAR struct mempool_s *g_iob_pool[CONFIG_IOB_NPOOLS] =
{
  &g_iob1,
#  if CONFIG_IOB_NPOOLS > 1
  &g_iob2,
#  endif
#  if CONFIG_IOB_NPOOLS > 2
  &g_iob3,
#  endif
};

#endif /* CONFIG_BMP */

#if CONFIG_IOB_EXTRA_HEADS > 0
#  ifdef CONFIG_BMP

/* Per-CPU head pool pointer table; CPUs without a head pool are NULL.
 * g_iob_head_pool alloc/release index this with this_cpu() exactly the
 * way they index g_iob_cpu_pool.  iob_alloc_internal() / iob_free() must
 * never cross-CPU access these pools — the caller pins to the current CPU
 * (same constraint that already applies to the data pools).
 */

FAR struct mempool_s *g_iob_head_cpu_pool[CONFIG_NCPUS] =
{
#    if CONFIG_NCPUS > 0
#      if CONFIG_IOB_EXTRA_HEADS_CPU0 > 0
  &g_iob_head_cpu0,
#      else
  NULL,
#      endif
#    endif
#    if CONFIG_NCPUS > 1
#      if CONFIG_IOB_EXTRA_HEADS_CPU1 > 0
  &g_iob_head_cpu1,
#      else
  NULL,
#      endif
#    endif
#    if CONFIG_NCPUS > 2
#      if CONFIG_IOB_EXTRA_HEADS_CPU2 > 0
  &g_iob_head_cpu2,
#      else
  NULL,
#      endif
#    endif
#    if CONFIG_NCPUS > 3
#      if CONFIG_IOB_EXTRA_HEADS_CPU3 > 0
  &g_iob_head_cpu3,
#      else
  NULL,
#      endif
#    endif
#    if CONFIG_NCPUS > 4
#      if CONFIG_IOB_EXTRA_HEADS_CPU4 > 0
  &g_iob_head_cpu4,
#      else
  NULL,
#      endif
#    endif
#    if CONFIG_NCPUS > 5
#      if CONFIG_IOB_EXTRA_HEADS_CPU5 > 0
  &g_iob_head_cpu5,
#      else
  NULL,
#      endif
#    endif
};

#  else /* !CONFIG_BMP */

FAR struct mempool_s *g_iob_head_pool = &g_iob_head;

#  endif /* CONFIG_BMP */
#endif /* CONFIG_IOB_EXTRA_HEADS > 0 */

/****************************************************************************
 * Public Functions
 ****************************************************************************/
