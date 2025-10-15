/****************************************************************************
 * include/nuttx/net/ieee1722.h
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

#ifndef __INCLUDE_NUTTX_NET_IEEE1722_H
#define __INCLUDE_NUTTX_NET_IEEE1722_H

/* IEEE1722 Avtp sub type */

#define IEEE1722_SUBTYPE_61883_IIDC             0x0
#define IEEE1722_SUBTYPE_MMA_STREAM             0x1
#define IEEE1722_SUBTYPE_AAF                    0x2
#define IEEE1722_SUBTYPE_CVF                    0x3
#define IEEE1722_SUBTYPE_CRF                    0x4
#define IEEE1722_SUBTYPE_TSCF                   0x5
#define IEEE1722_SUBTYPE_SVF                    0x6
#define IEEE1722_SUBTYPE_RVF                    0x7
#define IEEE1722_SUBTYPE_AEF_CONTINUOUS         0x6E
#define IEEE1722_SUBTYPE_VSF_STREAM             0x6F
#define IEEE1722_SUBTYPE_EF_STREAM              0x7F
#define IEEE1722_SUBTYPE_NTSCF                  0x82
#define IEEE1722_SUBTYPE_ESCF                   0xEC
#define IEEE1722_SUBTYPE_EECF                   0xED
#define IEEE1722_SUBTYPE_AEF_DISCRETE           0xEE
#define IEEE1722_SUBTYPE_ADP                    0xFA
#define IEEE1722_SUBTYPE_AECP                   0xFB
#define IEEE1722_SUBTYPE_ACMP                   0xFC
#define IEEE1722_SUBTYPE_MAAP                   0xFE
#define IEEE1722_SUBTYPE_EF_CONTROL             0xFF

#endif /* __INCLUDE_NUTTX_NET_IEEE1722_H */
