/****************************************************************************
 * drivers/devfreq/devfreq_qlearning.c
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
#include <nuttx/devfreq.h>
#include "qlearning/qlearning_manager.h"

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int devfreq_gov_qlearning_init(FAR struct devfreq_s *dev);
static int devfreq_gov_qlearning_exit(FAR struct devfreq_s *dev);
static int devfreq_gov_qlearning_start(FAR struct devfreq_s *dev);
static void devfreq_gov_qlearning_stop(FAR struct devfreq_s *dev);
static uint32_t devfreq_gov_qlearning_limit(FAR struct devfreq_s *dev);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct devfreq_governor_s g_devfreq_gov_qlearning =
{
  .name   = "qlearning",
  .init   = devfreq_gov_qlearning_init,
  .start  = devfreq_gov_qlearning_start,
  .stop   = devfreq_gov_qlearning_stop,
  .exit   = devfreq_gov_qlearning_exit,
  .limit  = devfreq_gov_qlearning_limit,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int devfreq_gov_qlearning_init(FAR struct devfreq_s *dev)
{
  return qlearning_manager_init(dev);
}

static int devfreq_gov_qlearning_start(FAR struct devfreq_s *dev)
{
  return qlearning_manager_start(dev);
}

static void devfreq_gov_qlearning_stop(FAR struct devfreq_s *dev)
{
  qlearning_manager_end(dev);
}

static int devfreq_gov_qlearning_exit(FAR struct devfreq_s *dev)
{
  return qlearning_manager_exit(dev);
}

static uint32_t devfreq_gov_qlearning_limit(FAR struct devfreq_s *dev)
{
  return qlearning_manager_limit(dev);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

FAR struct devfreq_governor_s *devfreq_qlearning(void)
{
  return &g_devfreq_gov_qlearning;
}
