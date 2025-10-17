/****************************************************************************
 * drivers/hwtracing/tricoreht/tricoreht_trif_core.h
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

#ifndef __DRIVERS_TRICOREHT_TRICOREHT_TRIF_CORE_H
#define __DRIVERS_TRICOREHT_TRICOREHT_TRIF_CORE_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/hwtracing/tricoreht/tricoreht_trif.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MTSC_LENTH                    0x100

/* TRIF_CTRL bit definitions */

#define TRIF_CTRL_OPMODE_MASK         (0xF << 0)   /* [3:0]   Operation mode select */
#define TRIF_CTRL_OPMODE_P            (1 << 4)     /* [4]     Operation mode protection */
#define TRIF_CTRL_TRCSTART            (1 << 5)     /* [5]     Trace Start */
#define TRIF_CTRL_TRCSTART_P          (1 << 6)     /* [6]     Trace Start protection */
#define TRIF_CTRL_TRCSUSP             (1 << 15)    /* [15]    Suspend asserted by TRACE */
#define TRIF_CTRL_SGBTTS              (1 << 16)    /* [16]    Training start */
#define TRIF_CTRL_SGBTTXA             (1 << 17)    /* [17]    RX Initialized */
#define TRIF_CTRL_SGBTTPV             (1 << 18)    /* [18]    Trace Port Verified */
#define TRIF_CTRL_SGBTTS_P            (1 << 19)    /* [19]    Training Start protection */
#define TRIF_CTRL_SGBTRSTN            (1 << 20)    /* [20]    Reset SGBT */

#define TRIF_OPMODE_INIT              (0x0U << 0)
#define TRIF_OPMODE_TRACE_TBUF_TPB    (0x1U << 0)
#define TRIF_OPMODE_TRACE_TBUF_TFIF   (0x2U << 0)
#define TRIF_OPMODE_TRACE_ONCHIP_RAM  (0x3U << 0)
#define TRIF_OPMODE_TRACE_STREAM_SGBT (0x4U << 0)
#define TRIF_OPMODE_TRACE_STREAM_ETH  (0x5U << 0)
#define TRIF_OPMODE_TRACE_STREAM_DAP  (0x6U << 0)

/* TRIF_TSR bit definitions */

#define TRIF_TSR_TRCON0               (1U << 0)   /* [0]  Trace in progress */
#define TRIF_TSR_TRCON1               (1U << 1)   /* [1]  Trace in progress */
#define TRIF_TSR_LASTMTSC             (1U << 4)   /* [4]  Last MTSC transferred */
#define TRIF_TSR_TMETGERR             (1U << 8)   /* [8]  TME Trigger error */
#define TRIF_TSR_TMESRIERR            (1U << 9)   /* [9]  TME SRI transfer error */
#define TRIF_TSR_SGBTPLK              (1U << 16)  /* [16] SGBT PHY TX PLL lock status */
#define TRIF_TSR_SGBTLNUP             (1U << 17)  /* [17] SGBT TX lane up */
#define TRIF_TSR_SGBTCHUP             (1U << 18)  /* [18] SGBT channel up */
#define TRIF_TSR_ARR                  (1U << 24)  /* [24] Application Reset request */
#define TRIF_TSR_APPRES               (1U << 25)  /* [25] Application Reset */
#define TRIF_TSR_CLKSELS_MASK         (3U << 26)  /* [27:26] Clock source status selection */
#define TRIF_TSR_CLKSELS_SHIFT        26
#define TRIF_TSR_TRCSTATE_MASK        (3U << 28)  /* [29:28] Trace start state machine */
#define TRIF_TSR_TRCSTATE_SHIFT       28
#define TRIF_TRCSTATE_IDLE            (0U << 28)  // 00B
#define TRIF_TRCSTATE_ENABLED         (1U << 28)  // 01B
#define TRIF_TRCSTATE_RESERVED        (2U << 28)  // 10B
#define TRIF_TRCSTATE_BLOCKED         (3U << 28)  // 11B
#define TRIF_TSR_APPTYPE              (1U << 30)  /* [30] Application type */
#define TRIF_TSR_DEVTYPE              (1U << 31)  /* [31] Device type */

/* TRIF_CLC bit definitions */

#define TRIF_CLC_DISR                 (1U << 0)   /* [0]  Module Disable Request Bit (rw) */
#define TRIF_CLC_DISS                 (1U << 1)   /* [1]  Module Disable Status Bit (rh) */
#define TRIF_CLC_EDIS                 (1U << 3)   /* [3]  Sleep Mode Enable Control (rw) */

/* TRIF_TBUFFLV bit definitions */

#define TRIF_TBUFFLV_CURLVL_MASK      (0x7FFU << 0)    /* [10:0] Current TBUF Fill Level */
#define TRIF_TBUFFLV_CURLVL_SHIFT     0
#define TRIF_TBUFFLV_MAXLVL_MASK      (0x7FFU << 16)   /* [26:16] Maximum TBUF FIFO Fill Level */
#define TRIF_TBUFFLV_MAXLVL_SHIFT     16

/* TRIF_RST_CTRLA bit definitions */

#define TRIF_RST_CTRLA_KRST           (1U << 0)   /* [0]  Kernel Reset (rw1sh) */
#define TRIF_RST_CTRLA_GRSTEN0        (1U << 8)   /* [8]  Global Module Reset Enable Group 0 */
#define TRIF_RST_CTRLA_GRSTEN1        (1U << 9)   /* [9]  Global Module Reset Enable Group 1 */
#define TRIF_RST_CTRLA_GRSTEN2        (1U << 10)  /* [10] Global Module Reset Enable Group 2 */
#define TRIF_RST_CTRLA_GRSTEN3        (1U << 11)  /* [11] Global Module Reset Enable Group 3 */

/* TRIF_RST_CTRLB bit definitions */

#define TRIF_RST_CTRLB_KRST           (1U << 0)   /* [0]  Kernel Reset (rw1sh) */
#define TRIF_RST_CTRLB_STATCLR        (1U << 31)  /* [31] Kernel Reset Status Clear (w) */

/* TRIF_TBTMESR_ bit definitions */

#define TRIF_TBTMESR_TLA_MASK         (0xFFFFU << 0)   /* [15:0]  Total trace lines available */
#define TRIF_TBTMESR_TLA_SHIFT        0
#define TRIF_TBTMESR_MWR_MASK         (0x1FFU << 16)   /* [24:16] MTSC Words Read */
#define TRIF_TBTMESR_MWR_SHIFT        16
#define TRIF_TBTMESR_TMEBUSY          (1U << 27)       /* [27]    TME transfer status */
#define TRIF_TBTMESR_OVERRUN0         (1U << 28)       /* [28]    TBUF overrun channel 0 */
#define TRIF_TBTMESR_OVERRUN1         (1U << 29)       /* [29]    TBUF overrun channel 1 */

/****************************************************************************
 * Public Types
 ****************************************************************************/

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: trif_tfif_register
 ****************************************************************************/

int trif_tfif_register(FAR struct tricoreht_trif_dev_s *trifdev,
                       FAR const struct hwtracing_desc_s *desc);

/****************************************************************************
 * Name: trif_etf_unregister
 ****************************************************************************/

void trif_tfif_unregister(FAR struct tricoreht_trif_dev_s *trifdev);

/****************************************************************************
 * Name: trif_dap_register
 ****************************************************************************/

int trif_dap_register(FAR struct hwtracing_dev_s * tfifdev,
                      FAR const struct hwtracing_desc_s *desc);

/****************************************************************************
 * Name: trif_dap_unregister
 ****************************************************************************/

void trif_dap_unregister(FAR struct hwtracing_dev_s * tfifdev);

/****************************************************************************
 * Name: trif_enable
 ****************************************************************************/

int trif_init(FAR struct hwtracing_dev_s *trifdev);

/****************************************************************************
 * Name: trif_enable
 ****************************************************************************/

int trif_enable(FAR struct hwtracing_dev_s *trifdev);

/****************************************************************************
 * Name: trif_disable
 ****************************************************************************/

void trif_disable(FAR struct hwtracing_dev_s *trifdev);

#endif  //__DRIVERS_TRICOREHT_TRICOREHT_TRIF_CORE_H