/****************************************************************************
 * drivers/aie/ethosu/ethosu_lowerhalf.c
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

#include <errno.h>
#include <debug.h>

#include <nuttx/aie/ethosu_aie.h>
#include <nuttx/aie/ai_engine.h>
#include <nuttx/kmalloc.h>
#include <nuttx/nuttx.h>
#include <nuttx/irq.h>
#include <nuttx/arch.h>

/* Include external ethos-u-core-driver headers */
#include <ethosu_driver.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct ethosu_lowerhalf_s
{
  struct aie_lowerhalf_s lower;                /* Common lower half interface */
  struct ethosu_driver drv;                    /* Ethos-U driver instance */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int ethosu_aie_init(FAR struct aie_lowerhalf_s *lower,
                           uintptr_t model);
static int ethosu_aie_deinit(FAR struct aie_lowerhalf_s *lower, int id);
static int ethosu_aie_feed_input(FAR struct aie_lowerhalf_s *lower, int id,
                                 uintptr_t input);
static int ethosu_aie_get_output(FAR struct aie_lowerhalf_s *lower, int id,
                                 uintptr_t output);

/* Local interrupt handler */

static int ethosu_npu_irq_handler(int irq, FAR void *context, FAR void *arg);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct aie_ops_s ethosu_aie_ops =
{
  .init       = ethosu_aie_init,
  .deinit     = ethosu_aie_deinit,
  .feed_input = ethosu_aie_feed_input,
  .get_output = ethosu_aie_get_output,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: ethosu_npu_irq_handler
 *
 * Description:
 *   NPU interrupt handler. This function is called when the NPU
 *   generates an interrupt, typically when an inference completes.
 *
 * Input Parameters:
 *   irq     - The interrupt number that triggered this handler
 *   context - Architecture-specific interrupt context information
 *   arg     - User-provided argument (ethosu_driver pointer)
 *
 * Returned Value:
 *   OK (0) - interrupt was handled
 *
 ****************************************************************************/

static int ethosu_npu_irq_handler(int irq, FAR void *context, FAR void *arg)
{
  FAR struct ethosu_driver *drv = (FAR struct ethosu_driver *)arg;

  UNUSED(irq);
  UNUSED(context);

  /* Call the ethos-u-core-driver interrupt handler */

  ethosu_irq_handler(drv);

  return OK;
}

/****************************************************************************
 * Name: ethosu_aie_init
 *
 * Description:
 *   Initialize the Ethos-U driver for AI inference operations.
 *
 * Input Parameters:
 *   lower - Pointer to the lower half driver structure.
 *   model - Model handle (currently unused in simplified implementation).
 *           Reserved for future session management features.
 *
 * Returned Value:
 *   OK (0)   - on success; a negated errno value on failure:
 *   -EINVAL  - Invalid input parameter (lower is NULL)
 *
 * Assumptions:
 *
 ****************************************************************************/

static int ethosu_aie_init(FAR struct aie_lowerhalf_s *lower,
                           uintptr_t model)
{
  UNUSED(lower);
  UNUSED(model);

  return OK;
}

/****************************************************************************
 * Name: ethosu_aie_deinit
 *
 * Description:
 *   Deinitialize the Ethos-U driver and clean up resources.
 *
 * Input Parameters:
 *   lower - Pointer to the lower half driver structure.
 *   id    - Session ID (currently unused in simplified implementation).
 *           Reserved for future session management features.
 *
 * Returned Value:
 *   OK (0) on success; a negated errno value on failure:
 *   -EINVAL - Invalid input parameter (lower is NULL)
 *
 * Assumptions:
 *
 ****************************************************************************/

static int ethosu_aie_deinit(FAR struct aie_lowerhalf_s *lower, int id)
{
  UNUSED(lower);
  UNUSED(id);

  return OK;
}

/****************************************************************************
 * Name: ethosu_aie_feed_input
 *
 * Description:
 *   Placeholder function for feeding input to the Ethos-U driver.
 *   Currently not implemented and returns OK immediately.
 *   Reserved for future implementation of inference input handling.
 *
 * Input Parameters:
 *   lower - Pointer to the lower half driver structure (currently unused).
 *   id    - Session/inference ID (currently unused).
 *   input - Input data pointer (currently unused).
 *
 * Returned Value:
 *   OK (0) - Always returns success in current implementation.
 *
 ****************************************************************************/

static int ethosu_aie_feed_input(FAR struct aie_lowerhalf_s *lower, int id,
                                 uintptr_t input)
{
  UNUSED(lower);
  UNUSED(input);
  UNUSED(id);

  return OK;
}

/****************************************************************************
 * Name: ethosu_aie_get_output
 *
 * Description:
 *   Execute an Ethos-U NPU inference using the provided parameters.
 *   This function validates input parameters, reserves an Ethos-U driver
 *   instance, performs the inference via ethosu_invoke(), and releases
 *   the driver afterwards.
 *
 * Input Parameters:
 *   lower  - Pointer to the lower half driver structure (currently unused).
 *   id     - Session/inference ID (currently unused).
 *   output - Pointer to struct ethosu_aie_invoke_params containing:
 *            - custom_data_ptr: Command stream for NPU
 *            - custom_data_size: Size of command stream
 *            - base_addr: Array of base addresses for tensors
 *            - base_addr_size: Array of tensor sizes
 *            - num_base_addr: Number of base addresses
 *
 * Returned Value:
 *   OK (0) on success; a negated errno value on failure:
 *   -EINVAL - Invalid input parameters
 *   -EBUSY  - Failed to reserve Ethos-U driver
 *   Other negative values from ethosu_invoke() on inference failure
 *
 ****************************************************************************/

static int ethosu_aie_get_output(FAR struct aie_lowerhalf_s *lower, int id,
                                 uintptr_t output)
{
  FAR struct ethosu_aie_invoke_params *params;
  FAR struct ethosu_driver *drv;
  int ret;

  UNUSED(lower);
  UNUSED(id);

  params = (FAR struct ethosu_aie_invoke_params *)output;
  if (params == NULL)
    {
      return -EINVAL;
    }

  /* Validate input parameters */

  if (params->custom_data_ptr == NULL || params->custom_data_size <= 0)
    {
      _err("ERROR: custom_data_ptr is NULL or custom_data_size <= 0\n");
      return -EINVAL;
    }

  if (params->base_addr == NULL || params->base_addr_size == NULL)
    {
      _err("ERROR: base_addr or base_addr_size is NULL\n");
      return -EINVAL;
    }

  /* Acquire a driver handle and perform the inference */

  drv = ethosu_reserve_driver();
  if (drv == NULL)
    {
      _err("ERROR: Failed to reserve Ethos-U driver\n");
      return -EBUSY;
    }

  ret = ethosu_invoke(drv,
                      params->custom_data_ptr,
                      params->custom_data_size,
                      params->base_addr,
                      params->base_addr_size,
                      params->num_base_addr);
  if (ret < 0)
    {
      _err("ERROR: ethosu_invoke failed: %d\n", ret);
    }

  ethosu_release_driver(drv);

  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: ethosu_initialize
 *
 * Description:
 *   Initialize the Ethos-U lower half driver and register it with the
 *   AI Engine upper half. This function creates and initializes the
 *   Ethos-U driver instance, sets up the AIE operations interface, and
 *   registers the device with the system. It serves as the board-specific
 *   initialization entry point for the Ethos-U NPU driver.
 *
 * Input Parameters:
 *   devpath          - The full path to the driver device node to be
 *                      registered (e.g., "/dev/aie0"). Must not be NULL.
 *   base_address     - NPU register base address
 *   fast_memory      - Fast memory area (optional)
 *   fast_memory_size - Size of fast memory area in bytes
 *   secure_enable    - Configure NPU secure mode (1=secure, 0=non-secure)
 *   privilege_enable - Configure NPU privilege (1=privileged,
 *                      0=unprivileged)
 *   irq              - IRQ number for the NPU
 *
 * Returned Value:
 *   OK (0) on success; a negated errno value on failure:
 *   -EINVAL - Invalid input parameter (devpath is NULL)
 *   -ENOMEM - Failed to allocate memory for driver structures
 *   Other negative values from aie_register() on registration failure
 *
 * Assumptions:
 *   - Called once during board initialization
 *   - The Ethos-U hardware is available at the specified base address
 *   - The AIE upper half driver is available in the system
 *
 ****************************************************************************/

int ethosu_initialize(FAR const char *devpath,
                      FAR void *base_address,
                      FAR void *fast_memory,
                      size_t fast_memory_size,
                      bool secure_enable,
                      bool privilege_enable,
                      int irq)
{
  FAR struct ethosu_lowerhalf_s *priv;
  int ret;

  /* Check input parameters */

  if (devpath == NULL)
    {
      _err("ERROR: devpath is NULL\n");
      return -EINVAL;
    }

  /* Allocate memory for the lowerhalf structure */

  priv = kmm_zalloc(sizeof(struct ethosu_lowerhalf_s));
  if (priv == NULL)
    {
      _err("ERROR: Failed to allocate memory for Ethos-U driver\n");
      return -ENOMEM;
    }

  _info("Initializing Ethos-U AI Engine lower half\n");

  /* Initialize the device structure */

  priv->lower.ops = &ethosu_aie_ops;

  /* ethosu initialization */

  ret = ethosu_init(&priv->drv,
                    base_address,
                    fast_memory,
                    fast_memory_size,
                    secure_enable,
                    privilege_enable);
  if (ret < 0)
    {
      _err("ERROR: Failed to initialize Ethos-U driver: %d\n", ret);
      kmm_free(priv);
      return ret;
    }

  /* Register the driver for interrupt handling */

  ret = irq_attach(irq, ethosu_npu_irq_handler, &priv->drv);
  if (ret < 0)
    {
      _err("ERROR: Failed to register Ethos-U interrupt : %d\n", ret);
      ethosu_deinit(&priv->drv);
      kmm_free(priv);
      return ret;
    }

  up_enable_irq(irq);
  _info("Successfully registered handler for hardware IRQ %d\n", irq);

  /* Register the driver with the AI Engine upper half */

  ret = aie_register(devpath, &priv->lower);
  if (ret < 0)
    {
      _err("ERROR: Failed to register Ethos-U AI Engine "
           "driver: %d\n", ret);
      up_disable_irq(irq);
      irq_detach(irq);
      ethosu_deinit(&priv->drv);
      kmm_free(priv);
      return ret;
    }

  _info("Ethos-U AI Engine driver registered at %s\n", devpath);
  return OK;
}
