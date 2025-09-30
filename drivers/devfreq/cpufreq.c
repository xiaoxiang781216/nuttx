/****************************************************************************
 * drivers/devfreq/cpufreq.c
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

#include <debug.h>

#include <nuttx/cpufreq.h>
#include <nuttx/devfreq.h>
#include <nuttx/kmalloc.h>
#include <nuttx/notifier.h>
#include <nuttx/sched.h>

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct cpufreq_data_s
{
  struct cpufreq_policy policy;
  FAR struct devfreq_s *dev;
};

/****************************************************************************
 * Private Functions Prototypes
 ****************************************************************************/

static const uint32_t *get_table_converter(FAR struct devfreq_s *devfreq);
static int target_index_converter(FAR struct devfreq_s *devfreq,
                                  size_t index);
static uint32_t get_frequency_converter(FAR struct devfreq_s *devfreq);
static int suspend_converter(FAR struct devfreq_s *devfreq);
static int resume_converter(FAR struct devfreq_s *devfreq);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct cpufreq_data_s g_cpufreq_data;
static const struct devfreq_driver_s g_cpufreq_ops =
{
  .get_table     = get_table_converter,
  .target_index  = target_index_converter,
  .get_frequency = get_frequency_converter,
  .suspend       = suspend_converter,
  .resume        = resume_converter,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static const uint32_t *get_table_converter(FAR struct devfreq_s *devfreq)
{
  FAR struct cpufreq_data_s *data = devfreq->priv;
  return data->policy.driver->get_table(&data->policy);
}

static int target_index_converter(FAR struct devfreq_s *devfreq,
                                  size_t index)
{
  FAR struct cpufreq_data_s *data = devfreq->priv;
  return data->policy.driver->target_index(&data->policy, index);
}

static uint32_t get_frequency_converter(FAR struct devfreq_s *devfreq)
{
  FAR struct cpufreq_data_s *data = devfreq->priv;
  return data->policy.driver->get_frequency(&data->policy);
}

static int suspend_converter(FAR struct devfreq_s *devfreq)
{
  FAR struct cpufreq_data_s *data = devfreq->priv;
  return data->policy.driver->suspend(&data->policy);
}

static int resume_converter(FAR struct devfreq_s *devfreq)
{
  FAR struct cpufreq_data_s *data = devfreq->priv;
  return data->policy.driver->resume(&data->policy);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int cpufreq_init(FAR struct cpufreq_driver *driver)
{
  FAR struct devfreq_governor_s *governor;

  if (!driver || !driver->get_table || !driver->get_frequency ||
      !driver->target_index)
    {
      return -EINVAL;
    }

#if defined(CONFIG_CPUFREQ_DEFAULT_GOV_PERFORMANCE)
  governor = devfreq_performance();
#elif defined(CONFIG_CPUFREQ_DEFAULT_GOV_POWERSAVE)
  governor = devfreq_powersave();
#elif defined(CONFIG_CPUFREQ_DEFAULT_GOV_ONDEMAND)
  governor = devfreq_ondemand();
#elif defined(CONFIG_CPUFREQ_DEFAULT_GOV_QLEARNING)
  governor = devfreq_qlearning();
#endif

  g_cpufreq_data.policy.driver = driver;
  g_cpufreq_data.dev = devfreq_register(CONFIG_CPUFREQ_DEVICE_NAME, governor,
                                        &g_cpufreq_ops,
                                        &g_cpufreq_data);
  if (g_cpufreq_data.dev == NULL)
    {
      return -ENOMEM;
    }

  return 0;
}

int cpufreq_uninit(void)
{
  return devfreq_unregister(g_cpufreq_data.dev);
}

FAR struct cpufreq_policy *cpufreq_policy_get(void)
{
  return &g_cpufreq_data.policy;
}

int cpufreq_suspend(FAR struct cpufreq_policy *policy)
{
  return devfreq_suspend(g_cpufreq_data.dev);
}

int cpufreq_resume(FAR struct cpufreq_policy *policy)
{
  return devfreq_resume(g_cpufreq_data.dev);
}

int cpufreq_register_notifier(FAR struct cpufreq_policy *policy,
                              FAR struct notifier_block *nb)
{
  return devfreq_register_notifier(g_cpufreq_data.dev, nb);
}

int cpufreq_unregister_notifier(FAR struct cpufreq_policy *policy,
                                FAR struct notifier_block *nb)
{
  return devfreq_unregister_notifier(g_cpufreq_data.dev, nb);
}

int cpufreq_get(FAR struct cpufreq_policy *policy)
{
  return devfreq_get_frequency(g_cpufreq_data.dev);
}

FAR struct qos_request_s *cpufreq_qos_add_request(
                            FAR struct cpufreq_policy *policy,
                            unsigned int min, unsigned int max)
{
  return devfreq_qos_add_request(g_cpufreq_data.dev, min, max);
}

int cpufreq_qos_update_request(FAR struct qos_request_s *qos,
                               unsigned int min, unsigned int max)
{
  return devfreq_qos_update_request(g_cpufreq_data.dev, qos, min, max);
}

int cpufreq_qos_remove_request(FAR struct qos_request_s *qos)
{
  return devfreq_qos_remove_request(g_cpufreq_data.dev, qos);
}
