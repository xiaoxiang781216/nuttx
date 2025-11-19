/****************************************************************************
 * drivers/hwtracing/tricoreht/tricoreht.c
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

#include <nuttx/hwtracing/tricoreht/tricoreht_mcds_dtu.h>
#include <nuttx/hwtracing/tricoreht/tricoreht.h>
#include <nuttx/hwtracing/hwtracing_note.h>
#include <debug.h>

#include "tricoreht_trif_core.h"
#include "tricoreht_mcds_core.h"
#include "IfxMcds4p2p_reg.h"
#include "IfxTrif_reg.h"
#include "IfxCpu.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Private Types
 ****************************************************************************/

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Private Data
 ****************************************************************************/

#ifdef TRICOREHT_TRIGGER_SECTION
locate_data(CONFIG_TRICOREHT_TRIGGER_SECTION)
#endif
static uint32_t g_hwtracing_trigger;

#ifdef CONFIG_TRICOREHT_NOTE_DTU
struct tricoreht_mcds_dtu_dev_s *dtu_dev;
struct hwtracing_dev_s trif_dap;

static const struct hwtracing_desc_s g_mcds_dtu =
{
  .name       = "mcds_dtu",
  .addr       = (uintptr_t)&MODULE_MCDS4P,
  .type       = HWTRACING_DEV_TYPE_SOURCE,
  .subtype    =
    {
       HWTRACING_DEV_SUBTYPE_SOURCE_PROC,
    },
  .cpu        = 0,
  .outport_num = 1,
  .outports[0] =
    {
      .remote = "trif_dap",
    }
};

static const struct hwtracing_desc_s g_trif_dap =
{
  .name       = "trif_dap",
  .addr       = (uintptr_t)&TRIF_TFIFREAD0,
  .type       = HWTRACING_DEV_TYPE_SINK,
  .subtype    =
    {
      HWTRACING_DEV_SUBTYPE_SINK_DAP,
    }
};
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: dtu_dap_enable
 ****************************************************************************/

bool tricoreht_enabled(void)
{
  uint32_t value = hwtracing_get32((uintptr_t)&TRIF_TSR);
  if ((value & TRIF_TSR_TRCSTATE_MASK) == TRIF_TRCSTATE_ENABLED)
    {
      return true;
    }

  return false;
}

/****************************************************************************
 * Name: tricoreht_init
 ****************************************************************************/

int tricoreht_init(void)
{
  int ret;

  __mtcr(CPU_TRCCFG, __mfcr(CPU_TRCCFG) | (1U << 15));

  /* MCDS TRIF init */

  mcds_init(NULL);
  trif_init(NULL);

#ifdef CONFIG_HWTRACING_NOTE
  ret = hwtracing_note_register(&g_hwtracing_trigger, tricoreht_enabled);
  if (ret < 0)
    {
      return ret;
    }
#endif

#ifdef CONFIG_TRICOREHT_NOTE_DTU
  dtu_dev = mcds_dtu_register(&g_mcds_dtu);
  if (dtu_dev == NULL)
    {
      hterr("%s:register failed\n", g_mcds_dtu.name);
      return -1;
    }

  ret = trif_dap_register(&trif_dap, &g_trif_dap);
  if (ret < 0)
    {
      hterr("%s:register failed\n", trif_dap.name);
      return ret;
    }

  struct mcds_dtu_config_s cfg;
  cfg.dtu_tc[0].ea_bnd[0] = &g_hwtracing_trigger;
  cfg.dtu_tc[0].ea_rng[0] = sizeof(g_hwtracing_trigger);
  cfg.dtu_tc[0].easet = 1;
  cfg.dtu_tc[0].trigger_mode = MCDS_TRIGGER_DIRECT;
  cfg.dtu_tc[0].msg_type = MCDS_DTU_MSG_DTW;

  cfg.dtu_tc[1].ea_bnd[0] = &g_hwtracing_trigger;
  cfg.dtu_tc[1].ea_rng[0] = sizeof(g_hwtracing_trigger);
  cfg.dtu_tc[1].easet = 1;
  cfg.dtu_tc[1].trigger_mode = MCDS_TRIGGER_DIRECT;
  cfg.dtu_tc[1].msg_type = MCDS_DTU_MSG_DTW;

  cfg.dtu_tc[2].ea_bnd[0] = &g_hwtracing_trigger;
  cfg.dtu_tc[2].ea_rng[0] = sizeof(g_hwtracing_trigger);
  cfg.dtu_tc[2].easet = 1;
  cfg.dtu_tc[2].trigger_mode = MCDS_TRIGGER_DIRECT;
  cfg.dtu_tc[2].msg_type = MCDS_DTU_MSG_DTW;

  cfg.dtu_tc[3].ea_bnd[0] = &g_hwtracing_trigger;
  cfg.dtu_tc[3].ea_rng[0] = sizeof(g_hwtracing_trigger);
  cfg.dtu_tc[3].easet = 1;
  cfg.dtu_tc[3].trigger_mode = MCDS_TRIGGER_DIRECT;
  cfg.dtu_tc[3].msg_type = MCDS_DTU_MSG_DTW;

  cfg.cpuset = 0xf;
#ifdef CONFIG_ARCH_CHIP_AURIX_TC4DX
  cfg.dtu_tc[4].ea_bnd[0] = &g_hwtracing_trigger;
  cfg.dtu_tc[4].ea_rng[0] = sizeof(g_hwtracing_trigger);
  cfg.dtu_tc[4].easet = 1;
  cfg.dtu_tc[4].trigger_mode = MCDS_TRIGGER_DIRECT;
  cfg.dtu_tc[4].msg_type = MCDS_DTU_MSG_DTW;

  cfg.dtu_tc[5].ea_bnd[0] = &g_hwtracing_trigger;
  cfg.dtu_tc[5].ea_rng[0] = sizeof(g_hwtracing_trigger);
  cfg.dtu_tc[5].easet = 1;
  cfg.dtu_tc[5].trigger_mode = MCDS_TRIGGER_DIRECT;
  cfg.dtu_tc[5].msg_type = MCDS_DTU_MSG_DTW;
  cfg.cpuset = 0x3f;
#endif
  mcds_dtu_config(dtu_dev, &cfg);
  hwtracing_enable(&dtu_dev->htdev, &trif_dap);
#endif

  return 0;
}
