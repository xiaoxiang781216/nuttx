/****************************************************************************
 * include/nuttx/serial/gsmmux.h
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

#ifndef __INCLUDE_NUTTX_SERIAL_GSMMUX_H
#define __INCLUDE_NUTTX_SERIAL_GSMMUX_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdbool.h>
#include <stddef.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct gsm_config
{
  unsigned int adaption;
  unsigned int encapsulation;
  unsigned int initiator;
  unsigned int t1;
  unsigned int t2;
  unsigned int t3;
  unsigned int n2;
  unsigned int mru;
  unsigned int mtu;
  unsigned int k;
  unsigned int i;
};

struct cmux_s;

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef __cplusplus
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

/****************************************************************************
 * Name: cmux_tty_initialize
 ****************************************************************************/

int cmux_tty_initialize(FAR struct cmux_s *cmux, FAR const char *path);

/****************************************************************************
 * Name: uart_cmux_register
 ****************************************************************************/

FAR struct cmux_s *uart_cmux_register(FAR const char *devname,
                                      FAR const struct gsm_config *config,
                                      FAR const char *devlist[]);

/****************************************************************************
 * Name: uart_cmux_initialize
 ****************************************************************************/

int uart_cmux_initialize(void);

#undef EXTERN
#ifdef __cplusplus
}
#endif

#endif /* __INCLUDE_NUTTX_SERIAL_GSMMUX_H */

