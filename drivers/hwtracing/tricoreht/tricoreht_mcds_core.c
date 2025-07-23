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
 * Name: mcds_tbuf_stop
 ****************************************************************************/

void mcds_tbuf_stop(void)
{
  MCDS4P_FIFOCTL.U |= MCDS_MCDS4P2P_FIFOCTL_SET;

  if (hwtracing_timeout(MCDS_MCDS4P2P_FIFOCTL_FLSH_READY,
                        MCDS_MCDS4P2P_FIFOCTL_FLSH_MASK,
                        (uintptr_t)&MCDS4P_FIFOCTL) < 0)
    {
      hterr("timeout mcds_tbuf_stop\n");
    }
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
      hterr("timeout mcds_tbuf_start\n");
    }
}
