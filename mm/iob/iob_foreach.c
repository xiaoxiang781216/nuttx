/****************************************************************************
 * mm/iob/iob_foreach.c
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

#include <nuttx/mm/iob.h>
#include <nuttx/mm/mempool.h>

#include "iob.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct iob_foreach_ctx_s
{
  iob_foreach_cb_t callback;
  FAR void *arg;
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void iob_foreach_mempool_cb(FAR void *blk, FAR void *arg)
{
  FAR struct iob_foreach_ctx_s *ctx = arg;
  FAR struct iob_s *iob = (FAR struct iob_s *)blk;

  ctx->callback(iob, ctx->arg);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: iob_foreach
 *
 * Description:
 *   Iterate over all allocated (in-use) IOBs across all IOB pools.
 *   For each allocated IOB, the callback is invoked with the IOB pointer
 *   and the user-provided argument.
 *
 ****************************************************************************/

void iob_foreach(iob_foreach_cb_t callback, FAR void *arg)
{
  struct iob_foreach_ctx_s ctx;
  int i;

  DEBUGASSERT(callback != NULL);

  ctx.callback = callback;
  ctx.arg = arg;

  for (i = 0; i < CONFIG_IOB_NPOOLS; i++)
    {
      mempool_foreach_used(g_iob_pool[i], iob_foreach_mempool_cb, &ctx);
    }
}

/****************************************************************************
 * Name: iob_pool_info
 *
 * Description:
 *   Get pool summary information for the specified IOB pool index.
 *
 ****************************************************************************/

int iob_pool_info(int poolidx, FAR struct mempoolinfo_s *info)
{
  return mempool_info(g_iob_pool[poolidx], info);
}
