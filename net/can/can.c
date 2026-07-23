/****************************************************************************
 * net/can/can.c
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

#include <ctype.h>
#include <poll.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <debug.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <arch/irq.h>

#include <nuttx/mm/iob.h>
#include <nuttx/mm/mempool.h>
#include <nuttx/net/can.h>
#include <nuttx/net/ip.h>
#include <nuttx/net/net.h>
#include <nuttx/net/netdev.h>
#include <nuttx/queue.h>
#include <nuttx/semaphore.h>
#ifdef CONFIG_NET_CAN_NOTIFIER
#  include <nuttx/wqueue.h>
#endif

#include "can/can.h"
#include "devif/devif.h"
#include "netdev/netdev.h"
#ifdef CONFIG_NET_CANFORWARD
#  include "netforward/netforward.h"
#endif
#include "socket/socket.h"
#include "utils/utils.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

#ifndef CONFIG_NET_CAN_WRITE_BUFFERS
/* This structure holds the state of the send operation until it can be
 * operated upon by the event handler.
 */

struct can_send_s
{
  FAR struct devif_callback_s *snd_cb;   /* Reference to callback instance */
  FAR struct iob_s            *snd_iob;  /* IOB containing the frame to send */
  sem_t                        snd_sem;  /* Used to wake up the waiting thread */
  ssize_t                      snd_sent; /* The number of bytes sent */
};
#endif

struct can_recvfrom_s
{
  FAR struct can_conn_s       *rcv_conn;   /* CAN connection associated with socket */
  FAR struct devif_callback_s *rcv_cb;     /* Reference to callback instance */
  FAR struct iob_s            *rcv_iob;    /* Received CAN frame IOB */
  sem_t                        rcv_sem;    /* Semaphore signals recv completion */
  int                          rcv_result; /* Success:OK, failure:negated errno */
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* A list of all active CAN connections */

static DEFINE_PER_CPU_BMP(dq_queue_t, g_active_can_conns);
#define g_active_can_conns this_cpu_var_bmp(g_active_can_conns)

/* Pool of receive state structures */

MEMPOOL_DEFINE(g_can_state_infos, sizeof(struct can_recvfrom_s),
               CONFIG_NET_CAN_PREALLOC_CONNS, CONFIG_NET_CAN_MAX_CONNS,
               CONFIG_NET_CAN_ALLOC_CONNS);

/****************************************************************************
 * Public Data
 ****************************************************************************/

/* The CAN connections lock */

#undef g_can_conn_lock
DEFINE_PER_CPU_BMP(rmutex_t, g_can_conn_lock) = NXRMUTEX_INITIALIZER;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: can_sendbuffer_notify
 ****************************************************************************/

#if CONFIG_NET_SEND_BUFSIZE > 0
static inline void can_sendbuffer_notify(FAR struct can_conn_s *conn)
{
  int val = 0;

  nxsem_get_value(&conn->sconn.s_sndsem, &val);
  if (val < 1)
    {
      nxsem_post(&conn->sconn.s_sndsem);
    }
}
#endif /* CONFIG_NET_SEND_BUFSIZE */

/****************************************************************************
 * Name: can_iob_framelen
 ****************************************************************************/

static inline unsigned int can_iob_framelen(FAR struct iob_s *iob)
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
 * Name: can_readahead_iob
 ****************************************************************************/

static inline int can_readahead_iob(FAR struct can_conn_s *conn,
                                    FAR struct iob_s **iob)
{
  *iob = iob_remove_queue(&conn->readahead);
  if (*iob != NULL)
    {
      return OK;
    }

  return -ENODATA;
}

/****************************************************************************
 * Name: can_recvfrom_initialize
 ****************************************************************************/

static inline void can_recvfrom_initialize(FAR struct can_conn_s *conn,
                                           FAR struct can_recvfrom_s *pstate)
{
  memset(pstate, 0, sizeof(struct can_recvfrom_s));
  nxsem_init(&pstate->rcv_sem, 0, 0);

  pstate->rcv_conn = conn;
}

/****************************************************************************
 * Name: can_recvfrom_eventhandler
 ****************************************************************************/

static uint32_t can_recvfrom_eventhandler(FAR struct net_driver_s *dev,
                                          FAR void *pvpriv, uint32_t flags)
{
  struct can_recvfrom_s *pstate = pvpriv;

  /* 'priv' might be null in some race conditions (?) */

  if (pstate)
    {
      if ((flags & CAN_NEWDATA) != 0)
        {
          pstate->rcv_iob = dev->d_iob;
          netdev_iob_clear(dev);
          dev->d_len = 0;

          /* Don't allow any further call backs. */

          pstate->rcv_cb->flags = 0;
          pstate->rcv_cb->priv  = NULL;
          pstate->rcv_cb->event = NULL;

          /* Indicate that the data has been consumed. */

          flags &= ~CAN_NEWDATA;

          /* Wake up the waiting thread. */

          nxsem_post(&pstate->rcv_sem);
        }
    }

  return flags;
}

/****************************************************************************
 * Name: can_recvfrom_result
 ****************************************************************************/

static inline ssize_t can_recvfrom_result(int result,
                                          FAR struct can_recvfrom_s *pstate)
{
  /* Check for a error/timeout detected by the event handler.  Errors are
   * signaled by negative errno values for the rcv length
   */

  if (pstate->rcv_result < 0)
    {
      /* This might return EAGAIN on a timeout */

      return pstate->rcv_result;
    }

  /* If conn_dev_sem_timedwait failed, then we were probably reawakened by
   * a signal. In this case, conn_dev_sem_timedwait will have returned
   * negated errno appropriately.
   */

  if (result < 0)
    {
      return result;
    }

  return pstate->rcv_iob != NULL ? OK : -EAGAIN;
}

#ifdef CONFIG_NET_CAN_WRITE_BUFFERS
/****************************************************************************
 * Name: can_send_eventhandler
 ****************************************************************************/

static uint32_t can_send_eventhandler(FAR struct net_driver_s *dev,
                                       FAR void *pvpriv, uint32_t flags)
{
  FAR struct can_conn_s *conn = pvpriv;

  DEBUGASSERT(dev != NULL && conn != NULL);

  /* Check if the outgoing packet is available. It may have been claimed
   * by a send event handler serving a different thread -OR- if the
   * output buffer currently contains unprocessed incoming data. In
   * these cases we will just have to wait for the next polling cycle.
   */

  if (dev->d_sndlen > 0 || (flags & CAN_NEWDATA) != 0)
    {
      return flags;
    }

  /* It looks like we are good to send the data */

  else
    {
      FAR struct iob_s *wrb_iob;

      wrb_iob = (FAR struct iob_s *)iob_remove_queue(&conn->write_q);
      if (wrb_iob == NULL)
        {
          return flags;
        }

      netdev_iob_replace(dev, wrb_iob);

      dev->d_sndlen = wrb_iob->io_pktlen;
      dev->d_len = can_iob_framelen(wrb_iob);
      ninfo("wrb=%p sndlen=%d\n", wrb_iob, dev->d_sndlen);

      if (iob_peek_queue(&conn->write_q) != NULL)
        {
          netdev_txnotify(dev, CAN_POLL);
        }
      else
        {
          conn->sndcb->flags = 0;
          conn->sndcb->priv = NULL;
          conn->sndcb->event = NULL;
        }

      flags &= ~CAN_POLL;

#if CONFIG_NET_SEND_BUFSIZE > 0
      can_sendbuffer_notify(conn);
#endif
    }

  return flags;
}

#else
/****************************************************************************
 * Name: can_send_eventhandler
 ****************************************************************************/

static uint32_t can_send_eventhandler(FAR struct net_driver_s *dev,
                                       FAR void *pvpriv, uint32_t flags)
{
  FAR struct can_send_s *pstate = pvpriv;

  if (pstate)
    {
      /* Check if the outgoing packet is available. It may have been claimed
       * by a send event handler serving a different thread -OR- if the
       * output buffer currently contains unprocessed incoming data. In
       * these cases we will just have to wait for the next polling cycle.
       */

      if (dev->d_sndlen > 0 || (flags & CAN_NEWDATA) != 0)
        {
          return flags;
        }

      /* It looks like we are good to send the data */

      else
        {
          FAR struct iob_s *iob = pstate->snd_iob;

          DEBUGASSERT(iob != NULL);

          netdev_iob_replace(dev, iob);

          dev->d_len       = can_iob_framelen(iob);
          dev->d_sndlen    = iob->io_pktlen;
          pstate->snd_iob  = NULL;
          pstate->snd_sent = dev->d_len;
        }

      /* Don't allow any further call backs. */

      pstate->snd_cb->flags = 0;
      pstate->snd_cb->priv  = NULL;
      pstate->snd_cb->event = NULL;

      /* Wake up the waiting thread */

      nxsem_post(&pstate->snd_sem);
    }

  return flags;
}
#endif

#ifdef CONFIG_NET_CAN_NOTIFIER
/****************************************************************************
 * Name: can_readahead_signal
 *
 * Description:
 *   Read-ahead data has been buffered.  Signal all threads waiting for
 *   read-ahead data to become available.
 *
 *   When read-ahead data becomes available, *all* of the workers waiting
 *   for read-ahead data will be executed.  If there are multiple workers
 *   waiting for read-ahead data then only the first to execute will get the
 *   data.  Others will need to call can_readahead_notifier_setup() once
 *   again.
 *
 * Input Parameters:
 *   conn  - The CAN connection where read-ahead data was just buffered.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void can_readahead_signal(FAR struct can_conn_s *conn)
{
  work_notifier_signal(WORK_CAN_READAHEAD, conn);
}
#endif

/****************************************************************************
 * Name: can_datahandler
 *
 * Description:
 *   Handle data that is not accepted by the application.  This may be called
 *   either (1) from the data receive logic if it cannot buffer the data, or
 *   (2) from the CAN event logic is there is no listener in place ready to
 *   receive the data.
 *
 * Input Parameters:
 *   dev  - The device which as active when the event was detected.
 *   conn - A pointer to the CAN connection structure
 *
 * Returned Value:
 *   The number of bytes actually buffered is returned.  This will be either
 *   zero or equal to buflen; partial packets are not buffered.
 *
 * Assumptions:
 * - The caller has checked that CAN_NEWDATA is set in flags and that is no
 *   other handler available to process the incoming data.
 * - This function must be called with the network locked.
 *
 ****************************************************************************/

static int can_datahandler(FAR struct net_driver_s *dev,
                           FAR struct can_conn_s *conn)
{
  FAR struct iob_s *iob = dev->d_iob;
  int ret;

  conn_lock(&conn->sconn);
#if CONFIG_NET_RECV_BUFSIZE > 0
  int bufnum = div_const_roundup(conn->sconn.s_rcvbufs, NET_CAN_PKTSIZE);

  /* Check the frame count pending on conn->readahead */

  ret = iob_get_queue_entry_count(&conn->readahead);
  if (ret >= bufnum)
    {
      nwarn("WARNNING: There are no free recive buffer to retain the data. "
            "Recive buffer number:%d, recived frames:%u \n", bufnum, ret);
      netdev_iob_release(dev);
      conn_unlock(&conn->sconn);
      return 0;
    }
#endif

  /* Concat the iob to readahead */

  IOB_SET_OWNER(iob, "can readahead");
  iob_add_queue(iob, &conn->readahead);
#ifdef CONFIG_NET_CAN_NOTIFIER
  /* Provide notification(s) that additional CAN read-ahead data is
   * available.
   */

  can_readahead_signal(conn);
#endif

  ret = iob->io_pktlen;

  /* Device buffer has been enqueued, clear the handle */

  netdev_iob_clear(dev);
  conn_unlock(&conn->sconn);
  return ret;
}

/****************************************************************************
 * Name: can_data_event
 *
 * Description:
 *   Handle data that is not accepted by the application because there is no
 *   listener in place ready to receive the data.
 *
 * Assumptions:
 * - The caller has checked that CAN_NEWDATA is set in flags and that is no
 *   other handler available to process the incoming data.
 * - This function must be called with the network locked.
 *
 ****************************************************************************/

static inline uint32_t
can_data_event(FAR struct net_driver_s *dev, FAR struct can_conn_s *conn,
               uint32_t flags)
{
  int buflen = dev->d_len;
  int recvlen;
  uint32_t ret;

  ret = (flags & ~CAN_NEWDATA);

  /* Save as the packet data as in the read-ahead buffer.  NOTE that
   * partial packets will not be buffered.
   */

  recvlen = can_datahandler(dev, conn);
  if (recvlen < buflen)
    {
      /* There is no handler to receive new data and there are no free
       * read-ahead buffers to retain the data -- drop the packet.
       */

      ninfo("Dropped %d bytes\n", dev->d_len);

#ifdef CONFIG_NET_STATISTICS
      /* No support CAN net statistics yet */

      /* g_netstats.tcp.drop++; */

#endif
    }

  /* In any event, the new data has now been handled */

  dev->d_len = 0;
  return ret;
}

/****************************************************************************
 * Name: can_input_conn
 *
 * Description:
 *   Handle incoming packet input
 *
 * Input Parameters:
 *   dev  - The device driver structure containing the received packet
 *   conn - A pointer to the CAN connection structure
 *
 * Returned Value:
 *   OK     The packet has been processed  and can be deleted
 *  -EAGAIN There is a matching socket, but could not dispatch the packet
 *          yet.  Useful when a packet arrives before a recv call is in
 *          place.
 *
 * Assumptions:
 *   This function can be called from an interrupt.
 *
 ****************************************************************************/

static int can_input_conn(FAR struct net_driver_s *dev,
                          FAR struct can_conn_s *conn)
{
  uint32_t flags;
  uint16_t buflen = dev->d_len;
  int ret = OK;

  /* Setup for the application callback */

  dev->d_appdata = dev->d_buf;
  dev->d_sndlen  = 0;
  dev->d_len     = buflen;

  if (conn->input != NULL)
    {
      flags = conn->input(conn, dev);
    }
  else
    {
      flags = can_callback(dev, conn, CAN_NEWDATA);
    }

  /* If the operation was successful, the CAN_NEWDATA flag is removed
   * and thus the packet can be deleted (OK will be returned).
   */

  if ((flags & CAN_NEWDATA) != 0)
    {
      /* No.. the packet was not processed now.  Return -EAGAIN so
       * that the driver may retry again later.  We still need to
       * set d_len to zero so that the driver is aware that there
       * is nothing to be sent.
       */

      nwarn("WARNING: Packet not processed\n");
      ret = -EAGAIN;
    }

  return ret;
}

/****************************************************************************
 * Name: can_conn_active()
 *
 * Description:
 *   Traverse the list of CAN connections that match dev.
 *
 * Input Parameters:
 *   dev  - The device to search for.
 *   conn - The current socket; may be NULL to start the search at the
 *          beginning.
 *
 * Assumptions:
 *   This function is called from CAN device logic.
 *
 ****************************************************************************/

static FAR struct can_conn_s *
can_conn_active(FAR struct net_driver_s *dev,
                FAR struct can_conn_s *conn)
{
  can_conn_list_lock();
  while ((conn = can_conn_next(conn)) != NULL)
    {
      if ((conn->dev == NULL && _SS_ISBOUND(conn->sconn.s_flags)) ||
          conn->dev == dev)
        {
          if (dev->d_iob->io_conn == &conn->sconn &&
              !_SO_GETOPT(conn->sconn.s_options, CAN_RAW_RECV_OWN_MSGS))
            {
              continue; /* Skip own messages */
            }

          if (conn->match != NULL && !conn->match(conn, dev))
            {
              continue;
            }

          break;
        }
    }

  can_conn_list_unlock();
  return conn;
}

/****************************************************************************
 * Name: can_in
 *
 * Description:
 *   Handle incoming packet input
 *
 * Input Parameters:
 *   dev - The device driver structure containing the received packet
 *
 * Returned Value:
 *   OK     The packet has been processed  and can be deleted
 *  -EAGAIN There is a matching socket, but could not dispatch the packet
 *          yet.  Useful when a packet arrives before a recv call is in
 *          place.
 *
 * Assumptions:
 *   This function can be called from an interrupt.
 *
 ****************************************************************************/

static int can_in(FAR struct net_driver_s *dev)
{
  FAR struct can_conn_s *conn;
  FAR struct can_conn_s *nextconn;
  int ret;

#ifdef CONFIG_NET_CANFORWARD
  /* Forward the packet if needed.  can_forward() also returns whether the
   * frame should still be delivered to local listeners.  Route-only frames
   * are not received here.  Unmatched frames are delivered locally (native
   * CAN RX legacy behavior); ACF-injected frames are pre-filtered before
   * reaching can_input(), so unmatched ACF frames never get here.
   */

  if (!can_forward(dev))
    {
      return OK;
    }
#endif

  /* Do we have upper listener on the dev. */

  conn = can_conn_active(dev, NULL);

  if (conn == NULL)
    {
      /* There is no listener on the dev.  Just drop the packet. */

      dev->d_len = 0;
      netdev_iob_release(dev);
      return OK;
    }

  /* Storing reception timestamp provided by realtime
   * if timestamp no provided by hardware.
   */

#ifdef CONFIG_NET_TIMESTAMP
  if ((dev->d_features & NETDEV_RX_STAMP) == 0)
    {
      clock_gettime(CLOCK_REALTIME, &dev->d_iob->io_time);
    }
#endif /* CONFIG_NET_TIMESTAMP */

  can_conn_list_lock();

  /* Do we have second socket that can hold this packet? */

  while ((nextconn = can_conn_active(dev, conn)) != NULL)
    {
      /* Yes... There are multiple listeners on the same dev.
       * We need to clone the packet and deliver it to each listener.
       */

      FAR struct iob_s *iob = netdev_iob_clone(dev, false);

      if (iob == NULL)
        {
          nerr("ERROR: IOB clone failed.\n");
          break; /* We can still process one time without clone. */
        }

      can_input_conn(dev, conn);

      netdev_iob_replace(dev, iob);
      conn = nextconn;
    }

  /* We can deliver the packet directly to the last listener. */

  ret = can_input_conn(dev, conn);

  can_conn_list_unlock();

  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

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
 *   This function can be called from an interrupt.
 *
 ****************************************************************************/

uint32_t can_callback(FAR struct net_driver_s *dev,
                      FAR struct can_conn_s *conn, uint32_t flags)
{
  /* Some sanity checking */

  if (conn)
    {
      conn_lock(&conn->sconn);
      flags = devif_conn_event(dev, flags, conn->sconn.s_list);

      /* Either we did not get the lock or there is no application listening
       * If we did not get a lock we store the frame in the read-ahead buffer
       */

      if ((flags & CAN_NEWDATA) != 0)
        {
          /* Data was not handled.. dispose of it appropriately */

          flags = can_data_event(dev, conn, flags);
        }

      conn_unlock(&conn->sconn);
    }

  return flags;
}

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
 *   The network is locked.
 *   dev is not NULL.
 *   conn is not NULL.
 *   The socket state (conn) is bound to the polling device (dev).
 *
 ****************************************************************************/

void can_poll(FAR struct net_driver_s *dev, FAR struct can_conn_s *conn)
{
  DEBUGASSERT(dev != NULL && conn != NULL && dev == conn->dev);

  /* Setup for the application callback */

  dev->d_len    = 0;
  dev->d_sndlen = 0;

  /* Perform the application callback */

  can_callback(dev, conn, CAN_POLL);

  /* Check if the application has data to send */

  if (dev->d_sndlen > 0)
    {
      return;
    }

  /* Make sure that d_len is zero meaning that there is nothing to be sent */

  dev->d_len = 0;
}

/****************************************************************************
 * Name: can_input
 *
 * Description:
 *   Handle incoming packet input
 *
 * Input Parameters:
 *   dev - The device driver structure containing the received packet
 *
 * Returned Value:
 *   OK     The packet has been processed  and can be deleted
 *  -EAGAIN There is a matching socket, but could not dispatch the packet
 *          yet.  Useful when a packet arrives before a recv call is in
 *          place.
 *
 * Assumptions:
 *   This function can be called from an interrupt.
 *
 ****************************************************************************/

int can_input(FAR struct net_driver_s *dev)
{
  FAR uint8_t *buf;
  int ret;

  netdev_lock(dev);

  if (dev->d_iob != NULL)
    {
      buf = dev->d_buf;

      /* Set the device buffer to the start of CAN frame data.
       * Use IOB_DATA instead of NETLLBUF because CAN frames have no L2
       * header — the frame starts at io_offset,
       * not io_offset - d_llhdrlen.
       * This also allows cross-link-layer forwarding (e.g. ETH to CAN)
       * to work without modifying d_llhdrlen on the source device.
       */

      dev->d_buf = (FAR uint8_t *)IOB_DATA(dev->d_iob);
      ret = can_in(dev);

      dev->d_buf = buf;

      netdev_unlock(dev);
      return ret;
    }

  ret = netdev_input(dev, can_in, false);
  netdev_unlock(dev);
  return ret;
}

/****************************************************************************
 * Name: can_conn_init()
 *
 * Description:
 *   Initialize an already allocated CAN connection structure.
 *
 ****************************************************************************/

void can_conn_init(FAR struct can_conn_s *conn)
{
  can_conn_list_lock();

  memset(conn, 0, sizeof(*conn));

  conn_init(&conn->sconn);
#if CONFIG_NET_CAN_SEND_BUFSIZE > 0
  conn->sconn.s_sndbufs = CONFIG_NET_CAN_SEND_BUFSIZE;
#endif

#if CONFIG_NET_CAN_RECV_BUFSIZE > 0
  conn->sconn.s_rcvbufs = CONFIG_NET_CAN_RECV_BUFSIZE;
#endif

  /* Enqueue the socket into the active list */

  dq_addlast(&conn->sconn.s_node, &g_active_can_conns);

  can_conn_list_unlock();
}

/****************************************************************************
 * Name: can_conn_uninit()
 *
 * Description:
 *   Uninitialize an already allocated CAN connection structure.
 *
 ****************************************************************************/

void can_conn_uninit(FAR struct can_conn_s *conn)
{
  can_conn_list_lock();

#ifdef CONFIG_NET_CAN_WRITE_BUFFERS
  if (conn->sndcb != NULL)
    {
      can_callback_free(conn->dev, conn, conn->sndcb);
      conn->sndcb = NULL;
    }
#endif

  /* Remove the socket from the active list */

  dq_rem(&conn->sconn.s_node, &g_active_can_conns);

#ifdef CONFIG_NET_CAN_WRITE_BUFFERS
  /* Free the write queue */

  iob_free_queue(&conn->write_q);

#  if CONFIG_NET_SEND_BUFSIZE > 0
  /* Notify the send buffer available */

  can_sendbuffer_notify(conn);
#  endif /* CONFIG_NET_SEND_BUFSIZE */

#endif

  /* Use conn_uninit to release all socket resources */

  conn_uninit(&conn->sconn);

  /* Free the readahead queue */

  iob_free_queue(&conn->readahead);

  can_conn_list_unlock();
}

/****************************************************************************
 * Name: can_conn_next()
 *
 * Description:
 *   Traverse the list of allocated CAN connections.
 *
 * Assumptions:
 *   This function is called from CAN device logic.
 *
 ****************************************************************************/

FAR struct can_conn_s *
can_conn_next(FAR struct can_conn_s *conn)
{
  if (conn == NULL)
    {
      return (FAR struct can_conn_s *)g_active_can_conns.head;
    }
  else
    {
      return (FAR struct can_conn_s *)conn->sconn.s_node.flink;
    }
}

/****************************************************************************
 * Name: can_get_ifindex
 ****************************************************************************/

int can_get_ifindex(FAR struct can_conn_s *conn)
{
#ifndef CONFIG_NETDEV_IFINDEX
  FAR const char *name;
  size_t i;
#endif

  if (conn->dev == NULL)
    {
      return 0;
    }

#ifdef CONFIG_NETDEV_IFINDEX
  return conn->dev->d_ifindex;
#else
  /* No ifindex table available, so derive the index from the trailing
   * numeric suffix of the interface name (e.g. "can0" -> 0, "vcan1" -> 1).
   */

  name = conn->dev->d_ifname;
  i    = strlen(name);

  while (i > 0 && isdigit(name[i - 1]))
    {
      i--;
    }

  return atoi(&name[i]);
#endif
}

/****************************************************************************
 * Name: can_conn_bind
 *
 * Description:
 *   can_bind() gives the socket 'conn' the local address 'addr'. 'addr'
 *   is 'addrlen' bytes long. Traditionally, this is called "assigning a name
 *   to a socket." When a socket is created with socket, it exists in a name
 *   space (address family) but has no name assigned.
 *
 * Input Parameters:
 *   conn     CAN connection structure
 *   addr     Socket local address
 *   addrlen  Length of 'addr'
 *
 * Returned Value:
 *   0 on success; -1 on error with errno set appropriately
 *
 ****************************************************************************/

int can_conn_bind(FAR struct can_conn_s *conn,
                  FAR const struct sockaddr *addr,
                  socklen_t addrlen)
{
  FAR struct sockaddr_can *canaddr;
#ifndef CONFIG_NETDEV_IFINDEX
  char netdev_name[IFNAMSIZ];
#endif

  if (conn == NULL)
    {
      return -EBADF;
    }

  DEBUGASSERT(addr != NULL &&
              addrlen >= sizeof(struct sockaddr_can));

  /* Save the address information in the socket state */

  canaddr = (FAR struct sockaddr_can *)addr;

  /* Bind CAN device to socket */

#ifdef CONFIG_NETDEV_IFINDEX
  conn->dev = netdev_findbyindex(canaddr->can_ifindex);
#else
  snprintf(netdev_name, sizeof(netdev_name), "can%d",
           canaddr->can_ifindex);
  conn->dev = netdev_findbyname((FAR const char *)&netdev_name);
#endif

  return conn->dev == NULL && canaddr->can_ifindex != 0 ? -ENODEV : OK;
}

/****************************************************************************
 * Name: can_recviob
 ****************************************************************************/

int can_recviob(FAR struct can_conn_s *conn, FAR struct iob_s **iob,
                int flags, unsigned int timeout)
{
  FAR struct net_driver_s *dev = NULL;
  FAR struct can_recvfrom_s *state;
  int ret;

  if (conn == NULL || iob == NULL)
    {
      return -EBADF;
    }

  *iob = NULL;

  conn_lock(&conn->sconn);
  ret = can_readahead_iob(conn, iob);
  if (ret == OK)
    {
      conn_unlock(&conn->sconn);
      return OK;
    }

  if (_SS_ISNONBLOCK(conn->sconn.s_flags) || (flags & MSG_DONTWAIT) != 0 ||
      timeout == 0)
    {
      conn_unlock(&conn->sconn);
      return -EAGAIN;
    }

  dev = conn->dev;
  if (dev == NULL && _SS_ISBOUND(conn->sconn.s_flags))
    {
      dev = netdev_default();
    }

  conn_unlock(&conn->sconn);

  if (dev == NULL)
    {
      return -ENODEV;
    }

  state = mempool_allocate(&g_can_state_infos, timeout);
  if (state == NULL)
    {
      return -ENOMEM;
    }

  can_recvfrom_initialize(conn, state);

  conn_dev_lock(&conn->sconn, dev);

  ret = can_readahead_iob(conn, iob);
  if (ret == OK)
    {
      conn_dev_unlock(&conn->sconn, dev);
      nxsem_destroy(&state->rcv_sem);
      mempool_release(&g_can_state_infos, state);
      return OK;
    }

  state->rcv_cb = can_callback_alloc(dev, conn);
  if (state->rcv_cb)
    {
      state->rcv_cb->flags = (CAN_NEWDATA | CAN_POLL);
      state->rcv_cb->priv  = state;
      state->rcv_cb->event = can_recvfrom_eventhandler;

      ret = conn_dev_sem_timedwait(&state->rcv_sem, true, timeout,
                                   &conn->sconn, dev);

      can_callback_free(dev, conn, state->rcv_cb);
      ret = can_recvfrom_result(ret, state);
      if (ret == OK)
        {
          *iob = state->rcv_iob;
        }
    }
  else
    {
      ret = -EBUSY;
    }

  conn_dev_unlock(&conn->sconn, dev);
  nxsem_destroy(&state->rcv_sem);
  mempool_release(&g_can_state_infos, state);
  return ret;
}

#ifdef CONFIG_NET_CAN_WRITE_BUFFERS
/****************************************************************************
 * Name: can_sendiob
 ****************************************************************************/

int can_sendiob(FAR struct can_conn_s *conn, FAR struct iob_s *iob,
                int flags)
{
  FAR struct net_driver_s *dev;
  bool empty;
  bool nonblock;
  int ret = OK;

  if (conn == NULL)
    {
      return -EBADF;
    }

  dev = conn->dev;
  if (dev == NULL)
    {
      return -ENODEV;
    }

  if (iob == NULL || iob->io_pktlen < CAN_MTU)
    {
      return -EINVAL;
    }

  nonblock =
      _SS_ISNONBLOCK(conn->sconn.s_flags) || (flags & MSG_DONTWAIT) != 0;

  conn_dev_lock(&conn->sconn, dev);

#if CONFIG_NET_SEND_BUFSIZE > 0
  if ((iob_get_queue_size(&conn->write_q) + iob->io_pktlen) >
      conn->sconn.s_sndbufs)
    {
      if (nonblock)
        {
          ret = -EAGAIN;
          goto errout_with_lock;
        }

      ret = conn_dev_sem_timedwait(&conn->sconn.s_sndsem, false,
                                   _SO_TIMEOUT(conn->sconn.s_sndtimeo),
                                   &conn->sconn, dev);
      if (ret < 0)
        {
          goto errout_with_lock;
        }
    }
#endif

  empty = (iob_peek_queue(&conn->write_q) == NULL);
  if (empty)
    {
      if (conn->sndcb == NULL)
        {
          conn->sndcb = can_callback_alloc(dev, conn);
        }

      if (conn->sndcb == NULL)
        {
          ret = -ENOMEM;
          goto errout_with_lock;
        }
    }

  iob->io_conn = &conn->sconn;
  IOB_SET_OWNER(iob, "can writequeue");
  iob_add_queue(iob, &conn->write_q);

  if (empty)
    {
      conn->sndcb->flags = CAN_POLL;
      conn->sndcb->priv  = conn;
      conn->sndcb->event = can_send_eventhandler;

      netdev_txnotify(dev, CAN_POLL);
    }

errout_with_lock:
  conn_dev_unlock(&conn->sconn, dev);
  return ret;
}

/****************************************************************************
 * Name: can_cansend
 ****************************************************************************/

int can_cansend(FAR struct can_conn_s *conn)
{
  if (conn == NULL)
    {
      return -EBADF;
    }

  /* In order to setup the send, we need to have at least
   * one free IOB to initialize the write buffer head.
   */

  if (iob_navail_with_size(NET_CAN_BUFSIZE, false) <= 0
#if CONFIG_NET_SEND_BUFSIZE > 0
      || iob_get_queue_size(&conn->write_q) >= conn->sconn.s_sndbufs
#endif
     )
    {
      return -EWOULDBLOCK;
    }

  return OK;
}

#else
/****************************************************************************
 * Name: can_sendiob
 ****************************************************************************/

int can_sendiob(FAR struct can_conn_s *conn, FAR struct iob_s *iob,
                int flags)
{
  FAR struct net_driver_s *dev;
  struct can_send_s state;
  int ret = OK;

  if (conn == NULL)
    {
      return -EBADF;
    }

  dev = conn->dev;
  if (dev == NULL)
    {
      return -ENODEV;
    }

  if (iob == NULL || iob->io_pktlen < CAN_MTU)
    {
      return -EINVAL;
    }

  conn_dev_lock(&conn->sconn, dev);
  memset(&state, 0, sizeof(struct can_send_s));
  nxsem_init(&state.snd_sem, 0, 0);

  state.snd_iob = iob;
  iob->io_conn  = &conn->sconn;

  state.snd_cb = can_callback_alloc(dev, conn);
  if (state.snd_cb == NULL)
    {
      ret = -EBUSY;
      goto errout;
    }

  state.snd_cb->flags = CAN_POLL;
  state.snd_cb->priv  = &state;
  state.snd_cb->event = can_send_eventhandler;

  netdev_txnotify(dev, CAN_POLL);

  if (_SS_ISNONBLOCK(conn->sconn.s_flags) || (flags & MSG_DONTWAIT) != 0)
    {
      ret = conn_dev_sem_timedwait(&state.snd_sem, true, 0,
                                   &conn->sconn, dev);
    }
  else
    {
      ret = conn_dev_sem_timedwait(&state.snd_sem, true, UINT_MAX,
                                   &conn->sconn, dev);
    }

  can_callback_free(dev, conn, state.snd_cb);

  if (state.snd_sent < 0)
    {
      ret = state.snd_sent;
    }

errout:
  nxsem_destroy(&state.snd_sem);
  conn_dev_unlock(&conn->sconn, dev);

  return ret;
}

/****************************************************************************
 * Name: can_cansend
 ****************************************************************************/

int can_cansend(FAR struct can_conn_s *conn)
{
  if (conn == NULL)
    {
      return -EBADF;
    }

  /* TODO Query CAN driver mailboxes to see if there's mailbox available */

  return OK;
}
#endif

