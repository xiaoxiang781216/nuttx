/****************************************************************************
 * drivers/devfreq/qlearning/qlearning_network.h
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

#ifndef __DRIVER_DEVFREQ_QLEARNING_NETWORK_H
#define __DRIVER_DEVFREQ_QLEARNING_NETWORK_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MAX_LAYER_SIZE    CONFIG_DEVFREQ_QLEARNING_NEURONS_PER_LAYER
#define MAX_LAYERS_NUM    CONFIG_DEVFREQ_QLEARNING_NUM_LAYERS

#define RELU              0
#define SIGMOID           1
#define TANH              2

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Fully Connected Layer */

struct layer_s
{
  uint32_t input_size;
  uint32_t output_size;
  uint32_t activation;
  float weights[MAX_LAYER_SIZE * MAX_LAYER_SIZE];
  float biases[MAX_LAYER_SIZE];
  float output[MAX_LAYER_SIZE];
};

/* Neural Networks */

struct network_s
{
  uint32_t layers_num;
  struct layer_s layers[MAX_LAYERS_NUM];
};

struct qlearning_params_s
{
  struct network_s net;
  float mean[MAX_LAYER_SIZE];
  float std[MAX_LAYER_SIZE];
};

/****************************************************************************
 * Public Functions Prototypes
 ****************************************************************************/

int qlearning_network_forward(FAR struct network_s *net, FAR float *input);

#endif /* __DRIVER_DEVFREQ_QLEARNING_NETWORK_H */
