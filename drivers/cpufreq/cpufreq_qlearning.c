/****************************************************************************
 * drivers/cpufreq/cpufreq_qlearning.c
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

#include "cpufreq_internal.h"
#include "qlearning/qlearning_manager.h"

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int cpufreq_gov_qlearning_init(FAR struct cpufreq_policy *policy);
static int cpufreq_gov_qlearning_exit(FAR struct cpufreq_policy *policy);
static int cpufreq_gov_qlearning_start(FAR struct cpufreq_policy *policy);
static void cpufreq_gov_qlearning_stop(FAR struct cpufreq_policy *policy);
static void cpufreq_gov_qlearning_limits(FAR struct cpufreq_policy *policy);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct cpufreq_governor g_cpufreq_gov_qlearning =
{
  .name   = "qlearning",
  .init   = cpufreq_gov_qlearning_init,
  .start  = cpufreq_gov_qlearning_start,
  .stop   = cpufreq_gov_qlearning_stop,
  .exit   = cpufreq_gov_qlearning_exit,
  .limits = cpufreq_gov_qlearning_limits,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int cpufreq_gov_qlearning_init(FAR struct cpufreq_policy *policy)
{
  return qlearning_manager_init(policy);
}

static int cpufreq_gov_qlearning_start(FAR struct cpufreq_policy *policy)
{
  return qlearning_manager_start(policy);
}

static void cpufreq_gov_qlearning_stop(FAR struct cpufreq_policy *policy)
{
  qlearning_manager_end(policy);
}

static int cpufreq_gov_qlearning_exit(FAR struct cpufreq_policy *policy)
{
  return qlearning_manager_exit(policy);
}

static void cpufreq_gov_qlearning_limits(FAR struct cpufreq_policy *policy)
{
  nxmutex_lock(&policy->lock);
  if (policy->max < policy->cur)
    {
      cpufreq_driver_target(policy, policy->max, CPUFREQ_RELATION_H);
    }
  else if (policy->min > policy->cur)
    {
      cpufreq_driver_target(policy, policy->min, CPUFREQ_RELATION_L);
    }

  nxmutex_unlock(&policy->lock);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

FAR struct cpufreq_governor *cpufreq_default_governor(void)
{
  return &g_cpufreq_gov_qlearning;
}
