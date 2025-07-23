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

#define MCDS_TBUF_SIZE                      0x10000

/* MCDS4P2P_FIFOCTL bit definitions */

#define MCDS_MCDS4P2P_FIFOCTL_TRG           (1U << 0)     /* [0]  Trigger Received Flag */
#define MCDS_MCDS4P2P_FIFOCTL_FFE           (1U << 1)     /* [1]  FIFO Feeder Empty */
#define MCDS_MCDS4P2P_FIFOCTL_TME           (1U << 2)     /* [2]  Trace Buffer Access Error */
#define MCDS_MCDS4P2P_FIFOCTL_DMC_MODE      (1U << 7)     /* [7]  Message sorting algorithm */
#define MCDS_MCDS4P2P_FIFOCTL_DMC_MODE_P    (1U << 8)     /* [8]  DMC Mode write protection */
#define MCDS_MCDS4P2P_FIFOCTL_TRDIS         (1U << 9)     /* [9]  Trigger Disable Flag */
#define MCDS_MCDS4P2P_FIFOCTL_TRON          (1U << 10)    /* [10] Clear Trigger Disable Flag */
#define MCDS_MCDS4P2P_FIFOCTL_TROFF         (1U << 11)    /* [11] Set Trigger Disable Flag */
#define MCDS_MCDS4P2P_FIFOCTL_FLSH_MASK     (3U << 12)    /* [13:12] Flush Flag */
#define MCDS_MCDS4P2P_FIFOCTL_FLSH_SHIFT    12
#define MCDS_MCDS4P2P_FIFOCTL_FLSH_STARTED  (0U << 12)    /* 00B: Trace session is started/on */
#define MCDS_MCDS4P2P_FIFOCTL_FLSH_RESERVED (1U << 12)    /* 01B: Reserved (acts like stopped) */
#define MCDS_MCDS4P2P_FIFOCTL_FLSH_READY    (2U << 12)    /* 10B: Not active but ready */
#define MCDS_MCDS4P2P_FIFOCTL_FLSH_STOPPED  (3U << 12)    /* 11B: Trace session is stopped */
#define MCDS_MCDS4P2P_FIFOCTL_CLR           (1U << 14)    /* [14] Clear Flush Flag */
#define MCDS_MCDS4P2P_FIFOCTL_SET           (1U << 15)    /* [15] Set Flush Flag */
#define MCDS_MCDS4P2P_FIFOCTL_CTTO_MASK     (0x7FU << 24) /* [30:24] Continuous Trace Time Out */
#define MCDS_MCDS4P2P_FIFOCTL_CTTO_SHIFT    24
#define MCDS_MCDS4P2P_FIFOCTL_CTTO_P        (1U << 31)    /* [31] CTTO write protection */

/****************************************************************************
 * Public Types
 ****************************************************************************/

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: mcds_tbuf_stop
 ****************************************************************************/

void mcds_tbuf_stop(void);

/****************************************************************************
 * Name: void mcds_tbuf_stop()
 ****************************************************************************/

void mcds_tbuf_start(void);

#endif  //__DRIVERS_TRICOREHT_TRICOREHT_MCDS_CORE_H