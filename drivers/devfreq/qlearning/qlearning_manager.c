/****************************************************************************
 * drivers/devfreq/qlearning/qlearning_manager.c
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
#include <nuttx/devfreq/qlearning.h>
#include <nuttx/devfreq.h>
#include <nuttx/power/pm.h>
#include <nuttx/wqueue.h>

#include "qlearning_manager.h"
#include "qlearning_network.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define QLEARNING_FLAG_INIT     (1 << 0)
#define QLEARNING_FLAG_LOAD     (1 << 1)
#define QLEARNING_FLAG_RUNNING  (1 << 2)

/****************************************************************************
 * Private Type Definitions
 ****************************************************************************/

struct qlearning_manager_s
{
  FAR struct qlearning_lowerhalf_s *lh;
  FAR struct qlearning_params_s *params;
  FAR struct qos_request_s *req;
  struct work_s qlearning_manager_work;
  struct pm_callback_s cb;
  float perf_data[MAX_LAYER_SIZE];
  uint32_t action[MAX_LAYER_SIZE];
  uint32_t target_freq;
  clock_t wait;
  uint8_t flags;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct qlearning_manager_s g_qlearning_manager;

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int qlearning_manager_worker(FAR struct devfreq_s *dev);
static void qlearning_manager_action_init(FAR struct devfreq_s *dev);
static int qlearning_manager_argmax(float *arr, size_t size);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void qlearning_manager_action_init(FAR struct devfreq_s *dev)
{
  FAR struct qlearning_manager_s *manager = dev->governor_data;
  FAR const uint32_t *table;
  int size = 0;

  for (table = dev->freq_table; *table != DEVFREQ_ENTRY_END; table++)
    {
      if (*table == DEVFREQ_ENTRY_INVALID)
        {
          continue;
        }

      manager->action[size++] = *table;
    }
}

static int qlearning_manager_argmax(float *arr, size_t size)
{
  int ret = 0;
  int i;

  if (size <= 0 || size > MAX_LAYER_SIZE)
    {
      return -EINVAL;
    }

  for (i = 1; i < size; i++)
    {
      if (arr[ret] < arr[i])
        {
          ret = i;
        }
    }

  return ret;
}

static int qlearning_manager_worker(FAR struct devfreq_s *dev)
{
  FAR struct qlearning_manager_s *manager = dev->governor_data;
  FAR struct qlearning_params_s *params = manager->params;
  FAR struct layer_s *layer;
  int layer_out;
  int action;
  int freq;
  int ret = OK;

  if (!(manager->flags & QLEARNING_FLAG_RUNNING))
    {
      return ret;
    }

  if (!(manager->flags & QLEARNING_FLAG_INIT))
    {
      qlearning_manager_action_init(dev);

      manager->wait = TICK_PER_MSEC * CONFIG_DEVFREQ_QLEARNING_WAIT_TIME;
      manager->flags |= QLEARNING_FLAG_INIT;
      goto out;
    }

  freq = devfreq_get_frequency(dev);
  if (freq < CONFIG_DEVFREQ_QLEARNING_MIN_FREQ)
    {
      goto out;
    }

  manager->lh->ops->fetch(NULL, manager->perf_data, MAX_LAYER_SIZE);
  ret = qlearning_network_forward(&params->net, manager->perf_data);
  if (ret < 0)
    {
      return ret;
    }

  layer_out = params->net.layers_num - 1;
  layer = &params->net.layers[layer_out];
  action = qlearning_manager_argmax(layer->output, layer->output_size) +
           CONFIG_DEVFREQ_QLEARNING_ACTION_OFFSET;
  manager->target_freq = manager->action[action];
  devfreq_qos_update_request(dev, manager->req, dev->min, dev->max);

out:
  if (manager->flags & QLEARNING_FLAG_RUNNING)
    {
      work_queue(HPWORK, &manager->qlearning_manager_work,
                 (worker_t)qlearning_manager_worker, dev,
                 manager->wait);
    }

  return ret;
}

static void qlearning_manager_callback(FAR struct pm_callback_s *cb,
                                       int domain, enum pm_state_e pmstate)
{
  FAR struct devfreq_s *dev;

  dev = devfreq_find_by_name(CONFIG_CPUFREQ_DEVICE_NAME);
  if (pmstate == PM_NORMAL)
    {
      qlearning_manager_start(dev);
    }
  else if (pmstate == PM_SLEEP)
    {
      qlearning_manager_end(dev);
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: devfreq_manager_init
 *
 * Description:
 *   init qlearning
 *
 * Input Parameters:
 *   dev - devfreq device
 *
 * Returned Value:
 *   zero on success, a negated errno value on failure.
 *
 ****************************************************************************/

int qlearning_manager_init(FAR struct devfreq_s *dev)
{
  FAR struct qlearning_manager_s *manager;

  manager              = &g_qlearning_manager;
  dev->governor_data   = manager;
  manager->cb.notify   = qlearning_manager_callback;
  manager->wait        = TICK_PER_MSEC *
                         CONFIG_DEVFREQ_QLEARNING_INIT_WAIT_TIME;
  manager->target_freq = CONFIG_DEVFREQ_QLEARNING_INIT_FREQ;
  manager->req         = devfreq_qos_add_request(dev, dev->min, dev->max);

  return pm_domain_register(PM_IDLE_DOMAIN, &manager->cb);
}

/****************************************************************************
 * Name: devfreq_manager_exit
 *
 * Description:
 *   exit qlearning
 *
 * Input Parameters:
 *   dev - devfreq device
 *
 * Returned Value:
 *   zero on success, a negated errno value on failure.
 *
 ****************************************************************************/

int qlearning_manager_exit(FAR struct devfreq_s *dev)
{
  FAR struct qlearning_manager_s *manager = dev->governor_data;
  devfreq_qos_remove_request(dev, manager->req);
  return pm_domain_unregister(PM_IDLE_DOMAIN, &manager->cb);
}

/****************************************************************************
 * Name: devfreq_manager_start
 *
 * Description:
 *   start qlearning
 *
 * Input Parameters:
 *   dev - devfreq device
 *
 * Returned Value:
 *   zero on success, a negated errno value on failure.
 *
 ****************************************************************************/

int qlearning_manager_start(FAR struct devfreq_s *dev)
{
  FAR struct qlearning_manager_s *manager = dev->governor_data;

  if (!(manager->flags & QLEARNING_FLAG_INIT))
    {
      return 0;
    }

  if (manager->flags & QLEARNING_FLAG_RUNNING)
    {
      return 0;
    }

  work_queue(LPWORK, &manager->qlearning_manager_work,
             (worker_t)qlearning_manager_worker, dev,
             manager->wait);
  manager->flags |= QLEARNING_FLAG_RUNNING;

  return 0;
}

/****************************************************************************
 * Name: devfreq_manager_end
 *
 * Description:
 *   stop qlearning
 *
 * Input Parameters:
 *   dev - devfreq device
 *
 * Returned Value:
 *   zero on success, a negated errno value on failure.
 *
 ****************************************************************************/

int qlearning_manager_end(FAR struct devfreq_s *dev)
{
  FAR struct qlearning_manager_s *manager = dev->governor_data;

  if (!(manager->flags & QLEARNING_FLAG_INIT))
    {
      return 0;
    }

  if (!(manager->flags & QLEARNING_FLAG_RUNNING))
    {
      return 0;
    }

  work_cancel(LPWORK, &manager->qlearning_manager_work);
  manager->flags &= ~QLEARNING_FLAG_RUNNING;

  return 0;
}

/****************************************************************************
 * Name: qlearning_manager_limit
 *
 * Description:
 *   get target frequency
 *
 * Input Parameters:
 *   dev - devfreq device
 *
 * Returned Value:
 *   target frequency
 *
 ****************************************************************************/

uint32_t qlearning_manager_limit(FAR struct devfreq_s *dev)
{
  FAR struct qlearning_manager_s *manager = dev->governor_data;
  uint32_t freq = manager->target_freq;

  if (freq > dev->max)
    {
      freq = dev->max;
    }

  if (freq < dev->min)
    {
      freq = dev->min;
    }

  return freq;
}

/****************************************************************************
 * Name: devfreq_qlearning_set
 *
 * Description:
 *   provide lowerhalf ops and mmap pointer of qlearning params file, please
 *   ensure mmap pointer can be cast into struct qlearning_params_s pointer.
 *
 * Input Parameters:
 *   lh - lowerhalf ops
 *   priv - mmap pointer of qlearning params file
 *
 * Returned Value:
 *   zero on success, a negated errno value on failure.
 *
 ****************************************************************************/

int devfreq_qlearning_set(FAR struct qlearning_lowerhalf_s *lh,
                          FAR void *priv)
{
  g_qlearning_manager.lh = lh;
  g_qlearning_manager.params = priv;
  return 0;
}
