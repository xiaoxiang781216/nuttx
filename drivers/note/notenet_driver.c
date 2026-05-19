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

  CODE int (*send)(FAR struct netpoll_s *np, FAR netpkt_queue_t *pktq);
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int note_netdev_add(FAR struct note_driver_s *driver,
                           FAR const void *note, size_t notelen,
                           bool noswitches);
static void note_netdev_flush(FAR struct note_driver_s *driver);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct note_driver_ops_s g_notenetdev_ops =
{
  .add = note_netdev_add,
  .flush = note_netdev_flush,
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

static int note_netdev_add(FAR struct note_driver_s *driver,
                           FAR const void *note, size_t notelen,
                           bool noswitches)
{
  FAR struct notenet_driver_s *drv =
    (FAR struct notenet_driver_s *)driver;
  size_t       maxlen = netpoll_get_maxlen(&drv->np);
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
      /* On copy failure (e.g. IOB exhaustion), discard the batch so the
       * next note starts from a clean queue state.
       */

      iob_free_queue(&drv->pktq);
      spin_unlock_irqrestore_notrace(&drv->lock, flags);
      return ret;
    }

  /* Flush on any queue overflow or >75% packet fill. netpoll is safe
   * from noswitches contexts (it bypasses the net stack), so we don't
   * need to defer when !noswitches.
   */

  UNUSED(noswitches);

  if (iob_get_queue_size(&drv->pktq) > 3 * maxlen / 4)
    {
      drv->send(&drv->np, &drv->pktq);
    }

  spin_unlock_irqrestore_notrace(&drv->lock, flags);

  return 0;
}

static void note_netdev_flush(FAR struct note_driver_s *driver)
{
  FAR struct notenet_driver_s *drv =
    (FAR struct notenet_driver_s *)driver;
  irqstate_t flags;

  flags = spin_lock_irqsave_notrace(&drv->lock);

  if (!IOB_QEMPTY(&drv->pktq))
    {
      drv->send(&drv->np, &drv->pktq);
    }

  spin_unlock_irqrestore_notrace(&drv->lock, flags);
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
