/****************************************************************************
 * drivers/timers/ocu_mcal.c
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
#include <stdio.h>
#include <string.h>

#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <nuttx/spinlock.h>
#include <nuttx/kmalloc.h>
#include <nuttx/timers/ocu.h>
#include <nuttx/timers/ocu_mcal.h>

#if OCU_DEV_ERR_REPORTING == STD_ON || \
    OCU_RUNTIME_ERR_REPORTING == STD_ON
#  include "Det.h"
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MCAL_OCU_MAX_CHANNELS  CONFIG_MCAL_OCU_MAX_CHANNELS

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct mcal_ocu_lowerhalf_s
{
  struct ocu_lowerhalf_s                      lower;
  FAR const struct mcal_ocu_channel_config_s *config;
  ocu_callback_t                              callback;
  FAR void                                   *cbarg;
  uint32_t                                    last_threshold;
  spinlock_t                                  lock;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int  mcal_ocu_setup(FAR struct ocu_lowerhalf_s *lower);
static int  mcal_ocu_shutdown(FAR struct ocu_lowerhalf_s *lower);
static int  mcal_ocu_start(FAR struct ocu_lowerhalf_s *lower);
static int  mcal_ocu_stop(FAR struct ocu_lowerhalf_s *lower);
static int  mcal_ocu_getstatus(FAR struct ocu_lowerhalf_s *lower,
              FAR struct ocu_status_s *status);
static void mcal_ocu_setcallback(FAR struct ocu_lowerhalf_s *lower,
              CODE ocu_callback_t callback, FAR void *arg);
#if OCU_GET_COUNTER_API == STD_ON
static int  mcal_ocu_getcounter(FAR struct ocu_lowerhalf_s *lower,
              FAR uint32_t *counter);
#endif
#if OCU_SET_ABSOLUTE_THRESHOLD_API == STD_ON
static int  mcal_ocu_setabsthreshold(FAR struct ocu_lowerhalf_s *lower,
              FAR struct ocu_abs_threshold_s *t);
#endif
#if OCU_SET_RELATIVE_THRESHOLD_API == STD_ON
static int  mcal_ocu_setrelthreshold(FAR struct ocu_lowerhalf_s *lower,
              FAR struct ocu_rel_threshold_s *t);
#endif
#if OCU_SET_ABSOLUTE_THRESHOLD_API == STD_ON
static int  mcal_ocu_setadvancethreshold(
              FAR struct ocu_lowerhalf_s *lower,
              FAR struct ocu_advance_threshold_s *t);
#endif
#if OCU_SET_PIN_STATE_API == STD_ON
static int  mcal_ocu_setpinstate(FAR struct ocu_lowerhalf_s *lower,
              enum ocu_pinstate_e state);
#endif
#if OCU_SET_PIN_ACTION_API == STD_ON
static int  mcal_ocu_setpinaction(FAR struct ocu_lowerhalf_s *lower,
              enum ocu_pinaction_e action);
#endif
#if OCU_NOTIFICATION_SUPPORTED == STD_ON
static int  mcal_ocu_notification(FAR struct ocu_lowerhalf_s *lower,
              bool enable);
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct ocu_ops_s g_mcal_ocu_ops =
{
  .setup              = mcal_ocu_setup,
  .shutdown           = mcal_ocu_shutdown,
  .start              = mcal_ocu_start,
  .stop               = mcal_ocu_stop,
  .getstatus          = mcal_ocu_getstatus,
  .setcallback        = mcal_ocu_setcallback,
#if OCU_GET_COUNTER_API == STD_ON
  .getcounter         = mcal_ocu_getcounter,
#endif
#if OCU_SET_ABSOLUTE_THRESHOLD_API == STD_ON
  .setabsthreshold    = mcal_ocu_setabsthreshold,
#endif
#if OCU_SET_RELATIVE_THRESHOLD_API == STD_ON
  .setrelthreshold    = mcal_ocu_setrelthreshold,
#endif
#if OCU_NOTIFICATION_SUPPORTED == STD_ON
  .notification       = mcal_ocu_notification,
#endif
#if OCU_SET_ABSOLUTE_THRESHOLD_API == STD_ON
  .setadvancethreshold = mcal_ocu_setadvancethreshold,
#endif
#if OCU_SET_PIN_STATE_API == STD_ON
  .setpinstate        = mcal_ocu_setpinstate,
#endif
#if OCU_SET_PIN_ACTION_API == STD_ON
  .setpinaction       = mcal_ocu_setpinaction,
#endif
  .ioctl              = NULL,
};

static struct mcal_ocu_lowerhalf_s g_mcal_ocu_channels[MCAL_OCU_MAX_CHANNELS];

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int mcal_ocu_setup(FAR struct ocu_lowerhalf_s *lower)
{
  FAR struct mcal_ocu_lowerhalf_s *priv =
    (FAR struct mcal_ocu_lowerhalf_s *)lower;
  int ret;

  /* Attach and enable the compare-match interrupt if the chip/board layer
   * supplied an ISR.  Only the SRC-to-CPU routing is handled here using the
   * platform-agnostic NuttX irq API; the chip-specific interrupt dispatch
   * lives inside the board-provided ISR.  When irq/isr are not configured
   * (e.g. interrupt routing is done by the MCAL Irq module), this is a
   * no-op.
   */

  if (priv->config->isr != NULL && priv->config->irq >= 0)
    {
      ret = irq_attach(priv->config->irq, priv->config->isr, priv);
      if (ret < 0)
        {
#if OCU_RUNTIME_ERR_REPORTING == STD_ON
          Det_ReportRuntimeError(OCU_MODULE_ID, OCU_INSTANCE_ID,
                                 OCU_INIT_SID, OCU_E_INIT_FAILED);
#endif
          return ret;
        }

      up_enable_irq(priv->config->irq);
    }

  return OK;
}

static int mcal_ocu_shutdown(FAR struct ocu_lowerhalf_s *lower)
{
  FAR struct mcal_ocu_lowerhalf_s *priv =
    (FAR struct mcal_ocu_lowerhalf_s *)lower;

  if (priv->config->isr != NULL && priv->config->irq >= 0)
    {
      up_disable_irq(priv->config->irq);
      irq_detach(priv->config->irq);
    }

  return OK;
}

static int mcal_ocu_start(FAR struct ocu_lowerhalf_s *lower)
{
  FAR struct mcal_ocu_lowerhalf_s *priv =
    (FAR struct mcal_ocu_lowerhalf_s *)lower;

  Ocu_StartChannel(priv->config->channel);

  return OK;
}

static int mcal_ocu_stop(FAR struct ocu_lowerhalf_s *lower)
{
  FAR struct mcal_ocu_lowerhalf_s *priv =
    (FAR struct mcal_ocu_lowerhalf_s *)lower;

  Ocu_StopChannel(priv->config->channel);

  return OK;
}

static int mcal_ocu_getstatus(FAR struct ocu_lowerhalf_s *lower,
                              FAR struct ocu_status_s *status)
{
  FAR struct mcal_ocu_lowerhalf_s *priv =
    (FAR struct mcal_ocu_lowerhalf_s *)lower;

  /* The upper-half owns the ACTIVE and NOTIF_EN flags; the lower-half only
   * reports PIN_AVAIL (a per-channel hardware capability) and the
   * hardware-specific fields.
   */

  status->flags = 0;

  if (priv->config->has_pin)
    {
      status->flags |= OCUFLAGS_PIN_AVAIL;
    }

#if OCU_GET_COUNTER_API == STD_ON
  status->counter = Ocu_GetCounter(priv->config->channel);
#else
  status->counter = 0;
#endif

  status->threshold  = priv->last_threshold;
  status->maxcounter = priv->config->max_counter;

  return OK;
}

#if OCU_GET_COUNTER_API == STD_ON
static int mcal_ocu_getcounter(FAR struct ocu_lowerhalf_s *lower,
                               FAR uint32_t *counter)
{
  FAR struct mcal_ocu_lowerhalf_s *priv =
    (FAR struct mcal_ocu_lowerhalf_s *)lower;

  *counter = Ocu_GetCounter(priv->config->channel);
  return OK;
}
#endif

#if OCU_SET_ABSOLUTE_THRESHOLD_API == STD_ON
static int mcal_ocu_setabsthreshold(FAR struct ocu_lowerhalf_s *lower,
                                    FAR struct ocu_abs_threshold_s *t)
{
  FAR struct mcal_ocu_lowerhalf_s *priv =
    (FAR struct mcal_ocu_lowerhalf_s *)lower;
  Ocu_ReturnType ret;

  ret = Ocu_SetAbsoluteThreshold(priv->config->channel,
                                 t->reference,
                                 t->absolute);

  t->result = ret;
  priv->last_threshold = t->absolute;
  return OK;
}
#endif

#if OCU_SET_RELATIVE_THRESHOLD_API == STD_ON
static int mcal_ocu_setrelthreshold(FAR struct ocu_lowerhalf_s *lower,
                                    FAR struct ocu_rel_threshold_s *t)
{
  FAR struct mcal_ocu_lowerhalf_s *priv =
    (FAR struct mcal_ocu_lowerhalf_s *)lower;
  Ocu_ReturnType ret;

  ret = Ocu_SetRelativeThreshold(priv->config->channel,
                                 t->relative);

  t->result = ret;

#if OCU_GET_COUNTER_API == STD_ON
  priv->last_threshold = (Ocu_GetCounter(priv->config->channel) +
                          t->relative) % (priv->config->max_counter + 1);
#endif
  return OK;
}
#endif

#if OCU_SET_PIN_STATE_API == STD_ON
static int mcal_ocu_setpinstate(FAR struct ocu_lowerhalf_s *lower,
                                enum ocu_pinstate_e state)
{
  FAR struct mcal_ocu_lowerhalf_s *priv =
    (FAR struct mcal_ocu_lowerhalf_s *)lower;
  Ocu_PinStateType mcal_state;

  if (!priv->config->has_pin)
    {
      return -ENODEV;
    }

  /* The upper-half already rejects OCUIOC_SETPINSTATE while the channel is
   * running, so no running-state check is needed here.
   */

  mcal_state = state == OCU_PINSTATE_HIGH ? OCU_HIGH : OCU_LOW;
  Ocu_SetPinState(priv->config->channel, mcal_state);
  return OK;
}
#endif

#if OCU_SET_PIN_ACTION_API == STD_ON
static int mcal_ocu_setpinaction(FAR struct ocu_lowerhalf_s *lower,
                                 enum ocu_pinaction_e action)
{
  FAR struct mcal_ocu_lowerhalf_s *priv =
    (FAR struct mcal_ocu_lowerhalf_s *)lower;
  Ocu_PinActionType mcal_action;

  if (!priv->config->has_pin)
    {
      return -ENODEV;
    }

  switch (action)
    {
      case OCU_PINACTION_SET_HIGH:
        mcal_action = OCU_SET_HIGH;
        break;

      case OCU_PINACTION_SET_LOW:
        mcal_action = OCU_SET_LOW;
        break;

      case OCU_PINACTION_TOGGLE:
        mcal_action = OCU_TOGGLE;
        break;

      case OCU_PINACTION_DISABLE:
      default:
        mcal_action = OCU_DISABLE;
        break;
    }

  Ocu_SetPinAction(priv->config->channel, mcal_action);
  return OK;
}
#endif

#if OCU_SET_ABSOLUTE_THRESHOLD_API == STD_ON
static int mcal_ocu_setadvancethreshold(
             FAR struct ocu_lowerhalf_s *lower,
             FAR struct ocu_advance_threshold_s *t)
{
  FAR struct mcal_ocu_lowerhalf_s *priv =
    (FAR struct mcal_ocu_lowerhalf_s *)lower;
  Ocu_ReturnType ret;
  uint32_t abs_val;
  uint32_t counter;

  abs_val = (priv->last_threshold + t->step) %
            (priv->config->max_counter + 1);
  counter = Ocu_GetCounter(priv->config->channel);

  ret = Ocu_SetAbsoluteThreshold(priv->config->channel,
                                 counter,
                                 abs_val);

  t->result = ret;
  priv->last_threshold = abs_val;
  return OK;
}
#endif

static void mcal_ocu_setcallback(FAR struct ocu_lowerhalf_s *lower,
                                 CODE ocu_callback_t callback,
                                 FAR void *arg)
{
  FAR struct mcal_ocu_lowerhalf_s *priv =
    (FAR struct mcal_ocu_lowerhalf_s *)lower;
  irqstate_t flags;

  flags = spin_lock_irqsave(&priv->lock);

  /* setcallback only stores the callback pointer.  Enabling/disabling the
   * hardware compare-match notification is done separately via
   * mcal_ocu_notification() (OCUIOC_NOTIFICATION ioctl).
   */

  priv->callback = callback;
  priv->cbarg    = arg;

  spin_unlock_irqrestore(&priv->lock, flags);
}

#if OCU_NOTIFICATION_SUPPORTED == STD_ON
static int mcal_ocu_notification(FAR struct ocu_lowerhalf_s *lower,
                                 bool enable)
{
  FAR struct mcal_ocu_lowerhalf_s *priv =
    (FAR struct mcal_ocu_lowerhalf_s *)lower;

  if (enable)
    {
      Ocu_EnableNotification(priv->config->channel);
    }
  else
    {
      Ocu_DisableNotification(priv->config->channel);
    }

  return OK;
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: mcal_ocu_notify
 ****************************************************************************/

void mcal_ocu_notify(Ocu_ChannelType channel)
{
  FAR struct mcal_ocu_lowerhalf_s *priv = NULL;
  uint32_t next_threshold;
  bool reload;
  size_t i;

  /* Locate the registered channel whose MCAL OCU channel number matches.
   * g_mcal_ocu_channels[] is indexed by "bus", which may differ from the
   * MCAL channel number, so we must search by config->channel rather than
   * using the argument as a table index directly.
   */

  for (i = 0; i < MCAL_OCU_MAX_CHANNELS; i++)
    {
      if (g_mcal_ocu_channels[i].config != NULL &&
          g_mcal_ocu_channels[i].config->channel == channel)
        {
          priv = &g_mcal_ocu_channels[i];
          break;
        }
    }

  if (priv == NULL)
    {
#if OCU_RUNTIME_ERR_REPORTING == STD_ON
      Det_ReportRuntimeError(OCU_MODULE_ID, OCU_INSTANCE_ID,
                             OCU_TOMATOMISR_SID, OCU_E_PARAM_INVALID_CHANNEL);
#endif
      return;
    }

  if (priv->callback == NULL)
    {
      return;
    }

  reload = priv->callback(&next_threshold, priv->cbarg);

  if (reload)
    {
      Ocu_ChannelType ch = priv->config->channel;

      switch (priv->config->rearm_mode)
        {
          case MCAL_OCU_REARM_ABSOLUTE:
            {
#if OCU_SET_ABSOLUTE_THRESHOLD_API == STD_ON
              uint32_t counter = Ocu_GetCounter(ch);
              Ocu_SetAbsoluteThreshold(ch, counter, next_threshold);
              priv->last_threshold = next_threshold;
#endif
            }
            break;

          case MCAL_OCU_REARM_RELATIVE:
            {
#if OCU_SET_RELATIVE_THRESHOLD_API == STD_ON
              uint32_t counter;
              Ocu_SetRelativeThreshold(ch, next_threshold);
              counter = Ocu_GetCounter(ch);
              priv->last_threshold = (counter + next_threshold) %
                                     (priv->config->max_counter + 1);
#endif
            }
            break;

          case MCAL_OCU_REARM_ADVANCE:
            {
#if OCU_SET_ABSOLUTE_THRESHOLD_API == STD_ON
              uint32_t counter = Ocu_GetCounter(ch);
              uint32_t abs_val;
              abs_val = (priv->last_threshold + next_threshold) %
                        (priv->config->max_counter + 1);
              Ocu_SetAbsoluteThreshold(ch, counter, abs_val);
              priv->last_threshold = abs_val;
#endif
            }
            break;
        }
    }
}

/****************************************************************************
 * Name: mcal_ocu_initialize
 ****************************************************************************/

int mcal_ocu_initialize(FAR const Ocu_ConfigType *config,
                        FAR const struct mcal_ocu_channel_config_s *channels,
                        size_t num_channels)
{
  char devpath[16];
  int ret;
  size_t i;

  DEBUGASSERT(config != NULL);
  DEBUGASSERT(channels != NULL);

  Ocu_Init(config);

  for (i = 0; i < num_channels; i++)
    {
      FAR struct mcal_ocu_lowerhalf_s *priv;
      int bus = channels[i].bus;

      DEBUGASSERT(bus >= 0 && bus < MCAL_OCU_MAX_CHANNELS);

      priv = &g_mcal_ocu_channels[bus];

      /* Reject a bus number that is already in use (a registered slot has
       * a non-NULL config), whether from a duplicate entry in this call or
       * a previous mcal_ocu_initialize() (e.g. another core).  Otherwise the
       * later channel would silently clobber the earlier /dev/ocu<bus>.
       */

      if (priv->config != NULL)
        {
#if OCU_RUNTIME_ERR_REPORTING == STD_ON
          Det_ReportRuntimeError(OCU_MODULE_ID, OCU_INSTANCE_ID,
                                 OCU_INIT_SID, OCU_E_BUSY);
#endif
          continue;
        }

      spin_lock_init(&priv->lock);
      priv->lower.ops = &g_mcal_ocu_ops;
      priv->config    = &channels[i];

      snprintf(devpath, sizeof(devpath), "/dev/ocu%d", bus);

      ret = ocu_register(devpath, &priv->lower);
      if (ret < 0)
        {
#if OCU_RUNTIME_ERR_REPORTING == STD_ON
          Det_ReportRuntimeError(OCU_MODULE_ID, OCU_INSTANCE_ID,
                                 OCU_INIT_SID, OCU_E_INIT_FAILED);
#endif
          return ret;
        }
    }

  return OK;
}
