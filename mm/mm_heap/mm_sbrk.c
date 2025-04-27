/****************************************************************************
 * mm/mm_heap/mm_sbrk.c
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

#include <assert.h>
#include <errno.h>
#include <unistd.h>

#include <nuttx/mm/mm.h>
#include <nuttx/addrenv.h>
#include <nuttx/arch.h>
#include <nuttx/pgalloc.h>

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: mm_sbrk
 *
 * Description:
 *    The sbrk() function is used to change the amount of space allocated
 *    for the calling process. The change is made by resetting the process's
 *    break value and allocating the appropriate amount of space.  The amount
 *    of allocated space increases as the break value increases.
 *
 *    The sbrk() function adds 'incr' bytes to the break value and changes
 *    the allocated space accordingly. If incr is negative, the amount of
 *    allocated space is decreased by incr bytes. The current value of the
 *    program break is returned by sbrk(0).
 *
 * Input Parameters:
 *    heap - The heap to be used.
 *    incr - Specifies the number of bytes to add or to remove from the
 *      space allocated for the process.
 *    mem  - The address of the new program break.
 *
 * Returned Value:
 *    ENOMEM - The requested change would allocate more space than
 *      allowed under system limits.
 *    EAGAIN - The total amount of system memory available for allocation
 *      to this process is temporarily insufficient. This may occur even
 *      though the space requested was less than the maximum data segment
 *      size.
 *
 ****************************************************************************/

int mm_sbrk(FAR struct mm_heap_s *heap, intptr_t incr, FAR void **mem)
{
  uintptr_t brkaddr;
  uintptr_t allocbase;
  unsigned int pgincr;
  size_t bytesize;

  DEBUGASSERT(incr >= 0);
  if (incr < 0)
    {
      return -ENOSYS;
    }

  /* Get the current break address (NOTE: assumes region 0). */

  brkaddr = (uintptr_t)mm_brkaddr(heap, 0);
  if (incr > 0)
    {
      /* Convert the increment to multiples of the page size */

      pgincr = MM_NPAGES(incr);

      /* Allocate the requested number of pages and map them to the
       * break address.
       */

      allocbase = pgalloc(brkaddr, pgincr);
      if (allocbase == 0)
        {
          return -EAGAIN;
        }

      /* Extend the heap (region 0) */

      bytesize = pgincr << MM_PGSHIFT;
      mm_extend(heap, (FAR void *)allocbase, bytesize, 0);
    }

  if (mem)
    {
      *mem = (FAR void *)brkaddr;
    }

  return 0;
}
