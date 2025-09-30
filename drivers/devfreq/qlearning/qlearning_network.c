/****************************************************************************
 * drivers/devfreq/qlearning/qlearning_network.c
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

#include <errno.h>
#include <math.h>

#include "qlearning_network.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: qlearning_network_forward()
 *
 * Description:
 *   forward pass through neural network
 *
 * Input Parameters:
 *   net - struct network_s, including structure info and weights.
 *   input - neural network input, its size can be get from net.
 *
 * Returned Value:
 *   Zero on success or a negated errno value on failure.
 *
 ****************************************************************************/

int qlearning_network_forward(FAR struct network_s *net, FAR float *input)
{
  FAR struct layer_s *layer;
  FAR float *weights;
  FAR float *biases;
  FAR float *output;
  int layers_num = net->layers_num;
  int layer_idx;
  int output_size;
  int input_size;
  int node_out;
  int node_in;
  float sum;

  if (!net || !input)
    {
      return -EINVAL;
    }

  for (layer_idx = 0; layer_idx < layers_num; layer_idx++)
    {
      layer       = &net->layers[layer_idx];
      output_size = layer->output_size;
      input_size  = layer->input_size;
      weights     = layer->weights;
      biases      = layer->biases;
      output      = layer->output;

      for (node_out = 0; node_out < output_size; node_out++)
        {
          sum = biases[node_out];
          for (node_in = 0; node_in < input_size; node_in++)
            {
              sum += input[node_in] *
                     weights[node_out * input_size + node_in];
            }

          if (layer_idx < layers_num - 1)
            {
              if (layer->activation == RELU)
                {
                  sum = fmaxf(0.0f, sum);
                }
              else if (layer->activation == SIGMOID)
                {
                  sum = 1.0f / (1.0f + expf(-sum));
                }
              else if (layer->activation == TANH)
                {
                  sum = tanhf(sum);
                }
              else
                {
                  return -EINVAL;
                }
            }

          output[node_out] = sum;
        }

      input = output;
    }

  return 0;
}
