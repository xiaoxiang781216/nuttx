/****************************************************************************
 * drivers/hwtracing/coresight/coresight_tpiu.c
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
#include <nuttx/kmalloc.h>
#include <nuttx/bits.h>

#include <nuttx/hwtracing/coresight/coresight_tpiu.h>

#include "coresight_common.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* TPIU registers */

#define TPIU_SUPP_PORTSZ           0x000
#define TPIU_CURR_PORTSZ           0x004
#define TPIU_SUPP_TRIGMODES        0x100
#define TPIU_TRIG_CNTRVAL          0x104
#define TPIU_TRIG_MULT             0x108
#define TPIU_SUPP_TESTPATM         0x200
#define TPIU_CURR_TESTPATM         0x204
#define TPIU_TEST_PATREPCNTR       0x208
#define TPIU_FFSR                  0x300
#define TPIU_FFCR                  0x304
#define TPIU_FSYNC_CNTR            0x308
#define TPIU_EXTCTL_INPORT         0x400
#define TPIU_EXTCTL_OUTPORT        0x404
#define TPIU_ITTRFLINACK           0xee4
#define TPIU_ITTRFLIN              0xee8
#define TPIU_ITATBDATA0            0xeec
#define TPIU_ITATBCTR2             0xef0
#define TPIU_ITATBCTR1             0xef4
#define TPIU_ITATBCTR0             0xef8

/* FFSR - 0x300 */

#define TPIU_FFSR_FT_STOPPED       BIT(1)

/* FFCR - 0x304 */

#define TPIU_FFCR_FON_MAN          BIT(6)
#define TPIU_FFCR_STOP_FI          BIT(12)

/****************************************************************************
 * Private Functions Prototypes
 ****************************************************************************/

static int tpiu_enable(FAR struct hwtracing_dev_s *htdev);
static void tpiu_disable(FAR struct hwtracing_dev_s *htdev);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct hwtracing_sink_ops_s g_tpiu_sink_ops =
{
  .enable  = tpiu_enable,
  .disable = tpiu_disable,
};

static const struct hwtracing_ops_s g_tpiu_ops =
{
  .sink_ops = &g_tpiu_sink_ops,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: tpiu_hw_disable
 ****************************************************************************/

static void tpiu_hw_disable(FAR struct coresight_tpiu_dev_s *tpiudev)
{
  coresight_unlock(tpiudev->htdev.addr);

  /* Trigger a formatter stop event. */

  hwtracing_modify32(TPIU_FFCR_STOP_FI, TPIU_FFCR_STOP_FI,
                     tpiudev->htdev.addr + TPIU_FFCR);
  hwtracing_modify32(TPIU_FFCR_FON_MAN, TPIU_FFCR_FON_MAN,
                     tpiudev->htdev.addr + TPIU_FFCR);
  if (hwtracing_timeout(0, TPIU_FFCR_FON_MAN,
                        tpiudev->htdev.addr + TPIU_FFCR) < 0)
    {
      hterr("timeout while waiting for completion of Manual Flush\n");
    }

  if (hwtracing_timeout(TPIU_FFSR_FT_STOPPED, TPIU_FFSR_FT_STOPPED,
                        tpiudev->htdev.addr + TPIU_FFSR) < 0)
    {
      hterr("timeout while waiting for Formatter to Stop\n");
    }

  coresight_lock(tpiudev->htdev.addr);
}

/****************************************************************************
 * Name: tpiu_enable
 ****************************************************************************/

static int tpiu_enable(FAR struct hwtracing_dev_s *htdev)
{
  return coresight_claim_device(htdev->addr);
}

/****************************************************************************
 * Name: tpiu_disable
 ****************************************************************************/

static void tpiu_disable(FAR struct hwtracing_dev_s *htdev)
{
  FAR struct coresight_tpiu_dev_s *tpiudev =
    (FAR struct coresight_tpiu_dev_s *)htdev;

  tpiu_hw_disable(tpiudev);
  coresight_disclaim_device(tpiudev->htdev.addr);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: tpiu_register
 *
 * Description:
 *   Register a tpiu devices.
 *
 * Input Parameters:
 *   desc  - A description of this coresight device.
 *
 * Returned Value:
 *   Pointer to a tpiu device on success; NULL on failure.
 *
 ****************************************************************************/

FAR struct coresight_tpiu_dev_s *
tpiu_register(FAR const struct hwtracing_desc_s *desc)
{
  FAR struct coresight_tpiu_dev_s *tpiudev;
  FAR struct hwtracing_dev_s *htdev;
  int ret;

  tpiudev = kmm_zalloc(sizeof(struct coresight_tpiu_dev_s));
  if (tpiudev == NULL)
    {
      hterr("%s:malloc failed!\n", desc->name);
      return NULL;
    }

  htdev = &tpiudev->htdev;
  htdev->ops = &g_tpiu_ops;
  ret = hwtracing_register(htdev, desc);
  if (ret < 0)
    {
      kmm_free(tpiudev);
      hterr("%s:register failed\n", desc->name);
      return NULL;
    }

  return tpiudev;
}

/****************************************************************************
 * Name: tpiu_unregister
 *
 * Description:
 *   Unregister a tpiu devices.
 *
 * Input Parameters:
 *   fundev  - Pointer to the tpiu device.
 *
 ****************************************************************************/

void tpiu_unregister(FAR struct coresight_tpiu_dev_s *tpiudev)
{
  hwtracing_unregister(&tpiudev->htdev);
  kmm_free(tpiudev);
}
