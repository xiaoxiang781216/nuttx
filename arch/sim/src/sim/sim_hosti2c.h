/****************************************************************************
 * arch/sim/src/sim/sim_hosti2c.h
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

#ifndef __ARCH_SIM_SRC_SIM_HOSTI2C_H
#define __ARCH_SIM_SRC_SIM_HOSTI2C_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#ifdef __SIM__
#include "config.h"
#endif

#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* NuttX msg flags (see i2c_master.h) */

#define HOST_I2C_M_READ       0x0001 /* Read data, from slave to master */
#define HOST_I2C_M_TEN        0x0002 /* Ten bit address */
#define HOST_I2C_M_NOSTOP     0x0040 /* Message should not end with a STOP */
#define HOST_I2C_M_NOSTART    0x0080 /* Message should not begin with a START */

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* NuttX i2c msg struct (see i2c_master.h) */

struct host_i2c_msg
{
  uint32_t frequency;         /* I2C frequency */
  uint16_t addr;              /* Slave address (7- or 10-bit) */
  uint16_t flags;             /* See I2C_M_* definitions */
  uint8_t *buffer;            /* Buffer to be transferred */
  ssize_t length;             /* Length of the buffer in bytes */
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

int host_i2cbus_transfer(int fd, struct host_i2c_msg *msgs, int count);
int host_i2c_open(const char *path);
void host_i2c_close(int fd);

#endif /* __ARCH_SIM_SRC_POSIX_SIM_I2C_H */
