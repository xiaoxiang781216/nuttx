/****************************************************************************
 * include/nuttx/hwtracing/tricoreht/tricoreht_mcds_dtu.h
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

#ifndef __INCLUDE_NUTTX_TRICOREHT_TRICOREHT_MCDS_DTU_H
#define __INCLUDE_NUTTX_TRICOREHT_TRICOREHT_MCDS_DTU_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/mutex.h>
#include <nuttx/hwtracing/hwtracing.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Public Types
 ****************************************************************************/

#define DTU_EA_TYPE                               0
#define DTU_AC_TYPE                               1
#define DTU_DA_TYPE                               2
#define DTU_NR_EA_CMP                             8  /* Number of Effective Address comparators (EABND/EARNG) */
#define DTU_NR_ATTR_CMP                           4  /* Number of Attribute comparators (ACBND/ACRNG/ACMSK) */
#define DTU_NR_DATA_CMP                           4  /* Number of Data comparators (WDBND/WDRNG/WDMSK/WDSGN) */
#define DTU_MAX_NR                                6
#define DTU_NR_TOTAL_CMP  (DTU_NR_EA_CMP + DTU_NR_ATTR_CMP + DTU_NR_DATA_CMP)  /* Total number of comparators */

/* DTU message type enumeration */

enum mcds_dtu_msg_type_e
{
  MCDS_DTU_MSG_DTW   = 0,                         /* Write transaction: address + data + attributes */
  MCDS_DTU_MSG_DTWD  = 1,                         /* Write transaction: data only */
  MCDS_DTU_MSG_DTWA  = 2,                         /* Write transaction: address only */
  MCDS_DTU_MSG_DTR   = 3,                         /* Read transaction: address + data + attributes */
  MCDS_DTU_MSG_DTRD  = 4,                         /* Read transaction: data only */
  MCDS_DTU_MSG_DTRA  = 5,                         /* Read transaction: address only */
  MCDS_DTU_MSG_ERR   = 6                          /* Error message (e.g. FIFO overflow) */
};

/* Trigger mode enum for better readability */

enum mcds_dtu_trigger_mode_e
{
  MCDS_TRIGGER_DISABLED = 0,
  MCDS_TRIGGER_INVERT,
  MCDS_TRIGGER_DIRECT,
  MCDS_TRIGGER_IGNORE
};

struct mcds_dtu_tc_s
{
  uint32_t ea_bnd[DTU_NR_EA_CMP];                  /* EABNDx: lower bound, x=0..7. */
  uint32_t ea_rng[DTU_NR_EA_CMP];                  /* EARNGx: range/size, x=0..7.  */
  uint8_t  ea_idx;                                 /* Index for the ea selection. */
  uint8_t  easet;                                  /* easet. */
  uint32_t ac_bnd[DTU_NR_ATTR_CMP];                /* ACBNDx: masked lower bound, x=0..3. */
  uint32_t ac_rng[DTU_NR_ATTR_CMP];                /* ACRNGx: range, x=0..3. */
  uint32_t ac_msk[DTU_NR_ATTR_CMP];                /* ACMSKx: mask bits, x=0..3. */
  uint8_t  ac_idx;                                 /* Index for the ac selection. */
  uint8_t  acset;                                  /* acset. */
  uint32_t wd_bnd_lo[DTU_NR_DATA_CMP];             /* WDBNDx[31:0],  x=0..3. */
  uint32_t wd_bnd_hi[DTU_NR_DATA_CMP];             /* WDHBNDx[63:32], x=0..3. */
  uint32_t wd_rng_lo[DTU_NR_DATA_CMP];             /* WDRNGx[31:0],   x=0..3. */
  uint32_t wd_rng_hi[DTU_NR_DATA_CMP];             /* WDHRNGx[63:32], x=0..3. */
  uint32_t wd_msk_lo[DTU_NR_DATA_CMP];             /* WDMSKx[31:0],   x=0..3. */
  uint32_t wd_msk_hi[DTU_NR_DATA_CMP];             /* WDHMSKx[63:32], x=0..3. */
  uint32_t wd_sign[DTU_NR_DATA_CMP];               /* WDSGNx,         x=0..3. */
  uint8_t  wd_idx;                                 /* Index for the wd selection. */
  uint8_t  wdset;                                  /* wdset. */
  enum mcds_dtu_trigger_mode_e  trigger_mode;      /* New: trigger mode (see enum mcds_trigger_mode_e). */
  enum mcds_dtu_msg_type_e      msg_type;          /* Output message type mode */
};

struct mcds_dtu_config_s
{
  struct mcds_dtu_tc_s dtu_tc[DTU_MAX_NR];         /* Configuration image to be programmed/read back. */
  uint32_t cpuset;                                 /* CPU set bitmask: each bit represents a CPU */
};

struct tricoreht_mcds_dtu_dev_s
{
  struct hwtracing_dev_s htdev;
  struct mcds_dtu_config_s cfg;                    /* Configuration image to be programmed/read back. */
  int      dtu_index;                              /* 0 or 1: select DTU0 or DTU1 for FIFO level inspection. */
  uint8_t  nr_ea_cmp;                              /* Number of EA comparators (expected: 8). */
  uint8_t  nr_attr_cmp;                            /* Number of attribute comparators (expected: 4). */
  uint8_t  nr_data_cmp;                            /* Number of data comparators (expected: 4). */
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: mcds_register
 *
 * Description:
 *   Register a mcds devices.
 *
 * Input Parameters:
 *   desc  - A description of this tricoreht device.
 *
 * Returned Value:
 *   Pointer to a mcds device on success; NULL on failure.
 *
 ****************************************************************************/

FAR struct tricoreht_mcds_dtu_dev_s *
mcds_dtu_register(FAR const struct hwtracing_desc_s *desc);

/****************************************************************************
 * Name: mcds_unregister
 *
 * Description:
 *   Unregister a mcds devices.
 *
 * Input Parameters:
 *   mcdsdev  - Pointer to the mcds device.
 *
 ****************************************************************************/

void mcds_dtu_unregister(FAR struct tricoreht_mcds_dtu_dev_s *dtudev);

/****************************************************************************
 * Name: mcds_dtu_config
 *
 * Description:
 *   Configure the etm device.
 *
 * Input Parameters:
 *   dtudev  - Pointer to the DTU device to config.
 *   config  - Configuration need to be set to dtu device.
 *
 * Returned Value:
 *   Zero on success; a negative value on failure.
 *
 ****************************************************************************/

int mcds_dtu_config(FAR struct tricoreht_mcds_dtu_dev_s *dtudev,
                    FAR const struct mcds_dtu_config_s *config);

#endif  //__INCLUDE_NUTTX_TRICOREHT_TRICOREHT_MCDS_DTU_H
