/****************************************************************************
 * include/nuttx/devfreq/qlearning.h
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

#ifndef __INCLUDE_NUTTX_DEVFREQ_QLEARNING_H
#define __INCLUDE_NUTTX_DEVFREQ_QLEARNING_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <sys/types.h>

/****************************************************************************
 * Public Type
 ****************************************************************************/

struct qlearning_lowerhalf_s
{
  FAR struct qlearning_operations_s *ops;
  FAR void *priv;
};

struct qlearning_operations_s
{
  CODE ssize_t (*fetch)(FAR void *priv, FAR float *data, size_t size);
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

int devfreq_qlearning_set(FAR struct qlearning_lowerhalf_s *lh,
                          FAR void *priv);

#endif /* __INCLUDE_NUTTX_DEVFREQ_QLEARNING_H */
