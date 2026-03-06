/****************************************************************************
 * mm/iob/iob.h
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

#ifndef __MM_IOB_IOB_H
#define __MM_IOB_IOB_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <debug.h>

#include <nuttx/mm/iob.h>
#include <nuttx/mm/mempool.h>

#ifdef CONFIG_MM_IOB

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#if defined(CONFIG_DEBUG_FEATURES) && defined(CONFIG_IOB_DEBUG)
#  ifdef CONFIG_SYSLOG_NETDEV
#    define ioberr               lowsyslog
#    define iobwarn              lowsyslog
#    define iobinfo              lowsyslog
#  else
#    define ioberr               _err
#    define iobwarn              _warn
#    define iobinfo              _info
#  endif
#else
#  define ioberr                 _none
#  define iobwarn                _none
#  define iobinfo                _none
#endif /* CONFIG_DEBUG_FEATURES && CONFIG_IOB_DEBUG */

/****************************************************************************
 * Public Data
 ****************************************************************************/

/* Multiple mempool for I/O buffers */

extern FAR struct mempool_s *g_iob_pool[];

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: iob_notifier_signal
 *
 * Description:
 *   An IOB has become available.  Signal all threads waiting for an IOB
 *   that an IOB is available.
 *
 *   When an IOB becomes available, *all* of the waiters in this thread will
 *   be signaled.  If there are multiple waiters then only the highest
 *   priority thread will get the IOB.  Lower priority threads will need to
 *   call iob_notifier_setup() once again.
 *
 * Input Parameters:
 *   None.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

#ifdef CONFIG_IOB_NOTIFIER
void iob_notifier_signal(void);
#endif

/****************************************************************************
 * Name: iob_find_mempool
 *
 * Description:
 *  Allocate an I/O buffer by taking the buffer at the head of the free list.
 *  This wait will be terminated when the specified timeout expires.
 *
 * Input Parameters:
 *   throttled  - An indication of the IOB allocation is "throttled"
 *   timeout    - Timeout value in milliseconds.
 *
 ****************************************************************************/

static inline_function
FAR struct mempool_s *iob_find_mempool(size_t size)
{
  int i = CONFIG_IOB_NPOOLS - 1;

  if (size > CONFIG_IOB_BUFSIZE)
    {
      return NULL;
    }
  else if (CONFIG_IOB_NPOOLS == 1)
    {
      return g_iob_pool[0];
    }

  do
    {
      if (IOB_ALIGN_SIZE(size) <= g_iob_pool[i]->blocksize)
        {
          return g_iob_pool[i];
        }
    }
  while (--i >= 0);

  return NULL;
}

#endif /* CONFIG_MM_IOB */
#endif /* __MM_IOB_IOB_H */
