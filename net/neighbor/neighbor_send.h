/****************************************************************************
 * net/neighbor/neighbor_send.h
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

#ifndef __NET_NEIGHBOR_NEIGHBOR_SEND_H
#define __NET_NEIGHBOR_NEIGHBOR_SEND_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/semaphore.h>
#include <nuttx/net/ip.h>

#ifdef CONFIG_NET_NEIGHBOR_SEND

/****************************************************************************
 * Public Type Definitions
 ****************************************************************************/

struct net_driver_s; /* Forward reference */

/* Used to notify a thread waiting for a particular Neighbor Advertisement */

struct neighbor_notify_s
{
  FAR struct neighbor_notify_s *nt_flink; /* Supports singly linked list */
  net_ipv6addr_t nt_ipaddr;               /* Waited for IP address in
                                           * the mapping
                                           */
  sem_t nt_sem;                           /* Will wake up the waiter */
  int nt_result;                          /* The result of the wait */
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: neighbor_send
 *
 * Description:
 *   The icmpv6_solicit() call may be to send an ICMPv6 Neighbor
 *   Solicitation to resolve an IPv6 address.  This function first checks if
 *   the IPv6 address is already in the Neighbor Table.  If so, then it
 *   returns success immediately.
 *
 *   If the requested IPv6 address in not in the Neighbor Table, then this
 *   function will send the Neighbor Solicitation, delay, then check if the
 *   IP address is now in the Neighbor able.  It will repeat this sequence
 *   until either (1) the IPv6 address mapping is now in the Neighbor table,
 *   or (2) a configurable number of timeouts occur without receiving the
 *   ICMPv6 Neighbor Advertisement.
 *
 * Input Parameters:
 *   dev      The suggested device driver structure to do the solicitation,
 *            can be NULL for auto decision, must set for link-local ipaddr.
 *   ipaddr   The IPv6 address to be queried.
 *
 * Returned Value:
 *   Zero (OK) is returned on success and the IP address mapping can now be
 *   found in the Neighbor Table.
 *   On error a negated errno value is returned:
 *
 *     -ETIMEDOUT:    The number or retry counts has been exceed.
 *     -EHOSTUNREACH: Could not find a route to the host
 *
 * Assumptions:
 *   This function is called from the normal tasking context.
 *
 ****************************************************************************/

int neighbor_send(FAR struct net_driver_s *dev,
                    const net_ipv6addr_t ipaddr);

/****************************************************************************
 * Name: neighbor_wait_setup
 *
 * Description:
 *   Called BEFORE an Neighbor Solicitation is sent.  This function sets up
 *   the Neighbor Advertisement timeout before the Neighbor Solicitation
 *   is sent so that there is no race condition when neighbor_wait() is
 *   called.
 *
 * Assumptions:
 *   This function is called from neighbor_send() and executes in the
 *   normal tasking environment.
 *
 ****************************************************************************/

void neighbor_wait_setup(const net_ipv6addr_t ipaddr,
                       FAR struct neighbor_notify_s *notify);

/****************************************************************************
 * Name: neighbor_wait_cancel
 *
 * Description:
 *   Cancel any wait set after neighbor_wait_setup is called but before
 *   neighbor_wait()is called (neighbor_wait() will automatically cancel the
 *   wait).
 *
 * Assumptions:
 *   This function may execute in the interrupt context when called from
 *   neighbor_wait().
 *
 ****************************************************************************/

int neighbor_wait_cancel(FAR struct neighbor_notify_s *notify);

/****************************************************************************
 * Name: neighbor_wait
 *
 * Description:
 *   Called each time that a Neighbor Solicitation is sent.  This function
 *   will sleep until either: (1) the matching Neighbor Advertisement is
 *   received, or (2) a timeout occurs.
 *
 * Assumptions:
 *   This function is called from neighbor_send() and must execute with
 *   the network un-locked (interrupts may be disabled to keep the things
 *   stable).
 *
 ****************************************************************************/

int neighbor_wait(FAR struct net_driver_s *dev,
                FAR struct neighbor_notify_s *notify, unsigned int timeout);

/****************************************************************************
 * Name: neighbor_notify
 *
 * Description:
 *   Called each time that a Neighbor Advertisement is received in order to
 *   wake-up any threads that may be waiting for this particular Neighbor
 *   Advertisement.
 *
 * Assumptions:
 *   This function is called from the MAC device driver indirectly through
 *   icmpv6_icmpv6in() will execute with the network locked.
 *
 ****************************************************************************/

void neighbor_notify(net_ipv6addr_t ipaddr);

#else /* !CONFIG_NET_NEIGHBOR_SEND */
#  define neighbor_send(d,i) (0)
#  define neighbor_wait_setup(i,n)
#  define neighbor_wait_cancel(n) (0)
#  define neighbor_wait(d,n,t) (0)
#  define neighbor_notify(i)
#endif /* CONFIG_NET_NEIGHBOR_SEND */

#endif /* __NET_NEIGHBOR_NEIGHBOR_SEND_H */
