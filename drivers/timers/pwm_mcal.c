/****************************************************************************
 * drivers/timers/pwm_mcal.c
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

#include <nuttx/kmalloc.h>
#include <nuttx/timers/pwm.h>
#include <nuttx/timers/pwm_mcal.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* AUTOSAR PWM duty cycle resolution: 0x0000 = 0%, 0x8000 = 100%
 * (see AUTOSAR SWS_Pwm_00006).
 */

#define MCAL_PWM_DUTY_MAX 0x8000u

/* Channel state flags */

#define MCAL_PWM_FLAG_IDLE   (1 << 0)
#define MCAL_PWM_FLAG_NOTIFY (1 << 1)

/* Adaptation layer Service IDs (start at 0x80) */

#define MCAL_PWM_SID_INITIALIZE (0x80u)

#ifndef PWM_NOTIFICATION_SUPPORTED
#  define PWM_NOTIFICATION_SUPPORTED STD_OFF
#endif

#ifndef PWM_DEV_ERR_REPORTING
#  define PWM_DEV_ERR_REPORTING STD_OFF
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct mcal_pwm_dev_s
{
  struct pwm_lowerhalf_s dev;                   /* NuttX PWM (MUST be first) */
  FAR const struct mcal_pwm_devinfo_s *devinfo; /* Board device info */
  uint8_t flags[];                              /* Per-channel state (FAM) */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int mcal_pwm_setup(FAR struct pwm_lowerhalf_s *dev);
static int mcal_pwm_shutdown(FAR struct pwm_lowerhalf_s *dev);
static int mcal_pwm_start(FAR struct pwm_lowerhalf_s *dev,
                          FAR const struct pwm_info_s *info);
static int mcal_pwm_stop(FAR struct pwm_lowerhalf_s *dev);
static int mcal_pwm_ioctl(FAR struct pwm_lowerhalf_s *dev,
                          int cmd, unsigned long arg);
static int mcal_pwm_set_output_idle(FAR struct pwm_lowerhalf_s *dev,
                                    int channel);
static int mcal_pwm_get_output_state(FAR struct pwm_lowerhalf_s *dev,
                                     int channel, FAR bool *state);
static int mcal_pwm_get_power_state(FAR struct pwm_lowerhalf_s *dev,
                                    FAR uint8_t *state);
static int mcal_pwm_set_power_state(FAR struct pwm_lowerhalf_s *dev,
                                    uint8_t state);

#if (PWM_NOTIFICATION_SUPPORTED == STD_ON)
static int mcal_pwm_enable_notify(FAR struct pwm_lowerhalf_s *dev,
                                  int channel, uint8_t edge);
static int mcal_pwm_disable_notify(FAR struct pwm_lowerhalf_s *dev,
                                   int channel);
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct pwm_ops_s g_mcal_pwm_ops =
{
  .setup           = mcal_pwm_setup,
  .shutdown        = mcal_pwm_shutdown,
  .start           = mcal_pwm_start,
  .stop            = mcal_pwm_stop,
  .ioctl           = mcal_pwm_ioctl,
  .set_output_idle = mcal_pwm_set_output_idle,
  .get_output_state = mcal_pwm_get_output_state,
#if (PWM_NOTIFICATION_SUPPORTED == STD_ON)
  .enable_notify   = mcal_pwm_enable_notify,
  .disable_notify  = mcal_pwm_disable_notify,
#endif
  .get_power_state = mcal_pwm_get_power_state,
  .set_power_state = mcal_pwm_set_power_state,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: mcal_pwm_get_chan
 *
 * Description:
 *   Find the mcal_pwm_chan_s entry for a given NuttX channel number.
 *   NuttX uses 1-based channel numbers in pwm_chan_s.channel.
 *   We match against the array index + 1.
 *
 ****************************************************************************/

static inline FAR const struct mcal_pwm_chan_s *
mcal_pwm_get_chan(FAR struct mcal_pwm_dev_s *priv, int channel)
{
  if (channel >= 1 && channel <= priv->devinfo->nchan)
    {
      return &priv->devinfo->chan[channel - 1];
    }

  return NULL;
}

/****************************************************************************
 * Name: mcal_pwm_start_one
 *
 * Description:
 *   Apply period+duty to a single MCAL channel based on its class.
 *   Converts NuttX ub16_t duty (0x0000~0xFFFF) to AUTOSAR MCAL duty
 *   (0x0000~0x8000); rounding (duty + 1) / 2 maps 0xFFFF exactly to 0x8000.
 *
 ****************************************************************************/

static int mcal_pwm_start_one(Pwm_ChannelType channel,
                              Pwm_ChannelClassType channel_class,
                              uint32_t clock_freq,
                              pwm_freq_t frequency,
                              ub16_t duty)
{
  uint16_t mcal_duty = (uint16_t)(((uint32_t)duty + 1) >> 1);

  if (frequency == 0)
    {
      return -EINVAL;
    }

  if (channel_class == PWM_VARIABLE_PERIOD)
    {
      /* Convert Hz to ticks. Hardware-specific period limits (e.g. timer
       * counter width) are enforced by the vendor MCAL driver.
       */

      Pwm_PeriodType period_ticks =
        (Pwm_PeriodType)(clock_freq / frequency);

      if (period_ticks == 0)
        {
          /* frequency > clock_freq -> truncated to 0 ticks; reject so the
           * vendor MCAL never receives a zero period (AUTOSAR-undefined).
           */

          return -EINVAL;
        }

      Pwm_SetPeriodAndDuty(channel, period_ticks, mcal_duty);
    }
  else
    {
      Pwm_SetDutyCycle(channel, mcal_duty);
    }

  return OK;
}

static int mcal_pwm_setup(FAR struct pwm_lowerhalf_s *dev)
{
  return OK;
}

static int mcal_pwm_shutdown(FAR struct pwm_lowerhalf_s *dev)
{
  FAR struct mcal_pwm_dev_s *priv = (FAR struct mcal_pwm_dev_s *)dev;
  int i;

  for (i = 0; i < priv->devinfo->nchan; i++)
    {
      Pwm_ChannelType ch;

      ch = priv->devinfo->chan[i].channel_id;

#if (PWM_NOTIFICATION_SUPPORTED == STD_ON)
      if (priv->flags[i] & MCAL_PWM_FLAG_NOTIFY)
        {
          Pwm_DisableNotification(ch);
          priv->flags[i] &= ~MCAL_PWM_FLAG_NOTIFY;
        }
#endif

      if (!(priv->flags[i] & MCAL_PWM_FLAG_IDLE))
        {
          Pwm_SetOutputToIdle(ch);
          priv->flags[i] |= MCAL_PWM_FLAG_IDLE;
        }
    }

  return OK;
}

static int mcal_pwm_start(FAR struct pwm_lowerhalf_s *dev,
                          FAR const struct pwm_info_s *info)
{
  FAR struct mcal_pwm_dev_s *priv = (FAR struct mcal_pwm_dev_s *)dev;
  int i;

  /* Iterate over the upper-half channel array, not priv->devinfo->nchan:
   * info->channels[] has fixed length CONFIG_PWM_NCHANNELS, and entries
   * use the 0=skip / -1=end convention to delimit valid slots.
   */

  for (i = 0; i < CONFIG_PWM_NCHANNELS; i++)
    {
      FAR const struct mcal_pwm_chan_s *mcal_ch;
      int ch_num;

      ch_num = info->channels[i].channel;

      /* channel: 0=skip, -1=end of list */

      if (ch_num == 0)
        {
          continue;
        }

      if (ch_num < 0)
        {
          break;
        }

      mcal_ch = mcal_pwm_get_chan(priv, ch_num);
      if (mcal_ch == NULL)
        {
          continue;
        }

      if (mcal_pwm_start_one(mcal_ch->channel_id, mcal_ch->channel_class,
              priv->devinfo->clock_freq, info->frequency,
              info->channels[i].duty) == OK)
        {
          priv->flags[ch_num - 1] &= ~MCAL_PWM_FLAG_IDLE;
        }
    }

  return OK;
}

static int mcal_pwm_stop(FAR struct pwm_lowerhalf_s *dev)
{
  FAR struct mcal_pwm_dev_s *priv = (FAR struct mcal_pwm_dev_s *)dev;
  int i;

  for (i = 0; i < priv->devinfo->nchan; i++)
    {
      Pwm_SetOutputToIdle(priv->devinfo->chan[i].channel_id);
      priv->flags[i] |= MCAL_PWM_FLAG_IDLE;
    }

  return OK;
}

static int mcal_pwm_ioctl(FAR struct pwm_lowerhalf_s *dev,
                          int cmd, unsigned long arg)
{
  return -ENOTTY;
}

static int mcal_pwm_set_output_idle(FAR struct pwm_lowerhalf_s *dev,
                                    int channel)
{
  FAR struct mcal_pwm_dev_s *priv = (FAR struct mcal_pwm_dev_s *)dev;
  FAR const struct mcal_pwm_chan_s *mcal_ch;

  mcal_ch = mcal_pwm_get_chan(priv, channel);
  if (mcal_ch == NULL)
    {
      return -EINVAL;
    }

  Pwm_SetOutputToIdle(mcal_ch->channel_id);
  priv->flags[channel - 1] |= MCAL_PWM_FLAG_IDLE;

  return OK;
}

static int mcal_pwm_get_output_state(FAR struct pwm_lowerhalf_s *dev,
                                     int channel, FAR bool *state)
{
  FAR struct mcal_pwm_dev_s *priv = (FAR struct mcal_pwm_dev_s *)dev;
  FAR const struct mcal_pwm_chan_s *mcal_ch;

  mcal_ch = mcal_pwm_get_chan(priv, channel);
  if (mcal_ch == NULL)
    {
      return -EINVAL;
    }

  *state = (Pwm_GetOutputState(mcal_ch->channel_id) == PWM_HIGH);

  return OK;
}

#if (PWM_NOTIFICATION_SUPPORTED == STD_ON)

static int mcal_pwm_enable_notify(FAR struct pwm_lowerhalf_s *dev,
                                  int channel, uint8_t edge)
{
  FAR struct mcal_pwm_dev_s *priv = (FAR struct mcal_pwm_dev_s *)dev;
  FAR const struct mcal_pwm_chan_s *mcal_ch;
  Pwm_EdgeNotificationType mcal_edge;

  switch (edge)
    {
      case PWM_EDGE_RISING:
        mcal_edge = PWM_RISING_EDGE;
        break;
      case PWM_EDGE_FALLING:
        mcal_edge = PWM_FALLING_EDGE;
        break;
      case PWM_EDGE_BOTH:
        mcal_edge = PWM_BOTH_EDGES;
        break;
      default:
        return -EINVAL;
    }

  mcal_ch = mcal_pwm_get_chan(priv, channel);
  if (mcal_ch == NULL)
    {
      return -EINVAL;
    }

  Pwm_EnableNotification(mcal_ch->channel_id, mcal_edge);
  priv->flags[channel - 1] |= MCAL_PWM_FLAG_NOTIFY;

  return OK;
}

static int mcal_pwm_disable_notify(FAR struct pwm_lowerhalf_s *dev,
                                   int channel)
{
  FAR struct mcal_pwm_dev_s *priv = (FAR struct mcal_pwm_dev_s *)dev;
  FAR const struct mcal_pwm_chan_s *mcal_ch;

  mcal_ch = mcal_pwm_get_chan(priv, channel);
  if (mcal_ch == NULL)
    {
      return -EINVAL;
    }

  Pwm_DisableNotification(mcal_ch->channel_id);
  priv->flags[channel - 1] &= ~MCAL_PWM_FLAG_NOTIFY;

  return OK;
}

#endif /* PWM_NOTIFICATION_SUPPORTED */

static int mcal_pwm_get_power_state(FAR struct pwm_lowerhalf_s *dev,
                                    FAR uint8_t *state)
{
  Pwm_PowerStateType pwm_state;
  Pwm_PowerStateRequestResultType result;

  if (Pwm_GetCurrentPowerState(&pwm_state, &result) != E_OK)
    {
      return -EIO;
    }

  *state = (uint8_t)pwm_state;
  return OK;
}

static int mcal_pwm_set_power_state(FAR struct pwm_lowerhalf_s *dev,
                                    uint8_t state)
{
  Pwm_PowerStateRequestResultType result;

  /* AUTOSAR SWS_Pwm_00166 two-step transition: prepare the target
   * state first, then commit via Pwm_SetPowerState().
   */

  if (Pwm_PreparePowerState((Pwm_PowerStateType)state, &result) != E_OK)
    {
      return result == PWM_POWER_STATE_NOT_SUPP ? -ENOSYS : -EIO;
    }

  if (Pwm_SetPowerState(&result) != E_OK)
    {
      return result == PWM_POWER_STATE_NOT_SUPP ? -ENOSYS : -EIO;
    }

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int mcal_pwm_initialize(FAR const Pwm_ConfigType *config,
                        FAR const struct mcal_pwm_devinfo_s *devinfos,
                        size_t ndevinfos)
{
  FAR struct mcal_pwm_dev_s *priv;
  int first_err = OK;
  int ret;
  size_t i;
  int j;

  if (config == NULL || devinfos == NULL || ndevinfos == 0)
    {
#if PWM_DEV_ERR_REPORTING == STD_ON
      Det_ReportError(PWM_MODULE_ID,
                      PWM_INSTANCE_ID,
                      MCAL_PWM_SID_INITIALIZE,
                      PWM_E_PARAM_POINTER);
#endif
      return -EINVAL;
    }

  Pwm_Init(config);

  for (i = 0; i < ndevinfos; i++)
    {
      priv = kmm_zalloc(sizeof(*priv) + devinfos[i].nchan);
      if (priv == NULL)
        {
          pwmerr("ERROR: alloc for %s failed\n", devinfos[i].devpath);
          if (first_err == OK)
            {
              first_err = -ENOMEM;
            }

          continue;
        }

      priv->dev.ops = &g_mcal_pwm_ops;
      priv->devinfo = &devinfos[i];

      for (j = 0; j < devinfos[i].nchan; j++)
        {
          priv->flags[j] = MCAL_PWM_FLAG_IDLE;
        }

      ret = pwm_register(devinfos[i].devpath, &priv->dev);
      if (ret < 0)
        {
          pwmerr("ERROR: pwm_register %s failed: %d\n",
                 devinfos[i].devpath, ret);
          kmm_free(priv);
          if (first_err == OK)
            {
              first_err = ret;
            }
        }
    }

  return first_err;
}
