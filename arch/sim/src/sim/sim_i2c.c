/****************************************************************************
 * arch/sim/src/sim/sim_i2c.c
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
#include <nuttx/i2c/i2c_master.h>

#include "sim_internal.h"
#include "sim_hosti2c.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct sim_i2cbus_master_s
{
  const struct i2c_ops_s *ops; /* I2C vtable */
  int file;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int sim_i2cbus_transfer(struct i2c_master_s *dev,
                                 struct i2c_msg_s *msgs, int count);
#ifdef CONFIG_I2C_RESET
static int sim_i2cbus_reset(struct i2c_master_s *dev);
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct i2c_ops_s g_sim_i2c_ops =
{
  .transfer = sim_i2cbus_transfer,
#ifdef CONFIG_I2C_RESET
  .reset = sim_i2cbus_reset,
#endif
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: sim_i2cbus_reset
 *
 * Description:
 *   Provide i2c reset
 *
 ****************************************************************************/

#ifdef CONFIG_I2C_RESET
static int sim_i2cbus_reset(struct i2c_master_s *dev)
{
  return -ENOSUP; /* Not implemented */
}
#endif

/****************************************************************************
 * Name: sim_i2cbus_transfer
 *
 * Description:
 *   Provide i2c transfer
 *
 ****************************************************************************/

static int sim_i2cbus_transfer(struct i2c_master_s *dev,
                               struct i2c_msg_s *msgs, int count)
{
  struct sim_i2cbus_master_s *priv = (struct sim_i2cbus_master_s *)dev;
  struct host_i2c_msg *host_msgs = (struct host_i2c_msg *)msgs;

  return host_i2cbus_transfer(priv->file, host_msgs, count);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: sim_i2cbus_initialize
 *
 * Description:
 *   Initialize one I2C bus
 *
 ****************************************************************************/

struct i2c_master_s *sim_i2cbus_initialize(int bus)
{
  struct sim_i2cbus_master_s *priv;
  char path[32];

  priv = kmm_zalloc(sizeof(*priv));
  if (priv == NULL)
    {
      i2cerr("Failed to allocate private i2c master driver");
      return NULL;
    }

  snprintf(path, sizeof(path), "/dev/i2c-%d", bus);
  priv->file = host_i2c_open(path);
  if (priv->file < 0)
    {
      i2cerr("Failed to open %s: %d", path, priv->file);
      kmm_free(priv);
      return NULL;
    }

  priv->ops = &g_sim_i2c_ops;
  return (struct i2c_master_s *)priv;
}

/****************************************************************************
 * Name: sim_i2cbus_uninitialize
 *
 * Description:
 *   Uninitialize an I2C bus
 *
 ****************************************************************************/

int sim_i2cbus_uninitialize(struct i2c_master_s *dev)
{
  struct sim_i2cbus_master_s *priv = (struct sim_i2cbus_master_s *)dev;
  if (priv->file >= 0)
    {
      host_i2c_close(priv->file);
    }

  kmm_free(priv);
  return 0;
}
