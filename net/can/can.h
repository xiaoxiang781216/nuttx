/****************************************************************************
 * net/can/can.h
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

#ifndef __NET_CAN_CAN_H
#define __NET_CAN_CAN_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>
#include <poll.h>

#include <nuttx/mutex.h>
#include <nuttx/can.h>
#include <nuttx/net/can.h>
#include <nuttx/net/net.h>
#include <nuttx/net/netdev.h>

#include "devif/devif.h"
#include "socket/socket.h"

#ifdef CONFIG_NET_CAN_NOTIFIER
#  include <nuttx/wqueue.h>
#endif

#ifdef CONFIG_NET_CAN

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Allocate a new packet socket data callback */

#define can_callback_alloc(dev,conn) \
  devif_callback_alloc(dev, &conn->sconn.s_list, &conn->sconn.s_listtail)
#define can_callback_free(dev,conn,cb) \
  devif_conn_callback_free(dev, cb, &conn->sconn.s_list, &conn->sconn.s_listtail)

/****************************************************************************
 * Name: can_iob_framelen
 *
 * Description:
 *   Return the length of the CAN frame carried by an I/O buffer.  The buffer
 *   may hold extra trailing data (e.g. a TX deadline appended after the
 *   frame), so the frame length can be smaller than iob->io_pktlen.
 *
 ****************************************************************************/

static inline_function unsigned int can_iob_framelen(FAR struct iob_s *iob)
{
#ifdef CONFIG_NET_CAN_CANFD
  if (iob->io_pktlen >= CANFD_MTU)
    {
      return CANFD_MTU;
    }
#endif

  return CAN_MTU;
}

/****************************************************************************
 * Public Type Definitions
 ****************************************************************************/

/* This is a container that holds the poll-related information */

struct can_conn_s;

struct can_poll_s
{
  FAR struct can_conn_s *conn;     /* Needed to handle loss of connection */
  FAR struct net_driver_s *dev;    /* Needed to free the callback structure */
  FAR struct pollfd *fds;          /* Needed to handle poll events */
  FAR struct devif_callback_s *cb; /* Needed to teardown the poll */
};

/* This structure describes common PF_CAN connection state */

struct can_conn_s
{
  /* Common prologue of all PF_CAN connection structures. */

  struct socket_conn_s sconn;

  FAR struct net_driver_s *dev;      /* Reference to CAN device */

  /* Read-ahead buffering.
   *
   *   readahead - A singly linked list of type struct iob_qentry_s
   *               where the CAN/IP read-ahead data is retained.
   */

  struct iob_queue_s readahead;      /* remove Read-ahead buffering */

#ifdef CONFIG_NET_CAN_WRITE_BUFFERS
  /* Write buffering
   *
   *   write_q   - The queue of unsent I/O buffers.  The head of this
   *               list may be partially sent.  FIFO ordering.
   */

  struct iob_queue_s write_q;        /* Write buffering for can messages */

  /* Callback instance for can send */

  FAR struct devif_callback_s *sndcb;
#endif

  /* The following is a list of poll structures of threads waiting for
   * socket events.
   */

  struct can_poll_s pollinfo[CONFIG_NET_CAN_NPOLLWAITERS];

  bool (*match)(FAR struct can_conn_s *conn,
                FAR struct net_driver_s *dev);
  uint32_t (*input)(FAR struct can_conn_s *conn,
                    FAR struct net_driver_s *dev);
};

/****************************************************************************
 * Public Data
 ****************************************************************************/

#ifdef __cplusplus
#  define EXTERN extern "C"
extern "C"
{
#else
#  define EXTERN extern
#endif

EXTERN const struct sock_intf_s g_can_raw_sockif;
#ifdef CONFIG_NET_CAN_ISOTP
EXTERN const struct sock_intf_s g_can_isotp_sockif;
#endif

/* The CAN connections lock */

DECLARE_PER_CPU_BMP(rmutex_t, g_can_conn_lock);
#define g_can_conn_lock this_cpu_var_bmp(g_can_conn_lock)

/****************************************************************************
 * Name: can_conn_list_lock
 *       can_conn_list_unlock
 *
 * Description:
 *   Lock and unlock the CAN connection list. This is used to protect
 *   the list of active connections.
 *
 * Assumptions:
 *   This function is called from driver.
 *
 ****************************************************************************/

static inline_function void can_conn_list_lock(void)
{
  nxrmutex_lock(&g_can_conn_lock);
}

static inline_function void can_conn_list_unlock(void)
{
  nxrmutex_unlock(&g_can_conn_lock);
}

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: can_get_ifindex
 *
 * Description:
 *   Return the bound interface index for a CAN connection, or zero if the
 *   socket is not bound to a device.
 *
 ****************************************************************************/

int can_get_ifindex(FAR struct can_conn_s *conn);

/****************************************************************************
 * Name: can_sockif
 *
 * Description:
 *   Return the socket interface associated with the CAN address family.
 *
 * Input Parameters:
 *   family   - Socket address family
 *   type     - Socket type
 *   protocol - Socket protocol
 *
 * Returned Value:
 *   On success, a non-NULL instance of struct sock_intf_s is returned.  NULL
 *   is returned only if the address family is not supported.
 *
 ****************************************************************************/

FAR const struct sock_intf_s *
can_sockif(sa_family_t family, int type, int protocol);

/****************************************************************************
 * Name: can_conn_bind
 *
 * Description:
 *   Bind a CAN connection to a local address.
 *
 ****************************************************************************/

int can_conn_bind(FAR struct can_conn_s *conn,
                  FAR const struct sockaddr *addr,
                  socklen_t addrlen);

/****************************************************************************
 * Name: can_conn_init()
 *
 * Description:
 *   Initialize an already allocated CAN connection structure.
 *
 ****************************************************************************/

void can_conn_init(FAR struct can_conn_s *conn);

/****************************************************************************
 * Name: can_conn_uninit()
 *
 * Description:
 *   Uninitialize an already allocated CAN connection structure.
 *
 ****************************************************************************/

void can_conn_uninit(FAR struct can_conn_s *conn);

/****************************************************************************
 * Name: can_conn_next()
 *
 * Description:
 *   Traverse the list of allocated CAN connections
 *
 * Assumptions:
 *   This function is called from CAN device logic.
 *
 ****************************************************************************/

FAR struct can_conn_s *can_conn_next(FAR struct can_conn_s *conn);

/****************************************************************************
 * Name: can_callback
 *
 * Description:
 *   Inform the application holding the packet socket of a change in state.
 *
 * Returned Value:
 *   OK if packet has been processed, otherwise ERROR.
 *
 * Assumptions:
 *   This function is called from network logic at with the network locked.
 *
 ****************************************************************************/

uint32_t can_callback(FAR struct net_driver_s *dev,
                      FAR struct can_conn_s *conn, uint32_t flags);

/****************************************************************************
 * Name: can_poll
 *
 * Description:
 *   Poll a CAN connection structure for availability of TX data
 *
 * Input Parameters:
 *   dev - The device driver structure to use in the send operation
 *   conn - The CAN connection to poll for TX data
 *
 * Returned Value:
 *   None
 *
 * Assumptions:
 *   Called from network stack logic with the network stack locked
 *
 ****************************************************************************/

void can_poll(FAR struct net_driver_s *dev, FAR struct can_conn_s *conn);

/****************************************************************************
 * Name: can_cansend
 *
 * Description:
 *   Return whether a CAN connection can send without blocking.
 *
 ****************************************************************************/

int can_cansend(FAR struct can_conn_s *conn);

/****************************************************************************
 * Name: can_sendiob
 *
 * Description:
 *   Send one complete CAN/CAN FD frame stored in an IOB.  The helper takes
 *   ownership of iob only on success.
 *
 ****************************************************************************/

int can_sendiob(FAR struct can_conn_s *conn, FAR struct iob_s *iob,
                int flags);

/****************************************************************************
 * Name: can_recviob
 *
 * Description:
 *   Receive one complete CAN/CAN FD frame IOB from read-ahead or from the
 *   next CAN_NEWDATA event.  Ownership of the returned IOB is transferred to
 *   the caller.
 *
 ****************************************************************************/

int can_recviob(FAR struct can_conn_s *conn, FAR struct iob_s **iob,
                int flags, unsigned int timeout);

#undef EXTERN
#ifdef __cplusplus
}
#endif

#endif /* CONFIG_NET_CAN */
#endif /* __NET_CAN_CAN_H */
