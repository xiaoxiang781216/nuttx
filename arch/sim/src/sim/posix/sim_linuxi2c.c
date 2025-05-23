/****************************************************************************
 * arch/sim/src/sim/posix/sim_linuxi2c.c
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

#include <sys/types.h>
#include <sys/ioctl.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <linux/i2c-dev.h>
#include <linux/i2c.h>

#include "sim_i2c.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define ERROR(fmt, ...) \
        syslog(LOG_ERR, "sim_i2cbuslinux: " fmt "\n", ##__VA_ARGS__)
#define INFO(fmt, ...) \
        syslog(LOG_ERR, "sim_i2cbuslinux: " fmt "\n", ##__VA_ARGS__)
#define DEBUG(fmt, ...)

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct linux_i2cbus_master_s
{
  const struct i2c_ops_s *ops; /* I2C vtable */
  int file;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int linux_i2cbus_transfer(struct i2c_master_s *dev,
                                 struct i2c_msg_s *msgs, int count);
#ifdef CONFIG_I2C_RESET
static int linux_i2cbus_reset(struct i2c_master_s *dev);
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct i2c_ops_s i2c_linux_ops =
{
  .transfer = linux_i2cbus_transfer,
#ifdef CONFIG_I2C_RESET
  .reset = linux_i2cbus_reset,
#endif
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: linux_i2cbus_reset
 *
 * Description:
 *   Provide i2c reset
 *
 ****************************************************************************/

#ifdef CONFIG_I2C_RESET
static int linux_i2cbus_reset(struct i2c_master_s *dev)
{
  return -1; /* Not implemented */
}
#endif

/****************************************************************************
 * Name: linux_i2cbus_transfer
 *
 * Description:
 *   Provide i2c transfer
 *
 ****************************************************************************/

static int linux_i2cbus_transfer(struct i2c_master_s *dev,
                                 struct i2c_msg_s *msgs, int count)
{
  struct linux_i2cbus_master_s *priv = (struct linux_i2cbus_master_s *)dev;
  struct i2c_rdwr_ioctl_data ioctl_data;
  struct i2c_msg host_msgs[count];
  int idx;

  if (count > I2C_RDRW_IOCTL_MAX_MSGS)
    {
      return -EINVAL;
    }

  memset(&ioctl_data, 0, sizeof(ioctl_data));
  ioctl_data.nmsgs = count;
  ioctl_data.msgs = host_msgs;
  for (idx = 0; idx < count; idx++)
    {
      if (msgs[idx].addr != msgs[0].addr)
        {
          return -ENOTSUP;
        }

      ioctl_data.msgs[idx].addr = msgs[idx].addr;
      ioctl_data.msgs[idx].buf = msgs[idx].buffer;
      ioctl_data.msgs[idx].len = msgs[idx].length;
      ioctl_data.msgs[idx].flags = 0;
      if (msgs[idx].flags & NUTTX_I2C_M_READ)
        {
           ioctl_data.msgs[idx].flags |= I2C_M_RD;
        }

      if (msgs[idx].flags & NUTTX_I2C_M_TEN)
        {
          ioctl_data.msgs[idx].flags |= I2C_M_TEN;
        }

      if (msgs[idx].flags & NUTTX_I2C_M_NOSTART)
        {
          ioctl_data.msgs[idx].flags |= I2C_M_NOSTART;
        }
    }

  return ioctl(priv->file, I2C_RDWR, &ioctl_data);
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
  struct linux_i2cbus_master_s *priv;
  char filename[20];

  priv = malloc(sizeof(*priv));
  if (priv == NULL)
    {
      ERROR("Failed to allocate private i2c master driver");
      return NULL;
    }

  snprintf(filename, sizeof(filename), "/dev/i2c-%d", bus);
  priv->file = open(filename, O_RDWR);
  if (priv->file < 0)
    {
      ERROR("Failed to open %s: %d", filename, priv->file);
      free(priv);
      return NULL;
    }

  priv->ops = &i2c_linux_ops;
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
  struct linux_i2cbus_master_s *priv = (struct linux_i2cbus_master_s *)dev;
  if (priv->file >= 0)
    {
      close(priv->file);
    }

  free(priv);
  return 0;
}
