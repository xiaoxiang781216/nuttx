/****************************************************************************
 * drivers/cpufreq/qos.c
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

#include <nuttx/spinlock.h>
#include <nuttx/cpufreq/qos.h>

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Locking rule: all changes to constraints or notifiers lists
 * or pm_qos_object list and pm_qos_objects need to happen with g_pm_qos_lock
 * held, taken with _irqsave.  One lock to rule them all
 */

static spinlock_t g_pm_qos_lock;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int32_t pm_qos_get_value(FAR struct pm_qos_constraints *c)
{
  if (plist_head_empty(&c->list))
    {
      return c->no_constraint_value;
    }

  switch (c->type)
    {
    case PM_QOS_MIN:
      {
        return plist_first(&c->list)->prio;
      }

    case PM_QOS_MAX:
      {
        return plist_last(&c->list)->prio;
      }

    default:
      return c->default_value;
    }
}

static void pm_qos_set_value(FAR struct pm_qos_constraints *c,
                             int32_t value)
{
  c->target_value = value;
}

/* pm_qos_flags_remove_req - Remove device PM QoS flags request.
 * pqf: Device PM QoS flags set to remove the request from.
 * req: Request to remove from the set.
 */

static void pm_qos_flags_remove_req(FAR struct pm_qos_flags *pqf,
                                    FAR struct pm_qos_flags_request *req)
{
  int32_t val = 0;

  list_delete(&req->node);
  list_for_every_entry(&pqf->list, req, typeof(*req), node)
    {
      val |= req->flags;
    }

  pqf->effective_flags = val;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/* pm_qos_read_value - Return the current effective constraint value.
 * c: List of PM QoS constraint requests.
 */

int32_t pm_qos_read_value(FAR struct pm_qos_constraints *c)
{
  return c->target_value;
}

/* pm_qos_update_target - Update a list of PM QoS constraint requests.
 * c: List of PM QoS requests.
 * node: Target list entry.
 * action: Action to carry out (add, update or remove).
 * value: New request value for the target list entry.
 *
 * Update the given list of PM QoS constraint requests, by carrying an
 * action involving the node list entry and value on it.
 *
 * The recognized values of action are PM_QOS_ADD_REQ (store value in node
 * and add it to the list), PM_QOS_UPDATE_REQ (remove node from the list,
 * store value in it and add it to the list again), and PM_QOS_REMOVE_REQ
 * (remove node from the list, ignore value).
 *
 * Return: 1 if the aggregate constraint value has changed, 0 otherwise.
 */

int pm_qos_update_target(FAR struct pm_qos_constraints *c,
                         FAR struct plist_node *node,
                         enum pm_qos_req_action action, int32_t value)
{
  irqstate_t flags;
  int32_t prev_value;
  int32_t curr_value;
  int32_t new_value;

  flags = spin_lock_irqsave(&g_pm_qos_lock);
  prev_value = pm_qos_get_value(c);

  if (value == PM_QOS_DEFAULT_VALUE)
    {
      new_value = c->default_value;
    }
  else
    {
      new_value = value;
    }

  switch (action)
    {
    case PM_QOS_REMOVE_REQ:
      plist_del(node, &c->list);
      break;

    case PM_QOS_UPDATE_REQ:
    case PM_QOS_UPDATE_REQ_NON_NOTIFY:
      /* To change the list, atomically remove, reinit with new value
       * and add, then see if the aggregate has changed.
       */

      plist_del(node, &c->list);

    case PM_QOS_ADD_REQ:
      plist_node_init(node, new_value);
      plist_add(node, &c->list);
      break;

    default:
      break;
    }

  curr_value = pm_qos_get_value(c);
  pm_qos_set_value(c, curr_value);

  spin_unlock_irqrestore(&g_pm_qos_lock, flags);

  if (prev_value == curr_value)
    {
      return 0;
    }

  if (c->notifiers && action != PM_QOS_UPDATE_REQ_NON_NOTIFY)
    {
      blocking_notifier_call_chain(c->notifiers, curr_value, NULL);
    }

  return 1;
}

/* pm_qos_update_flags - Update a set of PM QoS flags.
 * pqf: Set of PM QoS flags to update.
 * req: Request to add to the set, to modify, or to remove from the set.
 * action: Action to take on the set.
 * val: Value of the request to add or modify.
 *
 * Return: 1 if the aggregate constraint value has changed, 0 otherwise.
 */

int pm_qos_update_flags(FAR struct pm_qos_flags *pqf,
                        FAR struct pm_qos_flags_request *req,
                        enum pm_qos_req_action action, int32_t val)
{
  irqstate_t irqflags;
  int32_t prev_value;
  int32_t curr_value;

  irqflags = spin_lock_irqsave(&g_pm_qos_lock);
  prev_value = pqf->effective_flags;

  switch (action)
    {
    case PM_QOS_REMOVE_REQ:
      pm_qos_flags_remove_req(pqf, req);
      break;

    case PM_QOS_UPDATE_REQ:
      pm_qos_flags_remove_req(pqf, req);

    case PM_QOS_ADD_REQ:
      req->flags = val;
      list_initialize(&req->node);
      list_add_tail(&pqf->list, &req->node);
      pqf->effective_flags |= val;
      break;

    default:
      break;
    }

  curr_value = pqf->effective_flags;
  spin_unlock_irqrestore(&g_pm_qos_lock, irqflags);

  return prev_value != curr_value;
}

/* Definitions related to the frequency QoS below. */

/* freq_constraints_init - Initialize frequency QoS constraints.
 * qos: Frequency QoS constraints to initialize.
 */

void freq_constraints_init(FAR struct freq_constraints *qos)
{
  FAR struct pm_qos_constraints *c;

  c = &qos->min_freq;
  plist_head_init(&c->list);
  c->target_value        = FREQ_QOS_MIN_DEFAULT_VALUE;
  c->default_value       = FREQ_QOS_MIN_DEFAULT_VALUE;
  c->no_constraint_value = FREQ_QOS_MIN_DEFAULT_VALUE;
  c->type                = PM_QOS_MAX;
  c->notifiers           = &qos->min_freq_notifiers;
  BLOCKING_INIT_NOTIFIER_HEAD(c->notifiers);

  c = &qos->max_freq;
  plist_head_init(&c->list);
  c->target_value        = FREQ_QOS_MAX_DEFAULT_VALUE;
  c->default_value       = FREQ_QOS_MAX_DEFAULT_VALUE;
  c->no_constraint_value = FREQ_QOS_MAX_DEFAULT_VALUE;
  c->type                = PM_QOS_MIN;
  c->notifiers           = &qos->max_freq_notifiers;
  BLOCKING_INIT_NOTIFIER_HEAD(c->notifiers);
}

/* freq_qos_read_value - Get frequency QoS constraint for a given list.
 * qos: Constraints to evaluate.
 * type: QoS request type.
 */

int32_t freq_qos_read_value(FAR struct freq_constraints *qos,
                            enum freq_qos_req_type type)
{
  int32_t ret;

  switch (type)
    {
    case FREQ_QOS_MIN:
      ret = qos ? pm_qos_read_value(&qos->min_freq) :
            FREQ_QOS_MIN_DEFAULT_VALUE;
      break;

    case FREQ_QOS_MAX:
      ret = qos ? pm_qos_read_value(&qos->max_freq) :
            FREQ_QOS_MAX_DEFAULT_VALUE;
      break;

    default:
      ret = 0;
    }

  return ret;
}

/* freq_qos_apply - Add/modify/remove frequency QoS request.
 * req: Constraint request to apply.
 * action: Action to perform (add/update/remove).
 * value: Value to assign to the QoS request.
 *
 * This is only meant to be called from inside pm_qos, not drivers.
 */

int freq_qos_apply(FAR struct freq_qos_request *req,
                   enum pm_qos_req_action action, int32_t value)
{
  int ret;

  if (!req->qos)
    {
      return -EINVAL;
    }

  switch (req->type)
    {
    case FREQ_QOS_MIN:
      ret = pm_qos_update_target(&req->qos->min_freq, &req->pnode, action,
                                 value);
      break;

    case FREQ_QOS_MAX:
      ret = pm_qos_update_target(&req->qos->max_freq, &req->pnode, action,
                                 value);
      break;

    default:
      ret = -EINVAL;
    }

  return ret;
}

/* freq_qos_add_request - Insert new frequency QoS request into a given list.
 * qos: Constraints to update.
 * req: Preallocated request object.
 * type: Request type.
 * value: Request value.
 *
 * Insert a new entry into the qos list of requests, recompute the effective
 * QoS constraint value for that list and initialize the req object.  The
 * caller needs to save that object for later use in updates and removal.
 *
 * Return 1 if the effective constraint value has changed, 0 if the effective
 * constraint value has not changed, or a negative error code on failures.
 */

int freq_qos_add_request(FAR struct freq_constraints *qos,
                         FAR struct freq_qos_request *req,
                         enum freq_qos_req_type type, int32_t value)
{
  int ret;

  if (!qos || !req || value < 0)
    {
      return -EINVAL;
    }

  req->qos  = qos;
  req->type = type;

  ret = freq_qos_apply(req, PM_QOS_ADD_REQ, value);
  if (ret < 0)
    {
      req->qos  = NULL;
      req->type = 0;
    }

  return ret;
}

/* freq_qos_update_request - Modify existing frequency QoS request.
 * req: Request to modify.
 * new_value: New request value.
 *
 * Update an existing frequency QoS request along with the effective
 * constraint value for the list of requests it belongs to.
 *
 * Return 1 if the effective constraint value has changed, 0 if the effective
 * constraint value has not changed, or a negative error code on failures.
 */

int freq_qos_update_request(FAR struct freq_qos_request *req,
                            int32_t new_value)
{
  if (!req || new_value < 0)
    {
      return -EINVAL;
    }

  if (req->pnode.prio == new_value)
    {
      return 0;
    }

  return freq_qos_apply(req, PM_QOS_UPDATE_REQ, new_value);
}

/* freq_qos_remove_request - Remove frequency QoS request from its list.
 * req: Request to remove.
 *
 * Remove the given frequency QoS request from the list of constraints it
 * belongs to and recompute the effective constraint value for that list.
 *
 * Return 1 if the effective constraint value has changed, 0 if the effective
 * constraint value has not changed, or a negative error code on failures.
 */

int freq_qos_remove_request(FAR struct freq_qos_request *req)
{
  int ret;

  if (!req)
    {
      return -EINVAL;
    }

  ret = freq_qos_apply(req, PM_QOS_REMOVE_REQ, PM_QOS_DEFAULT_VALUE);

  req->qos  = NULL;
  req->type = 0;

  return ret;
}

/* freq_qos_add_notifier - Add frequency QoS change notifier.
 * qos: List of requests to add the notifier to.
 * type: Request type.
 * notifier: Notifier block to add.
 */

int freq_qos_add_notifier(FAR struct freq_constraints *qos,
                          enum freq_qos_req_type type,
                          FAR struct notifier_block *notifier)
{
  int ret = 0;

  if (!qos || !notifier)
    {
      return -EINVAL;
    }

  switch (type)
    {
    case FREQ_QOS_MIN:
      blocking_notifier_chain_register(qos->min_freq.notifiers, notifier);
      break;

    case FREQ_QOS_MAX:
      blocking_notifier_chain_register(qos->max_freq.notifiers, notifier);
      break;

    default:
      ret = -EINVAL;
      break;
    }

  return ret;
}

/* freq_qos_remove_notifier - Remove frequency QoS change notifier.
 * qos: List of requests to remove the notifier from.
 * type: Request type.
 * notifier: Notifier block to remove.
 */

int freq_qos_remove_notifier(FAR struct freq_constraints *qos,
                             enum freq_qos_req_type type,
                             FAR struct notifier_block *notifier)
{
  int ret = 0;

  if (!qos || !notifier)
    {
      return -EINVAL;
    }

  switch (type)
    {
    case FREQ_QOS_MIN:
      blocking_notifier_chain_unregister(qos->min_freq.notifiers, notifier);
      break;

    case FREQ_QOS_MAX:
      blocking_notifier_chain_unregister(qos->max_freq.notifiers, notifier);
      break;

    default:
      ret = -EINVAL;
      break;
    }

  return ret;
}
