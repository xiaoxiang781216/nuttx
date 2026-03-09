/****************************************************************************
 * fs/procfs/fs_procfsiobinfo.c
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

#include <sys/types.h>
#include <sys/stat.h>

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <debug.h>

#include <nuttx/fs/fs.h>
#include <nuttx/fs/procfs.h>
#include <nuttx/mm/iob.h>
#include <nuttx/mm/mempool.h>
#include <nuttx/net/netdev.h>

#include "fs_heap.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define IOBINFO_LINELEN   128

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct iobinfo_file_s
{
  struct procfs_file_s base;        /* Base open file structure */
  unsigned int linesize;            /* Number of valid characters in line[] */
  char line[IOBINFO_LINELEN];       /* Pre-allocated buffer for formatted lines */
};

struct iobinfo_foreach_ctx_s
{
  FAR struct iobinfo_file_s *procfile;
  FAR char **buffer;
  FAR size_t *buflen;
  FAR off_t *offset;
  FAR size_t *totalsize;
  int count;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int     iobinfo_open(FAR struct file *filep,
                            FAR const char *relpath,
                            int oflags, mode_t mode);
static int     iobinfo_close(FAR struct file *filep);
static ssize_t iobinfo_read(FAR struct file *filep, FAR char *buffer,
                            size_t buflen);

/****************************************************************************
 * Public Data
 ****************************************************************************/

const struct procfs_operations g_iobinfo_operations =
{
  iobinfo_open,   /* open */
  iobinfo_close,  /* close */
  iobinfo_read,   /* read */
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: iobinfo_emit
 *
 * Description:
 *   Helper to format a line into procfile->line and copy to user buffer,
 *   advancing buffer/offset/totalsize.
 *
 ****************************************************************************/

static void iobinfo_emit(FAR struct iobinfo_file_s *procfile,
                         FAR char **buffer, FAR size_t *buflen,
                         FAR off_t *offset, FAR size_t *totalsize,
                         FAR const char *fmt, ...)
{
  va_list ap;
  size_t linesize;
  size_t copysize;

  va_start(ap, fmt);
  linesize = vsnprintf(procfile->line, IOBINFO_LINELEN, fmt, ap);
  va_end(ap);

  if (linesize >= IOBINFO_LINELEN)
    {
      linesize = IOBINFO_LINELEN - 1;
    }

  copysize = procfs_memcpy(procfile->line, linesize, *buffer, *buflen,
                           offset);
  *totalsize += copysize;
  *buffer    += copysize;
  *buflen    -= copysize;
}

/****************************************************************************
 * Name: iobinfo_iob_callback
 *
 * Description:
 *   Callback invoked by iob_foreach() for each allocated IOB.
 *   If the IOB is a chain head, walk the chain and print details.
 *
 ****************************************************************************/

static void iobinfo_iob_callback(FAR struct iob_s *iob, FAR void *arg)
{
  FAR struct iobinfo_foreach_ctx_s *ctx = arg;
  FAR struct iob_s *next;
  int idx = 0;

  /* Determine if an IOB is a chain head. */

  if (iob->io_pktlen == 0 || iob->io_pktlen < iob->io_len)
    {
      return;
    }

  iobinfo_emit(ctx->procfile, ctx->buffer, ctx->buflen,
               ctx->offset, ctx->totalsize,
               "\n  Chain #%d: %p pktlen=%u, owner:[%s]\n",
               ctx->count, iob, iob->io_pktlen,
               iob->io_owner[0] != '\0' ? iob->io_owner : "(unowned)");

  do
    {
      next = IOB_LINK_GET(iob);
      iobinfo_emit(ctx->procfile, ctx->buffer, ctx->buflen,
                   ctx->offset, ctx->totalsize,
                   "    [%d] %p len=%-4u off=%-3u -> %p%s\n",
                   idx++, iob, iob->io_len, iob->io_offset,
                   next, (IOB_FLAG_TEST(iob, IOB_CHAIN_END) || !next)
                         ? " [END]" : "");
      iob = next;
    }
  while (iob != NULL);

  ctx->count++;
}

/****************************************************************************
 * Name: iobinfo_open
 ****************************************************************************/

static int iobinfo_open(FAR struct file *filep, FAR const char *relpath,
                        int oflags, mode_t mode)
{
  FAR struct iobinfo_file_s *procfile;

  finfo("Open '%s'\n", relpath);

  procfile = fs_heap_zalloc(sizeof(struct iobinfo_file_s));
  if (procfile == NULL)
    {
      ferr("ERROR: Failed to allocate file attributes\n");
      return -ENOMEM;
    }

  filep->f_priv = procfile;
  return OK;
}

/****************************************************************************
 * Name: iobinfo_close
 ****************************************************************************/

static int iobinfo_close(FAR struct file *filep)
{
  FAR struct iobinfo_file_s *procfile;

  procfile = (FAR struct iobinfo_file_s *)filep->f_priv;
  DEBUGASSERT(procfile);

  fs_heap_free(procfile);
  filep->f_priv = NULL;
  return OK;
}

/****************************************************************************
 * Name: iobinfo_read
 ****************************************************************************/

static ssize_t iobinfo_read(FAR struct file *filep, FAR char *buffer,
                            size_t buflen)
{
  FAR struct iobinfo_file_s *procfile = filep->f_priv;
  struct iobinfo_foreach_ctx_s ctx;
  struct mempoolinfo_s minfo;
  size_t totalsize = 0;
  off_t offset = filep->f_pos;
  int poolidx;

  iobinfo_emit(procfile, &buffer, &buflen, &offset, &totalsize,
               "=== IOB Pool Summary ===\n");
  iobinfo_emit(procfile, &buffer, &buflen, &offset, &totalsize,
               "%5s %12s %9s %9s %9s %9s %9s\n",
               "Pool", "BlockSize", "Total", "Used",
               "Free", "Pending", "Throttle");

  for (poolidx = 0; poolidx < CONFIG_IOB_NPOOLS; poolidx++)
    {
      iob_pool_info(poolidx, &minfo);

      iobinfo_emit(procfile, &buffer, &buflen, &offset, &totalsize,
                   "  #%d %12lu %9lu %9lu %9lu %9lu %9lu\n",
                   poolidx,
                   minfo.sizeblks,
                   minfo.aordblks + minfo.ordblks,
                   minfo.aordblks,
                   minfo.ordblks,
                   minfo.nwaiter,
                   minfo.throttle);
    }

  ctx.procfile  = procfile;
  ctx.buffer    = &buffer;
  ctx.buflen    = &buflen;
  ctx.offset    = &offset;
  ctx.totalsize = &totalsize;
  ctx.count     = 0;
  iobinfo_emit(procfile, &buffer, &buflen, &offset, &totalsize,
               "\n=== IOB Chain Detail ===\n");

  iob_foreach(iobinfo_iob_callback, &ctx);
  if (ctx.count == 0)
    {
      iobinfo_emit(procfile, &buffer, &buflen, &offset, &totalsize,
                   "  (no active IOB chains)\n");
    }

  filep->f_pos += totalsize;
  return totalsize;
}
