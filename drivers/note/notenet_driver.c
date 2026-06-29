/****************************************************************************
 * drivers/note/notenet_driver.c
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

#include <sys/ioctl.h>

#include <nuttx/atomic.h>
#include <nuttx/mm/iob.h>
#include <nuttx/percpu.h>
#include <nuttx/net/netpoll.h>
#include <nuttx/note/note_driver.h>
#include <nuttx/spinlock.h>

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct notenet_driver_s
{
  struct note_driver_s  driver;  /* Note driver part, must be first element */
  spinlock_t            lock;    /* Lock */
  netpkt_queue_t        pktq;    /* Allocated net packet queue */
  struct netpoll_s      np;      /* Netpoll */

  /* Send netpoll packet */

  CODE ssize_t (*send)(FAR struct netpoll_s *np, FAR netpkt_queue_t *pktq);
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static ssize_t note_netdev_add(FAR struct note_driver_s *driver,
                               FAR const void *note, size_t notelen,
                               bool noswitches);
static void note_netdev_flush(FAR struct note_driver_s *driver);
static int  note_netdev_ioctl(FAR struct file *filep, int cmd,
                              unsigned long arg);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct note_driver_ops_s g_notenetdev_ops =
{
  .add = note_netdev_add,
  .flush = note_netdev_flush,
  .ioctl = note_netdev_ioctl,
};

/****************************************************************************
 * Public Data
 ****************************************************************************/

DEFINE_PER_CPU_BMP(struct notenet_driver_s, g_notenet) =
{
  {
#ifdef CONFIG_SCHED_INSTRUMENTATION_FILTER
    "net",
    {
      {
        CONFIG_DRIVERS_NOTENET_FILTER_DEFAULT_MODE,
#  ifndef CONFIG_UP
        CONFIG_DRIVERS_NOTENET_CPUSET
#  endif
      },

#  ifdef CONFIG_SCHED_INSTRUMENTATION_DUMP
      {
        CONFIG_DRIVERS_NOTE_DEFAULT_LEVEL
      },
#  endif
    },
#endif
    &g_notenetdev_ops
  },
  .lock = SP_UNLOCKED
};
#define g_notenet this_cpu_var_bmp(g_notenet)

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: note_netdev_add
 *
 * Description:
 *   Add the variable length note to the transport layer
 *
 * Input Parameters:
 *   note    - The note buffer
 *   notelen - The buffer length
 *   noswitches - True: Can't do context switches now.
 *
 * Returned Value:
 *   None
 *
 * Assumptions:
 *   We are within a critical section.
 *
 ****************************************************************************/

static ssize_t note_netdev_add(FAR struct note_driver_s *driver,
                               FAR const void *note, size_t notelen,
                               bool noswitches)
{
  FAR struct notenet_driver_s *drv =
    (FAR struct notenet_driver_s *)driver;
  size_t       maxlen = netpoll_get_maxlen(&drv->np);
  size_t       dropped;
  ssize_t      sent;
  irqstate_t   flags;
  int          ret;

  flags = spin_lock_irqsave_notrace(&drv->lock);

  /* Ignore data if device is not ready */

  if (drv->np.lower == NULL ||
      IFF_IS_RUNNING(drv->np.lower->netdev.d_flags) == 0)
    {
      spin_unlock_irqrestore_notrace(&drv->lock, flags);
      return -ENODEV;
    }

  /* Copy the whole note; netpoll_queue_copyin grows the queue as needed,
   * splitting at maxlen boundaries (one queue entry per wire frame/packet).
   */

  ret = netpoll_queue_copyin(&drv->np, &drv->pktq, note, notelen);
  if (ret < 0)
    {
      /* On copy failure (e.g. IOB exhaustion), netpoll_queue_copyin()
       * has already stamped the partial tail and left the queue in a
       * walkable state.  Keep previously-queued bytes for the next
       * flush; the current note's bytes are accounted for by the
       * dispatcher via the negative return code.
       */

      spin_unlock_irqrestore_notrace(&drv->lock, flags);
      return ret;
    }

  dropped = netpoll_queue_get_payload_size(&drv->np, &drv->pktq);
  if (dropped <= 3 * maxlen / 4)
    {
      spin_unlock_irqrestore_notrace(&drv->lock, flags);
      return notelen;
    }

  /* Drain pktq.  Bytes that older (already-accounted) notes left behind
   * but that fail to reach the wire are charged to drop_bytes here so
   * they don't pollute this call's ssize-style return.
   */

  sent = drv->send(&drv->np, &drv->pktq);
  if (sent < 0)
    {
      sent = 0;
    }

  dropped -= sent;
  if (dropped > notelen)
    {
#ifdef CONFIG_DRIVERS_NOTE_STATS
      atomic_add_relaxed(&drv->driver.stats.drop_bytes, dropped - notelen);
#endif
      dropped = notelen;
    }

  spin_unlock_irqrestore_notrace(&drv->lock, flags);

  return notelen - dropped;
}

static void note_netdev_flush(FAR struct note_driver_s *driver)
{
  FAR struct notenet_driver_s *drv =
    (FAR struct notenet_driver_s *)driver;
  size_t       qsize;
  ssize_t      sent;
  irqstate_t   flags;

  flags = spin_lock_irqsave_notrace(&drv->lock);

  qsize = netpoll_queue_get_payload_size(&drv->np, &drv->pktq);
  if (qsize > 0)
    {
      sent = drv->send(&drv->np, &drv->pktq);
      if (sent < 0)
        {
          sent = 0;
        }

#ifdef CONFIG_DRIVERS_NOTE_STATS
      atomic_add_relaxed(&drv->driver.stats.drop_bytes, qsize - sent);
#else
      UNUSED(sent);
#endif
    }

  spin_unlock_irqrestore_notrace(&drv->lock, flags);
}

/****************************************************************************
 * Name: note_netdev_ioctl
 *
 * Description:
 *   Backend-side ioctl handler.  Today the only command we honour is
 *   NOTE_CLEAR, which is semantically opposite to NOTE_FLUSH:
 *
 *     NOTE_FLUSH -> push the queued batch out via drv->send().
 *     NOTE_CLEAR -> drop the queued batch without transmitting it.
 *
 *   Treating CLEAR like FLUSH (the previous dispatcher fallback) would
 *   transmit exactly the data the caller asked to discard, so notenet
 *   has to implement CLEAR locally.  Anything else is reported back as
 *   -ENOTTY so the common dispatcher in note_driver.c can handle it.
 *
 ****************************************************************************/

static int note_netdev_ioctl(FAR struct file *filep, int cmd,
                             unsigned long arg)
{
  FAR struct notenet_driver_s *drv = filep->f_inode->i_private;
  irqstate_t flags;

  UNUSED(arg);

  switch (cmd)
    {
      case NOTE_CLEAR:
        flags = spin_lock_irqsave_notrace(&drv->lock);
#ifdef CONFIG_DRIVERS_NOTE_STATS
        atomic_add_relaxed(&drv->driver.stats.drop_bytes,
                           netpoll_queue_get_payload_size(&drv->np,
                                                          &drv->pktq));
#endif
        iob_free_queue(&drv->pktq);
        spin_unlock_irqrestore_notrace(&drv->lock, flags);
        return OK;

      default:
        return -ENOTTY;
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: notenet_init
 *
 * Description:
 *   Register a netdev channel to note.
 *
 * Input Parameters:
 *   dev - The lower half netdev driver structure to be binded.
 *
 * Returned Value:
 *   Zero on success. A negated errno value is returned on a failure.
 *
 ****************************************************************************/

int notenet_init(FAR struct netdev_lowerhalf_s *dev)
{
  FAR struct notenet_driver_s *note = &g_notenet;
  int ret = -EINVAL;

  /* Initialize netpoll */

#ifdef CONFIG_NET_CAN
  if (dev->netdev.d_lltype == NET_LL_CAN)
    {
      note->send = netpoll_can_queue_send;

      ret = netpoll_can_init(&note->np, dev,
                             CONFIG_DRIVERS_NOTENET_PORT);
    }
  else
#endif
    {
#ifdef CONFIG_NET_UDP
      note->send = netpoll_udp_queue_send;

      ret = netpoll_udp_init(&note->np, dev,
                             CONFIG_DRIVERS_NOTENET_PORT,
                             CONFIG_DRIVERS_NOTENET_IPADDR,
                             CONFIG_DRIVERS_NOTENET_PORT,
                             CONFIG_DRIVERS_NOTENET_MAC);
#endif
    }

  return ret;
}
