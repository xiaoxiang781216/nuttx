/****************************************************************************
 * drivers/ioexpander/ioe_mcal.c
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

#include <assert.h>
#include <errno.h>
#include <stdint.h>

#include <nuttx/kmalloc.h>
#include <nuttx/ioexpander/ioe_mcal.h>
#include <nuttx/ioexpander/ioexpander.h>

#if DIO_DEV_ERR_CHECK == STD_ON
#include "Det.h"
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef PORT_SET_PIN_DIRECTION_API
#  define PORT_SET_PIN_DIRECTION_API STD_OFF
#endif

#ifndef DIO_FLIP_CHANNEL_API
#  define DIO_FLIP_CHANNEL_API STD_OFF
#endif

#define MCAL_DIO_SID_INITIALIZE (0x80u)
#define MCAL_DIO_SID_READ       (0x81u)
#define MCAL_DIO_SID_WRITE      (0x82u)
#define MCAL_DIO_SID_DIRECTION  (0x83u)
#define MCAL_DIO_SID_TOGGLE     (0x84u)

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct mcal_dio_dev_s
{
  struct ioexpander_dev_s dev;
  FAR const struct mcal_dio_pin_s *pins;
  uint8_t npins;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int mcal_dio_direction(FAR struct ioexpander_dev_s *dev,
                              uint8_t pin, int direction);
static int mcal_dio_option(FAR struct ioexpander_dev_s *dev,
                           uint8_t pin, int opt, FAR void *val);
static int mcal_dio_writepin(FAR struct ioexpander_dev_s *dev,
                             uint8_t pin, bool value);
static int mcal_dio_readpin(FAR struct ioexpander_dev_s *dev,
                            uint8_t pin, FAR bool *value);
static int mcal_dio_toggle(FAR struct ioexpander_dev_s *dev,
                           uint8_t pin, FAR bool *value);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct ioexpander_ops_s g_mcal_dio_ops =
{
  .ioe_direction  = mcal_dio_direction,
  .ioe_option     = mcal_dio_option,
  .ioe_writepin   = mcal_dio_writepin,
  .ioe_readpin    = mcal_dio_readpin,
  .ioe_readbuf    = mcal_dio_readpin,
  .ioe_toggle     = mcal_dio_toggle,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int mcal_dio_direction(FAR struct ioexpander_dev_s *dev,
                              uint8_t pin, int direction)
{
  FAR struct mcal_dio_dev_s *priv = (FAR struct mcal_dio_dev_s *)dev;

  if (pin >= priv->npins)
    {
#if DIO_DEV_ERR_CHECK == STD_ON
      Det_ReportError(DIO_MODULE_ID, DIO_INSTANCE_ID,
                      MCAL_DIO_SID_DIRECTION,
                      DIO_E_PARAM_INVALID_CHANNEL_ID);
#endif
      return -EINVAL;
    }

#if PORT_SET_PIN_DIRECTION_API == STD_ON
  if (direction >= IOEXPANDER_DIRECTION_OUT)
    {
      Port_SetPinDirection(priv->pins[pin].port_pin, PORT_PIN_OUT);
    }
  else
    {
      Port_SetPinDirection(priv->pins[pin].port_pin, PORT_PIN_IN);
    }

  return OK;
#else
  return -ENOTSUP;
#endif
}

static int mcal_dio_option(FAR struct ioexpander_dev_s *dev,
                           uint8_t pin, int opt, FAR void *val)
{
  FAR struct mcal_dio_dev_s *priv = (FAR struct mcal_dio_dev_s *)dev;

  if (pin >= priv->npins)
    {
#if DIO_DEV_ERR_CHECK == STD_ON
      Det_ReportError(DIO_MODULE_ID, DIO_INSTANCE_ID,
                      MCAL_DIO_SID_WRITE,
                      DIO_E_PARAM_INVALID_CHANNEL_ID);
#endif
      return -EINVAL;
    }

  return -ENOTSUP;
}

static int mcal_dio_writepin(FAR struct ioexpander_dev_s *dev,
                             uint8_t pin, bool value)
{
  FAR struct mcal_dio_dev_s *priv = (FAR struct mcal_dio_dev_s *)dev;

  if (pin >= priv->npins)
    {
#if DIO_DEV_ERR_CHECK == STD_ON
      Det_ReportError(DIO_MODULE_ID, DIO_INSTANCE_ID,
                      MCAL_DIO_SID_WRITE,
                      DIO_E_PARAM_INVALID_CHANNEL_ID);
#endif
      return -EINVAL;
    }

  Dio_WriteChannel(priv->pins[pin].channel_id,
                   value ? STD_HIGH : STD_LOW);
  return OK;
}

static int mcal_dio_readpin(FAR struct ioexpander_dev_s *dev,
                            uint8_t pin, FAR bool *value)
{
  FAR struct mcal_dio_dev_s *priv = (FAR struct mcal_dio_dev_s *)dev;
  Dio_LevelType level;

  if (pin >= priv->npins)
    {
#if DIO_DEV_ERR_CHECK == STD_ON
      Det_ReportError(DIO_MODULE_ID, DIO_INSTANCE_ID,
                      MCAL_DIO_SID_READ,
                      DIO_E_PARAM_INVALID_CHANNEL_ID);
#endif
      return -EINVAL;
    }

  if (value == NULL)
    {
#if DIO_DEV_ERR_CHECK == STD_ON
      Det_ReportError(DIO_MODULE_ID, DIO_INSTANCE_ID,
                      MCAL_DIO_SID_READ,
                      DIO_E_PARAM_POINTER);
#endif
      return -EINVAL;
    }

  level = Dio_ReadChannel(priv->pins[pin].channel_id);
  *value = (level == STD_HIGH);
  return OK;
}

static int mcal_dio_toggle(FAR struct ioexpander_dev_s *dev,
                           uint8_t pin, FAR bool *value)
{
  FAR struct mcal_dio_dev_s *priv = (FAR struct mcal_dio_dev_s *)dev;
  Dio_LevelType level;

  if (pin >= priv->npins)
    {
#if DIO_DEV_ERR_CHECK == STD_ON
      Det_ReportError(DIO_MODULE_ID, DIO_INSTANCE_ID,
                      MCAL_DIO_SID_TOGGLE,
                      DIO_E_PARAM_INVALID_CHANNEL_ID);
#endif
      return -EINVAL;
    }

  if (value == NULL)
    {
#if DIO_DEV_ERR_CHECK == STD_ON
      Det_ReportError(DIO_MODULE_ID, DIO_INSTANCE_ID,
                      MCAL_DIO_SID_TOGGLE,
                      DIO_E_PARAM_POINTER);
#endif
      return -EINVAL;
    }

#if DIO_FLIP_CHANNEL_API == STD_ON
  level = Dio_FlipChannel(priv->pins[pin].channel_id);
#else
  level = Dio_ReadChannel(priv->pins[pin].channel_id);
  level = (level == STD_HIGH) ? STD_LOW : STD_HIGH;
  Dio_WriteChannel(priv->pins[pin].channel_id, level);
#endif

  *value = (level == STD_HIGH);
  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

FAR struct ioexpander_dev_s *
mcal_dio_initialize(FAR const struct mcal_dio_pin_s *pins, uint8_t npins)
{
  FAR struct mcal_dio_dev_s *priv;

  if (pins == NULL || npins == 0)
    {
#if DIO_DEV_ERR_CHECK == STD_ON
      Det_ReportError(DIO_MODULE_ID, DIO_INSTANCE_ID,
                      MCAL_DIO_SID_INITIALIZE,
                      DIO_E_PARAM_POINTER);
#endif
      return NULL;
    }

  priv = kmm_zalloc(sizeof(*priv));
  if (priv == NULL)
    {
#if DIO_DEV_ERR_CHECK == STD_ON
      Det_ReportRuntimeError(DIO_MODULE_ID, DIO_INSTANCE_ID,
                             MCAL_DIO_SID_INITIALIZE,
                             DIO_E_PARAM_POINTER);
#endif
      return NULL;
    }

  priv->dev.ops = &g_mcal_dio_ops;
  priv->pins = pins;
  priv->npins = npins;

  return &priv->dev;
}
