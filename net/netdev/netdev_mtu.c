/****************************************************************************
 * net/netdev/netdev_mtu.c
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

#include <nuttx/config.h>

#include <assert.h>
#include <debug.h>
#include <errno.h>

#include <nuttx/net/ip.h>
#include <nuttx/net/netdev.h>

#include "netdev/netdev.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: netdev_getmtu
 *
 * Description:
 *   Get the current MTU of a network device, i.e. the size of the largest
 *   payload that the link layer can carry.
 *
 * Input Parameters:
 *   dev - The network device
 *
 * Returned Value:
 *   The current MTU of the device.
 *
 ****************************************************************************/

int netdev_getmtu(FAR struct net_driver_s *dev)
{
  DEBUGASSERT(dev != NULL);

  return dev->d_pktsize - dev->d_llhdrlen;
}

/****************************************************************************
 * Name: netdev_setmtu
 *
 * Description:
 *   Change the MTU of a network device.  The request is rejected if the
 *   requested MTU cannot be handled by the driver, i.e. if the resulting
 *   packet size would exceed d_maxpktsize.  Silently accepting a too large
 *   MTU would make the network stack generate frames that the driver has to
 *   drop (or worse, truncate).
 *
 *   No lower limit is enforced here.  The link types supported by NuttX
 *   range from CAN frames over 6LoWPAN radios to Ethernet, and each of them
 *   has its own notion of a meaningful minimum; a caller that needs the
 *   minimum of a specific protocol (e.g. the IPv6 minimum link MTU of RFC
 *   8200) has to check for it itself.
 *
 *   This is the only sanctioned way to modify d_pktsize after the device
 *   has been registered.
 *
 * Input Parameters:
 *   dev - The network device
 *   mtu - The requested MTU
 *
 * Returned Value:
 *   Zero (OK) is returned on success.  -EINVAL is returned if the requested
 *   MTU is out of the range supported by the device, in which case the MTU
 *   of the device is left unchanged.
 *
 ****************************************************************************/

int netdev_setmtu(FAR struct net_driver_s *dev, int mtu)
{
  int maxmtu;

  DEBUGASSERT(dev != NULL);
  maxmtu = dev->d_maxpktsize - dev->d_llhdrlen;

  if (mtu <= 0 || mtu > maxmtu)
    {
      nwarn("WARNING: %s: Rejecting MTU %d, supported range is [1, %d]\n",
            dev->d_ifname, mtu, maxmtu);
      return -EINVAL;
    }

  dev->d_pktsize = (uint16_t)(mtu + dev->d_llhdrlen);
  return OK;
}
