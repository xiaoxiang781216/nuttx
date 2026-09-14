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
#include <sys/param.h>

#include <nuttx/atomic.h>
#include <nuttx/mm/iob.h>
#include <nuttx/percpu.h>
#include <nuttx/net/netpoll.h>
#include <nuttx/note/note_driver.h>
#include <nuttx/ring.h>
#include <nuttx/spinlock.h>
#include <nuttx/wdog.h>

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct notenet_driver_s
{
  struct note_driver_s  driver;  /* Note driver part, must be first element */
  spinlock_t            lock;    /* Lock */
  netpkt_queue_t        pktq;    /* Allocated net packet queue */
  struct netpoll_s      np;      /* Netpoll */
#if CONFIG_DRIVERS_NOTENET_FLUSH_TIMEOUT > 0
  struct ring_s         ring;    /* Note ring staging notes for this CPU */
  struct wdog_s         wdog;    /* Periodic drain of the note ring */
#endif

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

#if CONFIG_DRIVERS_NOTENET_FLUSH_TIMEOUT > 0

/* Blob backing the per-CPU note ring: every core stages notes into its own
 * ring, but only the instance bound to the netdev may touch the transport.
 * BMP cannot arm a work item on a remote core, so the owner polls instead
 * of being kicked.
 */

static char g_notenet_blob[CONFIG_DRIVERS_NOTENET_BUFSIZE]
            aligned_data(RING_BLOB_ALIGN);
#endif

/****************************************************************************
 * Public Data
 ****************************************************************************/

#undef g_notenet
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
};
#define g_notenet this_cpu_var_bmp(g_notenet)

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: note_netdev_ready
 *
 * Description:
 *   Check whether this instance owns a usable transport.  Only the instance
 *   bound by notenet_init() ever has a lower half, so this also identifies
 *   the single CPU allowed to drain the ring.
 *
 ****************************************************************************/

static inline bool note_netdev_ready(FAR struct notenet_driver_s *drv)
{
  return drv->np.lower != NULL &&
         IFF_IS_RUNNING(drv->np.lower->netdev.d_flags) != 0;
}

/****************************************************************************
 * Name: note_netdev_send
 *
 * Description:
 *   Push the packet queue to the wire.  The caller must hold drv->lock.
 *
 * Input Parameters:
 *   force - True: send whatever is queued.  False: hold back until the
 *           queue is worth a nearly full frame, so a producer does not pay
 *           for a mostly empty packet.
 *
 ****************************************************************************/

static inline void note_netdev_send(FAR struct notenet_driver_s *drv,
                                    bool force)
{
  size_t  qsize;
  ssize_t sent;

  qsize = netpoll_queue_get_payload_size(&drv->np, &drv->pktq);
  if (!force && qsize <= 3 * netpoll_get_maxlen(&drv->np) / 4)
    {
      return;
    }

  sent = drv->send(&drv->np, &drv->pktq);
  if (sent < 0)
    {
      sent = 0;
    }

#ifdef CONFIG_DRIVERS_NOTE_STATS
  atomic_add_relaxed(&drv->driver.stats.drop_bytes, qsize - sent);
#endif
}

#if CONFIG_DRIVERS_NOTENET_FLUSH_TIMEOUT > 0

/****************************************************************************
 * Name: note_netdev_copyin
 *
 * Description:
 *   Append one ring segment to the packet queue.  A failed copy still
 *   consumes the ring slots, and note_netdev_send() only ever sees what
 *   reached the queue, so the loss has to be charged here.
 *
 ****************************************************************************/

static inline void note_netdev_copyin(FAR struct notenet_driver_s *drv,
                                      FAR const void *buf, uint32_t len)
{
  int ret;

  if (len == 0)
    {
      return;
    }

  ret = netpoll_queue_copyin(&drv->np, &drv->pktq, buf, len);

#ifdef CONFIG_DRIVERS_NOTE_STATS
  if (ret < 0)
    {
      atomic_add_relaxed(&drv->driver.stats.drop_bytes, len);
    }
#else
  UNUSED(ret);
#endif
}

/****************************************************************************
 * Name: note_netdev_add
 *
 * Description:
 *   Stage the variable length note in the ring shared by all CPUs.  The
 *   transport belongs to a single CPU, so a foreign producer must not touch
 *   it; staging keeps notes alive until the owner drains them.
 *
 * Input Parameters:
 *   note    - The note buffer
 *   notelen - The buffer length
 *   noswitches - True: Can't do context switches now.
 *
 * Returned Value:
 *   The number of bytes accepted into the ring.
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
#ifdef CONFIG_DRIVERS_NOTE_STATS
  uint32_t discard;
#endif

  ring_mpmc_init(&drv->ring, g_notenet_blob, sizeof(g_notenet_blob), 0);

  /* Stage the note without taking drv->lock: the ring is multi-producer
   * safe, so a foreign core no longer spins with interrupts disabled on a
   * lock held by the CPU talking to the transport.  The periodic drain is
   * what moves the bytes out.
   */

#ifdef CONFIG_DRIVERS_NOTE_STATS
  discard = ring_mpmc_overwrite(&drv->ring, note, notelen);
  if (discard != 0)
    {
      atomic_add_relaxed(&driver->stats.drop_bytes, discard);
    }
#else
  ring_mpmc_overwrite(&drv->ring, note, notelen);
#endif

  return MIN(notelen, ring_capacity(&drv->ring));
}
#else
static ssize_t note_netdev_add(FAR struct note_driver_s *driver,
                               FAR const void *note, size_t notelen,
                               bool noswitches)
{
  FAR struct notenet_driver_s *drv =
    (FAR struct notenet_driver_s *)driver;
  irqstate_t flags;
  int        ret;

  flags = spin_lock_irqsave_notrace(&drv->lock);

  /* Ignore data if device is not ready */

  if (!note_netdev_ready(drv))
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

  /* The note is now fully queued, so this call accepted all of notelen.
   * Any bytes that later miss the wire are charged to drop_bytes inside
   * note_netdev_send(); reporting them here too would double count.
   */

  note_netdev_send(drv, false);

  spin_unlock_irqrestore_notrace(&drv->lock, flags);

  return notelen;
}
#endif

/****************************************************************************
 * Name: note_netdev_flush
 *
 * Description:
 *   Drain the ring and send it out one frame at a time, taking drv->lock
 *   per frame instead of for the whole backlog.  Draining under a single
 *   lock could copy in more frames than the TX quota holds before any is
 *   sent, deadlocking netpoll_pkt_alloc(); sending after each frame caps
 *   outstanding quota at one slot and keeps the IRQ-off window short.
 *
 ****************************************************************************/

static void note_netdev_flush(FAR struct note_driver_s *driver)
{
  FAR struct notenet_driver_s *drv =
    (FAR struct notenet_driver_s *)driver;
#if CONFIG_DRIVERS_NOTENET_FLUSH_TIMEOUT > 0
  size_t maxlen = netpoll_get_maxlen(&drv->np);
  struct ring_buf_s buf;
  uint32_t count;
#endif
  irqstate_t flags;

  do
    {
      flags = spin_lock_irqsave_notrace(&drv->lock);

      if (!note_netdev_ready(drv))
        {
          spin_unlock_irqrestore_notrace(&drv->lock, flags);
          break;
        }

#if CONFIG_DRIVERS_NOTENET_FLUSH_TIMEOUT > 0
      count = ring_count(&drv->ring);
      if (count == 0)
        {
          spin_unlock_irqrestore_notrace(&drv->lock, flags);
          break;
        }

      count = MIN(count, maxlen);

      ring_mpmc_dequeue_start(&drv->ring, count, 1, &buf);
      note_netdev_copyin(drv, buf.ptr1, buf.len1);
      note_netdev_copyin(drv, buf.ptr2, buf.len2);
      ring_mpmc_dequeue_finish(&drv->ring, &buf);
#endif

      note_netdev_send(drv, true);

      spin_unlock_irqrestore_notrace(&drv->lock, flags);
    }
  while (CONFIG_DRIVERS_NOTENET_FLUSH_TIMEOUT);
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
#if CONFIG_DRIVERS_NOTENET_FLUSH_TIMEOUT > 0

        ring_mpmc_init(&drv->ring, g_notenet_blob,
                       sizeof(g_notenet_blob), 0);

        /* Skip rather than reset: producers run without drv->lock, so
         * clearing the indices outright would race with an in-flight
         * enqueue.  Skipping is CAS-based and never passes prod_tail.
         */

#  ifdef CONFIG_DRIVERS_NOTE_STATS
        atomic_add_relaxed(&drv->driver.stats.drop_bytes,
                           ring_mpmc_skip(&drv->ring,
                                          ring_count(&drv->ring)));
#  else
        ring_mpmc_skip(&drv->ring, ring_count(&drv->ring));
#  endif
#endif
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

#if CONFIG_DRIVERS_NOTENET_FLUSH_TIMEOUT > 0

/****************************************************************************
 * Name: note_netdev_drain
 *
 * Description:
 *   Periodic drain running on the CPU bound to the netdev.  Production
 *   cannot kick this because BMP has no way to arm a work item on a remote
 *   core, so the poll rearms itself unconditionally.
 *
 ****************************************************************************/

static void note_netdev_drain(wdparm_t arg)
{
  FAR struct notenet_driver_s *drv = (FAR struct notenet_driver_s *)arg;

  note_netdev_flush(&drv->driver);

  wd_start(&drv->wdog, MSEC2TICK(CONFIG_DRIVERS_NOTENET_FLUSH_TIMEOUT),
           note_netdev_drain, (wdparm_t)drv);
}
#endif

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

#if CONFIG_DRIVERS_NOTENET_FLUSH_TIMEOUT > 0
  if (ret >= 0)
    {
      ring_mpmc_init(&note->ring, g_notenet_blob,
                     sizeof(g_notenet_blob), 0);

      /* Seed the poll here instead of note_driver_initialize(): the work
       * queues do not exist until nx_bringup(), and this is also where we
       * learn which CPU owns the transport.
       */

      wd_start(&note->wdog, MSEC2TICK(CONFIG_DRIVERS_NOTENET_FLUSH_TIMEOUT),
               note_netdev_drain, (wdparm_t)note);
    }
#endif

  return ret;
}
