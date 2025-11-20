/****************************************************************************
 * sched/misc/ppccall.c
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

#include <nuttx/ppccall.h>
#include <nuttx/spinlock.h>

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct ppccall_info_s g_ppccall_info[PPC_maxppccall];
static spinlock_t g_ppccall_lock = SP_UNLOCKED;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int ppccall_register_one(uintptr_t nbr, uintptr_t func)
{
  FAR struct ppccall_info_s *info;
  int ret = OK;

  DEBUGASSERT(nbr < PPC_maxppccall);

  info = &g_ppccall_info[nbr];

  if (func != 0u && info->func != 0u)
    {
      /* Register PPC function but already registered */

      ret = -EEXIST;
    }
  else if (func == 0u && info->func == 0u)
    {
      /* Unregister PPC function but already unregistered */

      ret = -ENOENT;
    }
  else
    {
      info->func = func;
      info->tcb = func ? this_task() : NULL;
    }

  return ret;
}

static int ppccall_register(FAR const struct ppccall_item_s *items,
                            size_t nitem, bool reg)
{
  irqstate_t flags;
  uintptr_t nbr;
  size_t i;
  int ret = OK;

  flags = spin_lock_irqsave(&g_ppccall_lock);
  for (i = 0; i < nitem; i++)
    {
      nbr = items[i].nbr;
      if (reg)
        {
          ret = ppccall_register_one(nbr, items[i].func);
          if (ret < 0)
            {
              /* Unregister previous registered functions */

              while (i-- > 0)
                {
                  ppccall_register_one(items[i].nbr, 0u);
                }

              break;
            }
        }
      else
        {
          /* Unregister all the function and ignore failure */

          ppccall_register_one(nbr, 0u);
        }
    }

  spin_unlock_irqrestore(&g_ppccall_lock, flags);
  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int ppc_unregister(FAR const struct ppccall_item_s *items, size_t nitem)
{
  return ppccall_register(items, nitem, false);
}

int ppc_register(FAR const struct ppccall_item_s *items, size_t nitem)
{
  return ppccall_register(items, nitem, true);
}

FAR const struct ppccall_info_s *ppccall_find_info(uintptr_t nbr)
{
  FAR struct ppccall_info_s *info = NULL;
  irqstate_t flags;

  flags = spin_lock_irqsave(&g_ppccall_lock);
  if (nbr < PPC_maxppccall)
    {
      info = &g_ppccall_info[nbr];
      if (info->func == 0u)
        {
          info = NULL;
        }
    }

  spin_unlock_irqrestore(&g_ppccall_lock, flags);
  return info;
}
