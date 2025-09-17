/****************************************************************************
 * drivers/hwtracing/tricoreht/tricoreht_trif_dap.c
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

#include <errno.h>
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include <nuttx/fs/fs.h>
#include <nuttx/kmalloc.h>
#include <nuttx/irq.h>

#include <nuttx/hwtracing/tricoreht/tricoreht_trif.h>

#include "tricoreht_trif_core.h"
#include "tricoreht_mcds_core.h"
#include "IfxTrif_reg.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Private Functions Prototypes
 ****************************************************************************/

static int trif_dap_sink_enable(FAR struct hwtracing_dev_s *htdev);
static void trif_dap_sink_disable(FAR struct hwtracing_dev_s *htdev);
static int trif_dap_sink_init(FAR struct hwtracing_dev_s *htdev);

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* tfif_eth sink device operations. */

static const struct hwtracing_sink_ops_s g_trif_dap_sink_ops =
{
  .enable  = trif_dap_sink_enable,
  .disable = trif_dap_sink_disable,
  .init    = trif_dap_sink_init,
};

static const struct hwtracing_ops_s g_trif_dap_ops =
{
  .sink_ops = &g_trif_dap_sink_ops,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: trif_dap_sink_init
 ****************************************************************************/

static int trif_dap_sink_init(FAR struct hwtracing_dev_s *htdev)
{
  return trif_init(htdev);
}

/****************************************************************************
 * Name: trif_dap_sink_enable
 ****************************************************************************/

static int trif_dap_sink_enable(FAR struct hwtracing_dev_s *htdev)
{
  hwtracing_modify32(TRIF_CTRL_OPMODE_P | TRIF_OPMODE_TRACE_STREAM_DAP,
                     TRIF_CTRL_OPMODE_P | TRIF_CTRL_OPMODE_MASK,
                     &TRIF_CTRL.U);

  return trif_enable(htdev);
}

/****************************************************************************
 * Name: trif_dap_sink_disable
 ****************************************************************************/

static void trif_dap_sink_disable(FAR struct hwtracing_dev_s *htdev)
{
  trif_disable(htdev);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: trif_dap_register
 ****************************************************************************/

int trif_dap_register(FAR struct hwtracing_dev_s * tfifdev,
                      FAR const struct hwtracing_desc_s *desc)
{
  int ret;

  if (desc->type == HWTRACING_DEV_TYPE_SINK)
    {
      tfifdev->ops = &g_trif_dap_ops;
      ret = hwtracing_register(tfifdev, desc);
      if (ret < 0)
        {
          hterr("%s:tricoreht register failed\n", desc->name);
          return ret;
        }
    }
  else
    {
      hterr("unsupported tfif device type\n");
      return -EPERM;
    }

  return 0;
}

/****************************************************************************
 * Name: trif_dap_unregister
 ****************************************************************************/

void trif_dap_unregister(FAR struct hwtracing_dev_s * tfifdev)
{
  hwtracing_unregister(tfifdev);
}

