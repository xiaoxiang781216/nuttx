/****************************************************************************
 * net/can/can_raw.c
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

#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>

#include <assert.h>
#include <debug.h>
#include <errno.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <arch/irq.h>

#include <nuttx/mm/iob.h>
#include <nuttx/mm/mempool.h>
#include <nuttx/net/ip.h>
#include <nuttx/net/net.h>
#include <nuttx/net/netdev.h>
#include <nuttx/semaphore.h>

#include "can/can.h"
#include "devif/devif.h"
#include "netdev/netdev.h"
#include "socket/socket.h"
#include "utils/utils.h"

#ifdef CONFIG_NET_CANFORWARD_IEEE1722ACF
#  include "netforward/ieee1722acf.h"
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* This structure describes CAN RAW connection state */

struct can_raw_conn_s
{
  struct can_conn_s conn;      /* Common CAN connection state */
  int16_t           crefs;     /* Reference count */

  /* Rx sync callback.
   *
   *    rx_sync_callback_args - the user data passed to the callback
   */

  can_rx_cb_t rxcb;
  FAR void *rxarg;

#ifdef CONFIG_NET_CANPROTO_OPTIONS
  struct can_filter filters[CONFIG_NET_CAN_RAW_FILTER_MAX];
  uint32_t filter_count;
#  ifdef CONFIG_NET_CAN_ERRORS
  can_err_mask_t err_mask;
#  endif
#endif
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int can_raw_setup(FAR struct socket *psock);
static sockcaps_t can_raw_sockcaps(FAR struct socket *psock);
static void can_raw_addref(FAR struct socket *psock);
static int can_raw_bind(FAR struct socket *psock,
                        FAR const struct sockaddr *addr, socklen_t addrlen);
static int can_raw_getsockname(FAR struct socket *psock,
                               FAR struct sockaddr *addr,
                               FAR socklen_t *addrlen);
static int can_raw_poll(FAR struct socket *psock, FAR struct pollfd *fds,
                        bool setup);
static int can_raw_close(FAR struct socket *psock);
static uint32_t can_raw_input(FAR struct can_conn_s *conn,
                              FAR struct net_driver_s *dev);
static ssize_t can_raw_recvmsg(FAR struct socket *psock,
                               FAR struct msghdr *msg, int flags);
static ssize_t can_raw_sendmsg(FAR struct socket *psock,
                               FAR const struct msghdr *msg, int flags);
#ifdef CONFIG_NET_CANPROTO_OPTIONS
static int can_raw_getsockopt(FAR struct socket *psock, int level,
                              int option, FAR void *value,
                              FAR socklen_t *value_len);
static int can_raw_setsockopt(FAR struct socket *psock, int level,
                              int option, FAR const void *value,
                              socklen_t value_len);
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* The array containing all CAN RAW sockets. */

MEMPOOL_DEFINE(g_can_raw_conns, sizeof(struct can_raw_conn_s),
               CONFIG_NET_CAN_PREALLOC_CONNS, CONFIG_NET_CAN_MAX_CONNS,
               CONFIG_NET_CAN_ALLOC_CONNS);

/****************************************************************************
 * Public Data
 ****************************************************************************/

const struct sock_intf_s g_can_raw_sockif =
{
  can_raw_setup,       /* si_setup */
  can_raw_sockcaps,    /* si_sockcaps */
  can_raw_addref,      /* si_addref */
  can_raw_bind,        /* si_bind */
  can_raw_getsockname, /* si_getsockname */
  NULL,                /* si_getpeername */
  NULL,                /* si_listen */
  NULL,                /* si_connect */
  NULL,                /* si_accept */
  can_raw_poll,        /* si_poll */
  can_raw_sendmsg,     /* si_sendmsg */
  can_raw_recvmsg,     /* si_recvmsg */
  can_raw_close,       /* si_close */
  NULL,                /* si_ioctl */
  NULL,                /* si_socketpair */
  NULL                 /* si_shutdown */
#if defined(CONFIG_NET_SOCKOPTS) && defined(CONFIG_NET_CANPROTO_OPTIONS)
  , can_raw_getsockopt /* si_getsockopt */
  , can_raw_setsockopt /* si_setsockopt */
#endif
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: can_raw_match
 ****************************************************************************/

static bool can_raw_match(FAR struct can_conn_s *conn,
                          FAR struct net_driver_s *dev)
{
#ifdef CONFIG_NET_CANPROTO_OPTIONS
  FAR struct can_raw_conn_s *raw = (FAR struct can_raw_conn_s *)conn;
  canid_t can_id;
  uint32_t i;

  memcpy(&can_id, IOB_DATA(dev->d_iob), sizeof(canid_t));

#  ifdef CONFIG_NET_CAN_ERRORS
  /* error message frame */

  if ((can_id & CAN_ERR_FLAG) != 0)
    {
      return (can_id & raw->err_mask) != 0;
    }
#  endif

  for (i = 0; i < raw->filter_count; i++)
    {
      if (raw->filters[i].can_id & CAN_INV_FILTER)
        {
          if ((can_id & raw->filters[i].can_mask) !=
              ((raw->filters[i].can_id & ~CAN_INV_FILTER) &
               raw->filters[i].can_mask))
            {
              return true;
            }
        }
      else
        {
          if ((can_id & raw->filters[i].can_mask) ==
              (raw->filters[i].can_id & raw->filters[i].can_mask))
            {
              return true;
            }
        }
    }

  return false;
#else
  return true;
#endif
}

/****************************************************************************
 * Name: can_raw_input
 ****************************************************************************/

static uint32_t can_raw_input(FAR struct can_conn_s *conn,
                              FAR struct net_driver_s *dev)
{
  FAR struct can_raw_conn_s *raw = (FAR struct can_raw_conn_s *)conn;
  uint32_t flags;
  int ret;

  if (raw->rxcb != NULL)
    {
      FAR uint8_t *frame = dev->d_buf;
      FAR struct iob_s *iob = dev->d_iob;

      dev->d_buf = NULL;
      dev->d_iob = NULL;
      flags = CAN_NEWDATA;
      ret = raw->rxcb(raw->rxarg, iob);

      if (ret == OK)
        {
          flags = 0;
        }

      netdev_iob_replace(dev, iob);
      dev->d_buf = frame;
      return flags;
    }

  return can_callback(dev, conn, CAN_NEWDATA);
}

/****************************************************************************
 * Name: can_raw_setup
 *
 * Description:
 *   Called for socket() to verify that the provided socket type and
 *   protocol are usable by CAN RAW sockets.
 *
 ****************************************************************************/

static int can_raw_setup(FAR struct socket *psock)
{
  FAR struct can_conn_s *conn;
  FAR struct can_raw_conn_s *raw;
  int domain = psock->s_domain;
  int type = psock->s_type;
  int proto = psock->s_proto;

  DEBUGASSERT((unsigned int)proto <= UINT8_MAX);

  /* PF_CAN protocols accepted by this socket interface. */

  switch (proto)
    {
      case 0:            /* INET subsystem for netlib_ifup */
      case CAN_RAW:      /* RAW sockets */
      case CAN_1722ACF:  /* IEEE 1722 AVTP CAN ACF */
        break;

      default:
        return -EPROTONOSUPPORT;
    }

  if (domain != PF_CAN || (type != SOCK_RAW && type != SOCK_DGRAM))
    {
      return -EPROTONOSUPPORT;
    }

  raw = mempool_zallocate(&g_can_raw_conns, 0);
  if (raw == NULL)
    {
      return -ENOMEM;
    }

  conn = &raw->conn;
  can_conn_init(conn);
  raw->crefs  = 1;
  conn->match = can_raw_match;
  conn->input = can_raw_input;

#ifdef CONFIG_NET_CANPROTO_OPTIONS
  /* By default the filter is configured to catch all.  The zeroed
   * filter matches all frames, so only the count must be set.
   */

  raw->filter_count = 1;

  /* By default loopback is enabled */

  _SO_SETOPT(conn->sconn.s_options, CAN_RAW_LOOPBACK);
#endif

  psock->s_conn = conn;
  return OK;
}

/****************************************************************************
 * Name: can_raw_sockcaps
 ****************************************************************************/

static sockcaps_t can_raw_sockcaps(FAR struct socket *psock)
{
  /* Permit vfcntl to set socket to non-blocking */

  return SOCKCAP_NONBLOCKING;
}

/****************************************************************************
 * Name: can_raw_addref
 ****************************************************************************/

static void can_raw_addref(FAR struct socket *psock)
{
  FAR struct can_raw_conn_s *raw = psock->s_conn;

  DEBUGASSERT(raw->crefs > 0 && raw->crefs < 255);
  raw->crefs++;
}

/****************************************************************************
 * Name: can_raw_bind
 ****************************************************************************/

static int can_raw_bind(FAR struct socket *psock,
                        FAR const struct sockaddr *addr, socklen_t addrlen)
{
  return can_conn_bind(psock->s_conn, addr, addrlen);
}

/****************************************************************************
 * Name: can_raw_getsockname
 ****************************************************************************/

static int can_raw_getsockname(FAR struct socket *psock,
                               FAR struct sockaddr *addr,
                               FAR socklen_t *addrlen)
{
  FAR struct can_conn_s *conn = psock->s_conn;
  struct sockaddr_can canaddr;
  socklen_t copylen;

  memset(&canaddr, 0, sizeof(canaddr));
  canaddr.can_family  = AF_CAN;
  canaddr.can_ifindex = can_get_ifindex(conn);

  copylen = sizeof(canaddr);
  if (copylen > *addrlen)
    {
      copylen = *addrlen;
    }

  memcpy(addr, &canaddr, copylen);
  *addrlen = copylen;
  return OK;
}

/****************************************************************************
 * Name: can_raw_poll_eventhandler
 *
 * Description:
 *   This function is called to perform the actual CAN receive operation
 *   via the device interface layer. from can_input()
 *
 * Input Parameters:
 *   dev      The structure of the network driver that caused the event
 *   pvpriv   An instance of struct can_poll_s cast to void*
 *   flags    Set of events describing why the callback was invoked
 *
 * Returned Value:
 *   None
 *
 * Assumptions:
 *   This function must be called with the network locked.
 *
 ****************************************************************************/

static uint32_t can_raw_poll_eventhandler(FAR struct net_driver_s *dev,
                                          FAR void *pvpriv, uint32_t flags)
{
  FAR struct can_poll_s *info = pvpriv;

  DEBUGASSERT(!info || (info->conn && info->fds));

  /* 'priv' might be null in some race conditions (?) */

  if (info)
    {
      FAR struct can_conn_s *conn = info->conn;
      pollevent_t eventset = 0;

      /* Check for data or connection availability events. */

      if ((flags & CAN_NEWDATA) != 0)
        {
          eventset |= POLLIN;
        }

      /* Check for loss of connection events. */

      if ((flags & NETDEV_DOWN) != 0)
        {
          eventset |= (POLLHUP | POLLERR);
        }

      /* A poll is a sign that we are free to send data. */

      else if ((flags & CAN_POLL) != 0 &&
               can_cansend(conn) >= 0)
        {
          eventset |= POLLOUT;
        }

      /* Awaken the caller of poll() is requested event occurred. */

      poll_notify(&info->fds, 1, eventset);
    }

  return flags;
}

/****************************************************************************
 * Name: can_raw_poll
 *
 * Description:
 *   Poll a CAN connection for a socket protocol implementation.
 *
 *     POLLUP:  Will never be reported
 *     POLLERR: Reported in the event of any failure.
 *     POLLOUT: Always reported if requested.
 *     POLLIN:  Reported if requested but only when pending response data is
 *              available
 *
 * Input Parameters:
 *   psock - The socket to poll.
 *   fds   - The structure describing the events to be monitored.
 *   setup - true: Setup the poll; false: Tear down the poll.
 *
 * Returned Value:
 *   0: Success; Negated errno on failure.
 *
 ****************************************************************************/

static int can_raw_poll(FAR struct socket *psock, FAR struct pollfd *fds,
                        bool setup)
{
  FAR struct can_conn_s *conn = psock->s_conn;
  FAR struct can_poll_s *info = NULL;
  FAR struct devif_callback_s *cb;
  pollevent_t eventset = 0;
  int ret = OK;

  if (conn == NULL)
    {
      return -EBADF;
    }

  /* Check if we are setting up or tearing down the poll */

  if (setup)
    {
      int i;

      conn_dev_lock(&conn->sconn, conn->dev);

      for (i = 0; i < CONFIG_NET_CAN_NPOLLWAITERS; i++)
        {
          if (conn->pollinfo[i].fds == NULL)
            {
              info = &conn->pollinfo[i];
              break;
            }
        }

      if (info == NULL)
        {
          ret = -EBUSY;
          goto errout_with_lock;
        }

      info->dev = conn->dev;

      cb = can_callback_alloc(info->dev, conn);
      if (cb == NULL)
        {
          ret = -EBUSY;
          goto errout_with_lock;
        }

      /* Initialize the poll info container */

      info->conn = conn;
      info->fds  = fds;
      info->cb   = cb;

      /* Initialize the callback structure.  Save the reference to the info
       * structure as callback private data so that it will be available
       * during callback processing.
       */

      cb->flags = NETDEV_DOWN;
      cb->priv  = info;
      cb->event = can_raw_poll_eventhandler;

      if ((fds->events & POLLOUT) != 0)
        {
          cb->flags |= CAN_POLL;
        }

      if ((fds->events & POLLIN) != 0)
        {
          cb->flags |= CAN_NEWDATA;
        }

      /* Save the reference in the poll info structure as fds private as well
       * for use during poll teardown as well.
       */

      fds->priv = info;

      /* Check for read data availability now */

      if (!IOB_QEMPTY(&conn->readahead))
        {
          /* Normal data may be read without blocking. */

          eventset |= POLLRDNORM;
        }

      if (can_cansend(conn) >= 0)
        {
          /* A CAN frame may be sent without blocking. */

          eventset |= POLLWRNORM;
        }

      /* Check if any requested events are already in effect */

      poll_notify(&fds, 1, eventset);

errout_with_lock:
      conn_dev_unlock(&conn->sconn, conn->dev);
    }
  else
    {
      info = (FAR struct can_poll_s *)fds->priv;

      if (info != NULL)
        {
          /* Cancel any response notifications */

          conn_dev_lock(&conn->sconn, info->dev);
          can_callback_free(info->dev, conn, info->cb);

          /* Release the poll/select data slot */

          info->fds->priv = NULL;
          info->fds = NULL;

          /* Then free the poll info container */

          info->conn = NULL;
          conn_dev_unlock(&conn->sconn, info->dev);
        }
    }

  return ret;
}

/****************************************************************************
 * Name: can_raw_close
 ****************************************************************************/

static int can_raw_close(FAR struct socket *psock)
{
  FAR struct can_conn_s     *conn = psock->s_conn;
  FAR struct can_raw_conn_s *raw  = (FAR struct can_raw_conn_s *)conn;

  if (raw->crefs <= 1)
    {
      can_conn_uninit(conn);
      mempool_release(&g_can_raw_conns, conn);
    }
  else
    {
      raw->crefs--;
    }

  return OK;
}

/****************************************************************************
 * Name: can_raw_recvmsg
 *
 * Description:
 *   recvmsg() receives messages from a socket, and may be used to receive
 *   data on a socket whether or not it is connection-oriented.
 *
 *   If from is not NULL, and the underlying protocol provides the source
 *   address, this source address is filled in. The argument 'msg_namelen'
 *   initialized to the size of the buffer associated with from, and modified
 *   on return to indicate the actual size of the address stored there.
 *
 * Input Parameters:
 *   psock    A pointer to a NuttX-specific, internal socket structure
 *   msg      Buffer to receive the message
 *   flags    Receive flags (ignored)
 *
 ****************************************************************************/

static ssize_t can_raw_recvmsg(FAR struct socket *psock,
                               FAR struct msghdr *msg, int flags)
{
  FAR struct can_conn_s *conn;
  FAR struct iob_s *iob = NULL;
  ssize_t recvlen;
  int ret;

  conn = psock->s_conn;

  if (psock->s_type != SOCK_RAW)
    {
      nerr("ERROR: Unsupported socket type: %d\n", psock->s_type);
      return -ENOSYS;
    }

  if (msg->msg_iovlen != 1)
    {
      return -ENOTSUP;
    }

  while (iob == NULL)
    {
      ret = can_recviob(conn, &iob, flags,
                        _SO_TIMEOUT(conn->sconn.s_rcvtimeo));
      if (ret < 0)
        {
          return ret;
        }

#if defined(CONFIG_NET_CANPROTO_OPTIONS) && defined(CONFIG_NET_CAN_CANFD)
      if (!_SO_GETOPT(conn->sconn.s_options, CAN_RAW_FD_FRAMES) &&
          iob->io_pktlen > sizeof(struct can_frame))
        {
          iob_free_chain(iob);
          iob = NULL;
        }
#endif
    }

#ifdef CONFIG_NET_TIMESTAMP
  cmsg_store_timestamp(msg, &iob->io_time, conn->sconn.s_options);
#endif

  if (iob->io_conn == &conn->sconn)
    {
      msg->msg_flags |= MSG_CONFIRM;
    }

  recvlen = MIN(msg->msg_iov->iov_len, iob->io_pktlen);
  ret = iob_copyout(msg->msg_iov->iov_base, iob, recvlen, 0);
  if (ret < 0)
    {
      iob_free_chain(iob);
      return ret;
    }

  recvlen = ret;
  if (recvlen < iob->io_pktlen)
    {
      iob = iob_trimhead(iob, recvlen);
      if (iob != NULL)
        {
          conn_lock(&conn->sconn);
          iob_add_queue_head(iob, &conn->readahead);
          conn_unlock(&conn->sconn);
        }
    }
  else
    {
      iob_free_chain(iob);
    }

  return recvlen;
}

/****************************************************************************
 * Name: can_raw_sendmsg
 *
 * Description:
 *   The can_raw_sendmsg() call may be used only when the packet socket is
 *   in a connected state (so that the intended recipient is known).
 *
 * Input Parameters:
 *   psock    An instance of the internal socket structure.
 *   msg      CAN frame and optional CMSG
 *   flags    Send flags (ignored)
 *
 * Returned Value:
 *   On success, returns the number of characters sent.  On  error,
 *   a negated errno value is retruend.  See sendmsg() for the complete list
 *   of return values.
 *
 ****************************************************************************/

static ssize_t can_raw_sendmsg(FAR struct socket *psock,
                               FAR const struct msghdr *msg, int flags)
{
  FAR struct can_conn_s *conn;
  FAR struct iob_s *iob;
  size_t framelen;
  bool nonblock;
  int ret;

  /* Verify that the sockfd corresponds to valid, allocated socket */

  if (psock == NULL || psock->s_conn == NULL)
    {
      return -EBADF;
    }

  /* Only SOCK_RAW is supported */

  if (psock->s_type != SOCK_RAW)
    {
      /* EDESTADDRREQ.  Signifies that the socket is not connection-mode and
       * no peer address is set.
       */

      return -EDESTADDRREQ;
    }

  conn = psock->s_conn;

  if (msg == NULL || msg->msg_iovlen != 1)
    {
      return -EINVAL;
    }

  framelen = msg->msg_iov->iov_len;

#if defined(CONFIG_NET_CANPROTO_OPTIONS) && defined(CONFIG_NET_CAN_CANFD)
  if (_SO_GETOPT(conn->sconn.s_options, CAN_RAW_FD_FRAMES))
    {
      if (framelen != CANFD_MTU && framelen != CAN_MTU)
        {
          return -EINVAL;
        }
    }
  else
#endif
    {
      if (framelen != CAN_MTU)
        {
          return -EINVAL;
        }
    }

  nonblock =
      _SS_ISNONBLOCK(conn->sconn.s_flags) || (flags & MSG_DONTWAIT) != 0;

  if (nonblock)
    {
      iob = iob_timedalloc_with_size(NET_CAN_BUFSIZE, true, 0);
    }
  else
    {
      iob = iob_timedalloc_with_size(NET_CAN_BUFSIZE, true,
                                     _SO_TIMEOUT(conn->sconn.s_sndtimeo));
    }

  if (iob == NULL)
    {
      return nonblock ? -EAGAIN : -ENOMEM;
    }

  iob_reserve(iob, CONFIG_NET_LL_GUARDSIZE);
  iob_update_pktlen(iob, 0, false);

  if (nonblock)
    {
      ret = iob_trycopyin(iob, msg->msg_iov->iov_base, framelen, 0,
                          false);
    }
  else
    {
      ret = iob_copyin(iob, msg->msg_iov->iov_base, framelen, 0, false);
    }

  if (ret < 0)
    {
      goto errout_with_iob;
    }

#ifdef CONFIG_NET_CAN_RAW_TX_DEADLINE
  if (msg->msg_controllen > sizeof(struct cmsghdr))
    {
      FAR struct cmsghdr *cmsg = CMSG_FIRSTHDR(msg);

      if (_SO_GETOPT(conn->sconn.s_options, CAN_RAW_TX_DEADLINE) &&
          cmsg->cmsg_level == SOL_CAN_RAW &&
          cmsg->cmsg_type == CAN_RAW_TX_DEADLINE &&
          cmsg->cmsg_len == CMSG_LEN(sizeof(struct timeval)))
        {
          if (nonblock)
            {
              ret = iob_trycopyin(iob, CMSG_DATA(cmsg),
                                  sizeof(struct timeval), framelen, false);
            }
          else
            {
              ret = iob_copyin(iob, CMSG_DATA(cmsg),
                               sizeof(struct timeval), framelen, false);
            }

          if (ret < 0)
            {
              goto errout_with_iob;
            }
        }
    }
#endif

#ifdef CONFIG_NET_CANFORWARD_IEEE1722ACF
  if (psock->s_proto == CAN_1722ACF)
    {
      FAR struct net_driver_s *dev = conn->dev;
      FAR struct sockaddr_can *canaddr =
          (FAR struct sockaddr_can *)msg->msg_name;
      struct cangw_rule_s fwdrule;

      if (dev == NULL)
        {
          iob_free_chain(iob);
          return -ENODEV;
        }

      if (canaddr == NULL || canaddr->can_family != AF_CAN)
        {
          nerr("ERROR: CAN 1722ACF setting is not right\n");
          iob_free_chain(iob);
          return -EINVAL;
        }

      fwdrule.dest_dev = dev;
      fwdrule.gwtype = CGW_TYPE_CAN_ETH1722;
      fwdrule.gw.cegw.stream_index =
          canaddr->can_addr.eth1722acf.stream_index;
      fwdrule.gw.cegw.acf_msg_type =
          canaddr->can_addr.eth1722acf.acf_msg_type;
      fwdrule.gw.cegw.can_bus_id = canaddr->can_addr.eth1722acf.can_bus_id;

      ret = ieee1722acf_send(&fwdrule, iob);
      iob_free_chain(iob);
      if (ret < 0)
        {
          nerr("ERROR: Failed to send can msg into acf stream: %d\n", ret);
          return ret;
        }

      return framelen;
    }
#endif

  ret = can_sendiob(conn, iob, flags);
  if (ret < 0)
    {
      goto errout_with_iob;
    }

  return framelen;

errout_with_iob:
  iob_free_chain(iob);
  return ret;
}

#ifdef CONFIG_NET_CANPROTO_OPTIONS
/****************************************************************************
 * Name: can_raw_getsockopt
 *
 * Description:
 *   can_raw_getsockopt() retrieves the value for the option specified by the
 *   'option' argument for the socket specified by the 'psock' argument.  If
 *   the size of the option value is greater than 'value_len', the value
 *   stored in the object pointed to by the 'value' argument will be silently
 *   truncated. Otherwise, the length pointed to by the 'value_len' argument
 *   will be modified to indicate the actual length of the 'value'.
 *
 *   See <sys/socket.h> a complete list of values for the socket-level
 *   'option' argument.  Protocol-specific options are are protocol specific
 *   header files (such as nuttx/can.h for the case of the CAN protocol).
 *
 * Input Parameters:
 *   psock     Socket structure of the socket to query
 *   level     Protocol level to set the option
 *   option    identifies the option to get
 *   value     Points to the argument value
 *   value_len The length of the argument value
 *
 * Returned Value:
 *   Returns zero (OK) on success.  On failure, it returns a negated errno
 *   value to indicate the nature of the error.  See psock_getsockopt() for
 *   the complete list of appropriate return error codes.
 *
 ****************************************************************************/

static int can_raw_getsockopt(FAR struct socket *psock, int level,
                              int option, FAR void *value,
                              FAR socklen_t *value_len)
{
  FAR struct can_conn_s *conn;
  FAR struct can_raw_conn_s *raw;
  int ret = OK;

  DEBUGASSERT(value != NULL && value_len != NULL);
  conn = psock->s_conn;
  raw = (FAR struct can_raw_conn_s *)conn;

  if (level != SOL_CAN_RAW)
    {
      return -ENOPROTOOPT;
    }

  if (psock->s_type != SOCK_RAW)
    {
      nerr("ERROR:  Not a RAW CAN connection\n");
      return -ENOTCONN;
    }

  /* Lock the network so that we have exclusive access to the socket
   * options.
   */

  conn_lock(&conn->sconn);

  switch (option)
    {
      case CAN_RAW_FILTER:
        if (*value_len % sizeof(struct can_filter) != 0)
          {
            ret = -EINVAL;
          }
        else if (*value_len > CONFIG_NET_CAN_RAW_FILTER_MAX *
                 sizeof(struct can_filter))
          {
            ret = -EINVAL;
          }
        else
          {
            uint32_t count = raw->filter_count;
            uint32_t i;

            if (*value_len < count * sizeof(struct can_filter))
              {
                count = *value_len / sizeof(struct can_filter);
              }
            else
              {
                *value_len = count * sizeof(struct can_filter);
              }

            for (i = 0; i < count; i++)
              {
                ((FAR struct can_filter *)value)[i] = raw->filters[i];
              }
          }
        break;

#ifdef CONFIG_NET_CAN_ERRORS
      case CAN_RAW_ERR_FILTER:
        if (*value_len < sizeof(can_err_mask_t))
          {
            conn_unlock(&conn->sconn);
            return -EINVAL;
          }
        else
          {
            FAR can_err_mask_t *mask = (FAR can_err_mask_t *)value;
            *mask = raw->err_mask;
            *value_len = sizeof(can_err_mask_t);
          }
        break;
#endif

      case CAN_RAW_LOOPBACK:
      case CAN_RAW_RECV_OWN_MSGS:
#ifdef CONFIG_NET_CAN_CANFD
      case CAN_RAW_FD_FRAMES:
#endif
#ifdef CONFIG_NET_CAN_RAW_TX_DEADLINE
      case CAN_RAW_TX_DEADLINE:
#endif
        /* Verify that option is the size of an 'int'.  Should also check
         * that 'value' is properly aligned for an 'int'
         */

        if (*value_len < sizeof(int))
          {
            conn_unlock(&conn->sconn);
            return -EINVAL;
         }

        /* Sample the current options.  This is atomic operation and so
         * should not require any special steps for thread safety. We
         * this outside of the macro because you can never be sure what
         * a macro will do.
         */

          *(FAR int *)value = _SO_GETOPT(conn->sconn.s_options, option);
          *value_len        = sizeof(int);
          break;

      default:
        nerr("ERROR: Unrecognized RAW CAN connection option: %d\n", option);
        ret = -ENOPROTOOPT;
        break;
    }

  conn_unlock(&conn->sconn);

  return ret;
}

/****************************************************************************
 * Name: can_raw_setsockopt
 *
 * Description:
 *   can_raw_setsockopt() sets the CAN-protocol option specified by the
 *   'option' argument to the value pointed to by the 'value' argument for
 *   the socket specified by the 'psock' argument.
 *
 *   See <nuttx/can.h> for the a complete list of values of CAN protocol
 *   options.
 *
 * Input Parameters:
 *   psock     Socket structure of socket to operate on
 *   level     Protocol level to set the option
 *   option    identifies the option to set
 *   value     Points to the argument value
 *   value_len The length of the argument value
 *
 * Returned Value:
 *   Returns zero (OK) on success.  On failure, it returns a negated errno
 *   value to indicate the nature of the error.  See psock_setcockopt() for
 *   the list of possible error values.
 *
 ****************************************************************************/

static int can_raw_setsockopt(FAR struct socket *psock, int level,
                              int option, FAR const void *value,
                              socklen_t value_len)
{
  FAR struct can_conn_s *conn;
  FAR struct can_raw_conn_s *raw;
  int ret = OK;

  DEBUGASSERT(value_len == 0 || value != NULL);

  conn = psock->s_conn;
  raw = (FAR struct can_raw_conn_s *)conn;

  if (level != SOL_CAN_RAW)
    {
      return -ENOPROTOOPT;
    }

  if (psock->s_type != SOCK_RAW)
    {
      nerr("ERROR:  Not a RAW CAN connection\n");
      return -ENOTCONN;
    }

  /* Lock the network so that we have exclusive access to the socket
   * options.
   */

  conn_lock(&conn->sconn);

  switch (option)
    {
      case CAN_RAW_FILTER:
        if (value_len == 0)
          {
            raw->filter_count = 0;
          }
        else if (value_len % sizeof(struct can_filter) != 0)
          {
            ret = -EINVAL;
          }
        else if (value_len > CONFIG_NET_CAN_RAW_FILTER_MAX *
                 sizeof(struct can_filter))
          {
            ret = -EINVAL;
          }
        else
          {
            uint32_t i;

            raw->filter_count = value_len / sizeof(struct can_filter);

            for (i = 0; i < raw->filter_count; i++)
              {
                raw->filters[i] = ((struct can_filter *)value)[i];
              }
          }
        break;

#ifdef CONFIG_NET_CAN_ERRORS
      case CAN_RAW_ERR_FILTER:
        if (value_len != sizeof(can_err_mask_t))
          {
            conn_unlock(&conn->sconn);
            return -EINVAL;
          }

        raw->err_mask = *(FAR can_err_mask_t *)value;
        break;
#endif

      case CAN_RAW_LOOPBACK:
      case CAN_RAW_RECV_OWN_MSGS:
#ifdef CONFIG_NET_CAN_CANFD
      case CAN_RAW_FD_FRAMES:
#endif
#ifdef CONFIG_NET_CAN_RAW_TX_DEADLINE
      case CAN_RAW_TX_DEADLINE:
#endif
        /* Verify that option is the size of an 'int'.  Should also check
         * that 'value' is properly aligned for an 'int'
         */

        if (value_len != sizeof(int))
          {
            conn_unlock(&conn->sconn);
            return -EINVAL;
          }

        /* Set or clear the option bit */

        if (*(FAR int *)value)
          {
            _SO_SETOPT(conn->sconn.s_options, option);
          }
        else
          {
            _SO_CLROPT(conn->sconn.s_options, option);
          }

        break;

      default:
        nerr("ERROR: Unrecognized CAN option: %d\n", option);
        ret = -ENOPROTOOPT;
        break;
    }

  conn_unlock(&conn->sconn);

  return ret;
}
#endif /* CONFIG_NET_CANPROTO_OPTIONS */

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: can_set_rx_callback
 *
 * Description:
 *   register rx sync callback to the can connection structures.
 *
 * Input Parameters:
 *   psock - The socket structure of the CAN connection
 *   callback - The callback function to be called when a frame is received
 *   args - The argument to be passed to the callback function
 *
 * Returned Value:
 *   Zero (OK) is returned on success.  Otherwise, a negated errno value is
 *   returned.
 *
 ****************************************************************************/

int can_set_rx_callback(FAR struct socket *psock, can_rx_cb_t callback,
                        FAR void *args)
{
  FAR struct can_raw_conn_s *raw;

  if (psock == NULL || callback == NULL)
    {
      return -EBADF;
    }

  raw = psock->s_conn;
  if (raw == NULL)
    {
      return -EBADF;
    }

  raw->rxcb = callback;
  raw->rxarg = args;
  return OK;
}
