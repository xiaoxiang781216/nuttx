/****************************************************************************
 * include/nuttx/ioexpander/ioe_mcal.h
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

#ifndef __INCLUDE_NUTTX_IOEXPANDER_IOE_MCAL_H
#define __INCLUDE_NUTTX_IOEXPANDER_IOE_MCAL_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_MCAL_DIO

#include <stddef.h>
#include <stdint.h>

#include <nuttx/ioexpander/ioexpander.h>

#include "Dio.h"
#include "Port.h"

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct mcal_dio_pin_s
{
  Dio_ChannelType channel_id;   /* MCAL DIO channel (read/write/flip) */
  Port_PinType port_pin;        /* MCAL PORT pin (direction change) */
  uint8_t pintype;              /* GPIO_OUTPUT_PIN / GPIO_INPUT_PIN */
};

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
 * Name: mcal_dio_initialize
 *
 * Description:
 *   Create an IOExpander instance backed by AUTOSAR MCAL DIO/PORT.
 *   DIO has no init function per AUTOSAR spec [SWS_Dio_00063]; Port_Init
 *   must be called before this function.
 *
 * Input Parameters:
 *   pins  - Board-defined pin configuration array
 *   npins - Number of entries in pins array (max CONFIG_IOEXPANDER_NPINS)
 *
 * Returned Value:
 *   Non-NULL IOExpander device pointer on success; NULL on failure.
 *
 ****************************************************************************/

FAR struct ioexpander_dev_s *
mcal_dio_initialize(FAR const struct mcal_dio_pin_s *pins, uint8_t npins);

#undef EXTERN
#ifdef __cplusplus
}
#endif

#endif /* CONFIG_MCAL_DIO */
#endif /* __INCLUDE_NUTTX_IOEXPANDER_IOE_MCAL_H */
