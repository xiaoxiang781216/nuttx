/****************************************************************************
 * drivers/devfreq/qlearning/qlearning_manager.h
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

#ifndef __DRIVERS_DEVFREQ_QLEARNING_MANAGER_H
#define __DRIVERS_DEVFREQ_QLEARNING_MANAGER_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/devfreq.h>

/****************************************************************************
 * Public Functions Prototypes
 ****************************************************************************/

int qlearning_manager_init(FAR struct devfreq_s *dev);
int qlearning_manager_exit(FAR struct devfreq_s *dev);
int qlearning_manager_start(FAR struct devfreq_s *dev);
int qlearning_manager_end(FAR struct devfreq_s *dev);
uint32_t qlearning_manager_limit(FAR struct devfreq_s *dev);

#endif /* __DRIVERS_DEVFREQ_QLEARNING_MANAGER_H */
