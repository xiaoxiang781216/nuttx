/****************************************************************************
 * drivers/hwtracing/tricoreht/tricoreht_trif_core.c
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

#include "tricoreht_trif_core.h"
#include "IfxTrif_reg.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: trif_register
 *
 * Description:
 *   Register a trif devices.
 *
 * Input Parameters:
 *   desc  - A description of this tricoreht device.
 *
 * Returned Value:
 *   Pointer to a trif device on success; NULL on failure.
 *
 ****************************************************************************/

FAR struct tricoreht_trif_dev_s *
trif_register(FAR const struct hwtracing_desc_s *desc)
{
  FAR struct tricoreht_trif_dev_s *trifdev;
  int ret = -EINVAL;

  trifdev = kmm_zalloc(sizeof(struct tricoreht_trif_dev_s));
  if (trifdev == NULL)
    {
      hterr("%s:malloc failed!\n", desc->name);
      return NULL;
    }

  switch (desc->subtype.sink_subtype)
    {
      case HWTRACING_DEV_SUBTYPE_SINK_TFIF:
        trifdev->type = TRIF_CONFIG_TYPE_TFIF;
        ret = trif_tfif_register(trifdev, desc);
        break;
      case HWTRACING_DEV_SUBTYPE_SINK_RAM:
      case HWTRACING_DEV_SUBTYPE_SINK_SGBT:
      case HWTRACING_DEV_SUBTYPE_SINK_ETH:
        break;

      default:
        hterr("config type error\n");
        break;
    }

  if (ret < 0)
    {
      kmm_free(trifdev);
      return NULL;
    }

  nxmutex_init(&trifdev->lock);
  return trifdev;
}

/****************************************************************************
 * Name: trif_unregister
 *
 * Description:
 *   Unregister a trif devices.
 *
 * Input Parameters:
 *   trifdev  - Pointer to the trif device.
 *
 ****************************************************************************/

void trif_unregister(FAR struct tricoreht_trif_dev_s *trifdev)
{
  switch (trifdev->type)
    {
      case TRIF_CONFIG_TYPE_TFIF:
        trif_tfif_unregister(trifdev);
        break;

      case TRIF_CONFIG_TYPE_TME_ETH:
      case TRIF_CONFIG_TYPE_TME_RAM:
      case TRIF_CONFIG_TYPE_SGBT:
        break;

      default:
        hterr("wrong config type\n");
        break;
    }

  nxmutex_destroy(&trifdev->lock);
  kmm_free(trifdev);
}

/****************************************************************************
 * Name: trif_disable
 ****************************************************************************/

void trif_disable(FAR struct hwtracing_dev_s *trifdev)
{
  /* change trace session state to block */

  hwtracing_modify32(TRIF_CTRL_TRCSTART_P,
                     TRIF_CTRL_TRCSTART_P | TRIF_CTRL_TRCSTART,
                     &TRIF_CTRL.U);

  /* state */

  if (hwtracing_timeout(TRIF_TRCSTATE_BLOCKED,
                        TRIF_TSR_TRCSTATE_MASK,
                        (uintptr_t)&TRIF_TSR) < 0)
    {
      hterr("timeout TRIF_TSR_STATE\n");
      return;
    }

  /* init mode */

  hwtracing_modify32(TRIF_CTRL_OPMODE_P | TRIF_OPMODE_INIT,
                     TRIF_CTRL_OPMODE_P | TRIF_CTRL_OPMODE_MASK,
                     &TRIF_CTRL.U);

  /* clear tsr isr */

  TRIF_TSR.U = 0;
  TRIF_ISR.U = 0;
  TRIF_TBUF0FLV.U &= ~TRIF_TBUFFLV_MAXLVL_MASK;
#ifdef CONFIG_ARCH_CHIP_AURIX_TC4DX
  TRIF_TBUF1FLV.U &= ~TRIF_TBUFFLV_MAXLVL_MASK;
#endif
  /* clsoe clk */

  TRIF_CLC.U |= TRIF_CLC_DISR;
}

/****************************************************************************
 * Name: trif_enable
 ****************************************************************************/

int trif_enable(FAR struct hwtracing_dev_s *trifdev)
{
  TRIF_CTRL.U |= TRIF_CTRL_TRCSTART | TRIF_CTRL_TRCSTART_P;
  TRIF_CTRL.U &= ~TRIF_CTRL_TRCSTART;

  /* state */

  if (hwtracing_timeout(TRIF_TRCSTATE_ENABLED,
                        TRIF_TSR_TRCSTATE_MASK,
                        (uintptr_t)&TRIF_TSR) < 0)
    {
      hterr("timeout TRIF_TSR_STATE\n");
      return -ETIME;
    }

  /* session */
#ifdef CONFIG_ARCH_CHIP_AURIX_TC4DX
  if (hwtracing_timeout(TRIF_TSR_TRCON0 | TRIF_TSR_TRCON1,
                        TRIF_TSR_TRCON0 | TRIF_TSR_TRCON1,
                        (uintptr_t)&TRIF_TSR) < 0)
#else
  if (hwtracing_timeout(TRIF_TSR_TRCON0, TRIF_TSR_TRCON0,
                        (uintptr_t)&TRIF_TSR) < 0)
#endif
    {
      hterr("timeout TRIF_TSR_SESSION\n");
      return -ETIME;
    }

  return 0;
}

/****************************************************************************
 * Name: trif_enable
 ****************************************************************************/

int trif_init(FAR struct hwtracing_dev_s *trifdev)
{
  /* reset */

  TRIF_RST_CTRLA.U = TRIF_RST_CTRLA_KRST;
  TRIF_RST_CTRLB.U = TRIF_RST_CTRLB_KRST;

  /* enable clk */

  TRIF_CLC.U &= ~TRIF_CLC_DISR;

  if (hwtracing_timeout(0x0, TRIF_CLC_DISS,
                        (uintptr_t)&TRIF_CLC) < 0)
    {
      hterr("timeout TRIF_CLC\n");
      return -ETIME;
    }

  /* init mode */

  hwtracing_modify32(TRIF_CTRL_OPMODE_P | TRIF_OPMODE_INIT,
                     TRIF_CTRL_OPMODE_P | TRIF_CTRL_OPMODE_MASK,
                     &TRIF_CTRL.U);

  return 0;
}
