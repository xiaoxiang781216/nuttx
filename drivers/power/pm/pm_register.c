/****************************************************************************
 * drivers/power/pm/pm_register.c
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

#include <nuttx/init.h>
#include <nuttx/queue.h>
#include <nuttx/power/pm.h>
#include <nuttx/nuttx.h>

#include "pm.h"

#ifdef CONFIG_PM

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: pm_register_callback
 *
 * Description:
 *   This function register callback in order of priority to target domain.
 *
 * Input Parameters:
 *   pdom - Target register domain.
 *   cb   - An instance of struct pm_callback_s providing the driver
 *            callback functions.
 *
 * Returned Value:
 *   Zero (OK) on success; otherwise a negated errno value is returned.
 *
 ****************************************************************************/

static int pm_register_callback(FAR struct pm_domain_s *pdom,
                                FAR struct pm_callback_s *cb)
{
  dq_queue_t *head = &pdom->registry;
  dq_entry_t *node;

  for (node = head->tail; node; node = node->flink)
    {
      struct pm_callback_s *ncb;

      ncb = container_of(node, struct pm_callback_s, entry);
      if (ncb == cb)
        {
          return -EEXIST;
        }

      /* Callbacks with higher prio should be in front of registry dqlist
       * so that they are called first.
       */

      if (cb->prio <= ncb->prio)
        {
          dq_addafter(node, &cb->entry, head);
          return OK;
        }
    }

  dq_addfirst(&cb->entry, head);
  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: pm_domain_register
 *
 * Description:
 *   This function is called by a device driver in order to register to
 *   receive power management event callbacks.
 *
 * Input Parameters:
 *   domain - Target register domain.
 *   cb     - An instance of struct pm_callback_s providing the driver
 *               callback functions.
 *
 * Returned Value:
 *    Zero (OK) on success; otherwise a negated errno value is returned.
 *
 ****************************************************************************/

int pm_domain_register(int domain, FAR struct pm_callback_s *cb)
{
  FAR struct pm_domain_s *pdom;
  irqstate_t flags;
  int ret;

  DEBUGASSERT(domain >= 0 && domain < CONFIG_PM_NDOMAINS && cb != NULL);

  pdom  = &g_pmdomains[domain];
  flags = spin_lock_irqsave(&pdom->lock);

  /* Add the new entry to the end of the list of registered callbacks */

  ret = pm_register_callback(pdom, cb);
#if defined (CONFIG_PM_STAT)
  cb->preparefail.state = PM_RESTORE;
#endif
  spin_unlock_irqrestore(&pdom->lock, flags);
  return ret;
}

#endif /* CONFIG_PM */
