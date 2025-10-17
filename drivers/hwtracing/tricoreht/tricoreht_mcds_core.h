/****************************************************************************
 * drivers/hwtracing/tricoreht/tricoreht_mcds_core.h
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

#ifndef __DRIVERS_TRICOREHT_TRICOREHT_MCDS_CORE_H
#define __DRIVERS_TRICOREHT_TRICOREHT_MCDS_CORE_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/hwtracing/hwtracing.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MCDS_MCDS4P_TBUF_SIZE                 0x8000
#ifdef CONFIG_ARCH_CHIP_AURIX_TC4DX
#  define MCDS_MCDS2P_TBUF_SIZE               0x4000
#elif defined(CONFIG_ARCH_CHIP_AURIX_TC48X)
#  define MCDS_MCDS2P_TBUF_SIZE               0
#endif
#define MCDS_TBUF_SIZE                        MCDS_MCDS4P_TBUF_SIZE + MCDS_MCDS2P_TBUF_SIZE
#define MCDS_MCDS4P_PRE_TRIGGER_DEFAULT       (0x00007FE0U)      /* Pre-trigger/Pre-post */
#define MCDS_MCDS2P_PRE_TRIGGER_DEFAULT       (0x00003FE0U)      /* Pre-trigger/Pre-post */
#define MCDS_MCDS4P2P_NR_EVT                  16
#define MCDS_MCDS4P2P_NR_ACT                  24
#define MCDS_CDS4P2P_ACT_MAX_EVTNUM           4
#define MCDS_CDS4P2P_EVT_DEFAULT              (0xffffffff)

/* POB_ACTx — Evt Definition  */

#define MCDS_MCDS4P2P_TCEVT_EIQ_SHIFT(q)      (2U * (q))        /* q = 0..15 */
#define MCDS_MCDS4P2P_TCEVT_EIQ_MASK(q)       (0x3U << MCDS_MCDS4P2P_TCEVT_EIQ_SHIFT(q))
#define MCDS_MCDS4P2P_TCEVT_EIQ_DISABLED      0U    /* 00b: input replaced by 0 */
#define MCDS_MCDS4P2P_TCEVT_EIQ_INVERT        1U    /* 01b: inverted */
#define MCDS_MCDS4P2P_TCEVT_EIQ_DIRECT        2U    /* 10b: direct */
#define MCDS_MCDS4P2P_TCEVT_EIQ_IGNORE        3U    /* 11b: input replaced by 1 */

/* POB_ACTx — Action Definition (TCACTx) */

#define MCDS_MCDS4P2P_TCACT_AIS_SHIFT(q)      (8U * (q))       /* [8*q+4 : 8*q], 5 bits: Event ID (0..31) q = 0..3*/
#define MCDS_MCDS4P2P_TCACT_AIS_MASK(q)       (0x1FU << MCDS_MCDS4P2P_TCACT_AIS_SHIFT(q))
#define MCDS_MCDS4P2P_TCACT_AIQ_SHIFT(q)      (8U * (q) + 5U)     /* [8*q+6 : 8*q+5], 2 bits: qualifier */
#define MCDS_MCDS4P2P_TCACT_AIQ_MASK(q)       (0x3U  << MCDS_MCDS4P2P_TCACT_AIQ_SHIFT(q))
#define MCDS_MCDS4P2P_TCACT_LV(q)             (1U << (8U * (q) + 7U))     /* [8*q+7], 1 bit: 0=edge, 1=level */
#define MCDS_MCDS4P2P_TCACT_AIQ_IGNORE        0U
#define MCDS_MCDS4P2P_TCACT_AIQ_INVERT        1U
#define MCDS_MCDS4P2P_TCACT_AIQ_DIRECT        2U
#define MCDS_MCDS4P2P_TCACT_AIQ_FORCE1        3U

/* TMUX Trace Source Control (MUX) */

#define MCDS_MCDS4P2P_MUX_TMUX0_MASK          (0x7U << 0)     /* [2:0]   Trace Source Select 0 */
#define MCDS_MCDS4P2P_MUX_TMUX0_SHIFT         0
#define MCDS_MCDS4P2P_MUX_TM0_EN              (1U << 3)       /* [3]     Trace Source Select 0 Enable */
#define MCDS_MCDS4P2P_MUX_TMUX0_RSVD_MASK     (0x7U << 4)     /* [6:4]   Reserved, read 0, write 0 */
#define MCDS_MCDS4P2P_MUX_TM0_P               (1U << 7)       /* [7]     Trace Source Select 0 Protection (write 1 with update) */
#define MCDS_MCDS4P2P_MUX_TMUX0_VAL(v)        (((uint32_t)((v) & 0x7U)) << MCDS_MCDS4P2P_MUX_TMUX0_SHIFT)
#define MCDS_MCDS4P2P_MUX_TMUX1_MASK          (0x7U << 8)   /* [10:8]  Trace Source Select 1 */
#define MCDS_MCDS4P2P_MUX_TMUX1_SHIFT         8
#define MCDS_MCDS4P2P_MUX_TM1_EN              (1U << 11)      /* [11]    Trace Source Select 1 Enable */
#define MCDS_MCDS4P2P_MUX_TMUX1_RSVD_MASK     (0x7U << 12)    /* [14:12] Reserved, read 0, write 0 */
#define MCDS_MCDS4P2P_MUX_TM1_P               (1U << 15)      /* [15]    Trace Source Select 1 Protection (write 1 with update) */
#define MCDS_MCDS4P2P_MUX_TMUX1_VAL(v)        (((uint32_t)((v) & 0x7U)) << MCDS_MCDS4P2P_MUX_TMUX1_SHIFT)
#define MCDS_MCDS4P2P_MUX_TMUX2_MASK          (0x7U << 16)    /* [18:16] Trace Source Select 2 */
#define MCDS_MCDS4P2P_MUX_TMUX2_SHIFT         16
#define MCDS_MCDS4P2P_MUX_TM2_EN              (1U << 19)      /* [19]    Trace Source Select 2 Enable */
#define MCDS_MCDS4P2P_MUX_TMUX2_RSVD_MASK     (0x7U << 20)    /* [22:20] Reserved, read 0, write 0 */
#define MCDS_MCDS4P2P_MUX_TM2_P               (1U << 23)      /* [23]    Trace Source Select 2 Protection (write 1 with update) */
#define MCDS_MCDS4P2P_MUX_TMUX2_VAL(v)        (((uint32_t)((v) & 0x7U)) << MCDS_MCDS4P2P_MUX_TMUX2_SHIFT)
#define MCDS_MCDS4P2P_MUX_TMUX3_MASK          (0x7U << 24)    /* [26:24] Trace Source Select 3 */
#define MCDS_MCDS4P2P_MUX_TMUX3_SHIFT         24
#define MCDS_MCDS4P2P_MUX_TM3_EN              (1U << 27)      /* [27]    Trace Source Select 3 Enable */
#define MCDS_MCDS4P2P_MUX_TMUX3_RSVD_MASK     (0x7U << 28)    /* [30:28] Reserved, read 0, write 0 */
#define MCDS_MCDS4P2P_MUX_TM3_P               (1U << 31)      /* [31]    Trace Source Select 3 Protection (write 1 with update) */
#define MCDS_MCDS4P2P_MUX_RSVD_ALL_MASK       (0x7U<<4) | (0x7U<<12) | (0x7U<<20) | (0x7U<<28) 

/* Unified TMUX value definitions (shifted) */

#define MCDS_MCDS4P2P_TMUX0_TCMUX_CPU0        (0x0u << MCDS_MCDS4P2P_MUX_TMUX0_SHIFT)
#define MCDS_MCDS4P2P_TMUX0_PPUMUX            (0x1u << MCDS_MCDS4P2P_MUX_TMUX0_SHIFT)
#define MCDS_MCDS4P2P_TMUX0_NA_001            (0x1u << MCDS_MCDS4P2P_MUX_TMUX0_SHIFT)
#define MCDS_MCDS4P2P_TMUX0_NA_010            (0x2u << MCDS_MCDS4P2P_MUX_TMUX0_SHIFT)
#define MCDS_MCDS4P2P_TMUX0_OLDA              (0x3u << MCDS_MCDS4P2P_MUX_TMUX0_SHIFT)
#define MCDS_MCDS4P2P_TMUX0_DMA0_MIF0         (0x4u << MCDS_MCDS4P2P_MUX_TMUX0_SHIFT)
#define MCDS_MCDS4P2P_TMUX0_NA_101            (0x5u << MCDS_MCDS4P2P_MUX_TMUX0_SHIFT)
#define MCDS_MCDS4P2P_TMUX0_COMPB             (0x6u << MCDS_MCDS4P2P_MUX_TMUX0_SHIFT)
#define MCDS_MCDS4P2P_TMUX0_NA_111            (0x7u << MCDS_MCDS4P2P_MUX_TMUX0_SHIFT)
#define MCDS_MCDS4P2P_TMUX1_TCMUX_CPU1        (0x0u << MCDS_MCDS4P2P_MUX_TMUX1_SHIFT)
#define MCDS_MCDS4P2P_TMUX1_OCDS_OTGB         (0x1u << MCDS_MCDS4P2P_MUX_TMUX1_SHIFT)
#define MCDS_MCDS4P2P_TMUX1_NA_001            (0x1u << MCDS_MCDS4P2P_MUX_TMUX1_SHIFT)
#define MCDS_MCDS4P2P_TMUX1_NA_010            (0x2u << MCDS_MCDS4P2P_MUX_TMUX1_SHIFT)
#define MCDS_MCDS4P2P_TMUX1_OLDA              (0x3u << MCDS_MCDS4P2P_MUX_TMUX1_SHIFT)
#define MCDS_MCDS4P2P_TMUX1_DMA0_MIF0         (0x4u << MCDS_MCDS4P2P_MUX_TMUX1_SHIFT)
#define MCDS_MCDS4P2P_TMUX1_NA_101            (0x5u << MCDS_MCDS4P2P_MUX_TMUX1_SHIFT)
#define MCDS_MCDS4P2P_TMUX1_SPB               (0x6u << MCDS_MCDS4P2P_MUX_TMUX1_SHIFT)
#define MCDS_MCDS4P2P_TMUX1_NA_111            (0x7u << MCDS_MCDS4P2P_MUX_TMUX1_SHIFT)
#define MCDS_MCDS4P2P_TMUX2_TCMUX_CPU2        (0x0u << MCDS_MCDS4P2P_MUX_TMUX2_SHIFT)
#define MCDS_MCDS4P2P_TMUX2_PPUMUX            (0x1u << MCDS_MCDS4P2P_MUX_TMUX2_SHIFT)
#define MCDS_MCDS4P2P_TMUX2_NA_001            (0x1u << MCDS_MCDS4P2P_MUX_TMUX2_SHIFT)
#define MCDS_MCDS4P2P_TMUX2_NA_010            (0x2u << MCDS_MCDS4P2P_MUX_TMUX2_SHIFT)
#define MCDS_MCDS4P2P_TMUX2_NA_011            (0x3u << MCDS_MCDS4P2P_MUX_TMUX2_SHIFT)
#define MCDS_MCDS4P2P_TMUX2_DMA0_MIF1         (0x4u << MCDS_MCDS4P2P_MUX_TMUX2_SHIFT)
#define MCDS_MCDS4P2P_TMUX2_NA_101            (0x5u << MCDS_MCDS4P2P_MUX_TMUX2_SHIFT)
#define MCDS_MCDS4P2P_TMUX2_SPB               (0x6u << MCDS_MCDS4P2P_MUX_TMUX2_SHIFT)
#define MCDS_MCDS4P2P_TMUX2_NA_111            (0x7u << MCDS_MCDS4P2P_MUX_TMUX2_SHIFT)
#define MCDS_MCDS4P2P_TMUX3_TCMUX_CPU3        (0x0u << MCDS_MCDS4P2P_MUX_TMUX3_SHIFT)
#define MCDS_MCDS4P2P_TMUX3_OCDS_OTGB         (0x1u << MCDS_MCDS4P2P_MUX_TMUX3_SHIFT)
#define MCDS_MCDS4P2P_TMUX3_NA_001            (0x1u << MCDS_MCDS4P2P_MUX_TMUX3_SHIFT)
#define MCDS_MCDS4P2P_TMUX3_NA_010            (0x2u << MCDS_MCDS4P2P_MUX_TMUX3_SHIFT)
#define MCDS_MCDS4P2P_TMUX3_OLDA              (0x3u << MCDS_MCDS4P2P_MUX_TMUX3_SHIFT)
#define MCDS_MCDS4P2P_TMUX3_DMA0_MIF1         (0x4u << MCDS_MCDS4P2P_MUX_TMUX3_SHIFT)
#define MCDS_MCDS4P2P_TMUX3_NA_101            (0x5u << MCDS_MCDS4P2P_MUX_TMUX3_SHIFT)
#define MCDS_MCDS4P2P_TMUX3_COMPB             (0x6u << MCDS_MCDS4P2P_MUX_TMUX3_SHIFT)
#define MCDS_MCDS4P2P_TMUX3_TCMUX_CPUS        (0x7u << MCDS_MCDS4P2P_MUX_TMUX3_SHIFT)
#define MCDS_MCDS4P2P_TMUX3_NA_111            (0x7u << MCDS_MCDS4P2P_MUX_TMUX3_SHIFT)

/* MCDS_MCDS4P2P_MUXTCRC bit definitions */

#define MCDS_MCDS4P2P_MUXTCRC_TCMUX0_MASK     (3U << 0)   /* [1:0]  TCMUX0 Trace Source Select */
#define MCDS_MCDS4P2P_MUXTCRC_TCMUX0_SHIFT    0
#define MCDS_MCDS4P2P_MUXTCRC_TCMUX0_CPUZ     (0U << 0)   /* 00B: CPUz */
#define MCDS_MCDS4P2P_MUXTCRC_TCMUX0_MONITOR  (1U << 0)   /* 01B: IFX internal monitoring */
#define MCDS_MCDS4P2P_MUXTCRC_TCMUX0_UNUSED   (2U << 0)   /* 10B: not used */
#define MCDS_MCDS4P2P_MUXTCRC_TCMUX0_MEMSLAVE (3U << 0)   /* 11B: CPUz_MEMSlave (PSPR, DSPR …) */
#define MCDS_MCDS4P2P_MUXTCRC_TCMUX1_MASK     (3U << 2)   /* [3:2]  TCMUX1 Trace Source Select */
#define MCDS_MCDS4P2P_MUXTCRC_TCMUX1_SHIFT    2
#define MCDS_MCDS4P2P_MUXTCRC_TCMUX1_MONITOR  (1U << 2)   /* 01B: IFX internal monitoring */
#define MCDS_MCDS4P2P_MUXTCRC_TCMUX1_CPUZ     (0U << 2)   /* 00B: CPUz */
#define MCDS_MCDS4P2P_MUXTCRC_TCMUX1_UNUSED   (2U << 2)   /* 10B: not used */
#define MCDS_MCDS4P2P_MUXTCRC_TCMUX1_MEMSLAVE (3U << 2)   /* 11B: CPUz_MEMSlave (PSPR, DSPR …) */
#define MCDS_MCDS4P2P_MUXTCRC_TCMUX2_MASK     (3U << 4)   /* [5:4]  TCMUX2 Trace Source Select */
#define MCDS_MCDS4P2P_MUXTCRC_TCMUX2_SHIFT    4
#define MCDS_MCDS4P2P_MUXTCRC_TCMUX2_CPUZ     (0U << 4)   /* 00B: CPUz */
#define MCDS_MCDS4P2P_MUXTCRC_TCMUX2_MONITOR  (1U << 4)   /* 01B: IFX internal monitoring */
#define MCDS_MCDS4P2P_MUXTCRC_TCMUX2_UNUSED   (2U << 4)   /* 10B: not used */
#define MCDS_MCDS4P2P_MUXTCRC_TCMUX2_MEMSLAVE (3U << 4)   /* 11B: CPUz_MEMSlave (PSPR, DSPR …) */
#define MCDS_MCDS4P2P_MUXTCRC_TCMUX3_MASK     (3U << 6)   /* [7:6]  TCMUX3 Trace Source Select */
#define MCDS_MCDS4P2P_MUXTCRC_TCMUX3_SHIFT    6
#define MCDS_MCDS4P2P_MUXTCRC_TCMUX3_CPUZ     (0U << 6)   /* 00B: CPUz */
#define MCDS_MCDS4P2P_MUXTCRC_TCMUX3_MONITOR  (1U << 6)   /* 01B: IFX internal monitoring */
#define MCDS_MCDS4P2P_MUXTCRC_TCMUX3_UNUSED   (2U << 6)   /* 10B: not used */
#define MCDS_MCDS4P2P_MUXTCRC_TCMUX3_MEMSLAVE (3U << 6)   /* 11B: CPUz_MEMSlave (PSPR, DSPR …) */
#define MCDS_MCDS4P2P_MUXTCRC_PPUMUX          (1U << 8)   /* [8]    PPUMUX Trace Source Select */
#define MCDS_MCDS4P2P_MUXTCRC_SPUMUX          (1U << 9)   /* [9]    SPUMUX Trace Source Select */
#define MCDS_MCDS4P2P_MUXTCRC_TCMUX_P         (1U << 15)  /* [15]   TCMUX Trace Source Select Protection */
#define MCDS_MCDS4P2P_MUXTCRC_SRIRDWR0        (1U << 16)  /* [16]   SRI read/write data Select 0 */
#define MCDS_MCDS4P2P_MUXTCRC_SRIRDWR1        (1U << 17)  /* [17]   SRI read/write data Select 1 */
#define MCDS_MCDS4P2P_MUXTCRC_SRIRDWR2        (1U << 18)  /* [18]   SRI read/write data Select 2 */
#define MCDS_MCDS4P2P_MUXTCRC_SRIRDWR3        (1U << 19)  /* [19]   SRI read/write data Select 3 */
#define MCDS_MCDS4P2P_MUXTCRC_SRIRDWR_P       (1U << 23)  /* [23]   SRI read/write data Select Protection */
#define MCDS_MCDS4P2P_MUXTCRC_RC              (1U << 24)  /* [24]   Reference Clock Select */
#define MCDS_MCDS4P2P_MUXTCRC_RC_P            (1U << 27)  /* [27]   Reference Clock Select Protection */
#define MCDS_MCDS4P2P_MUXTCRC_PERF_EN         (1U << 31)  /* [31]   Performance Counter Interface to OTGB Enable */

/* MCDS4P_CLC bit definitions */

#define MCDS_MCDS4P2P_CLC_DISR                (1U << 0)   /* [0]  Module Disable Request Bit (rw) */
#define MCDS_MCDS4P2P_CLC_DISS                (1U << 1)   /* [1]  Module Disable Status Bit (rh) */
#define MCDS_MCDS4P2P_CLC_EDIS                (1U << 3)   /* [3]  Sleep Mode Enable Control (rw) */

/* MCDS_MCDS4P2P_FIFOCTL bit definitions */

#define MCDS_MCDS4P2P_FIFOCTL_TRG             (1U << 0)     /* [0]  Trigger Received Flag */
#define MCDS_MCDS4P2P_FIFOCTL_FFE             (1U << 1)     /* [1]  FIFO Feeder Empty */
#define MCDS_MCDS4P2P_FIFOCTL_TME             (1U << 2)     /* [2]  Trace Buffer Access Error */
#define MCDS_MCDS4P2P_FIFOCTL_DMC_MODE        (1U << 7)     /* [7]  Message sorting algorithm */
#define MCDS_MCDS4P2P_FIFOCTL_DMC_MODE_P      (1U << 8)     /* [8]  DMC Mode write protection */
#define MCDS_MCDS4P2P_FIFOCTL_TRDIS           (1U << 9)     /* [9]  Trigger Disable Flag */
#define MCDS_MCDS4P2P_FIFOCTL_TRON            (1U << 10)    /* [10] Clear Trigger Disable Flag */
#define MCDS_MCDS4P2P_FIFOCTL_TROFF           (1U << 11)    /* [11] Set Trigger Disable Flag */
#define MCDS_MCDS4P2P_FIFOCTL_FLSH_MASK       (3U << 12)    /* [13:12] Flush Flag */
#define MCDS_MCDS4P2P_FIFOCTL_FLSH_SHIFT      12
#define MCDS_MCDS4P2P_FIFOCTL_FLSH_STARTED    (0U << 12)    /* 00B: Trace session is started/on */
#define MCDS_MCDS4P2P_FIFOCTL_FLSH_RESERVED   (1U << 12)    /* 01B: Reserved (acts like stopped) */
#define MCDS_MCDS4P2P_FIFOCTL_FLSH_READY      (2U << 12)    /* 10B: Not active but ready */
#define MCDS_MCDS4P2P_FIFOCTL_FLSH_STOPPED    (3U << 12)    /* 11B: Trace session is stopped */
#define MCDS_MCDS4P2P_FIFOCTL_CLR             (1U << 14)    /* [14] Clear Flush Flag */
#define MCDS_MCDS4P2P_FIFOCTL_SET             (1U << 15)    /* [15] Set Flush Flag */
#define MCDS_MCDS4P2P_FIFOCTL_CTTO_MASK       (0x7FU << 24) /* [30:24] Continuous Trace Time Out */
#define MCDS_MCDS4P2P_FIFOCTL_CTTO_SHIFT      24
#define MCDS_MCDS4P2P_FIFOCTL_CTTO_P          (1U << 31)    /* [31] CTTO write protection */

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* DTU POB selection */

enum mcds_pob_e
{
  MCDS_MCDS4P_POBX = 0,                       /* (cpu0) */
  MCDS_MCDS4P_POBY = 1,                       /* (cpu1) */
  MCDS_MCDS4P_POBW = 2,                       /* (cpu2) */
  MCDS_MCDS4P_POBZ = 3,                       /* (cpu3) */
#ifdef CONFIG_ARCH_CHIP_AURIX_TC4DX
  MCDS_MCDS2P_POBX = 4,                       /* (cpu4) */
  MCDS_MCDS2P_POBW = 5,                       /* (cpu5) */
#endif
  MCDS_MCDS4P2P_MAX                           /* Number of pob */
};

enum mcds_tubf_trigger_mode_e
{
    MCDS_TRIGGER_MODE_PRE       = 0,          /* Pre-trigger */
    MCDS_TRIGGER_MODE_POST      = 1,          /* Post-trigger */
    MCDS_TRIGGER_MODE_PRE_POST  = 2           /* Pre-post trigger */
};

struct mcds_evt_matrix_node_s
{
  int tcevt;      /* 0-15 */
  int evt_eiq;    /* 0-15 */
};

struct mcds_evt_matrix_s
{
  int num;
  struct mcds_evt_matrix_node_s evt_matrix[MCDS_MCDS4P2P_NR_EVT];
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: mcds_tbuf_init
 ****************************************************************************/

void mcds_tbuf_init(enum mcds_tubf_trigger_mode_e type);

/****************************************************************************
 * Name: mcds_tbuf_stop
 ****************************************************************************/

void mcds_tbuf_stop(void);

/****************************************************************************
 * Name: void mcds_tbuf_stop()
 ****************************************************************************/

void mcds_tbuf_start(void);

/****************************************************************************
 * Name: mcds_init
 ****************************************************************************/

int mcds_init(FAR struct hwtracing_dev_s *mcdsdev);

/****************************************************************************
 * Name: mcds_enable
 ****************************************************************************/

int mcds_enable(FAR struct hwtracing_dev_s *mcdsdev);

/****************************************************************************
 * Name: mcds_disable
 ****************************************************************************/

void mcds_disable(FAR struct hwtracing_dev_s *mcdsdev);

#endif  //__DRIVERS_TRICOREHT_TRICOREHT_MCDS_CORE_H
