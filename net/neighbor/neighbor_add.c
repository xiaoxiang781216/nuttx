/****************************************************************************
 * net/neighbor/neighbor_add.c
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

#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <debug.h>

#include <net/if.h>

#include <nuttx/net/net.h>
#include <nuttx/net/ip.h>
#include <nuttx/net/neighbor.h>

#include "inet/inet.h"
#include "icmpv6/icmpv6.h"
#include "netdev/netdev.h"
#include "netlink/netlink.h"
#include "neighbor/neighbor.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: neighbor_add
 *
 * Description:
 *   Add the new address association to the Neighbor Table (if it is not
 *   already there).
 *
 * Input Parameters:
 *   dev    - Driver instance associated with the MAC
 *   ipaddr - The IPv6 address of the mapping.
 *   addr   - The link layer address of the mapping
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void neighbor_add(FAR struct net_driver_s *dev, FAR net_ipv6addr_t ipaddr,
                  FAR uint8_t *addr)
{
  uint8_t lltype;
  clock_t oldest_time;
  int     oldest_ndx;
  bool    found = false;
  bool    new_entry;
  int     i;

  DEBUGASSERT(dev != NULL);

#ifdef CONFIG_NET_ETHERNET
  if (dev->d_lltype != NET_LL_ETHERNET &&
      dev->d_lltype != NET_LL_IEEE80211)
#endif
    {
      DEBUGASSERT(addr != NULL);
    }

  /* Find the matching entry, first unused entry, or the oldest used entry.
   * The unused entry will have ne_time == 0 and should generate the oldest
   * time.  REVISIT:  Could this fail on clock wraparound?  A more explicit
   * check might be to compare ne_ipaddr with the IPv6 unspecified address.
   */

  oldest_time = g_neighbors[0].ne_time;
  oldest_ndx  = 0;
  lltype      = dev->d_lltype;

  for (i = 0; i < CONFIG_NET_IPv6_NCONF_ENTRIES; ++i)
    {
      if (g_neighbors[i].ne_addr.na_lltype == lltype &&
          net_ipv6addr_cmp(g_neighbors[i].ne_ipaddr, ipaddr))
        {
          oldest_ndx = i;
          found = true;
          break;
        }

      if ((int)(g_neighbors[i].ne_time - oldest_time) < 0)
        {
          oldest_ndx = i;
          oldest_time = g_neighbors[i].ne_time;
        }
    }

#ifdef CONFIG_NET_IPv6_NEIGHBOR_SEND_QUEUE
  if (!found && g_neighbors[oldest_ndx].ne_time != 0)
    {
      /* Neighbor entry will be replaced, clean delayed IOBs if exist */

      work_cancel_sync(LPWORK, &g_neighbors[oldest_ndx].ne_work);
      iob_free_queue(&g_neighbors[oldest_ndx].ne_queue);
    }
  else if (found && addr != NULL)
    {
      /* Found existing entry and updating with real MAC address,
       * move queued packets to device output queue
       */

      work_cancel_sync(LPWORK, &g_neighbors[oldest_ndx].ne_work);
      iob_concat_queue(&dev->d_neighout, &g_neighbors[oldest_ndx].ne_queue);
    }
#endif

  /* When overwite old entry, need to notify RTM_DELNEIGH */

  if (!found && g_neighbors[oldest_ndx].ne_time != 0)
    {
      netlink_neigh_notify(&g_neighbors[oldest_ndx], RTM_DELNEIGH,
                           AF_INET6);
    }

  /* Need to notify when entry is not found or changes in table */

  /* Determine if this is a new entry or a change. If addr is NULL, addr we
   * treat that as an all-zero link-layer address (used to mark in-progress
   * or unreachable states).
   */

#ifdef CONFIG_NET_ETHERNET
  if (addr == NULL)
    {
      addr = (FAR uint8_t *)g_zero_ethaddr.ether_addr_octet;
    }
#endif

  new_entry = !found || memcmp(&g_neighbors[oldest_ndx].ne_addr.u, addr,
                             g_neighbors[oldest_ndx].ne_addr.na_llsize) != 0;

  /* Use the oldest or first free entry (either pointed to by the
   * "oldest_ndx" variable).
   */

  g_neighbors[oldest_ndx].ne_dev  = dev;
  g_neighbors[oldest_ndx].ne_time = clock_systime_ticks();
  net_ipv6addr_copy(g_neighbors[oldest_ndx].ne_ipaddr, ipaddr);

  g_neighbors[oldest_ndx].ne_addr.na_lltype = lltype;
  g_neighbors[oldest_ndx].ne_addr.na_llsize = netdev_lladdrsize(dev);

  memcpy(&g_neighbors[oldest_ndx].ne_addr.u, addr,
         g_neighbors[oldest_ndx].ne_addr.na_llsize);

  /* Notify the new entry */

  if (new_entry)
    {
      netlink_neigh_notify(&g_neighbors[oldest_ndx], RTM_NEWNEIGH,
                           AF_INET6);
    }

#ifdef CONFIG_NET_IPv6_NEIGHBOR_SEND_QUEUE
  if (!IOB_QEMPTY(&dev->d_neighout))
    {
      /* In Rx context, we need to backup the dev iob and related members,
       * as some drivers netdev_txnotify_dev() execute transmit in sync mode
       * which will modify iob and other members.
       * We need to restore the dev iob and related members after
       * netdev_txnotify_dev() return because iob will be used in left
       * bottom Rx process.
       */

      uint16_t len = dev->d_len;
      FAR struct iob_s *iob = dev->d_iob;

      dev->d_iob = NULL;
      dev->d_buf = NULL;
      netdev_txnotify_dev(dev, ICMPv6_POLL);
      netdev_iob_replace(dev, iob);
      dev->d_len = len;
    }
#endif

  /* Dump the contents of the new entry */

  neighbor_dumpentry("Added entry", &g_neighbors[oldest_ndx]);
}
