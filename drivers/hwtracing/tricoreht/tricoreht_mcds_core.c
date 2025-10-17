/****************************************************************************
 * drivers/hwtracing/tricoreht/tricoreht_mcds_core.c
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

#include <debug.h>

#include "tricoreht_mcds_core.h"
#include "IfxMcds4p2p_reg.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: mcds_tbuf_init
 ****************************************************************************/

void mcds_tbuf_init(enum mcds_tubf_trigger_mode_e type)
{
  MCDS4P_FIFOBOT.B.BOTTOM = 0;
  MCDS4P_FIFOTOP.B.TOP = MCDS_MCDS4P_TBUF_SIZE - 1;

  if (type == MCDS_TRIGGER_MODE_POST)
    {
      MCDS4P_FIFOPRE.U = 0;
    }
  else if (type == MCDS_TRIGGER_MODE_PRE)
    {
      MCDS4P_FIFOPRE.U = MCDS4P_FIFOTOP.U
                         - MCDS4P_FIFOBOT.U;
    }
  else
    {
      MCDS4P_FIFOPRE.U = MCDS_MCDS4P_PRE_TRIGGER_DEFAULT;
    }

#ifdef CONFIG_ARCH_CHIP_AURIX_TC4DX
  MCDS2P_FIFOBOT.B.BOTTOM = 0;
  MCDS2P_FIFOTOP.B.TOP = MCDS_MCDS2P_TBUF_SIZE - 1;

  if (type == MCDS_TRIGGER_MODE_POST)
    {
      MCDS2P_FIFOPRE.U = 0;
    }
  else if (type == MCDS_TRIGGER_MODE_PRE)
    {
      MCDS2P_FIFOPRE.U = MCDS2P_FIFOTOP.U
                         - MCDS2P_FIFOBOT.U;
    }
  else
    {
      MCDS2P_FIFOPRE.U = MCDS_MCDS2P_PRE_TRIGGER_DEFAULT;
    }
#endif
}

/****************************************************************************
 * Name: mcds_tbuf_stop
 ****************************************************************************/

void mcds_tbuf_stop(void)
{
  MCDS4P_FIFOCTL.U |= MCDS_MCDS4P2P_FIFOCTL_SET;

  if (hwtracing_timeout(MCDS_MCDS4P2P_FIFOCTL_FLSH_READY,
                        MCDS_MCDS4P2P_FIFOCTL_FLSH_MASK,
                        (uintptr_t)&MCDS4P_FIFOCTL) < 0)
    {
      hterr("timeout mcds_tbuf_stop MCDS4P\n");
    }

#ifdef CONFIG_ARCH_CHIP_AURIX_TC4DX
  MCDS2P_FIFOCTL.U |= MCDS_MCDS4P2P_FIFOCTL_SET;

  if (hwtracing_timeout(MCDS_MCDS4P2P_FIFOCTL_FLSH_READY,
                        MCDS_MCDS4P2P_FIFOCTL_FLSH_MASK,
                        (uintptr_t)&MCDS2P_FIFOCTL) < 0)
    {
      hterr("timeout mcds_tbuf_stop MCDS2P\n");
    }
#endif
}

/****************************************************************************
 * Name: mcds_tbuf_start
 ****************************************************************************/

void mcds_tbuf_start(void)
{
  MCDS4P_FIFOCTL.U |= MCDS_MCDS4P2P_FIFOCTL_CLR;

  if (hwtracing_timeout(0x0, MCDS_MCDS4P2P_FIFOCTL_FLSH_MASK,
                        (uintptr_t)&MCDS4P_FIFOCTL) < 0)
    {
      hterr("timeout mcds_tbuf_start MCDS4P\n");
    }

#ifdef CONFIG_ARCH_CHIP_AURIX_TC4DX
  MCDS2P_FIFOCTL.U |= MCDS_MCDS4P2P_FIFOCTL_CLR;

  if (hwtracing_timeout(0x0, MCDS_MCDS4P2P_FIFOCTL_FLSH_MASK,
                        (uintptr_t)&MCDS2P_FIFOCTL) < 0)
    {
      hterr("timeout mcds_tbuf_start MCDS2P\n");
    }
#endif
}

/****************************************************************************
 * Name: mcds_disable
 ****************************************************************************/

void mcds_disable(FAR struct hwtracing_dev_s *mcdsdev)
{
  MCDS4P_TBUFFLV.U = 0;

  /* close clk */

  MCDS4P_CLC.B.DISR = 1;

  if (hwtracing_timeout(MCDS_MCDS4P2P_CLC_DISS,
                        MCDS_MCDS4P2P_CLC_DISS,
                        (uintptr_t)&MCDS4P_CLC) < 0)
    {
      hterr("timeout MCDS4P_CLC\n");
      return;
    }

#ifdef CONFIG_ARCH_CHIP_AURIX_TC4DX
  MCDS2P_TBUFFLV.U = 0;

  /* close clk */

  MCDS2P_CLC.B.DISR = 1;

  if (hwtracing_timeout(MCDS_MCDS4P2P_CLC_DISS,
                        MCDS_MCDS4P2P_CLC_DISS,
                        (uintptr_t)&MCDS2P_CLC) < 0)
    {
      hterr("timeout MCDS2P_CLC\n");
      return;
    }
#endif
}

/****************************************************************************
 * Name: mcds_enable
 ****************************************************************************/

int mcds_enable(FAR struct hwtracing_dev_s *mcdsdev)
{
  /* The default data source for TMUX X/Y/Z/W is selected as CPUZ */

  hwtracing_modify32(MCDS_MCDS4P2P_MUXTCRC_TCMUX0_CPUZ |
                     MCDS_MCDS4P2P_MUXTCRC_TCMUX1_CPUZ |
                     MCDS_MCDS4P2P_MUXTCRC_TCMUX2_CPUZ |
                     MCDS_MCDS4P2P_MUXTCRC_TCMUX3_CPUZ |
                     MCDS_MCDS4P2P_MUXTCRC_TCMUX_P,
                     MCDS_MCDS4P2P_MUXTCRC_TCMUX0_MASK |
                     MCDS_MCDS4P2P_MUXTCRC_TCMUX1_MASK |
                     MCDS_MCDS4P2P_MUXTCRC_TCMUX2_MASK |
                     MCDS_MCDS4P2P_MUXTCRC_TCMUX3_MASK |
                     MCDS_MCDS4P2P_MUXTCRC_TCMUX_P,
                     &MCDS4P_MUXTCRC.U);

  /* DMUX uses CPU by default. */

  hwtracing_modify32(MCDS_MCDS4P2P_TMUX0_TCMUX_CPU0 |
                     MCDS_MCDS4P2P_MUX_TM0_EN       |
                     MCDS_MCDS4P2P_MUX_TM0_P        |
                     MCDS_MCDS4P2P_TMUX1_TCMUX_CPU1 |
                     MCDS_MCDS4P2P_MUX_TM1_EN       |
                     MCDS_MCDS4P2P_MUX_TM1_P        |
                     MCDS_MCDS4P2P_TMUX2_TCMUX_CPU2 |
                     MCDS_MCDS4P2P_MUX_TM2_EN       |
                     MCDS_MCDS4P2P_MUX_TM2_P        |
                     MCDS_MCDS4P2P_TMUX3_TCMUX_CPU3 |
                     MCDS_MCDS4P2P_MUX_TM3_EN       |
                     MCDS_MCDS4P2P_MUX_TM3_P,
                     MCDS_MCDS4P2P_MUX_TMUX0_MASK   |
                     MCDS_MCDS4P2P_MUX_TM0_EN       |
                     MCDS_MCDS4P2P_MUX_TM0_P        |
                     MCDS_MCDS4P2P_MUX_TMUX1_MASK   |
                     MCDS_MCDS4P2P_MUX_TM1_EN       |
                     MCDS_MCDS4P2P_MUX_TM1_P        |
                     MCDS_MCDS4P2P_MUX_TMUX2_MASK   |
                     MCDS_MCDS4P2P_MUX_TM2_EN       |
                     MCDS_MCDS4P2P_MUX_TM2_P        |
                     MCDS_MCDS4P2P_MUX_TMUX3_MASK   |
                     MCDS_MCDS4P2P_MUX_TM3_EN       |
                     MCDS_MCDS4P2P_MUX_TM3_P,
                     &MCDS4P_MUX.U);

  /* Clear the trigger disable bit, set the CLR bit,
   * and prepare to start TRACE
   */

  hwtracing_modify32(MCDS_MCDS4P2P_FIFOCTL_TRON |
                     MCDS_MCDS4P2P_FIFOCTL_CLR,
                     MCDS_MCDS4P2P_FIFOCTL_TRON |
                     MCDS_MCDS4P2P_FIFOCTL_CLR,
                     &MCDS4P_FIFOCTL.U);

#ifdef CONFIG_ARCH_CHIP_AURIX_TC4DX

  /* The default data source for TMUX X/W is selected as CPUZ */

  hwtracing_modify32(MCDS_MCDS4P2P_MUXTCRC_TCMUX0_CPUZ |
                     MCDS_MCDS4P2P_MUXTCRC_TCMUX3_CPUZ |
                     MCDS_MCDS4P2P_MUXTCRC_TCMUX_P,
                     MCDS_MCDS4P2P_MUXTCRC_TCMUX0_MASK |
                     MCDS_MCDS4P2P_MUXTCRC_TCMUX3_MASK |
                     MCDS_MCDS4P2P_MUXTCRC_TCMUX_P,
                     &MCDS2P_MUXTCRC.U);

  /* DMUX uses CPU by default. */

  hwtracing_modify32(MCDS_MCDS4P2P_TMUX0_TCMUX_CPU0 |
                     MCDS_MCDS4P2P_MUX_TM0_EN       |
                     MCDS_MCDS4P2P_MUX_TM0_P        |
                     MCDS_MCDS4P2P_TMUX3_TCMUX_CPU3 |
                     MCDS_MCDS4P2P_MUX_TM3_EN       |
                     MCDS_MCDS4P2P_MUX_TM3_P,
                     MCDS_MCDS4P2P_MUX_TMUX0_MASK   |
                     MCDS_MCDS4P2P_MUX_TM0_EN       |
                     MCDS_MCDS4P2P_MUX_TM0_P        |
                     MCDS_MCDS4P2P_MUX_TMUX3_MASK   |
                     MCDS_MCDS4P2P_MUX_TM3_EN       |
                     MCDS_MCDS4P2P_MUX_TM3_P,
                     &MCDS2P_MUX.U);

  /* Clear the trigger disable bit, set the CLR bit,
   * and prepare to start TRACE
   */

  hwtracing_modify32(MCDS_MCDS4P2P_FIFOCTL_TRON |
                     MCDS_MCDS4P2P_FIFOCTL_CLR,
                     MCDS_MCDS4P2P_FIFOCTL_TRON |
                     MCDS_MCDS4P2P_FIFOCTL_CLR,
                     &MCDS2P_FIFOCTL.U);
#endif

  return 0;
}

/****************************************************************************
 * Name: mcds_enable
 ****************************************************************************/

int mcds_init(FAR struct hwtracing_dev_s *mcdsdev)
{
  /* reset */

  MCDS4P_RST_CTRLA.U = 0x1;
  MCDS4P_RST_CTRLB.U = 0x1;

  /* enable clk */

  MCDS4P_CLC.B.DISR = 0;

  if (hwtracing_timeout(0x0, MCDS_MCDS4P2P_CLC_DISS,
                        (uintptr_t)&MCDS4P_CLC) < 0)
    {
      hterr("timeout MCDS4P_CLC\n");
      return -ETIME;
    }

  /* clear mux en */

  hwtracing_modify32(0U,
                     MCDS_MCDS4P2P_MUX_TM0_EN |
                     MCDS_MCDS4P2P_MUX_TM1_EN |
                     MCDS_MCDS4P2P_MUX_TM2_EN |
                     MCDS_MCDS4P2P_MUX_TM3_EN,
                     &MCDS4P_MUX.U);

  /* clear clr */

  hwtracing_modify32(MCDS_MCDS4P2P_FIFOCTL_CLR,
                     MCDS_MCDS4P2P_FIFOCTL_CLR,
                     &MCDS4P_FIFOCTL.U);

#ifdef CONFIG_ARCH_CHIP_AURIX_TC4DX

  /* reset */

  MCDS2P_RST_CTRLA.U = 0x1;
  MCDS2P_RST_CTRLB.U = 0x1;

  /* enable clk */

  MCDS2P_CLC.B.DISR = 0;

  if (hwtracing_timeout(0x0, MCDS_MCDS4P2P_CLC_DISS,
                        (uintptr_t)&MCDS2P_CLC) < 0)
    {
      hterr("timeout MCDS2P_CLC\n");
      return -ETIME;
    }

  /* clear mux en */

  hwtracing_modify32(0U,
                     MCDS_MCDS4P2P_MUX_TM0_EN |
                     MCDS_MCDS4P2P_MUX_TM1_EN |
                     MCDS_MCDS4P2P_MUX_TM2_EN |
                     MCDS_MCDS4P2P_MUX_TM3_EN,
                     &MCDS2P_MUX.U);

  /* clear clr */

  hwtracing_modify32(MCDS_MCDS4P2P_FIFOCTL_CLR,
                     MCDS_MCDS4P2P_FIFOCTL_CLR,
                     &MCDS2P_FIFOCTL.U);
#endif

  return 0;
}
