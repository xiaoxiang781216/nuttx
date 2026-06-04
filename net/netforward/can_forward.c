/****************************************************************************
 * net/netforward/can_forward.c
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

#include <assert.h>
#include <debug.h>
#include <errno.h>

#include <nuttx/can.h>
#include <nuttx/hashtable.h>
#include <nuttx/kmalloc.h>
#include <nuttx/mm/iob.h>
#include <nuttx/mutex.h>
#include <nuttx/wqueue.h>
#include <nuttx/spinlock.h>
#include <nuttx/net/can.h>
#include <nuttx/net/net.h>
#include <nuttx/net/netdev.h>
#include <nuttx/net/netstats.h>
#include <netpacket/cangw.h>

#include "devif/devif.h"
#include "netforward/ieee1722acf.h"
#include "netforward/netforward.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* CAN gateway hash table configuration.
 * Exact-match rules are hashed into 2^N buckets by CAN ID.
 * Wildcard/partial-mask/INV rules go into a dedicated wildcard bucket.
 */

#define NET_CANFORWARD_HASH_SIZE ((1 << CONFIG_NET_CANFORWARD_HASH_BITS) + 1)
#define NET_CANFORWARD_WILDCARD_BUCKET  (NET_CANFORWARD_HASH_SIZE - 1)

/****************************************************************************
 * Private Types
 ****************************************************************************/

/****************************************************************************
 * Private Data
 ****************************************************************************/

#ifdef CONFIG_NET_CANFORWARD_ASYNC
/* Global async forwarding queue for low-priority CAN gateway rules.
 * Protected by g_canfwd_lock (spinlock) since producers run in RX path
 * under netdev_lock and the consumer runs in a dedicated workqueue
 * (priority configurable via NET_CANFORWARD_WORK_PRIORITY).
 */

static DEFINE_PER_CPU_BSS_BMP(struct work_s, g_canfwd_work);
#define g_canfwd_work this_cpu_var_bmp(g_canfwd_work)

static DEFINE_PER_CPU_BSS_BMP(sq_queue_t, g_canfwd_pending);
#define g_canfwd_pending this_cpu_var_bmp(g_canfwd_pending)

static DEFINE_PER_CPU_BMP(spinlock_t, g_canfwd_lock) = SP_UNLOCKED;
#define g_canfwd_lock this_cpu_var_bmp(g_canfwd_lock)

static DEFINE_PER_CPU_BSS_BMP(FAR struct kwork_wqueue_s *, g_canfwd_wqueue);
#define g_canfwd_wqueue this_cpu_var_bmp(g_canfwd_wqueue)

static DEFINE_PER_CPU_BMP(mutex_t, g_canfwd_wqueue_lock) =
  NXMUTEX_INITIALIZER;
#define g_canfwd_wqueue_lock this_cpu_var_bmp(g_canfwd_wqueue_lock)
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#ifdef CONFIG_NET_CANFORWARD_ASYNC

/****************************************************************************
 * Name: can_async_worker
 *
 * Description:
 *   Dedicated workqueue handler: drain all pending low-priority
 *   CAN forwards.
 *   Atomically moves the entire pending list to a local queue, then
 *   processes each forward_s by calling netfwd_forward().
 *
 *   We pass fwd->f_dev (the destination device) as the first argument
 *   to netfwd_forward(), which makes dev == fwddev and hits the
 *   same-device fast path — no lock reordering needed since we are
 *   not in any device's RX context.
 *
 ****************************************************************************/

static void can_async_worker(FAR void *arg)
{
  sq_queue_t local;
  irqstate_t irqflags;

  /* Atomically grab the entire pending list */

  irqflags = spin_lock_irqsave(&g_canfwd_lock);
  sq_move(&g_canfwd_pending, &local);
  spin_unlock_irqrestore(&g_canfwd_lock, irqflags);

  /* Forward each one — only lock the target device */

  while (!sq_empty(&local))
    {
      FAR struct forward_s *fwd = (FAR struct forward_s *)
                                   sq_remfirst(&local);
      int ret;

      /* Lock the target device and forward.  Passing f_dev as the
       * source device hits the same-device path in netfwd_forward(),
       * which calls netfwd_forward_notify() directly without any
       * lock save/restore dance.
       */

      netdev_lock(fwd->f_dev);
      ret = IFF_IS_RUNNING(fwd->f_dev->d_flags)
            ? netfwd_forward(fwd->f_dev, fwd) : -EHOSTUNREACH;
      netdev_unlock(fwd->f_dev);

      if (ret < 0)
        {
          nwarn("WARNING: Async CAN forward failed: %d\n", ret);
          if (fwd->f_iob != NULL)
            {
              iob_free_chain(fwd->f_iob);
            }

          netfwd_free(fwd);
#  ifdef CONFIG_NET_STATISTICS
          g_netstats.cangw.drop++;
#  endif
        }
#  ifdef CONFIG_NET_STATISTICS
      else
        {
          g_netstats.cangw.forward++;
        }
#  endif
    }

#if CONFIG_NET_CANFORWARD_WORK_PERIOD_MS > 0
  /* Periodic mode: re-schedule self for next drain cycle */

  work_queue_next_wq(g_canfwd_wqueue, &g_canfwd_work,
                     can_async_worker, NULL,
                     MSEC2TICK(CONFIG_NET_CANFORWARD_WORK_PERIOD_MS));
#endif
}

/****************************************************************************
 * Name: canfwd_ensure_wqueue
 *
 * Description:
 *   Lazy-create the dedicated CAN gateway workqueue on first use.
 *   Returns the workqueue pointer, or NULL on failure.
 *
 ****************************************************************************/

static FAR struct kwork_wqueue_s *canfwd_ensure_wqueue(void)
{
  /* Fast path: workqueue already created, no locking needed. */

  if (g_canfwd_wqueue == NULL)
    {
      /* Slow path: use mutex for one-time creation.  A mutex is required
       * here because work_queue_create() allocates memory and creates a
       * kernel thread — operations that must not run with interrupts
       * disabled (spinlock).  Double-check after acquiring the lock to
       * guard against a higher-priority RX thread that may have preempted
       * us and already completed the creation.
       */

      nxmutex_lock(&g_canfwd_wqueue_lock);
      if (g_canfwd_wqueue == NULL)
        {
          g_canfwd_wqueue = work_queue_create("canfwd",
                                CONFIG_NET_CANFORWARD_WORK_PRIORITY,
                                NULL,
                                CONFIG_NET_CANFORWARD_WORK_STACKSIZE,
                                1);

#if CONFIG_NET_CANFORWARD_WORK_PERIOD_MS > 0
          if (g_canfwd_wqueue != NULL)
            {
              work_queue_wq(g_canfwd_wqueue, &g_canfwd_work,
                            can_async_worker, NULL,
                            MSEC2TICK(CONFIG_NET_CANFORWARD_WORK_PERIOD_MS));
            }
#endif
        }

      nxmutex_unlock(&g_canfwd_wqueue_lock);
    }

  return g_canfwd_wqueue;
}

#endif /* CONFIG_NET_CANFORWARD_ASYNC */

/****************************************************************************
 * Name: can_forward_is_exact_match
 *
 * Description:
 *   Check if a rule is an exact-match rule suitable for hash indexing.
 *   Criteria:
 *     - Standard frame: mask covers all 11 bits (0x7FF), no INV flag
 *     - Extended frame: mask covers all 29 bits (0x1FFFFFFF), no INV flag
 *
 ****************************************************************************/

static inline_function bool
can_forward_is_exact_match(FAR const struct cangw_rule_s *rule)
{
  canid_t id   = rule->gw.ccgw.filter.can_id;
  canid_t mask = rule->gw.ccgw.filter.can_mask;

  /* INV rules match all IDs except the specified one, will go into
   * the wildcard bucket and are always traversed.
   */

  if (id & CAN_INV_FILTER)
    {
      return false;
    }

  /* Extended frame: mask must cover all 29 bits */

  if (id & CAN_EFF_FLAG)
    {
      return (mask & CAN_EFF_MASK) == CAN_EFF_MASK;
    }

  /* Standard frame: mask must cover all 11 bits */

  return (mask & CAN_SFF_MASK) == CAN_SFF_MASK;
}

/****************************************************************************
 * Name: can_dev_forward
 *
 * Description:
 *   Forward a CAN frame to the destination device specified by the rule.
 *
 *   When CONFIG_NET_CANFORWARD_ASYNC is enabled:
 *     Rules without CGW_FLAGS_CAN_ASYNC: forwarded synchronously via
 *     netfwd_forward() in the RX path — lowest latency.
 *
 *     Rules with CGW_FLAGS_CAN_ASYNC: IOB is cloned and the forward_s
 *     is enqueued to a global async list, then scheduled on a dedicated
 *     workqueue whose priority is below real-time periodic tasks.
 *     This reduces the time spent in the RX path during fan-out.
 *
 *   When CONFIG_NET_CANFORWARD_ASYNC is not enabled:
 *     All rules are forwarded synchronously in the RX path.
 *
 * Input Parameters:
 *   dev      - The device on which the packet was received.
 *   fwdrule  - The forward target rule.
 *
 * Returned Value:
 *   Zero is returned if the packet was successfully forwarded or enqueued;
 *   A negated errno value is returned if the packet is not forwardable.
 *
 ****************************************************************************/

static int can_dev_forward(FAR struct net_driver_s *dev,
                           FAR struct cangw_rule_s *fwdrule)
{
  FAR struct forward_s *fwd;
  FAR struct net_driver_s *fwddev = fwdrule->dest_dev;
  int ret = -ENOMEM;

  /* Check if forwarding is enabled on the destination device. */

  if (IFF_IS_NODST_FORWARD(fwddev->d_flags))
    {
      nwarn("WARNING: CAN forwarding disabled on destination device %s\n",
            fwddev->d_ifname);
      ret = -EOPNOTSUPP;
      goto errout;
    }

  /* If the interface isn't "Running", we can't forward. */

  if (!IFF_IS_RUNNING(fwddev->d_flags))
    {
      nwarn("WARNING: device is DOWN\n");
      ret = -EHOSTUNREACH;
      goto errout;
    }

  /* Get a pre-allocated forwarding structure,  This structure will be
   * completely zeroed when we receive it.
   */

  fwd = netfwd_alloc();
  if (fwd == NULL)
    {
      nwarn("WARNING: Failed to allocate forwarding structure\n");
      goto errout;
    }

  /* Initialize the easy stuff in the forwarding structure */

  fwd->f_dev = fwddev;     /* Forwarding device */
  fwd->f_domain = PF_CAN;  /* CAN domain */

  /* Relay the device buffer */

  fwd->f_iob = netdev_iob_clone(dev, true);  /* IOB chain to be forwarded */
  if (fwd->f_iob == NULL)
    {
      nwarn("WARNING: Failed to clone IOB\n");
      goto errout_with_fwd;
    }

  /* Split by forwarding mode:
   *   Synchronous — forward in RX context (no ASYNC flag)
   *   Async       — enqueue to async list (ASYNC flag set)
   */

#ifdef CONFIG_NET_CANFORWARD_ASYNC

  if (!(fwdrule->flags & CGW_FLAGS_CAN_ASYNC))
    {
      ret = netfwd_forward(dev, fwd);
      if (ret >= 0)
        {
#  ifdef CONFIG_NET_STATISTICS
          g_netstats.cangw.forward++;
#  endif
          return OK;
        }
    }
  else
    {
      irqstate_t irqflags;

      if (canfwd_ensure_wqueue() == NULL)
        {
          nwarn("WARNING: Failed to create canfwd workqueue\n");
          goto errout_with_iob;
        }

      irqflags = spin_lock_irqsave(&g_canfwd_lock);
      sq_addlast((FAR sq_entry_t *)fwd, &g_canfwd_pending);
      spin_unlock_irqrestore(&g_canfwd_lock, irqflags);

#if CONFIG_NET_CANFORWARD_WORK_PERIOD_MS == 0
      if (work_available(&g_canfwd_work))
        {
          work_queue_wq(g_canfwd_wqueue, &g_canfwd_work,
                        can_async_worker, NULL, 0);
        }
#endif

#  ifdef CONFIG_NET_STATISTICS
      g_netstats.cangw.async++;
#  endif
      return OK;
    }
#else
  /* Then set up to forward the packet according to the protocol. */

  ret = netfwd_forward(dev, fwd);
  if (ret >= 0)
    {
#  ifdef CONFIG_NET_STATISTICS
      g_netstats.cangw.forward++;
#  endif
      return OK;
    }
#endif

  /* Only reached on netfwd_forward() failure */

#ifdef CONFIG_NET_CANFORWARD_ASYNC
errout_with_iob:
#endif
  if (fwd->f_iob != NULL)
    {
      iob_free_chain(fwd->f_iob);
    }

errout_with_fwd:
  netfwd_free(fwd);

errout:
#ifdef CONFIG_NET_STATISTICS
  g_netstats.cangw.drop++;
#endif
  return ret;
}

/****************************************************************************
 * Name: can_forward_filter
 *
 * Description:
 *   Check if the input id are match to one forward target rule.
 *
 * Input Parameters:
 *   filter   - The forward filter.
 *   id       - CAN msg ID.
 *
 * Returned Value:
 *   0 - not match, 1 = match
 *
 ****************************************************************************/

static inline_function int can_forward_filter(FAR struct can_filter *filter,
                                              canid_t id)
{
  if (filter->can_id & CAN_INV_FILTER)
    {
      return ((id & filter->can_mask) !=
                ((filter->can_id & ~CAN_INV_FILTER) & filter->can_mask));
    }

  return ((id & filter->can_mask) ==
            (filter->can_id & filter->can_mask));
}

/****************************************************************************
 * Name: can_forward_match_bucket
 *
 * Description:
 *   Traverse all rules in a single hash bucket and forward matching frames.
 *
 ****************************************************************************/

static inline_function void
can_forward_match_bucket(FAR struct net_driver_s *dev,
                         FAR hash_head_t *bucket, canid_t id)
{
  FAR struct cangw_rule_s *fwdrule;

  sq_for_every_entry(bucket, fwdrule, struct cangw_rule_s, node)
    {
      if (fwdrule->flags & CGW_FLAGS_CAN_DISABLED)
        {
          continue;
        }

      if (!can_forward_filter(&fwdrule->gw.ccgw.filter, id))
        {
          continue;
        }

      /* Note: ccgw.filter and cegw.filter share the same offset in the
       * union, so the filter check above is valid for all gateway types.
       */

      switch (fwdrule->gwtype)
        {
          case CGW_TYPE_CAN_CAN:
            can_dev_forward(dev, fwdrule);
#ifdef CONFIG_NET_STATISTICS
            g_netstats.cangw.recv++;
#endif
            break;

#ifdef CONFIG_NET_CANFORWARD_IEEE1722ACF
          case CGW_TYPE_CAN_ETH1722:
            {
              FAR struct iob_s *saved_iob = dev->d_iob;
              FAR uint8_t *saved_buf = dev->d_buf;
              int saved_len = dev->d_len;
              unsigned int blcount;

              dev->d_iob = NULL;
              dev->d_buf = NULL;
              dev->d_len = 0;

              netdev_breaklock(dev, &blcount);
              ieee1722acf_send(fwdrule, saved_iob);
              netdev_restorelock(dev, blcount);

              netdev_iob_release(dev);
              dev->d_iob = saved_iob;
              dev->d_buf = saved_buf;
              dev->d_len = saved_len;
            }

#ifdef CONFIG_NET_STATISTICS
            g_netstats.cangw.recv++;
#endif
            break;
#endif
          default:
            break;
        }
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: add_cangw_rule
 *
 * Description:
 *   Add a CAN->CAN gateway rule to a dev gw rule list.
 *
 ****************************************************************************/

int add_cangw_rule(FAR struct net_driver_s *dev,
                   FAR struct cangw_rule_s *cgw_rules)
{
  unsigned int bucket;

  DEBUGASSERT(dev && cgw_rules);

#ifdef CONFIG_NET_CANFORWARD_IEEE1722ACF
  if (cgw_rules->gwtype == CGW_TYPE_CAN_ETH1722)
    {
      /* Verify that the target stream exists on dest device */

      netdev_lock(cgw_rules->dest_dev);
      if (!find_1722acf_stream(cgw_rules->dest_dev,
                               cgw_rules->gw.cegw.stream_index))
        {
          netdev_unlock(cgw_rules->dest_dev);
          nwarn("WARNING: Target stream index %u not found on %s, "
                "create stream first with eth1722acf tool\n",
                cgw_rules->gw.cegw.stream_index,
                cgw_rules->dest_dev->d_ifname);
          return -ENOENT;
        }

      netdev_unlock(cgw_rules->dest_dev);
    }
#endif

  netdev_lock(dev);

  /* Allocate hash table on first rule addition */

  if (dev->d_cangw_rules == NULL)
    {
      dev->d_cangw_rules =
        hashtable_alloc_n(NET_CANFORWARD_HASH_SIZE);
      if (dev->d_cangw_rules == NULL)
        {
          netdev_unlock(dev);
          return -ENOMEM;
        }
    }

  if (can_forward_is_exact_match(cgw_rules))
    {
      /* Exact-match rule: hash by CAN ID into normal bucket */

      bucket = HASH(cgw_rules->gw.ccgw.filter.can_id,
                    CONFIG_NET_CANFORWARD_HASH_BITS);
    }
  else
    {
      /* Wildcard/partial-mask/INV rule: place in wildcard bucket */

      bucket = NET_CANFORWARD_WILDCARD_BUCKET;
    }

  dq_addfirst(&cgw_rules->node, &dev->d_cangw_rules[bucket]);
  netdev_unlock(dev);

  return OK;
}

/****************************************************************************
 * Name: cangw_rule_match
 *
 * Description:
 *   Check if a rule in the list matches the given criteria.
 *
 * Input Parameters:
 *   rtrule - The rule in the device's rule list.
 *   rule   - The match criteria provided by the caller.
 *
 * Returned Value:
 *   true if the rule matches, false otherwise.
 *
 ****************************************************************************/

static inline_function bool
cangw_rule_match(FAR const struct cangw_rule_s *rtrule,
                 FAR const struct cangw_rule_s *rule)
{
  /* Match gateway type */

  if (rtrule->gwtype != rule->gwtype)
    {
      return false;
    }

  /* Match filter if caller provided a non-zero filter */

  if (rule->gw.ccgw.filter.can_id != 0 ||
      rule->gw.ccgw.filter.can_mask != 0)
    {
      if (rtrule->gw.ccgw.filter.can_id !=
                                       rule->gw.ccgw.filter.can_id ||
          rtrule->gw.ccgw.filter.can_mask !=
                                       rule->gw.ccgw.filter.can_mask)
        {
          return false;
        }
    }

  /* Match destination device if specified */

  if (rule->dest_dev && rtrule->dest_dev != rule->dest_dev)
    {
      return false;
    }

  return true;
}

/****************************************************************************
 * Name: remove_cangw_rule
 *
 * Description:
 *   Remove CAN->CAN gateway rules from a device's rule list.
 *
 * Input Parameters:
 *   dev     - The network device whose rule list to modify.
 *   rule    - Match criteria. If NULL, all rules on the device are removed.
 *   removed - If non-NULL, removed nodes are collected into this queue
 *             and the caller is responsible for freeing them.
 *             If NULL, heap-allocated nodes are freed immediately;
 *             non-heap nodes (e.g. embedded in a static config struct)
 *             are only unlinked from the list.
 *
 * Returned Value:
 *   The number of rules removed.
 *
 ****************************************************************************/

int remove_cangw_rule(FAR struct net_driver_s *dev,
                      FAR const struct cangw_rule_s *rule,
                      FAR dq_queue_t *removed)
{
  FAR struct cangw_rule_s *rtrule;
  FAR struct cangw_rule_s *tmp;
  int count = 0;
  int i;

  DEBUGASSERT(dev);

  if (removed)
    {
      dq_init(removed);
    }

  netdev_lock(dev);

  if (dev->d_cangw_rules == NULL)
    {
      netdev_unlock(dev);
      return 0;
    }

  hashtable_for_every_entry_safe_n(dev->d_cangw_rules, rtrule, tmp,
                                   i, NET_CANFORWARD_HASH_SIZE,
                                   struct cangw_rule_s, node)
    {
      /* If rule is NULL, remove all; otherwise match by criteria */

      if (rule == NULL || cangw_rule_match(rtrule, rule))
        {
          dq_rem(&rtrule->node, &dev->d_cangw_rules[i]);

          if (removed)
            {
              /* Caller takes ownership — collect for deferred free */

              dq_addlast(&rtrule->node, removed);
            }
          else if (kmm_heapmember(rtrule))
            {
              /* Only free nodes that were individually
               * heap-allocated
               */

              kmm_free(rtrule);
            }

          count++;
        }
    }

  netdev_unlock(dev);
  return count;
}

/****************************************************************************
 * Name: modify_cangw_rule
 *
 * Description:
 *   Modify flags of CAN gateway rules on a device. If rule is provided,
 *   only matching rules are affected; otherwise all rules are affected.
 *   The CGW_FLAGS_CAN_DISABLED and CGW_FLAGS_CAN_ASYNC bits from
 *   flags are applied.
 *
 * Input Parameters:
 *   dev   - Source device whose rule list to search
 *   rule  - Rule to match (NULL = affect all rules)
 *   flags - Flags to apply (CGW_FLAGS_CAN_DISABLED and
 *           CGW_FLAGS_CAN_ASYNC bits are used)
 *
 * Returned Value:
 *   Number of rules affected, or -ENOENT if no matching rule found.
 *
 ****************************************************************************/

int modify_cangw_rule(FAR struct net_driver_s *dev,
                      FAR const struct cangw_rule_s *rule,
                      uint16_t flags)
{
  FAR struct cangw_rule_s *rtrule;
  int count = 0;
  int i;

  DEBUGASSERT(dev);

  netdev_lock(dev);

  if (dev->d_cangw_rules == NULL)
    {
      netdev_unlock(dev);
      return -ENOENT;
    }

  hashtable_for_every_entry_n(dev->d_cangw_rules, rtrule,
                              i, NET_CANFORWARD_HASH_SIZE,
                              struct cangw_rule_s, node)
    {
      if (rule == NULL || cangw_rule_match(rtrule, rule))
        {
          rtrule->flags = (rtrule->flags &
                           ~(CGW_FLAGS_CAN_DISABLED |
                             CGW_FLAGS_CAN_ASYNC)) |
                          (flags &
                           (CGW_FLAGS_CAN_DISABLED |
                            CGW_FLAGS_CAN_ASYNC));
          count++;
        }
    }

  netdev_unlock(dev);
  return count > 0 ? count : -ENOENT;
}

/****************************************************************************
 * Name: can_forward
 *
 * Description:
 *   This function is called from can_input when a packet is received that
 *   is not destined for us.  In this case, the packet may need to be
 *   forwarded to another device depending routing table information.
 *
 * Input Parameters:
 *   dev   - The device on which the packet was received and which contains
 *           the can packet.
 *
 *   On input:
 *   - dev->d_buf holds the received packet.
 *   - dev->d_len holds the length of the received packet.
 *
 * Returned Value:
 *   Zero is returned if the packet was successfully forwarded or no rule
 *   matched;  A negated errno value is returned if forwarding is disabled
 *   on the source device.
 *
 ****************************************************************************/

int can_forward(FAR struct net_driver_s *dev)
{
#ifdef CONFIG_NET_CAN_CANFD
  FAR struct canfd_frame *frame
                            = (FAR struct canfd_frame *)IOB_DATA(dev->d_iob);
#else
  FAR struct can_frame *frame = (FAR struct can_frame *)IOB_DATA(dev->d_iob);
#endif
  unsigned int bucket;

  /* Check if source device supports IP forwarding capability */

  if (IFF_IS_NOSRC_FORWARD(dev->d_flags))
    {
      nwarn("WARNING: CAN forwarding disabled on source device %s\n",
            dev->d_ifname);
      return -EOPNOTSUPP;
    }

  if (dev->d_iob->io_conn != NULL || (frame->can_id & CAN_ERR_FLAG) != 0)
    {
      /* Do not forward transmit confirmation frames & error frames */

      return OK;
    }

  /* No rules configured, nothing to forward */

  if (dev->d_cangw_rules == NULL)
    {
      return OK;
    }

  /* Step 1: Look up the exact-match hash bucket for this frame's ID.
   * Callers must ensure filter.can_id carries CAN_EFF_FLAG for extended
   * frames so that the hash key matches between add_cangw_rule() and here.
   */

  bucket = HASH(frame->can_id, CONFIG_NET_CANFORWARD_HASH_BITS);
  can_forward_match_bucket(dev, &dev->d_cangw_rules[bucket], frame->can_id);

  /* Step 2: Traverse the wildcard bucket (partial-mask/INV rules) */

  can_forward_match_bucket(dev,
    &dev->d_cangw_rules[NET_CANFORWARD_WILDCARD_BUCKET],
    frame->can_id);

  return OK;
}
