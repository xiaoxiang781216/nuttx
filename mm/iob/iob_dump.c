/****************************************************************************
 * mm/iob/iob_dump.c
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

#include <stdarg.h>
#include <stdint.h>
#include <sys/param.h>
#include <debug.h>

#include <nuttx/mm/iob.h>
#include <nuttx/mm/mempool.h>

#include "iob.h"

#ifdef CONFIG_DEBUG_FEATURES

/****************************************************************************
 * Pre-processor definitions
 ****************************************************************************/

#define IOB_NITEMS   32                    /* 32 bytes displayed per line */
#define IOB_LINESIZE (3 * IOB_NITEMS + 4)  /* 2 hex chars, ASCII char, 3 spaces, NUL */

/* Select the owner string for the dump line.  syslog() is a function-like
 * macro, so a #ifdef inside its argument list is UB and MSVC rejects it;
 * resolve the CONFIG_DEBUG_IOB choice here instead.
 */

#ifdef CONFIG_DEBUG_IOB
#  define IOB_OWNER_STR(iob) \
     ((iob)->io_owner[0] != '\0' ? (iob)->io_owner : "(unowned)")
#else
#  define IOB_OWNER_STR(iob) "(n/a)"
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* iob_foreach() invokes the callback while the mempool spinlock is held with
 * interrupts disabled, so the callback must not call syslog() (it may block
 * on the console TX semaphore, which cannot be posted with IRQs disabled,
 * and it would sleep while holding a spinlock on SMP).  The callback only
 * records the IOB pointers here; the actual syslog output is emitted after
 * iob_foreach() returns and the lock has been released.
 *
 * A single static array is sufficient because iob_dump_all() is serialized
 * by its rate-limited caller (only one dump is in flight at a time).
 */

static FAR struct iob_s *g_iob_dump_record[CONFIG_IOB_DUMP_MAX_ENTRIES + 1];

/****************************************************************************
 * Name: iob_nibble
 *
 * Description:
 *  Convert a binary nibble to a hexadecimal character.
 *
 ****************************************************************************/

static char iob_nibble(unsigned char nibble)
{
  if (nibble < 10)
    {
      return '0' + nibble;
    }
  else
    {
      return 'a' + nibble - 10;
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: iob_dump
 *
 * Description:
 *   Dump the contents of a I/O buffer chain
 *
 ****************************************************************************/

void iob_dump(FAR const char *msg, FAR struct iob_s *iob, unsigned int len,
              unsigned int offset)
{
  FAR struct iob_s *head;
  uint8_t data[IOB_NITEMS];
  char buf[IOB_LINESIZE];
  unsigned int maxlen;
  unsigned int nbytes;
  unsigned int lndx;
  unsigned int cndx;

  head = iob;
  syslog(LOG_DEBUG, "%s: iob=%p pktlen=%d\n", msg, head, head->io_pktlen);

  /* Check if the offset is beyond the data in the I/O buffer chain */

  if (offset > head->io_pktlen)
    {
      ioberr("ERROR: offset is past the end of data: %u > %u\n",
             offset, head->io_pktlen);
      return;
    }

  /* Dump I/O buffer headers */

  for (; iob; iob = IOB_LINK_GET(iob))
    {
      syslog(LOG_DEBUG, "  iob=%p len=%d offset=%d\n",
             iob, iob->io_len, iob->io_offset);
    }

  /* Get the amount of data to be displayed, limited by the amount that we
   * have beyond the offset.
   */

  maxlen = head->io_pktlen - offset;
  len = MIN(len, maxlen);

  /* Then beginning printing with the buffer containing the offset in groups
   * of IOB_NITEMS bytes.
   */

  for (lndx = 0; lndx < len; lndx += IOB_NITEMS, offset += IOB_NITEMS)
    {
      /* Copy IOB_NITEMS-bytes into our local buffer from the current
       * offset
       */

      nbytes = iob_copyout(data, head, IOB_NITEMS, offset);

      /* Make sure that we have something to print */

      if (nbytes > 0)
        {
          FAR char *ptr = buf;

          for (cndx = 0; cndx < IOB_NITEMS; cndx++)
            {
              if (cndx == IOB_NITEMS / 2)
                {
                  *ptr++ = ' ';
                }

              if ((lndx + cndx) < len)
                {
                  *ptr++ = iob_nibble((data[cndx] >> 4) & 0xf);
                  *ptr++ = iob_nibble(data[cndx] & 0xf);
                }
              else
                {
                  *ptr++ = ' ';
                  *ptr++ = ' ';
                }
            }

          *ptr++ = ' ';
          for (cndx = 0; cndx < IOB_NITEMS; cndx++)
            {
              if (cndx == IOB_NITEMS / 2)
                {
                  *ptr++ = ' ';
                }

              if ((lndx + cndx) < len)
                {
                  if (data[cndx] >= 0x20 && data[cndx] < 0x7f)
                    {
                      *ptr++ = data[cndx];
                    }
                  else
                    {
                      *ptr++ = '.';
                    }
                }
            }

          *ptr = '\0';
          syslog(LOG_DEBUG, "  %04x: %s\n", offset, buf);
        }
    }
}

/****************************************************************************
 * Name: iob_dump_cb
 ****************************************************************************/

static void iob_dump_cb(FAR struct iob_s *iob, FAR void *arg)
{
  FAR int *total = arg;

  /* Count every IOB so the summary line reports the true total, but only
   * record up to the cap.  This runs with the pool spinlock held and
   * interrupts disabled, so it must NOT call syslog(); it only records the
   * IOB pointer to be printed later, after the lock has been released.
   */

  if (*total >= 0 && *total < CONFIG_IOB_DUMP_MAX_ENTRIES)
    {
      g_iob_dump_record[*total] = iob;
    }

  (*total)++;
}

/****************************************************************************
 * Name: iob_dump_all
 *
 * Description:
 *   Emit a syslog dump of every IOB pool's usage counters followed by every
 *   currently-allocated IOB.  The variadic arguments form a printf-style
 *   header line describing why the dump was requested.
 *
 *   WARNING: This function is not reentrant.  It records the IOBs into a
 *   single shared static buffer (g_iob_dump_record), so it must not be
 *   called concurrently.  The rate-limited caller in iob_alloc.c ensures
 *   only one dump is in flight at a time.
 *
 ****************************************************************************/

void iob_dump_all(FAR const char *fmt, ...)
{
  struct mempoolinfo_s minfo;
  va_list ap;
  int total = 0;
  int nrecord;
  int i;

  va_start(ap, fmt);
  vsyslog(LOG_ERR, fmt, ap);
  va_end(ap);

  syslog(LOG_ERR,
         "%5s %10s %8s %8s %8s %8s %8s\n",
         "pool", "blocksize", "total", "used", "free", "waiter", "throttle");

  for (i = 0; i < CONFIG_IOB_NPOOLS; i++)
    {
      if (iob_pool_info(i, &minfo) < 0)
        {
          continue;
        }

      syslog(LOG_ERR, "  #%d %10lu %8lu %8lu %8lu %8lu %8lu\n",
             i,
             minfo.sizeblks,
             minfo.aordblks + minfo.ordblks,
             minfo.aordblks,
             minfo.ordblks,
             minfo.nwaiter,
             minfo.throttle);
    }

  /* Record the IOB pointers under the pool lock (no syslog here). */

  iob_foreach(iob_dump_cb, &total);

  /* Now that the lock has been released, emit the recorded IOBs. */

  nrecord = MIN(total, CONFIG_IOB_DUMP_MAX_ENTRIES);

  for (i = 0; i < nrecord; i++)
    {
      FAR struct iob_s *iob = g_iob_dump_record[i];

      syslog(LOG_ERR,
             "  iob #%d %p pktlen=%u len=%u off=%u next=%p owner=[%s]\n",
             i, iob, iob->io_pktlen, iob->io_len,
             iob->io_offset, IOB_LINK_GET(iob),
             IOB_OWNER_STR(iob));
    }

  if (total == 0)
    {
      syslog(LOG_ERR, "  (no active IOBs)\n");
    }
  else if (total > CONFIG_IOB_DUMP_MAX_ENTRIES)
    {
      syslog(LOG_ERR,
             "  ... (truncated: %d more iob(s) not shown, cap=%d)\n",
             total - CONFIG_IOB_DUMP_MAX_ENTRIES,
             CONFIG_IOB_DUMP_MAX_ENTRIES);
    }

  syslog(LOG_ERR, "IOB dump end: iobs_allocated=%d\n", total);
}

#endif /* CONFIG_DEBUG_FEATURES */
