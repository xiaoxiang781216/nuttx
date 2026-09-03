/****************************************************************************
 * drivers/analog/npcm_adc.c
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

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <assert.h>
#include <debug.h>

#include <nuttx/analog/adc.h>
#include <nuttx/analog/ioctl.h>
#include <nuttx/analog/npcm_adc.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Register offsets (see docs/silkit/common/device-register-spec.md §1). */

#define NPCM_ADC_CON        0x00      /* Control register              */
#define NPCM_ADC_DATA       0x04      /* Conversion result (10-bit RO) */

/* ADC_CON bit fields. */

#define ADC_CON_MUX_SHIFT   24
#define ADC_CON_MUX_MASK    (0xfu << ADC_CON_MUX_SHIFT)
#define ADC_CON_INT_EN      (1u << 21) /* Data-ready interrupt enable   */
#define ADC_CON_REFSEL      (1u << 19) /* 1 = internal 2V reference     */
#define ADC_CON_INT         (1u << 18) /* Data ready (write 1 to clear) */
#define ADC_CON_EN          (1u << 17) /* Module enable                 */
#define ADC_CON_RST         (1u << 16) /* Write 1: soft reset           */
#define ADC_CON_CONV        (1u << 13) /* Write 1: start one conversion */

#define NPCM_ADC_DATA_MASK  0x3ff      /* 10-bit result                 */

/* Bound on the busy-wait for a single conversion.  On the QEMU model the
 * conversion completes on the next tick (CONV self-clears essentially
 * immediately), so this only guards against a wedged/absent device.
 */

#define NPCM_ADC_CONV_TIMEOUT  100000

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct npcm_adc_dev_s
{
  FAR const struct adc_callback_s *cb;      /* Upper-half callback         */
  uintptr_t                        base;    /* MMIO register base          */
  int                              irq;     /* Data-ready IRQ (unused yet) */

  /* Channels sampled on each ANIOC_TRIGGER.  Bit n selects channel n;
   * defaults to all NPCM_ADC_NCHANNELS channels.
   */

  uint8_t                          chanstrobed;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int  npcm_adc_bind(FAR struct adc_dev_s *dev,
                          FAR const struct adc_callback_s *callback);
static void npcm_adc_reset(FAR struct adc_dev_s *dev);
static int  npcm_adc_setup(FAR struct adc_dev_s *dev);
static void npcm_adc_shutdown(FAR struct adc_dev_s *dev);
static void npcm_adc_rxint(FAR struct adc_dev_s *dev, bool enable);
static int  npcm_adc_ioctl(FAR struct adc_dev_s *dev, int cmd,
                          unsigned long arg);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct adc_ops_s g_adcops =
{
  npcm_adc_bind,      /* ao_bind     */
  npcm_adc_reset,     /* ao_reset    */
  npcm_adc_setup,     /* ao_setup    */
  npcm_adc_shutdown,  /* ao_shutdown */
  npcm_adc_rxint,     /* ao_rxint    */
  npcm_adc_ioctl      /* ao_ioctl    */
};

static struct npcm_adc_dev_s g_adcpriv;

static struct adc_dev_s g_adcdev =
{
  &g_adcops,     /* ad_ops  */
  &g_adcpriv     /* ad_priv */
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* Direct MMIO accessors.  Kept local so the driver is architecture neutral
 * (armv8-a / armv8-r) and does not depend on any chip-specific getreg macro.
 */

static inline uint32_t npcm_adc_getreg(uintptr_t base, unsigned int off)
{
  return *(volatile uint32_t *)(base + off);
}

static inline void npcm_adc_putreg(uintptr_t base, unsigned int off,
                                   uint32_t val)
{
  *(volatile uint32_t *)(base + off) = val;
}

/****************************************************************************
 * Name: npcm_adc_readchannel
 *
 * Description:
 *   Select the channel, start a single conversion and return the 10-bit
 *   result.  Polls CONV (hardware self-clears on completion).
 *
 ****************************************************************************/

static int npcm_adc_readchannel(FAR struct npcm_adc_dev_s *priv,
                                uint8_t channel, FAR int32_t *data)
{
  uint32_t con;
  int i;

  if (channel >= NPCM_ADC_NCHANNELS)
    {
      return -EINVAL;
    }

  /* Read-modify-write CON: keep REFSEL/DIV/EN, program MUX, ensure enabled
   * and start a conversion.  Clear any stale data-ready status (INT is
   * write-1-to-clear).
   */

  con  = npcm_adc_getreg(priv->base, NPCM_ADC_CON);
  con &= ~ADC_CON_MUX_MASK;
  con |= ((uint32_t)channel << ADC_CON_MUX_SHIFT) & ADC_CON_MUX_MASK;
  con |= ADC_CON_EN | ADC_CON_CONV | ADC_CON_INT;
  npcm_adc_putreg(priv->base, NPCM_ADC_CON, con);

  /* Wait for CONV to self-clear (conversion done). */

  for (i = 0; i < NPCM_ADC_CONV_TIMEOUT; i++)
    {
      if ((npcm_adc_getreg(priv->base, NPCM_ADC_CON) & ADC_CON_CONV) == 0)
        {
          *data = (int32_t)(npcm_adc_getreg(priv->base, NPCM_ADC_DATA) &
                            NPCM_ADC_DATA_MASK);
          return OK;
        }
    }

  aerr("NPCM ADC: conversion timeout on channel %u\n",
       (unsigned int)channel);
  return -ETIMEDOUT;
}

/****************************************************************************
 * Name: npcm_adc_bind
 ****************************************************************************/

static int npcm_adc_bind(FAR struct adc_dev_s *dev,
                        FAR const struct adc_callback_s *callback)
{
  FAR struct npcm_adc_dev_s *priv =
    (FAR struct npcm_adc_dev_s *)dev->ad_priv;

  DEBUGASSERT(priv != NULL);
  priv->cb = callback;
  return OK;
}

/****************************************************************************
 * Name: npcm_adc_reset
 ****************************************************************************/

static void npcm_adc_reset(FAR struct adc_dev_s *dev)
{
  FAR struct npcm_adc_dev_s *priv =
    (FAR struct npcm_adc_dev_s *)dev->ad_priv;

  /* Issue a soft reset, then enable the module with the internal 2V
   * reference (matches the device reset defaults).
   */

  npcm_adc_putreg(priv->base, NPCM_ADC_CON, ADC_CON_RST);
  npcm_adc_putreg(priv->base, NPCM_ADC_CON, ADC_CON_REFSEL | ADC_CON_EN);

  priv->chanstrobed = (1u << NPCM_ADC_NCHANNELS) - 1;
}

/****************************************************************************
 * Name: npcm_adc_setup
 ****************************************************************************/

static int npcm_adc_setup(FAR struct adc_dev_s *dev)
{
  FAR struct npcm_adc_dev_s *priv =
    (FAR struct npcm_adc_dev_s *)dev->ad_priv;
  uint32_t con = npcm_adc_getreg(priv->base, NPCM_ADC_CON);

  npcm_adc_putreg(priv->base, NPCM_ADC_CON, con | ADC_CON_EN);
  return OK;
}

/****************************************************************************
 * Name: npcm_adc_shutdown
 ****************************************************************************/

static void npcm_adc_shutdown(FAR struct adc_dev_s *dev)
{
  FAR struct npcm_adc_dev_s *priv =
    (FAR struct npcm_adc_dev_s *)dev->ad_priv;
  uint32_t con = npcm_adc_getreg(priv->base, NPCM_ADC_CON);

  npcm_adc_putreg(priv->base, NPCM_ADC_CON, con & ~ADC_CON_EN);
}

/****************************************************************************
 * Name: npcm_adc_rxint
 *
 * Description:
 *   Conversions are driven by polling in this implementation, so there is
 *   no interrupt to enable/disable.  Present for upper-half compatibility.
 *
 ****************************************************************************/

static void npcm_adc_rxint(FAR struct adc_dev_s *dev, bool enable)
{
}

/****************************************************************************
 * Name: npcm_adc_ioctl
 ****************************************************************************/

static int npcm_adc_ioctl(FAR struct adc_dev_s *dev, int cmd,
                         unsigned long arg)
{
  FAR struct npcm_adc_dev_s *priv =
    (FAR struct npcm_adc_dev_s *)dev->ad_priv;
  int ret = OK;

  switch (cmd)
    {
      case ANIOC_TRIGGER:
        {
          int32_t data;
          int i;

          for (i = 0; i < NPCM_ADC_NCHANNELS && ret == OK; i++)
            {
              if (((priv->chanstrobed >> i) & 1u) == 0)
                {
                  continue;
                }

              ret = npcm_adc_readchannel(priv, (uint8_t)i, &data);
              if (ret == OK && priv->cb != NULL &&
                  priv->cb->au_receive != NULL)
                {
                  ret = priv->cb->au_receive(dev, (uint8_t)i, data);
                }
            }
        }
        break;

      default:
        ret = -ENOTTY;
        break;
    }

  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: npcm_adc_initialize
 ****************************************************************************/

FAR struct adc_dev_s *npcm_adc_initialize(uintptr_t base, int irq)
{
  FAR struct npcm_adc_dev_s *priv =
    (FAR struct npcm_adc_dev_s *)g_adcdev.ad_priv;

  if (base == 0)
    {
      return NULL;
    }

  priv->cb          = NULL;
  priv->base        = base;
  priv->irq         = irq;
  priv->chanstrobed = (1u << NPCM_ADC_NCHANNELS) - 1;

  return &g_adcdev;
}
