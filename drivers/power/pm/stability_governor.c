/****************************************************************************
 * drivers/power/pm/stability_governor.c
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

#include <nuttx/clock.h>
#include <nuttx/power/pm.h>
#include <nuttx/sched.h>
#include <nuttx/wdog.h>

#include <sys/types.h>
#include <stdbool.h>

#include "pm.h"

/****************************************************************************
 * Private Type Declarations
 ****************************************************************************/

struct pm_stability_governor_domain_s
{
  /* The Idle is wakeup from the governor wdog itself */

  bool wdog_wakeup;

  /* This state has not been maintained long enough to meet the threshold. */

  enum pm_state_e state_pending;

#ifdef CONFIG_SMP

  /* SMP call data to wake self up */

  struct smp_call_delay_data_s call_data;
#else

  /* Timer to wakeup system, delay the sleep request */

  struct wdog_s wdog;
#endif
};

struct pm_stability_governor_s
{
  struct pm_stability_governor_domain_s domain[CONFIG_PM_NDOMAINS];
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/* PM governor methods */

static void stability_governor_statechanged(int domain,
                                            enum pm_state_e newstate);
static enum pm_state_e stability_governor_checkstate(int domain);
static void stability_governor_activity(int domain, int count);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct pm_governor_s g_stability_governor_ops =
{
  NULL,                            /* initialize */
  NULL,                            /* deinitialize */
  stability_governor_statechanged, /* statechanged */
  stability_governor_checkstate,   /* checkstate */
  stability_governor_activity,     /* activity */
  NULL                             /* priv */
};

#ifdef PM_GOVERNOR_STABILITY_THRESH
static const
clock_t g_stability_governor_thresh[CONFIG_PM_NDOMAINS][PM_COUNT] =
{
  PM_GOVERNOR_STABILITY_THRESH
};
#else
static const clock_t g_stability_governor_thresh[PM_COUNT] =
{
  0,
  CONFIG_PM_GOVERNOR_STABILITY_IDLE_THRESH,
  CONFIG_PM_GOVERNOR_STABILITY_STANDBY_THRESH,
  CONFIG_PM_GOVERNOR_STABILITY_SLEEP_THRESH
};
#endif

static struct pm_stability_governor_s g_stability_governor;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: stability_governor_cb
 ****************************************************************************/

static int stability_governor_cb(void *arg)
{
  /* Timer cb only to make sure system will wake from WFI
   * For SMP, cb maybe called in smp async callback.
   */

  FAR struct pm_stability_governor_domain_s *dom;
  dom = (FAR struct pm_stability_governor_domain_s *)arg;
  dom->wdog_wakeup = true;
  return 0;
}

/****************************************************************************
 * Name: stability_governor_statechanged
 ****************************************************************************/

static void stability_governor_statechanged(int domain,
                                            enum pm_state_e newstate)
{
  FAR struct pm_stability_governor_domain_s *gdom;
  FAR struct wdog_s *wdog;

  gdom = &g_stability_governor.domain[domain];

#ifdef CONFIG_SMP
  wdog = &gdom->call_data.delay;
#else
  wdog = &gdom->wdog;
#endif

  if (newstate == PM_RESTORE)
    {
      if (WDOG_ISACTIVE(wdog))
        {
          /* The left tick from wdog, if >0 should be other irq source */

          sclock_t left = wd_gettime(wdog);
          if (left <= 0)
            {
              gdom->wdog_wakeup = true;
            }

          /* Don't have to execute callback if already wakeup by wdog
           * For SMP case, the wdog should cb should be executing when wokeup
           * so active wdog can always be canceled.
           */

          wd_cancel(wdog);
        }
    }
  else
    {
      /* PM_NORMAL always no detect, if thresh 0, also no detect */

      enum pm_state_e state = gdom->state_pending;
#ifdef PM_GOVERNOR_STABILITY_THRESH
      clock_t thresh        = g_stability_governor_thresh[domain][state];
#else
      clock_t thresh        = g_stability_governor_thresh[state];
#endif
      if (thresh > 0 && state != newstate)
        {
#ifdef CONFIG_SMP
          nxsched_smp_call_delay_init(&gdom->call_data,
                                      stability_governor_cb,
                                      gdom);

          nxsched_smp_call_single_delay(this_cpu(), thresh,
                                        &gdom->call_data);
#else
          wd_start(wdog, thresh, (wdentry_t)stability_governor_cb, 0);
#endif
        }
    }
}

/****************************************************************************
 * Name: user_governor_checkstate
 ****************************************************************************/

static enum pm_state_e stability_governor_checkstate(int domain)
{
  FAR struct pm_stability_governor_domain_s *gdom;
  FAR struct pm_domain_s *pdom;
  enum pm_state_e state_pending;
  enum pm_state_e state;
  bool wdog_wakeup;
  clock_t thresh;

  gdom = &g_stability_governor.domain[domain];
  pdom = &g_pmdomains[domain];
  state = PM_NORMAL;

  /* Find the lowest power-level which is not locked. */

  while (dq_empty(&pdom->wakelock[state]) && state < (PM_COUNT - 1))
    {
      state++;
    }

  state_pending = gdom->state_pending;
  wdog_wakeup = gdom->wdog_wakeup;
  gdom->state_pending = state;
  gdom->wdog_wakeup = false;

#ifdef PM_GOVERNOR_STABILITY_THRESH
  thresh = g_stability_governor_thresh[domain][state];
#else
  thresh = g_stability_governor_thresh[state];
#endif
  /* If pm stability check disabled state or pm stable enough, do nothing */

  if (thresh > 0 &&
     (!wdog_wakeup || state_pending != state))
    {
      state = pdom->state;
      if (thresh > 0)
        {
          /* The domain last state can not be backward, need to holding
           * to the lowest power-level with stability check disabled
           */

          for (; state > PM_NORMAL; state--)
            {
              if (thresh == 0)
                {
                  break;
                }
            }
        }
    }

  /* Return the found state */

  return state;
}

/****************************************************************************
 * Name: greedy_activity
 ****************************************************************************/

static void stability_governor_activity(int domain, int count)
{
  pm_staytimeout(domain, PM_NORMAL, (count ? count : 1) * 1000);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: pm_stability_governor_initialize
 *
 * Description:
 *   Register the user_governor driver as the specified device.
 *
 * Returned Value:
 *   Zero (OK) is returned on success.  Otherwise a negated errno value is
 *   returned to indicate the nature of the failure.
 *
 ****************************************************************************/

FAR const struct pm_governor_s *pm_stability_governor_initialize(void)
{
  return &g_stability_governor_ops;
}
