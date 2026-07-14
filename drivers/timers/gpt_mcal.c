/****************************************************************************
 * drivers/timers/gpt_mcal.c
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
#include <debug.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <nuttx/irq.h>
#include <nuttx/kmalloc.h>
#include <nuttx/spinlock.h>
#include <nuttx/timers/timer.h>
#include <nuttx/timers/gpt_mcal.h>

#include "Gpt_Cbk.h"

#if defined(GPT_DEV_ERROR_DETECT) && \
    GPT_DEV_ERROR_DETECT == STD_ON || \
    defined(GPT_RUNTIME_ERROR_DETECT) && \
    GPT_RUNTIME_ERROR_DETECT == STD_ON
#include "Det.h"
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MCAL_GPT_MAX_CHANNELS GPT_TOTAL_NO_OF_NORMAL_CHANNELS

#define MCAL_GPT_SID_INITIALIZE      (0x80u)
#define MCAL_GPT_SID_DISPATCH_NOTIFY (0x81u)

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct mcal_gpt_dev_s
{
  struct timer_lowerhalf_s lower;
  FAR const struct mcal_gpt_config_s *config;
  spinlock_t lock;
  uint32_t timeout_us;
  tccb_t callback;
  FAR void *callback_arg;
  bool started;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static void gpt_dispatch_notification(uint16_t channel);
static int mcal_gpt_start(FAR struct timer_lowerhalf_s *lower);
static int mcal_gpt_stop(FAR struct timer_lowerhalf_s *lower);
static int mcal_gpt_getstatus(FAR struct timer_lowerhalf_s *lower,
                              FAR struct timer_status_s *status);
static int mcal_gpt_settimeout(FAR struct timer_lowerhalf_s *lower,
                               uint32_t timeout);
static void mcal_gpt_setcallback(FAR struct timer_lowerhalf_s *lower,
                                 tccb_t callback, FAR void *arg);
static int mcal_gpt_maxtimeout(FAR struct timer_lowerhalf_s *lower,
                               FAR uint32_t *maxtimeout);
static int mcal_gpt_ioctl(FAR struct timer_lowerhalf_s *lower,
                          int cmd, unsigned long arg);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct timer_ops_s g_mcal_gpt_ops =
{
  .start       = mcal_gpt_start,
  .stop        = mcal_gpt_stop,
  .getstatus   = mcal_gpt_getstatus,
  .settimeout  = mcal_gpt_settimeout,
  .setcallback = mcal_gpt_setcallback,
  .ioctl       = mcal_gpt_ioctl,
  .maxtimeout  = mcal_gpt_maxtimeout,
};

static FAR struct mcal_gpt_dev_s *g_gpt_devs[MCAL_GPT_MAX_CHANNELS];

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static inline uint32_t gpt_us_to_ticks(uint32_t us, uint32_t freq_hz)
{
  return (uint32_t)(((uint64_t)us * freq_hz) / 1000000ull);
}

static inline uint32_t gpt_ticks_to_us(uint32_t ticks, uint32_t freq_hz)
{
  if (freq_hz == 0)
    {
      return 0;
    }

  return (uint32_t)(((uint64_t)ticks * 1000000ull) / freq_hz);
}

static int mcal_gpt_start(FAR struct timer_lowerhalf_s *lower)
{
  FAR struct mcal_gpt_dev_s *priv = (FAR struct mcal_gpt_dev_s *)lower;
  uint32_t ticks;
  irqstate_t flags;

  flags = spin_lock_irqsave(&priv->lock);

  if (priv->started)
    {
      spin_unlock_irqrestore(&priv->lock, flags);
      return OK;
    }

  if (priv->timeout_us == 0)
    {
      spin_unlock_irqrestore(&priv->lock, flags);
      return -EINVAL;
    }

  ticks = gpt_us_to_ticks(priv->timeout_us, priv->config->frequency_hz);
  if (ticks == 0)
    {
      ticks = 1;
    }

  if (ticks > priv->config->max_ticks)
    {
      ticks = priv->config->max_ticks;
    }

  Gpt_StartTimer(priv->config->channel, ticks);

  priv->started = true;
  spin_unlock_irqrestore(&priv->lock, flags);
  return OK;
}

static int mcal_gpt_stop(FAR struct timer_lowerhalf_s *lower)
{
  FAR struct mcal_gpt_dev_s *priv = (FAR struct mcal_gpt_dev_s *)lower;
  irqstate_t flags;

  flags = spin_lock_irqsave(&priv->lock);

  if (!priv->started)
    {
      spin_unlock_irqrestore(&priv->lock, flags);
      return OK;
    }

  Gpt_StopTimer(priv->config->channel);

  priv->started = false;
  spin_unlock_irqrestore(&priv->lock, flags);
  return OK;
}

static int mcal_gpt_getstatus(FAR struct timer_lowerhalf_s *lower,
                              FAR struct timer_status_s *status)
{
  FAR struct mcal_gpt_dev_s *priv = (FAR struct mcal_gpt_dev_s *)lower;
  irqstate_t flags;

  flags = spin_lock_irqsave(&priv->lock);

  status->flags = 0;

  if (priv->callback != NULL)
    {
      status->flags |= TCFLAGS_HANDLER;
    }

  status->timeout = priv->timeout_us;

  if (priv->started)
    {
      status->flags |= TCFLAGS_ACTIVE;
#if defined(GPT_TIME_REMAINING_API) && GPT_TIME_REMAINING_API == STD_ON
      status->timeleft = gpt_ticks_to_us(
          Gpt_GetTimeRemaining(priv->config->channel),
          priv->config->frequency_hz);
#else
      status->timeleft = 0;
#endif
    }
  else
    {
      status->timeleft = 0;
    }

  spin_unlock_irqrestore(&priv->lock, flags);
  return OK;
}

static int mcal_gpt_settimeout(FAR struct timer_lowerhalf_s *lower,
                               uint32_t timeout)
{
  FAR struct mcal_gpt_dev_s *priv = (FAR struct mcal_gpt_dev_s *)lower;
  uint32_t ticks;
  irqstate_t flags;

  flags = spin_lock_irqsave(&priv->lock);

  priv->timeout_us = timeout;

  if (priv->started)
    {
      Gpt_StopTimer(priv->config->channel);

      ticks = gpt_us_to_ticks(timeout, priv->config->frequency_hz);
      if (ticks == 0)
        {
          ticks = 1;
        }

      if (ticks > priv->config->max_ticks)
        {
          ticks = priv->config->max_ticks;
        }

      Gpt_StartTimer(priv->config->channel, ticks);
    }

  spin_unlock_irqrestore(&priv->lock, flags);
  return OK;
}

static void mcal_gpt_setcallback(FAR struct timer_lowerhalf_s *lower,
                                 tccb_t callback, FAR void *arg)
{
  FAR struct mcal_gpt_dev_s *priv = (FAR struct mcal_gpt_dev_s *)lower;
  irqstate_t flags;

  flags = spin_lock_irqsave(&priv->lock);

  priv->callback = callback;
  priv->callback_arg = arg;

  if (callback != NULL)
    {
      Gpt_EnableNotification(priv->config->channel);
    }
  else
    {
      Gpt_DisableNotification(priv->config->channel);
    }

  spin_unlock_irqrestore(&priv->lock, flags);
}

static int mcal_gpt_maxtimeout(FAR struct timer_lowerhalf_s *lower,
                               FAR uint32_t *maxtimeout)
{
  FAR struct mcal_gpt_dev_s *priv = (FAR struct mcal_gpt_dev_s *)lower;

  DEBUGASSERT(maxtimeout != NULL);

  *maxtimeout = gpt_ticks_to_us(priv->config->max_ticks,
                                priv->config->frequency_hz);
  return OK;
}

static int mcal_gpt_ioctl(FAR struct timer_lowerhalf_s *lower,
                          int cmd, unsigned long arg)
{
  return -ENOTTY;
}

static void gpt_dispatch_notification(uint16_t channel)
{
  FAR struct mcal_gpt_dev_s *priv;
  uint32_t next_interval_us = 0;

  if (channel >= MCAL_GPT_MAX_CHANNELS)
    {
#if defined(GPT_DEV_ERROR_DETECT) && GPT_DEV_ERROR_DETECT == STD_ON
      Det_ReportError(GPT_MODULE_ID, GPT_INSTANCE_ID,
                      MCAL_GPT_SID_DISPATCH_NOTIFY,
                      GPT_E_PARAM_CHANNEL);
#endif
      return;
    }

  priv = g_gpt_devs[channel];

  if (priv == NULL || priv->callback == NULL)
    {
      return;
    }

  priv->callback(&next_interval_us, priv->callback_arg);
}

#define GPT_NOTIFICATION(ch)                \
  void Gpt_Notification_Channel##ch(void)   \
  {                                         \
    gpt_dispatch_notification(ch);          \
  }

GPT_NOTIFICATION(0)
#if GPT_TOTAL_NO_OF_NORMAL_CHANNELS > 1
GPT_NOTIFICATION(1)
#endif
#if GPT_TOTAL_NO_OF_NORMAL_CHANNELS > 2
GPT_NOTIFICATION(2)
#endif
#if GPT_TOTAL_NO_OF_NORMAL_CHANNELS > 3
GPT_NOTIFICATION(3)
#endif
#if GPT_TOTAL_NO_OF_NORMAL_CHANNELS > 4
GPT_NOTIFICATION(4)
#endif
#if GPT_TOTAL_NO_OF_NORMAL_CHANNELS > 5
GPT_NOTIFICATION(5)
#endif
#if GPT_TOTAL_NO_OF_NORMAL_CHANNELS > 6
GPT_NOTIFICATION(6)
#endif
#if GPT_TOTAL_NO_OF_NORMAL_CHANNELS > 7
GPT_NOTIFICATION(7)
#endif
#undef GPT_NOTIFICATION

#if GPT_TOTAL_NO_OF_NORMAL_CHANNELS > 8
#  error "gpt_mcal.c: GPT_NOTIFICATION only covers channels 0-7"
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int mcal_gpt_initialize(FAR const Gpt_ConfigType *config,
                        FAR const struct mcal_gpt_config_s *devinfos,
                        size_t ndevinfos)
{
  FAR struct mcal_gpt_dev_s *priv;
  char devpath[16];
  size_t i;

  if (devinfos == NULL || ndevinfos == 0)
    {
#if defined(GPT_DEV_ERROR_DETECT) && GPT_DEV_ERROR_DETECT == STD_ON
      Det_ReportError(GPT_MODULE_ID, GPT_INSTANCE_ID,
                      MCAL_GPT_SID_INITIALIZE,
                      GPT_E_PARAM_POINTER);
#endif
      return -EINVAL;
    }

  if (config != NULL)
    {
      Gpt_Init(config);
    }

  for (i = 0; i < ndevinfos; i++)
    {
      if (devinfos[i].channel >= MCAL_GPT_MAX_CHANNELS)
        {
#if defined(GPT_DEV_ERROR_DETECT) && GPT_DEV_ERROR_DETECT == STD_ON
          Det_ReportError(GPT_MODULE_ID, GPT_INSTANCE_ID,
                          MCAL_GPT_SID_INITIALIZE,
                          GPT_E_PARAM_CHANNEL);
#endif
          continue;
        }

      priv = kmm_zalloc(sizeof(*priv));
      if (priv == NULL)
        {
#if defined(GPT_RUNTIME_ERROR_DETECT) && \
    GPT_RUNTIME_ERROR_DETECT == STD_ON
          Det_ReportRuntimeError(GPT_MODULE_ID, GPT_INSTANCE_ID,
                                 MCAL_GPT_SID_INITIALIZE,
                                 GPT_E_INIT_FAILED);
#endif
          return -ENOMEM;
        }

      priv->lower.ops = &g_mcal_gpt_ops;
      priv->config = &devinfos[i];
      spin_lock_init(&priv->lock);

      if (devinfos[i].isr != NULL && devinfos[i].irq >= 0)
        {
          irq_attach(devinfos[i].irq, devinfos[i].isr, priv);
          up_enable_irq(devinfos[i].irq);
        }

      snprintf(devpath, sizeof(devpath), "/dev/timer%d",
               devinfos[i].timer_index);

      if (timer_register(devpath, &priv->lower) == NULL)
        {
#if defined(GPT_RUNTIME_ERROR_DETECT) && \
    GPT_RUNTIME_ERROR_DETECT == STD_ON
          Det_ReportRuntimeError(GPT_MODULE_ID, GPT_INSTANCE_ID,
                                 MCAL_GPT_SID_INITIALIZE,
                                 GPT_E_INIT_FAILED);
#endif
          if (devinfos[i].isr != NULL && devinfos[i].irq >= 0)
            {
              up_disable_irq(devinfos[i].irq);
              irq_detach(devinfos[i].irq);
            }

          kmm_free(priv);
          return -ENODEV;
        }

      g_gpt_devs[devinfos[i].channel] = priv;

      tmrinfo("MCAL GPT: %s ch=%d freq=%luHz\n",
              devpath, devinfos[i].channel,
              (unsigned long)devinfos[i].frequency_hz);
    }

  return OK;
}
