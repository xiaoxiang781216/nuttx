/************************************************************************************
 * include/nuttx/wireless/wireless_priv.h
 * Wireless network private IOCTL commands
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
 ************************************************************************************/

/* This file includes private definitions to be used in all wireless network drivers
 * (when applicable).
 */

#ifndef __INCLUDE_NUTTX_WIRELESS_WIRELESS_PRIV_H
#define __INCLUDE_NUTTX_WIRELESS_WIRELESS_PRIV_H

/************************************************************************************
 * Included Files
 ************************************************************************************/

#include <nuttx/wireless/wireless.h>

/************************************************************************************
 * Pre-processor Definitions
 ************************************************************************************/

/* Network Driver Private IOCTL Commands ********************************************/

/* The Private IOCTL Commands should be definded as SIOCSIWXXXX and SIOCGIWXXXX
 *  SIOCSIWXXXX : set params (even number)
 *  SIOCGIWXXXX : get params (odd number)
 *
 * NOTE: The range of Private IOCTL Commands is
 *   form SIOCIWFIRSTPRIV to SIOCIWLASTPRIV
 */

#define SIOCSIWDTIM SIOCIWFIRSTPRIV         /* Set DTIM interval time */
#define SIOCGIWDTIM (SIOCIWFIRSTPRIV + 1)   /* Get DTIM interval time */

#endif /* __INCLUDE_NUTTX_WIRELESS_WIRELESS_PRIV_H */
