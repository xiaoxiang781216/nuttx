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

#include <nuttx/net/netpoll.h>
#include <nuttx/note/note_driver.h>
#include <nuttx/panic_notifier.h>
#include <nuttx/spinlock.h>
#include <nuttx/wqueue.h>

/****************************************************************************
 * Pre-processor definitions
 ****************************************************************************/

#define NOTENET_WORK_DELAY MSEC2TICK(CONFIG_DRIVERS_NOTENET_WORK_DELAY)

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct notenet_driver_s
{
  struct note_driver_s  driver;  /* Note driver part, must be first element */
  spinlock_t            lock;    /* Lock */
  FAR netpkt_t         *pkt;     /* Allocated net packet */
  size_t                next;    /* The next index (where data is added) */
  struct work_s         work;    /* Used for deferred callback work */
  struct netpoll_s      np;      /* Netpoll */
  struct notifier_block nb;      /* Panic notifer */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static bool notenetdev_transfer(FAR struct notenet_driver_s *drv);
static void notenetdev_work(FAR void *priv);
static void note_netdev_add(FAR struct note_driver_s *driver,
                            FAR const void *note, size_t notelen,
                            bool noswitches);
static int netdev_note_panic_callback(FAR struct notifier_block *nb,
                                      unsigned long action,
                                      FAR void *data);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct note_driver_ops_s g_notenetdev_ops =
{
  note_netdev_add
};

/****************************************************************************
 * Public Data
 ****************************************************************************/

struct notenet_driver_s g_notenet =
{
  {
#ifdef CONFIG_SCHED_INSTRUMENTATION_FILTER
    "net",
    {
      {
        CONFIG_DRIVERS_NOTENET_FILTER_DEFAULT_MODE,
#ifdef CONFIG_SMP
        DRIVERS_NOTENET_CPUSET
#endif
      },
    },
#endif
    &g_notenetdev_ops
  },
  .lock = SP_UNLOCKED
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: notenetdev_transfer
 ****************************************************************************/

static bool notenetdev_transfer(FAR struct notenet_driver_s *drv)
{
  irqstate_t flags;
  int ret;

  flags = spin_lock_irqsave_notrace(&drv->lock);

  if (drv->next == 0)
    {
      spin_unlock_irqrestore_notrace(&drv->lock, flags);
      return true;
    }

  /* Send IOB */

  ret = netpoll_udp_pkt_send(&drv->np, drv->pkt);
  if (ret < 0)
    {
      spin_unlock_irqrestore_notrace(&drv->lock, flags);
      return false;
    }

  /* Get IOB - blocking */

  drv->pkt = netpoll_udp_pkt_alloc(&drv->np);
  drv->next = 0;

  spin_unlock_irqrestore_notrace(&drv->lock, flags);

  return true;
}

/****************************************************************************
 * Name: notenetdev_work
 ****************************************************************************/

static void notenetdev_work(FAR void *arg)
{
  FAR struct notenet_driver_s *drv = arg;

  if (!notenetdev_transfer(drv))
    {
      work_queue(HPWORK, &drv->work, notenetdev_work, arg,
                 NOTENET_WORK_DELAY);
    }
}

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

static void note_netdev_add(FAR struct note_driver_s *driver,
                            FAR const void *note, size_t notelen,
                            bool noswitches)
{
  FAR struct notenet_driver_s *drv =
    (FAR struct notenet_driver_s *)driver;
  clock_t      delay  = NOTENET_WORK_DELAY;
  size_t       maxlen = netpoll_get_maxlen(&drv->np);
  size_t       space;
  irqstate_t   flags;

  flags = spin_lock_irqsave_notrace(&drv->lock);

  /* Ignore data if device is not ready */

  if (!drv->pkt)
    {
      spin_unlock_irqrestore(&drv->lock, flags);
      return;
    }

try:
  if (drv->next + notelen > maxlen)
    {
      /* Unlock before entering syslog_netdev_transfer,
       * it is already locked internally
       */

      spin_unlock_irqrestore_notrace(&drv->lock, flags);
      notenetdev_transfer(drv);
      flags = spin_lock_irqsave_notrace(&drv->lock);
      goto try;
    }

  /* Copy data to packet */

  netpoll_pkt_copy(&drv->np, drv->pkt, note, notelen, drv->next);
  drv->next += notelen;

  /* Start work immediately when data more then 75% */

  space = maxlen - drv->next;
  if (space < maxlen / 4)
    {
      delay = 0;
    }

  spin_unlock_irqrestore_notrace(&drv->lock, flags);

  if (!noswitches && work_available(&drv->work))
    {
      work_queue(HPWORK, &drv->work, notenetdev_work, drv, delay);
    }
}

/****************************************************************************
 * Name: netdev_note_panic_callback
 *
 * Description:
 *   This is panic callback for note driver.
 *
 ****************************************************************************/

static int netdev_note_panic_callback(FAR struct notifier_block *nb,
                                      unsigned long action,
                                      FAR void *data)
{
  FAR struct notenet_driver_s *note =
    container_of(nb, struct notenet_driver_s, nb);

  if (action == PANIC_KERNEL_FINAL)
    {
      notenetdev_transfer(note);
    }

  return 0;
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
  FAR netpkt_t                *pkt;
  irqstate_t                   flags;
  int                          ret;

  /* Initialize netpoll */

  ret = netpoll_udp_init(&note->np, dev,
                         CONFIG_DRIVERS_NOTENET_PORT,
                         CONFIG_DRIVERS_NOTENET_IPADDR,
                         CONFIG_DRIVERS_NOTENET_PORT,
                         CONFIG_DRIVERS_NOTENET_MAC);
  if (ret < 0)
    {
      return ret;
    }

  /* Allocate iob */

  pkt = netpoll_udp_pkt_alloc(&note->np);

  /* Register panic notifier */

  note->nb.notifier_call = netdev_note_panic_callback;
  panic_notifier_chain_register(&note->nb);

  flags = spin_lock_irqsave_notrace(&note->lock);
  note->pkt = pkt;
  spin_unlock_irqrestore_notrace(&note->lock, flags);

  return 0;
}
