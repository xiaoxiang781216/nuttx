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

#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <linux/ioctl.h>

#include "sim_internal.h"
#include "sim_hosti2c.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: host_i2cbus_transfer
 *
 * Description:
 *   Provide i2c transfer
 *
 ****************************************************************************/

int host_i2cbus_transfer(int fd, struct host_i2c_msg *msgs, int count)
{
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
      if (msgs[idx].flags & HOST_I2C_M_READ)
        {
           ioctl_data.msgs[idx].flags |= I2C_M_RD;
        }

      if (msgs[idx].flags & HOST_I2C_M_TEN)
        {
          ioctl_data.msgs[idx].flags |= I2C_M_TEN;
        }

      if (msgs[idx].flags & HOST_I2C_M_NOSTART)
        {
          ioctl_data.msgs[idx].flags |= I2C_M_NOSTART;
        }
    }

  return host_uninterruptible(ioctl, fd, I2C_RDWR, &ioctl_data);
}

int host_i2c_open(const char *path)
{
  return host_uninterruptible(open, path, O_RDWR);
}

void host_i2c_close(int fd)
{
  host_uninterruptible(close, fd);
}
