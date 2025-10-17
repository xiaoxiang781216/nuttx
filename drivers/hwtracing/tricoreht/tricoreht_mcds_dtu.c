/****************************************************************************
 * drivers/hwtracing/tricoreht/tricoreht_mcds_dtu.c
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
#include <nuttx/list.h>
#include <nuttx/lib/math32.h>
#include <nuttx/bits.h>

#include <nuttx/hwtracing/tricoreht/tricoreht_mcds_dtu.h>

#include "tricoreht_trif_core.h"
#include "tricoreht_mcds_core.h"
#include "IfxTrif_reg.h"
#include "IfxMcds4p2p_reg.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/*  POB_DTUTFLV — DTU FIFOs Fill Level (TCXDTUFLV) */

#define MCDS_DTU_TCXDTUFLV_CURLVL0_MASK    (0xFU << 0)   /* [3:0]   Current DTU0 FIFO fill level (messages) */
#define MCDS_DTU_TCXDTUFLV_CURLVL0_SHIFT   0
#define MCDS_DTU_TCXDTUFLV_MAXLVL0_MASK    (0xFU << 4)   /* [7:4]   Max DTU0 FIFO fill level (write 0 to clear) */
#define MCDS_DTU_TCXDTUFLV_MAXLVL0_SHIFT   4
#define MCDS_DTU_TCXDTUFLV_CURLVL1_MASK    (0xFU << 8)   /* [11:8]  Current DTU1 FIFO fill level (messages) */
#define MCDS_DTU_TCXDTUFLV_CURLVL1_SHIFT   8
#define MCDS_DTU_TCXDTUFLV_MAXLVL1_MASK    (0xFU << 12)  /* [15:12] Max DTU1 FIFO fill level (write 0 to clear) */
#define MCDS_DTU_TCXDTUFLV_MAXLVL1_SHIFT   12

/* POB_ACMSKx — Masked Data Comparator Mask (TCXACMSKx) */

#define MCDS_DTU_TCXACMSK_SVM              (1U << 0)     /* [0]     Supervisor Mode bit pass-through mask */
#define MCDS_DTU_TCXACMSK_MASTER_MASK      (0x1FU << 1)  /* [5:1]   Master ID mask */
#define MCDS_DTU_TCXACMSK_MASTER_SHIFT     1
#define MCDS_DTU_TCXACMSK_SUBCH_MASK       (0x7FU << 6)  /* [12:6]  Sub-channel ID mask */
#define MCDS_DTU_TCXACMSK_SUBCH_SHIFT      6
#define MCDS_DTU_TCXACMSK_WR               (1U << 13)    /* [13]    Direction: write bit mask */
#define MCDS_DTU_TCXACMSK_RD               (1U << 14)    /* [14]    Direction: read bit mask */
#define MCDS_DTU_TCXACMSK_VM_MASK          (0x7U << 16)  /* [18:16] Virtual Machine ID mask */
#define MCDS_DTU_TCXACMSK_VM_SHIFT         16
#define MCDS_DTU_TCXACMSK_PRS_MASK         (0x7U << 20)  /* [22:20] Protection Set ID mask */
#define MCDS_DTU_TCXACMSK_PRS_SHIFT        20

/*  POB_WDSGNx — Signed Data Comparator “Sign” (TCXWDSGNx) */

#define MCDS_DTU_TCXWDSGN_SIGN_MASK        (0x3FU << 0)  /* [5:0]   Signed compare configuration */
#define MCDS_DTU_TCXWDSGN_SIGN_SHIFT       0

/****************************************************************************
 * Private Types
 ****************************************************************************/

/****************************************************************************
 * Private Functions Prototypes
 ****************************************************************************/

static int mcds_dtu_enable(FAR struct hwtracing_dev_s *htdev);
static void mcds_dtu_disable(FAR struct hwtracing_dev_s *htdev);
static int mcds_dtu_init(FAR struct hwtracing_dev_s *htdev);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct hwtracing_source_ops_s g_mcds_dtu_source_ops =
{
  .enable  = mcds_dtu_enable,
  .disable = mcds_dtu_disable,
  .init    = mcds_dtu_init,
};

static const struct hwtracing_ops_s g_mcds_dtu_ops =
{
  .source_ops = &g_mcds_dtu_source_ops,
};

static const struct mcds_evt_matrix_s g_dtu_evt_matrix[DTU_NR_TOTAL_CMP] =
{
  /* dtu_ea_trg */

  {
    5,
    {
      {0, 7},
      {1, 7},
      {6, 6},
      {12, 5},
      {14, 5}
    }
  },
  {
    5,
    {
      {0, 8},
      {1, 8},
      {6, 7},
      {12, 6},
      {14, 6}
    }
  },
  {
    5,
    {
      {2, 7},
      {3, 7},
      {6, 8},
      {12, 7},
      {14, 7}
    }
  },
  {
    4,
    {
      {2, 8},
      {3, 8},
      {7, 6},
      {14, 8}
    }
  },
  {
    5,
    {
      {4, 7},
      {5, 7},
      {7, 7},
      {13, 5},
      {15, 5}
    }
  },
  {
    5,
    {
      {4, 8},
      {5, 8},
      {7, 8},
      {13, 6},
      {15, 6}
    }
  },
  {
    6,
    {
      {8, 7},
      {9, 7},
      {10, 7},
      {11, 7},
      {13, 7},
      {15, 7}
    }
  },
  { 5,
    {
      {8, 8},
      {9, 8},
      {10, 8},
      {11, 8},
      {15, 8}
    }
  },

  /* dtu_dat_trg */

  {
    7,
    {
      {0, 9},
      {4, 9},
      {8, 9},
      {12, 8},
      {13, 8},
      {14, 9},
      {15, 9}
    }
  },
  {
    7,
    {
      {1, 9},
      {5, 9},
      {9, 9},
      {12, 9},
      {13, 9},
      {14, 10},
      {15, 10}
    }
  },
  {
    3,
    {
      {2, 9},
      {6, 9},
      {10, 9}
    }
  },
  {
    3,
    {
      {3, 9},
      {7, 9},
      {11, 9}
    }
  },

  /* dtu_acc_trg */

  {
    15,
    {
      {0, 10},
      {1, 10},
      {2, 10},
      {3, 10},
      {4, 10},
      {5, 10},
      {6, 10},
      {7, 10},
      {8, 10},
      {9, 10},
      {10, 10},
      {11, 10},
      {12, 10},
      {13, 10},
      {14, 11},
      {15, 11}
    }
  },
  {
    8,
    {
      {4, 11},
      {5, 11},
      {8, 11},
      {9, 11},
      {12, 11},
      {13, 11},
      {14, 12},
      {15, 12}
    }
  },
  {
    6,
    {
      {6, 11},
      {7, 11},
      {10, 11},
      {11, 11},
      {12, 12},
      {13, 12}
    }
  },
  {
    6,
    {
      {6, 12},
      {7, 12},
      {8, 12},
      {9, 12},
      {14, 13},
      {15, 13}
    }
  },
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: mcds_dtu_getcap
 *
 * Description:
 *   Get capabilities of current dtu architecture .
 *
 ****************************************************************************/

static void mcds_dtu_getcap(FAR struct tricoreht_mcds_dtu_dev_s *dtudev)
{
  dtudev->nr_ea_cmp   = DTU_NR_EA_CMP;
  dtudev->nr_attr_cmp = DTU_NR_ATTR_CMP;
  dtudev->nr_data_cmp = DTU_NR_DATA_CMP;
}

/****************************************************************************
 * Name: mcds_dtu_set_pob
 ****************************************************************************/

static inline void mcds_dtu_set_pob(enum mcds_pob_e pob,
                                    FAR const struct mcds_dtu_tc_s *tc)
{
  switch (pob)
    {
      case MCDS_MCDS4P_POBX:

        /* ---- TCX: EA comparators ---- */

        MCDS4P_TCXEABND0.U = tc->ea_bnd[0];
        MCDS4P_TCXEARNG0.U = tc->ea_rng[0];
        MCDS4P_TCXEABND1.U = tc->ea_bnd[1];
        MCDS4P_TCXEARNG1.U = tc->ea_rng[1];
        MCDS4P_TCXEABND2.U = tc->ea_bnd[2];
        MCDS4P_TCXEARNG2.U = tc->ea_rng[2];
        MCDS4P_TCXEABND3.U = tc->ea_bnd[3];
        MCDS4P_TCXEARNG3.U = tc->ea_rng[3];
        MCDS4P_TCXEABND4.U = tc->ea_bnd[4];
        MCDS4P_TCXEARNG4.U = tc->ea_rng[4];
        MCDS4P_TCXEABND5.U = tc->ea_bnd[5];
        MCDS4P_TCXEARNG5.U = tc->ea_rng[5];
        MCDS4P_TCXEABND6.U = tc->ea_bnd[6];
        MCDS4P_TCXEARNG6.U = tc->ea_rng[6];
        MCDS4P_TCXEABND7.U = tc->ea_bnd[7];
        MCDS4P_TCXEARNG7.U = tc->ea_rng[7];

        /* ---- TCX: WD comparators ---- */

        MCDS4P_TCXWDBND0.U  = tc->wd_bnd_lo[0];
        MCDS4P_TCXWDHBND0.U = tc->wd_bnd_hi[0];
        MCDS4P_TCXWDBND1.U  = tc->wd_bnd_lo[1];
        MCDS4P_TCXWDHBND1.U = tc->wd_bnd_hi[1];
        MCDS4P_TCXWDBND2.U  = tc->wd_bnd_lo[2];
        MCDS4P_TCXWDHBND2.U = tc->wd_bnd_hi[2];
        MCDS4P_TCXWDBND3.U  = tc->wd_bnd_lo[3];
        MCDS4P_TCXWDHBND3.U = tc->wd_bnd_hi[3];
        MCDS4P_TCXWDRNG0.U  = tc->wd_rng_lo[0];
        MCDS4P_TCXWDHRNG0.U = tc->wd_rng_hi[0];
        MCDS4P_TCXWDRNG1.U  = tc->wd_rng_lo[1];
        MCDS4P_TCXWDHRNG1.U = tc->wd_rng_hi[1];
        MCDS4P_TCXWDRNG2.U  = tc->wd_rng_lo[2];
        MCDS4P_TCXWDHRNG2.U = tc->wd_rng_hi[2];
        MCDS4P_TCXWDRNG3.U  = tc->wd_rng_lo[3];
        MCDS4P_TCXWDHRNG3.U = tc->wd_rng_hi[3];
        MCDS4P_TCXWDMSK0.U  = tc->wd_msk_lo[0];
        MCDS4P_TCXWDHMSK0.U = tc->wd_msk_hi[0];
        MCDS4P_TCXWDSGN0.U  = tc->wd_sign[0];
        MCDS4P_TCXWDMSK1.U  = tc->wd_msk_lo[1];
        MCDS4P_TCXWDHMSK1.U = tc->wd_msk_hi[1];
        MCDS4P_TCXWDSGN1.U  = tc->wd_sign[1];
        MCDS4P_TCXWDMSK2.U  = tc->wd_msk_lo[2];
        MCDS4P_TCXWDHMSK2.U = tc->wd_msk_hi[2];
        MCDS4P_TCXWDSGN2.U  = tc->wd_sign[2];
        MCDS4P_TCXWDMSK3.U  = tc->wd_msk_lo[3];
        MCDS4P_TCXWDHMSK3.U = tc->wd_msk_hi[3];
        MCDS4P_TCXWDSGN3.U  = tc->wd_sign[3];

        /* ---- DCU fifo level ---- */

        MCDS4P_TCXDTUFLV.U = 0;

        break;

      case MCDS_MCDS4P_POBY:

        /* ---- TCY: EA ---- */

        MCDS4P_TCYEABND0.U = tc->ea_bnd[0];
        MCDS4P_TCYEARNG0.U = tc->ea_rng[0];
        MCDS4P_TCYEABND1.U = tc->ea_bnd[1];
        MCDS4P_TCYEARNG1.U = tc->ea_rng[1];
        MCDS4P_TCYEABND2.U = tc->ea_bnd[2];
        MCDS4P_TCYEARNG2.U = tc->ea_rng[2];
        MCDS4P_TCYEABND3.U = tc->ea_bnd[3];
        MCDS4P_TCYEARNG3.U = tc->ea_rng[3];
        MCDS4P_TCYEABND4.U = tc->ea_bnd[4];
        MCDS4P_TCYEARNG4.U = tc->ea_rng[4];
        MCDS4P_TCYEABND5.U = tc->ea_bnd[5];
        MCDS4P_TCYEARNG5.U = tc->ea_rng[5];
        MCDS4P_TCYEABND6.U = tc->ea_bnd[6];
        MCDS4P_TCYEARNG6.U = tc->ea_rng[6];
        MCDS4P_TCYEABND7.U = tc->ea_bnd[7];
        MCDS4P_TCYEARNG7.U = tc->ea_rng[7];

        /* ---- TCY: WD ---- */

        MCDS4P_TCYWDBND0.U  = tc->wd_bnd_lo[0];
        MCDS4P_TCYWDHBND0.U = tc->wd_bnd_hi[0];
        MCDS4P_TCYWDBND1.U  = tc->wd_bnd_lo[1];
        MCDS4P_TCYWDHBND1.U = tc->wd_bnd_hi[1];
        MCDS4P_TCYWDBND2.U  = tc->wd_bnd_lo[2];
        MCDS4P_TCYWDHBND2.U = tc->wd_bnd_hi[2];
        MCDS4P_TCYWDBND3.U  = tc->wd_bnd_lo[3];
        MCDS4P_TCYWDHBND3.U = tc->wd_bnd_hi[3];
        MCDS4P_TCYWDRNG0.U  = tc->wd_rng_lo[0];
        MCDS4P_TCYWDHRNG0.U = tc->wd_rng_hi[0];
        MCDS4P_TCYWDRNG1.U  = tc->wd_rng_lo[1];
        MCDS4P_TCYWDHRNG1.U = tc->wd_rng_hi[1];
        MCDS4P_TCYWDRNG2.U  = tc->wd_rng_lo[2];
        MCDS4P_TCYWDHRNG2.U = tc->wd_rng_hi[2];
        MCDS4P_TCYWDRNG3.U  = tc->wd_rng_lo[3];
        MCDS4P_TCYWDHRNG3.U = tc->wd_rng_hi[3];
        MCDS4P_TCYWDMSK0.U  = tc->wd_msk_lo[0];
        MCDS4P_TCYWDHMSK0.U = tc->wd_msk_hi[0];
        MCDS4P_TCYWDSGN0.U  = tc->wd_sign[0];
        MCDS4P_TCYWDMSK1.U  = tc->wd_msk_lo[1];
        MCDS4P_TCYWDHMSK1.U = tc->wd_msk_hi[1];
        MCDS4P_TCYWDSGN1.U  = tc->wd_sign[1];
        MCDS4P_TCYWDMSK2.U  = tc->wd_msk_lo[2];
        MCDS4P_TCYWDHMSK2.U = tc->wd_msk_hi[2];
        MCDS4P_TCYWDSGN2.U  = tc->wd_sign[2];
        MCDS4P_TCYWDMSK3.U  = tc->wd_msk_lo[3];
        MCDS4P_TCYWDHMSK3.U = tc->wd_msk_hi[3];
        MCDS4P_TCYWDSGN3.U  = tc->wd_sign[3];

        /* ---- DCU fifo level ---- */

        MCDS4P_TCYDTUFLV.U = 0;

        break;

      case MCDS_MCDS4P_POBW:

        /* ---- TCW: EA ---- */

        MCDS4P_TCWEABND0.U = tc->ea_bnd[0];
        MCDS4P_TCWEARNG0.U = tc->ea_rng[0];
        MCDS4P_TCWEABND1.U = tc->ea_bnd[1];
        MCDS4P_TCWEARNG1.U = tc->ea_rng[1];
        MCDS4P_TCWEABND2.U = tc->ea_bnd[2];
        MCDS4P_TCWEARNG2.U = tc->ea_rng[2];
        MCDS4P_TCWEABND3.U = tc->ea_bnd[3];
        MCDS4P_TCWEARNG3.U = tc->ea_rng[3];
        MCDS4P_TCWEABND4.U = tc->ea_bnd[4];
        MCDS4P_TCWEARNG4.U = tc->ea_rng[4];
        MCDS4P_TCWEABND5.U = tc->ea_bnd[5];
        MCDS4P_TCWEARNG5.U = tc->ea_rng[5];
        MCDS4P_TCWEABND6.U = tc->ea_bnd[6];
        MCDS4P_TCWEARNG6.U = tc->ea_rng[6];
        MCDS4P_TCWEABND7.U = tc->ea_bnd[7];
        MCDS4P_TCWEARNG7.U = tc->ea_rng[7];

        /* ---- TCW: WD ---- */

        MCDS4P_TCWWDBND0.U  = tc->wd_bnd_lo[0];
        MCDS4P_TCWWDHBND0.U = tc->wd_bnd_hi[0];
        MCDS4P_TCWWDBND1.U  = tc->wd_bnd_lo[1];
        MCDS4P_TCWWDHBND1.U = tc->wd_bnd_hi[1];
        MCDS4P_TCWWDBND2.U  = tc->wd_bnd_lo[2];
        MCDS4P_TCWWDHBND2.U = tc->wd_bnd_hi[2];
        MCDS4P_TCWWDBND3.U  = tc->wd_bnd_lo[3];
        MCDS4P_TCWWDHBND3.U = tc->wd_bnd_hi[3];
        MCDS4P_TCWWDRNG0.U  = tc->wd_rng_lo[0];
        MCDS4P_TCWWDHRNG0.U = tc->wd_rng_hi[0];
        MCDS4P_TCWWDRNG1.U  = tc->wd_rng_lo[1];
        MCDS4P_TCWWDHRNG1.U = tc->wd_rng_hi[1];
        MCDS4P_TCWWDRNG2.U  = tc->wd_rng_lo[2];
        MCDS4P_TCWWDHRNG2.U = tc->wd_rng_hi[2];
        MCDS4P_TCWWDRNG3.U  = tc->wd_rng_lo[3];
        MCDS4P_TCWWDHRNG3.U = tc->wd_rng_hi[3];
        MCDS4P_TCWWDMSK0.U  = tc->wd_msk_lo[0];
        MCDS4P_TCWWDHMSK0.U = tc->wd_msk_hi[0];
        MCDS4P_TCWWDSGN0.U  = tc->wd_sign[0];
        MCDS4P_TCWWDMSK1.U  = tc->wd_msk_lo[1];
        MCDS4P_TCWWDHMSK1.U = tc->wd_msk_hi[1];
        MCDS4P_TCWWDSGN1.U  = tc->wd_sign[1];
        MCDS4P_TCWWDMSK2.U  = tc->wd_msk_lo[2];
        MCDS4P_TCWWDHMSK2.U = tc->wd_msk_hi[2];
        MCDS4P_TCWWDSGN2.U  = tc->wd_sign[2];
        MCDS4P_TCWWDMSK3.U  = tc->wd_msk_lo[3];
        MCDS4P_TCWWDHMSK3.U = tc->wd_msk_hi[3];
        MCDS4P_TCWWDSGN3.U  = tc->wd_sign[3];

        /* ---- DCU fifo level ---- */

        MCDS4P_TCWDTUFLV.U = 0;

        break;

      case MCDS_MCDS4P_POBZ:

        /* ---- TCZ: EA ---- */

        MCDS4P_TCZEABND0.U = tc->ea_bnd[0];
        MCDS4P_TCZEARNG0.U = tc->ea_rng[0];
        MCDS4P_TCZEABND1.U = tc->ea_bnd[1];
        MCDS4P_TCZEARNG1.U = tc->ea_rng[1];
        MCDS4P_TCZEABND2.U = tc->ea_bnd[2];
        MCDS4P_TCZEARNG2.U = tc->ea_rng[2];
        MCDS4P_TCZEABND3.U = tc->ea_bnd[3];
        MCDS4P_TCZEARNG3.U = tc->ea_rng[3];
        MCDS4P_TCZEABND4.U = tc->ea_bnd[4];
        MCDS4P_TCZEARNG4.U = tc->ea_rng[4];
        MCDS4P_TCZEABND5.U = tc->ea_bnd[5];
        MCDS4P_TCZEARNG5.U = tc->ea_rng[5];
        MCDS4P_TCZEABND6.U = tc->ea_bnd[6];
        MCDS4P_TCZEARNG6.U = tc->ea_rng[6];
        MCDS4P_TCZEABND7.U = tc->ea_bnd[7];
        MCDS4P_TCZEARNG7.U = tc->ea_rng[7];

        /* ---- TCZ: WD ---- */

        MCDS4P_TCZWDBND0.U  = tc->wd_bnd_lo[0];
        MCDS4P_TCZWDHBND0.U = tc->wd_bnd_hi[0];
        MCDS4P_TCZWDBND1.U  = tc->wd_bnd_lo[1];
        MCDS4P_TCZWDHBND1.U = tc->wd_bnd_hi[1];
        MCDS4P_TCZWDBND2.U  = tc->wd_bnd_lo[2];
        MCDS4P_TCZWDHBND2.U = tc->wd_bnd_hi[2];
        MCDS4P_TCZWDBND3.U  = tc->wd_bnd_lo[3];
        MCDS4P_TCZWDHBND3.U = tc->wd_bnd_hi[3];
        MCDS4P_TCZWDRNG0.U  = tc->wd_rng_lo[0];
        MCDS4P_TCZWDHRNG0.U = tc->wd_rng_hi[0];
        MCDS4P_TCZWDRNG1.U  = tc->wd_rng_lo[1];
        MCDS4P_TCZWDHRNG1.U = tc->wd_rng_hi[1];
        MCDS4P_TCZWDRNG2.U  = tc->wd_rng_lo[2];
        MCDS4P_TCZWDHRNG2.U = tc->wd_rng_hi[2];
        MCDS4P_TCZWDRNG3.U  = tc->wd_rng_lo[3];
        MCDS4P_TCZWDHRNG3.U = tc->wd_rng_hi[3];
        MCDS4P_TCZWDMSK0.U  = tc->wd_msk_lo[0];
        MCDS4P_TCZWDHMSK0.U = tc->wd_msk_hi[0];
        MCDS4P_TCZWDSGN0.U  = tc->wd_sign[0];
        MCDS4P_TCZWDMSK1.U  = tc->wd_msk_lo[1];
        MCDS4P_TCZWDHMSK1.U = tc->wd_msk_hi[1];
        MCDS4P_TCZWDSGN1.U  = tc->wd_sign[1];
        MCDS4P_TCZWDMSK2.U  = tc->wd_msk_lo[2];
        MCDS4P_TCZWDHMSK2.U = tc->wd_msk_hi[2];
        MCDS4P_TCZWDSGN2.U  = tc->wd_sign[2];
        MCDS4P_TCZWDMSK3.U  = tc->wd_msk_lo[3];
        MCDS4P_TCZWDHMSK3.U = tc->wd_msk_hi[3];
        MCDS4P_TCZWDSGN3.U  = tc->wd_sign[3];

        /* ---- DCU fifo level ---- */

        MCDS4P_TCZDTUFLV.U = 0;
        break;

#ifdef CONFIG_ARCH_CHIP_AURIX_TC4DX
      case MCDS_MCDS2P_POBX:

        /* ---- TCZ: EA ---- */

        MCDS2P_TCXEABND0.U = tc->ea_bnd[0];
        MCDS2P_TCXEARNG0.U = tc->ea_rng[0];
        MCDS2P_TCXEABND1.U = tc->ea_bnd[1];
        MCDS2P_TCXEARNG1.U = tc->ea_rng[1];
        MCDS2P_TCXEABND2.U = tc->ea_bnd[2];
        MCDS2P_TCXEARNG2.U = tc->ea_rng[2];
        MCDS2P_TCXEABND3.U = tc->ea_bnd[3];
        MCDS2P_TCXEARNG3.U = tc->ea_rng[3];
        MCDS2P_TCXEABND4.U = tc->ea_bnd[4];
        MCDS2P_TCXEARNG4.U = tc->ea_rng[4];
        MCDS2P_TCXEABND5.U = tc->ea_bnd[5];
        MCDS2P_TCXEARNG5.U = tc->ea_rng[5];
        MCDS2P_TCXEABND6.U = tc->ea_bnd[6];
        MCDS2P_TCXEARNG6.U = tc->ea_rng[6];
        MCDS2P_TCXEABND7.U = tc->ea_bnd[7];
        MCDS2P_TCXEARNG7.U = tc->ea_rng[7];

        /* ---- TCZ: WD ---- */

        MCDS2P_TCXWDBND0.U  = tc->wd_bnd_lo[0];
        MCDS2P_TCXWDHBND0.U = tc->wd_bnd_hi[0];
        MCDS2P_TCXWDBND1.U  = tc->wd_bnd_lo[1];
        MCDS2P_TCXWDHBND1.U = tc->wd_bnd_hi[1];
        MCDS2P_TCXWDBND2.U  = tc->wd_bnd_lo[2];
        MCDS2P_TCXWDHBND2.U = tc->wd_bnd_hi[2];
        MCDS2P_TCXWDBND3.U  = tc->wd_bnd_lo[3];
        MCDS2P_TCXWDHBND3.U = tc->wd_bnd_hi[3];
        MCDS2P_TCXWDRNG0.U  = tc->wd_rng_lo[0];
        MCDS2P_TCXWDHRNG0.U = tc->wd_rng_hi[0];
        MCDS2P_TCXWDRNG1.U  = tc->wd_rng_lo[1];
        MCDS2P_TCXWDHRNG1.U = tc->wd_rng_hi[1];
        MCDS2P_TCXWDRNG2.U  = tc->wd_rng_lo[2];
        MCDS2P_TCXWDHRNG2.U = tc->wd_rng_hi[2];
        MCDS2P_TCXWDRNG3.U  = tc->wd_rng_lo[3];
        MCDS2P_TCXWDHRNG3.U = tc->wd_rng_hi[3];
        MCDS2P_TCXWDMSK0.U  = tc->wd_msk_lo[0];
        MCDS2P_TCXWDHMSK0.U = tc->wd_msk_hi[0];
        MCDS2P_TCXWDSGN0.U  = tc->wd_sign[0];
        MCDS2P_TCXWDMSK1.U  = tc->wd_msk_lo[1];
        MCDS2P_TCXWDHMSK1.U = tc->wd_msk_hi[1];
        MCDS2P_TCXWDSGN1.U  = tc->wd_sign[1];
        MCDS2P_TCXWDMSK2.U  = tc->wd_msk_lo[2];
        MCDS2P_TCXWDHMSK2.U = tc->wd_msk_hi[2];
        MCDS2P_TCXWDSGN2.U  = tc->wd_sign[2];
        MCDS2P_TCXWDMSK3.U  = tc->wd_msk_lo[3];
        MCDS2P_TCXWDHMSK3.U = tc->wd_msk_hi[3];
        MCDS2P_TCXWDSGN3.U  = tc->wd_sign[3];

        /* ---- DCU fifo level ---- */

        MCDS2P_TCXDTUFLV.U = 0;
        break;

      case MCDS_MCDS2P_POBW:

        /* ---- TCZ: EA ---- */

        MCDS2P_TCWEABND0.U = tc->ea_bnd[0];
        MCDS2P_TCWEARNG0.U = tc->ea_rng[0];
        MCDS2P_TCWEABND1.U = tc->ea_bnd[1];
        MCDS2P_TCWEARNG1.U = tc->ea_rng[1];
        MCDS2P_TCWEABND2.U = tc->ea_bnd[2];
        MCDS2P_TCWEARNG2.U = tc->ea_rng[2];
        MCDS2P_TCWEABND3.U = tc->ea_bnd[3];
        MCDS2P_TCWEARNG3.U = tc->ea_rng[3];
        MCDS2P_TCWEABND4.U = tc->ea_bnd[4];
        MCDS2P_TCWEARNG4.U = tc->ea_rng[4];
        MCDS2P_TCWEABND5.U = tc->ea_bnd[5];
        MCDS2P_TCWEARNG5.U = tc->ea_rng[5];
        MCDS2P_TCWEABND6.U = tc->ea_bnd[6];
        MCDS2P_TCWEARNG6.U = tc->ea_rng[6];
        MCDS2P_TCWEABND7.U = tc->ea_bnd[7];
        MCDS2P_TCWEARNG7.U = tc->ea_rng[7];

        /* ---- TCZ: WD ---- */

        MCDS2P_TCWWDBND0.U  = tc->wd_bnd_lo[0];
        MCDS2P_TCWWDHBND0.U = tc->wd_bnd_hi[0];
        MCDS2P_TCWWDBND1.U  = tc->wd_bnd_lo[1];
        MCDS2P_TCWWDHBND1.U = tc->wd_bnd_hi[1];
        MCDS2P_TCWWDBND2.U  = tc->wd_bnd_lo[2];
        MCDS2P_TCWWDHBND2.U = tc->wd_bnd_hi[2];
        MCDS2P_TCWWDBND3.U  = tc->wd_bnd_lo[3];
        MCDS2P_TCWWDHBND3.U = tc->wd_bnd_hi[3];
        MCDS2P_TCWWDRNG0.U  = tc->wd_rng_lo[0];
        MCDS2P_TCWWDHRNG0.U = tc->wd_rng_hi[0];
        MCDS2P_TCWWDRNG1.U  = tc->wd_rng_lo[1];
        MCDS2P_TCWWDHRNG1.U = tc->wd_rng_hi[1];
        MCDS2P_TCWWDRNG2.U  = tc->wd_rng_lo[2];
        MCDS2P_TCWWDHRNG2.U = tc->wd_rng_hi[2];
        MCDS2P_TCWWDRNG3.U  = tc->wd_rng_lo[3];
        MCDS2P_TCWWDHRNG3.U = tc->wd_rng_hi[3];
        MCDS2P_TCWWDMSK0.U  = tc->wd_msk_lo[0];
        MCDS2P_TCWWDHMSK0.U = tc->wd_msk_hi[0];
        MCDS2P_TCWWDSGN0.U  = tc->wd_sign[0];
        MCDS2P_TCWWDMSK1.U  = tc->wd_msk_lo[1];
        MCDS2P_TCWWDHMSK1.U = tc->wd_msk_hi[1];
        MCDS2P_TCWWDSGN1.U  = tc->wd_sign[1];
        MCDS2P_TCWWDMSK2.U  = tc->wd_msk_lo[2];
        MCDS2P_TCWWDHMSK2.U = tc->wd_msk_hi[2];
        MCDS2P_TCWWDSGN2.U  = tc->wd_sign[2];
        MCDS2P_TCWWDMSK3.U  = tc->wd_msk_lo[3];
        MCDS2P_TCWWDHMSK3.U = tc->wd_msk_hi[3];
        MCDS2P_TCWWDSGN3.U  = tc->wd_sign[3];

        /* ---- DCU fifo level ---- */

        MCDS2P_TCWDTUFLV.U = 0;
        break;
#endif

      default:
        return;
    }
}

/****************************************************************************
 * Name: mcds_dtu_set_tc
 ****************************************************************************/

static inline void mcds_dtu_set_tc(uint32_t cpuset,
                                   FAR struct tricoreht_mcds_dtu_dev_s *dev)
{
  if (cpuset & (1U << 0))
    {
      mcds_dtu_set_pob(MCDS_MCDS4P_POBX, &dev->cfg.dtu_tc[0]);
    }

  if (cpuset & (1U << 1))
    {
      mcds_dtu_set_pob(MCDS_MCDS4P_POBY, &dev->cfg.dtu_tc[1]);
    }

  if (cpuset & (1U << 2))
    {
      mcds_dtu_set_pob(MCDS_MCDS4P_POBW, &dev->cfg.dtu_tc[2]);
    }

  if (cpuset & (1U << 3))
    {
      mcds_dtu_set_pob(MCDS_MCDS4P_POBZ, &dev->cfg.dtu_tc[3]);
    }

#ifdef CONFIG_ARCH_CHIP_AURIX_TC4DX
  if (cpuset & (1U << 4))
    {
      mcds_dtu_set_pob(MCDS_MCDS2P_POBX, &dev->cfg.dtu_tc[4]);
    }

  if (cpuset & (1U << 5))
    {
      mcds_dtu_set_pob(MCDS_MCDS2P_POBW, &dev->cfg.dtu_tc[5]);
    }
#endif
}

/****************************************************************************
 * Name: mcds_dtu_init
 ****************************************************************************/

static int mcds_dtu_init(FAR struct hwtracing_dev_s *htdev)
{
  int ret;

  ret = mcds_init(htdev);

  mcds_tbuf_init(MCDS_TRIGGER_MODE_PRE_POST);

  return ret;
}

/****************************************************************************
 * Name: mcds_dtu_evt_settrigger
 ****************************************************************************/

static inline void mcds_dtu_evt_settrigger(FAR
                                           struct tricoreht_mcds_dtu_dev_s
                                           *dtudev, int reg_type, int pob)
{
  int num = 0;
  uint8_t cmpset = 0;
  int tcevt;
  int evt_eiq;
  uint32_t *evt_regs[MCDS_MCDS4P2P_MAX] =
  {
    (uint32_t *)&MCDS4P_TCXEVT0.U,
    (uint32_t *)&MCDS4P_TCYEVT0.U,
    (uint32_t *)&MCDS4P_TCWEVT0.U,
    (uint32_t *)&MCDS4P_TCZEVT0.U,
#ifdef CONFIG_ARCH_CHIP_AURIX_TC4DX
    (uintptr_t)(&MCDS2P_TCXEVT0.U),
    (uintptr_t)(&MCDS2P_TCWEVT0.U),
#endif
  };

  if (reg_type == DTU_DA_TYPE)
    {
      num    = DTU_NR_DATA_CMP;
      cmpset = dtudev->cfg.dtu_tc[pob].wdset;
    }
  else if (reg_type == DTU_EA_TYPE)
    {
      num    = DTU_NR_EA_CMP;
      cmpset = dtudev->cfg.dtu_tc[pob].easet;
    }

  for (int i = 0; i < num; i++)
    {
      if (cmpset & (1U << i))
        {
          tcevt   = g_dtu_evt_matrix[i].evt_matrix[0].tcevt;
          evt_eiq = g_dtu_evt_matrix[i].evt_matrix[0].evt_eiq;

          hwtracing_put32(MCDS_CDS4P2P_EVT_DEFAULT,
                          (uintptr_t)(evt_regs[pob] + tcevt));
          hwtracing_modify32(dtudev->cfg.dtu_tc[i].trigger_mode
                             << MCDS_MCDS4P2P_TCEVT_EIQ_SHIFT(evt_eiq),
                             MCDS_MCDS4P2P_TCEVT_EIQ_MASK(evt_eiq),
                             (uintptr_t)(evt_regs[pob] + tcevt));
          htinfo("pob %d tcevt %d eiq %d evt %08" PRIx32 "\n",
                 pob, tcevt, evt_eiq, *(evt_regs[pob] + tcevt));
        }
    }
}

/****************************************************************************
 * Name: mcds_dtu_act_setevt
 ****************************************************************************/

static inline void mcds_dtu_act_setevt(FAR struct tricoreht_mcds_dtu_dev_s
                                       *dtudev, int reg_type,
                                       uint32_t action, int pob)
{
  int idx = 0;
  uint8_t cmpset = 0;
  uintptr_t ac_regs[MCDS_MCDS4P2P_MAX] =
    {
      (uintptr_t)(&MCDS4P_TCXACT0.U + action),
      (uintptr_t)(&MCDS4P_TCYACT0.U + action),
      (uintptr_t)(&MCDS4P_TCWACT0.U + action),
      (uintptr_t)(&MCDS4P_TCZACT0.U + action),
#ifdef CONFIG_ARCH_CHIP_AURIX_TC4DX
      (uintptr_t)(&MCDS2P_TCXACT0.U + action),
      (uintptr_t)(&MCDS2P_TCWACT0.U + action),
#endif
    };

  if (reg_type == DTU_DA_TYPE)
    {
      idx    = DTU_NR_EA_CMP;
      cmpset = dtudev->cfg.dtu_tc[pob].wdset;
    }
  else if (reg_type == DTU_EA_TYPE)
    {
      cmpset = dtudev->cfg.dtu_tc[pob].easet;
    }

  for (int i = 0; i < MCDS_CDS4P2P_ACT_MAX_EVTNUM; i++)
    {
      if (cmpset & (1U << i))
        {
          /* 1. Each action group supports 4 event configurations.
           * Here, all DTU comparison registers are configured
           * into the action, and the action is finally triggered
           * in an OR manner.
           * 2. By default, each DTU trigger event uses the first
           * index in the event matrix
           */

          hwtracing_modify32((g_dtu_evt_matrix[i + idx].evt_matrix[0].tcevt
                             << MCDS_MCDS4P2P_TCACT_AIS_SHIFT(i)) |
                             (dtudev->cfg.dtu_tc[pob].trigger_mode <<
                             MCDS_MCDS4P2P_TCACT_AIQ_SHIFT(i)) |
                             MCDS_MCDS4P2P_TCACT_LV(i),
                             MCDS_MCDS4P2P_TCACT_AIS_MASK(i) |
                             MCDS_MCDS4P2P_TCACT_AIQ_MASK(i) |
                             MCDS_MCDS4P2P_TCACT_LV(0),
                             ac_regs[pob]);

          htinfo("pob %d tcevt %d actn %" PRIu32 " act %08" PRIx32 "\n",
                 pob,
                 g_dtu_evt_matrix[i + idx].evt_matrix[0].tcevt,
                 action,
                 *(uint32_t *)(ac_regs[pob]));
        }
    }
}

/****************************************************************************
 * Name: mcds_dtu_enable
 ****************************************************************************/

static int mcds_dtu_enable(FAR struct hwtracing_dev_s *htdev)
{
  int cpu;
  int cpuset;
  FAR struct tricoreht_mcds_dtu_dev_s *dtudev =
          (FAR struct tricoreht_mcds_dtu_dev_s *)htdev;

  cpuset = dtudev->cfg.cpuset;

  for (cpu = 0; cpu < MCDS_MCDS4P2P_MAX; cpu++)
    {
      if (cpuset & (1U << cpu))
        {
          switch (dtudev->cfg.dtu_tc[cpu].msg_type)
            {
              case MCDS_DTU_MSG_DTW:

                   /* dtu_wdat high dtu_wadr high level */

                   mcds_dtu_evt_settrigger(dtudev, DTU_EA_TYPE, cpu);
                   mcds_dtu_act_setevt(dtudev, DTU_EA_TYPE, 2, cpu);
                   mcds_dtu_act_setevt(dtudev, DTU_EA_TYPE, 3, cpu);

                   break;

              case MCDS_DTU_MSG_DTWA:

                   /* dtu_wdat low dtu_wadr high level */

                  mcds_dtu_evt_settrigger(dtudev, DTU_EA_TYPE, cpu);
                  mcds_dtu_act_setevt(dtudev, DTU_EA_TYPE, 3, cpu);

                  break;

              case MCDS_DTU_MSG_DTWD:

                  /* dtu_wdat high dtu_wadr low level */

                  mcds_dtu_evt_settrigger(dtudev, DTU_DA_TYPE, cpu);
                  mcds_dtu_act_setevt(dtudev, DTU_DA_TYPE, 2, cpu);

                  break;

                case MCDS_DTU_MSG_DTR:

                  /* dtu_wdat high dtu_wadr high level */

                  mcds_dtu_evt_settrigger(dtudev, DTU_EA_TYPE, cpu);
                  mcds_dtu_act_setevt(dtudev, DTU_EA_TYPE, 4, cpu);
                  mcds_dtu_act_setevt(dtudev, DTU_EA_TYPE, 5, cpu);

                case MCDS_DTU_MSG_DTRA:

                  /* dtu_wdat low dtu_wadr high level */

                  mcds_dtu_evt_settrigger(dtudev, DTU_EA_TYPE, cpu);
                  mcds_dtu_act_setevt(dtudev, DTU_EA_TYPE, 5, cpu);

                  break;

                case MCDS_DTU_MSG_DTRD:

                  /* dtu_rdat high dtu_radr low level */

                  mcds_dtu_evt_settrigger(dtudev, DTU_DA_TYPE, cpu);
                  mcds_dtu_act_setevt(dtudev, DTU_DA_TYPE, 4, cpu);

                  break;

                case MCDS_DTU_MSG_ERR:
                  break;

                default:

                  /* Unknown msg_type */

                  break;
              }
         }
    }

  mcds_dtu_set_tc(cpuset, dtudev);

  mcds_enable(htdev);

  return 0;
}

/****************************************************************************
 * Name: mcds_dtu_disable
 ****************************************************************************/

static void mcds_dtu_disable(FAR struct hwtracing_dev_s *htdev)
{
  mcds_disable(htdev);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: mcds_dtu_config
 *
 * Description:
 *   Configure the dtu device.
 *
 * Input Parameters:
 *   dtudev  - Pointer to the dtu device to config.
 *   config  - Configuration need to be set to dtu device.
 *
 * Returned Value:
 *   Zero on success; a negative value on failure.
 *
 ****************************************************************************/

int mcds_dtu_config(FAR struct tricoreht_mcds_dtu_dev_s *dtudev,
                    FAR const struct mcds_dtu_config_s *config)
{
  memcpy(&dtudev->cfg, config, sizeof(struct mcds_dtu_config_s));

  return 0;
}

/****************************************************************************
 * Name: mcds_dtu_register
 *
 * Description:
 *   Register an dtu/PTM devices.
 *
 * Input Parameters:
 *   desc  - A description of this coresight device.
 *
 * Returned Value:
 *   Pointer to an dtu device on success; NULL on failure.
 *
 ****************************************************************************/

FAR struct tricoreht_mcds_dtu_dev_s *
mcds_dtu_register(FAR const struct hwtracing_desc_s *desc)
{
  FAR struct tricoreht_mcds_dtu_dev_s *dtudev;
  FAR struct hwtracing_dev_s *htdev;
  int ret;

  dtudev = kmm_zalloc(sizeof(struct tricoreht_mcds_dtu_dev_s));
  if (dtudev == NULL)
    {
      hterr("%s:malloc failed!\n", desc->name);
      return NULL;
    }

  dtudev->htdev.addr = desc->addr;
  mcds_dtu_getcap(dtudev);

  htdev = &dtudev->htdev;
  htdev->ops = &g_mcds_dtu_ops;
  ret = hwtracing_register(htdev, desc);
  if (ret < 0)
    {
      kmm_free(dtudev);
      hterr("%s:register failed\n", desc->name);
      return NULL;
    }

  return dtudev;
}

/****************************************************************************
 * Name: mcds_dtu_unregister
 *
 * Description:
 *   Unregister an EMT/PTM device.
 *
 ****************************************************************************/

void mcds_dtu_unregister(FAR struct tricoreht_mcds_dtu_dev_s *dtudev)
{
  hwtracing_unregister(&dtudev->htdev);
  kmm_free(dtudev);
}
