/****************************************************************************
 * net/neighbor/neighbor_lookup.c
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

#include <errno.h>
#include <debug.h>
#include <string.h>

#include <nuttx/clock.h>
#include <nuttx/mm/iob.h>
#include <nuttx/net/ip.h>
#include <nuttx/net/neighbor.h>
#include <nuttx/wqueue.h>

#include "inet/inet.h"
#include "netdev/netdev.h"
#include "neighbor/neighbor.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#if defined(CONFIG_NET_ICMPv6_NEIGHBOR) || defined(CONFIG_NET_IPv6_NEIGHBOR_SEND_QUEUE)
#  define NEIGHBOR_INPROGRESS_TICK MSEC2TICK(CONFIG_ICMPv6_NEIGHBOR_MAXTRIES * CONFIG_ICMPv6_NEIGHBOR_DELAYMSEC)
#  define NEIGHBOR_MAXAGE_UNREACHABLE_TICK SEC2TICK(10 * CONFIG_NET_NEIGHBOR_MAXAGE_UNREACHABLE)
#endif
/****************************************************************************
 * Private Types
 ****************************************************************************/

struct neighbor_table_info_s
{
  net_ipv6addr_t              ni_ipaddr; /* IPv6 address for lookup */
  FAR struct neighbor_addr_s *ni_laddr;  /* Location to return the link
                                          * layer address */
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: neighbor_match
 *
 * Description:
 *   This is a callback that checks if the network device has the
 *   indicated IPv6 address assigned to it.
 *
 ****************************************************************************/

static int neighbor_match(FAR struct net_driver_s *dev, FAR void *arg)
{
  FAR struct neighbor_table_info_s *info = arg;

  /* Check if the network device has been assigned the IP address of the
   * lookup.
   */

  if (!NETDEV_IS_MY_V6ADDR(dev, info->ni_ipaddr))
    {
      return 0;
    }

  /* Yes.. Return the matching link layer address if the caller of
   * neighbor_lookup() provided a non-NULL location.
   */

  if (info->ni_laddr != NULL)
    {
      info->ni_laddr->na_lltype = dev->d_lltype;
      info->ni_laddr->na_llsize = netdev_lladdrsize(dev);
      memcpy(&info->ni_laddr->u, &dev->d_mac, info->ni_laddr->na_llsize);
    }

  /* Return success in any event */

  return 1;
}

#ifdef CONFIG_NET_IPv6_NEIGHBOR_SEND_QUEUE

/****************************************************************************
 * Name: neighbor_unreach_work
 *
 * Description:
 *   Work queue callback to clean up IOB queue for neighbor entry that
 *   remains unreachable or in-progress beyond the timeout period.
 *
 ****************************************************************************/

static void neighbor_unreach_work(FAR void *param)
{
  FAR struct neighbor_entry_s *entry = (FAR struct neighbor_entry_s *)param;
  iob_free_queue(&entry->ne_queue);
}

#endif /* CONFIG_NET_IPv6_NEIGHBOR_SEND_QUEUE */

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name:  neighbor_lookup
 *
 * Description:
 *   Find an entry in the Neighbor Table and return its link layer address.
 *
 * Input Parameters:
 *   ipaddr - The IPv6 address to use in the lookup;
 *   laddr  - Location to return the corresponding link layer address.
 *            This address may be NULL.  In that case, this function may be
 *            used simply to determine if the link layer address is
 *            available.
 *
 * Returned Value:
 *   Zero (OK) if the link layer address is returned.  A negated errno value
 *   is returned on any error:
 *
 *     -EINPROGRESS: Neighbor solicitation is already in progress
 *                   (all-zero MAC address in table)
 *     -ENOENT:      No matching neighbor entry found
 *
 ****************************************************************************/

int neighbor_lookup(FAR const net_ipv6addr_t ipaddr,
                    FAR struct neighbor_addr_s *laddr)
{
  FAR struct neighbor_entry_s *neighbor;
  struct neighbor_table_info_s info;

  /* Check if the IPv6 address is already in the neighbor table. */

  neighbor = neighbor_findentry(ipaddr);
  if (neighbor != NULL)
    {
#if defined(CONFIG_NET_ETHERNET) && \
    (defined(CONFIG_NET_ICMPv6_NEIGHBOR) || defined(CONFIG_NET_IPv6_NEIGHBOR_SEND_QUEUE))
      int llsize;
      llsize = neighbor->ne_addr.na_llsize;
      if (llsize == sizeof(g_zero_ethaddr) &&
          memcmp(&neighbor->ne_addr.u, g_zero_ethaddr.ether_addr_octet,
                 llsize) == 0)
        {
          /* If ICMPv6 neighbor discovery is enabled, apply a timeout to
           * the in-progress marker. If the marker is still within the
           * timeout window, report -EINPROGRESS. If the marker expired,
           * treat the entry as not found so a new solicitation can be
           * initiated. If ICMPv6 neighbor is not enabled, conservatively
           * report -EINPROGRESS.
           */

          clock_t elapsed;
          elapsed = clock_systime_ticks() - neighbor->ne_time;
          if (elapsed <= NEIGHBOR_INPROGRESS_TICK)
            {
              ninfo("Neighbor solicitation in progress (elapsed=%lu)\n",
                    (unsigned long)elapsed);
              return -EINPROGRESS;
            }
          else if (elapsed <= NEIGHBOR_MAXAGE_UNREACHABLE_TICK)
            {
              ninfo("Neighbor unreachable (elapsed=%lu)\n",
                    (unsigned long)elapsed);
              return -ENETUNREACH;
            }
          else
            {
              ninfo("Neighbor in-progress marker expired (elapsed=%lu)\n",
                    (unsigned long)elapsed);
              return -ENOENT;
            }
        }
#endif

      /* Yes.. return the link layer address if the caller has provided a
       * non-NULL address in 'laddr'.
       */

      if (laddr != NULL)
        {
          memcpy(laddr, &neighbor->ne_addr, sizeof(*laddr));
        }

      /* Return success in any case meaning that a valid link layer
       * address mapping is available for the IPv6 address.
       */

      return OK;
    }

  /* No.. check if the IPv6 address is the address assigned to a local
   * network device.  If so, return a mapping of that IPv6 address
   * to the linker layer address assigned to the network device.
   */

  net_ipv6addr_copy(info.ni_ipaddr, ipaddr);
  info.ni_laddr = laddr;

  if (netdev_foreach(neighbor_match, &info) != 0)
    {
      return OK;
    }

  /* Not found */

  return -ENOENT;
}

#ifdef CONFIG_NET_IPv6_NEIGHBOR_SEND_QUEUE

/****************************************************************************
 * Name: neighbor_queue_iob
 *
 * Description:
 *   Queue an IOB which L2 layer is unfinished to the target neighbor
 *   entry's delay queue which in progress waiting for a Neighbor
 *   Advertisement response.
 *
 * Input Parameters:
 *   dev     - The device driver structure
 *   ipaddr  - The IPv6 address
 *   iob     - The IOB to be queued
 *
 * Returned Value:
 *   Zero (OK) if the Neighbor Table entry was successfully modified.
 *   A negated errno value is returned on any error.
 *
 * Assumptions
 *   The network is locked to assure exclusive access to the Neighbor Table
 *
 ****************************************************************************/

int neighbor_queue_iob(FAR struct net_driver_s *dev,
                       FAR const net_ipv6addr_t ipaddr,
                       FAR struct iob_s *iob)
{
  FAR struct neighbor_entry_s *entry;

  /* The IPv6 address should be in the Neighbor Table and ND in progress. */

  entry = neighbor_findentry(ipaddr);
  if (entry && entry->ne_dev == dev)
    {
      /* Check if this is an in-progress entry (all zeros MAC address) */

      if (entry->ne_addr.na_lltype == NET_LL_ETHERNET ||
          entry->ne_addr.na_lltype == NET_LL_IEEE80211)
        {
          if (memcmp(&entry->ne_addr.u.na_ethernet, &g_zero_ethaddr,
                     sizeof(struct ether_addr)) == 0)
            {
              if (iob_tryadd_queue(iob, &entry->ne_queue) == 0)
                {
                  if (work_available(&entry->ne_work))
                    {
                      work_queue(LPWORK, &entry->ne_work,
                                 neighbor_unreach_work,
                                 entry, NEIGHBOR_INPROGRESS_TICK);
                    }

                  return OK;
                }

              return -ENOMEM;
            }
        }
    }

  return -ENOENT;
}

#endif /* CONFIG_NET_IPv6_NEIGHBOR_SEND_QUEUE */
