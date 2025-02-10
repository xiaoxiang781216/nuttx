/****************************************************************************
 * drivers/safety/mpu_monitor.c
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

#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <debug.h>

#include <nuttx/irq.h>
#include <nuttx/safety/safety.h>
#include <nuttx/safety/mpu_monitor.h>

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* Private data structure for mpu monitor */

typedef struct
{
  FAR struct safety_lowerhalf_s lower;  /* Lower half driver structure */
  uintptr_t inject_address;             /* Inject address */
  int irq;                              /* MemManage fault irq */
  memfault_t memfault;                  /* Pointer to the input memfault handler */
  bool memfault_flag;                   /* memfault flag */
} mpu_monitor_priv_t;

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int mpu_monitor_setup(FAR struct safety_lowerhalf_s *lower);
static int mpu_monitor_shutdown(FAR struct safety_lowerhalf_s *lower);
static int mpu_monitor_inject(FAR struct safety_lowerhalf_s *lower,
                              FAR void *arg);
static int mpu_monitor_selftest(FAR struct safety_lowerhalf_s *lower);
static int mpu_memfault_handler(int irq, FAR void *context, FAR void *arg);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct safety_ops_s g_mpu_ops =
{
  .setup    = mpu_monitor_setup,
  .shutdown = mpu_monitor_shutdown,
  .inject   = mpu_monitor_inject,
  .selftest = mpu_monitor_selftest,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: mpu_memfault_handler
 *
 * Description:
 *   The MPU MemManage Fault handler in selftest.
 *
 * Input Parameters:
 *   irq      - The irq number
 *   context  - Pointer to the context of the interrupt
 *   arg      - Pointer to the arg of the interrupt
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure
 *
 ****************************************************************************/

static int mpu_memfault_handler(int irq, FAR void *context, FAR void *arg)
{
  FAR mpu_monitor_priv_t *priv = (FAR mpu_monitor_priv_t *)arg;

  DEBUGASSERT(priv);
  DEBUGASSERT(priv->memfault);

  priv->memfault_flag = true;

  /* set arg = 0xffffffff to skip MemManage Fault */

  priv->memfault(irq, context, (FAR void *)0xffffffff);

  return OK;
}

/****************************************************************************
 * Name: mpu_monitor_setup
 *
 * Description:
 *   Setup the MPU monitor and configure irq.
 *
 * Input Parameters:
 *   lower  - Pointer to the lower half driver structure
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure
 *
 ****************************************************************************/

static int mpu_monitor_setup(FAR struct safety_lowerhalf_s *lower)
{
  FAR mpu_monitor_priv_t *priv = (FAR mpu_monitor_priv_t *)lower;

  DEBUGASSERT(priv);

  irq_attach(priv->irq, mpu_memfault_handler, priv);

  return OK;
}

/****************************************************************************
 * Name: mpu_monitor_shutdown
 *
 * Description:
 *   Shutdown the MPU monitor.
 *
 * Input Parameters:
 *   lower  - Pointer to the lower half driver structure
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure
 *
 ****************************************************************************/

static int mpu_monitor_shutdown(FAR struct safety_lowerhalf_s *lower)
{
  FAR mpu_monitor_priv_t *priv = (FAR mpu_monitor_priv_t *)lower;

  DEBUGASSERT(priv);
  DEBUGASSERT(priv->memfault);

  irq_attach(priv->irq, priv->memfault, NULL);

  return OK;
}

/****************************************************************************
 * Name: mpu_monitor_inject
 *
 * Description:
 *   Inject a fault into the MPU monitor for testing purposes.
 *
 * Input Parameters:
 *   lower  - Pointer to the lower half driver structure
 *   arg    - Pointer to injection parameters
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure
 *
 ****************************************************************************/

static int mpu_monitor_inject(FAR struct safety_lowerhalf_s *lower,
                              FAR void *arg)
{
  FAR mpu_monitor_priv_t *priv = (FAR mpu_monitor_priv_t *)lower;
  FAR uint32_t *inject_value = (FAR uint32_t *)arg;

  DEBUGASSERT(priv);
  DEBUGASSERT(inject_value);

  /* generate a memmanage fault */

  *(FAR uintptr_t *)(priv->inject_address) = *inject_value;

  return OK;
}

/****************************************************************************
 * Name: mpu_monitor_selftest
 *
 * Description:
 *   Test that the MPU monitor works properly.
 *
 * Input Parameters:
 *   lower  - Pointer to the lower half driver structure
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure
 *
 ****************************************************************************/

static int mpu_monitor_selftest(FAR struct safety_lowerhalf_s *lower)
{
  FAR mpu_monitor_priv_t *priv = (FAR mpu_monitor_priv_t *)lower;

  DEBUGASSERT(priv);

  if (priv->memfault_flag)
    {
      priv->memfault_flag = false;
      return OK;
    }

  return ERROR;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: mpu_monitor_initialize
 *
 * Description:
 *   Initialize the mpu monitor
 *
 * Input Parameters:
 *   inject_address - The inject address, which will cause
 *                    a memmanage fault when injected
 *   irq            - MemManage Fault irq number
 *   memfault       - Pointer to the input memmanage fault handler
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure
 *
 ****************************************************************************/

int mpu_monitor_initialize(uintptr_t inject_address, int irq,
                           memfault_t memfault)
{
  FAR mpu_monitor_priv_t *priv;
  int ret;

  DEBUGASSERT(memfault);

  priv = kmm_zalloc(sizeof(mpu_monitor_priv_t));
  if (!priv)
    {
      saerr("kmm_zalloc failed for mpu monitor!\n");
      return -ENOMEM;
    }

  priv->inject_address = inject_address;
  priv->memfault       = memfault;
  priv->irq            = irq;
  priv->lower.ops      = &g_mpu_ops;

  ret = safety_register(&priv->lower, SAFETY_MODULE_MPU, 0);
  if (ret != OK)
    {
      saerr("register mpu monitor failed!\n");
      kmm_free(priv);
      return ret;
    }

  return OK;
}
