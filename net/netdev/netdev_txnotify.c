/****************************************************************************
 * net/netdev/netdev_txnotify.c
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

#include <sys/types.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <debug.h>

#include <nuttx/net/netdev.h>
#include <nuttx/net/ip.h>

#include "netdev/netdev.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: netdev_ipv4_txnotify
 *
 * Description:
 *   Notify the device driver that forwards the IPv4 address that new TX
 *   data is available.
 *
 * Input Parameters:
 *   lipaddr - The local address bound to the socket
 *   ripaddr - The remote address to send the data
 *
 * Returned Value:
 *  None
 *
 ****************************************************************************/

#ifdef CONFIG_NET_IPv4
void netdev_ipv4_txnotify(in_addr_t lipaddr, in_addr_t ripaddr,
                          uint32_t polltype)
{
  /* Find the device driver that serves the subnet of the remote address */

  netdev_txnotify(netdev_findby_ripv4addr(lipaddr, ripaddr), polltype);
}
#endif /* CONFIG_NET_IPv4 */

/****************************************************************************
 * Name: netdev_ipv6_txnotify
 *
 * Description:
 *   Notify the device driver that forwards the IPv4 address that new TX
 *   data is available.
 *
 * Input Parameters:
 *   lipaddr - The local address bound to the socket
 *   ripaddr - The remote address to send the data
 *
 * Returned Value:
 *  None
 *
 ****************************************************************************/

#ifdef CONFIG_NET_IPv6
void netdev_ipv6_txnotify(FAR const net_ipv6addr_t lipaddr,
                          FAR const net_ipv6addr_t ripaddr,
                          uint32_t polltype)
{
  /* Find the device driver that serves the subnet of the remote address */

  netdev_txnotify(netdev_findby_ripv6addr(lipaddr, ripaddr), polltype);
}
#endif /* CONFIG_NET_IPv6 */

/****************************************************************************
 * Name: netdev_txnotify_defer
 *
 * Description:
 *   Notify the device driver that new TX data is available.  This variant
 *   would be called when the upper level logic already understands how the
 *   packet will be routed.
 *
 *   When defer is false, d_txavail is called immediately to trigger TX.
 *   When defer is true, only the poll type flags are set so that deferred
 *   TX work will be triggered after the current RX processing completes,
 *   avoiding interruption of an ongoing receive flow.
 *
 * Input Parameters:
 *   dev      - The network device driver state structure.
 *   polltype - The type of poll to be triggered for the device.
 *   defer    - If true, only set flags without calling d_txavail.
 *
 * Returned Value:
 *  None
 *
 ****************************************************************************/

void netdev_txnotify_defer(FAR struct net_driver_s *dev, uint32_t polltype,
                           bool defer)
{
  if (dev != NULL)
    {
      /* Set the poll type flags */

      dev->d_polltype |= polltype;

      /* Notify the device driver only if not deferred and there is
       * pending TX work.
       */

      if (!defer && dev->d_polltype != 0 && dev->d_txavail != NULL)
        {
          dev->d_txavail(dev);
        }
    }
}
