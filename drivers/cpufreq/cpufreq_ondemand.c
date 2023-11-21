/****************************************************************************
 * drivers/cpufreq/cpufreq_ondemand.c
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

#include <nuttx/kmalloc.h>
#include <sys/param.h>

#include "cpufreq_internal.h"
#include "sched/sched.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define CPUFREQ_MIN_SAMPLING_INTERVAL   (2 * USEC_PER_TICK)
#define CPUFREQ_LOAD_THRESHOLD_MAX      (100)

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct cpufreq_ondemand_s
{
  struct work_s work;
  unsigned int threshold;
  unsigned int sample_rate;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int cpufreq_gov_ondemand_init(FAR struct cpufreq_policy *policy);
static int cpufreq_gov_ondemand_exit(FAR struct cpufreq_policy *policy);
static int cpufreq_gov_ondemand_start(FAR struct cpufreq_policy *policy);
static void cpufreq_gov_ondemand_stop(FAR struct cpufreq_policy *policy);
static void cpufreq_gov_ondemand_limits(FAR struct cpufreq_policy *policy);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct cpufreq_governor g_cpufreq_gov_ondemand =
{
  .name   = "ondemand",
  .init   = cpufreq_gov_ondemand_init,
  .exit   = cpufreq_gov_ondemand_exit,
  .start  = cpufreq_gov_ondemand_start,
  .stop   = cpufreq_gov_ondemand_stop,
  .limits = cpufreq_gov_ondemand_limits,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static unsigned int
cpufreq_gov_ondemand_cpuload(FAR struct cpufreq_policy *policy)
{
  struct cpuload_s loadavg;
  unsigned int idleload = 0;
  int cpu;

  for (cpu = 0; cpu < CONFIG_SMP_NCPUS; cpu++)
    {
      clock_cpuload(cpu, &loadavg);
      idleload += loadavg.active * 100 / loadavg.total;
    }

  return 100 - idleload;
}

static void cpufreq_ondemand_worker(FAR void *arg)
{
  FAR struct cpufreq_policy *policy = arg;
  FAR struct cpufreq_ondemand_s *data;
  unsigned int target_freq;
  unsigned int cpuload;

  data = policy->governor_data;
  cpuload = cpufreq_gov_ondemand_cpuload(policy);
  nxmutex_lock(&policy->lock);
  if (cpuload > data->threshold)
    {
      if (policy->cur < policy->max)
        {
          target_freq = policy->max;
          cpufreq_driver_target(policy, target_freq, CPUFREQ_RELATION_H);
        }
    }
  else
    {
      target_freq = policy->min + cpuload *
                    (policy->max - policy->min) / 100;
      cpufreq_driver_target(policy, target_freq, CPUFREQ_RELATION_L);
    }

  nxmutex_unlock(&policy->lock);
  work_queue(HPWORK,
             &data->work,
             cpufreq_ondemand_worker,
             policy,
             data->sample_rate / USEC_PER_TICK);
}

static int cpufreq_gov_ondemand_init(FAR struct cpufreq_policy *policy)
{
  FAR struct cpufreq_ondemand_s *data;

  data = kmm_zalloc(sizeof(struct cpufreq_ondemand_s));
  if (!data)
    {
      return -ENOMEM;
    }

  data->threshold = MIN(CPUFREQ_LOAD_THRESHOLD_MAX,
                        CONFIG_CPUFREQ_LOAD_THRESHOLD);
  data->sample_rate = MAX(CPUFREQ_MIN_SAMPLING_INTERVAL,
                          CONFIG_CPUFREQ_SAMPLE_RATE);
  policy->governor_data = data;
  return 0;
}

static int cpufreq_gov_ondemand_exit(FAR struct cpufreq_policy *policy)
{
  FAR struct cpufreq_ondemand_s *data = policy->governor_data;

  kmm_free(data);
  return 0;
}

static int cpufreq_gov_ondemand_start(FAR struct cpufreq_policy *policy)
{
  FAR struct cpufreq_ondemand_s *data = policy->governor_data;

  work_queue(HPWORK,
             &data->work,
             cpufreq_ondemand_worker,
             policy,
             0);
  return 0;
}

static void cpufreq_gov_ondemand_stop(FAR struct cpufreq_policy *policy)
{
  FAR struct cpufreq_ondemand_s *data = policy->governor_data;

  work_cancel_sync(HPWORK, &data->work);
}

static void cpufreq_gov_ondemand_limits(FAR struct cpufreq_policy *policy)
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
  return &g_cpufreq_gov_ondemand;
}
