/****************************************************************************
 * drivers/timers/wdg_mcal.c
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

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <nuttx/clock.h>
#include <nuttx/kmalloc.h>
#include <nuttx/spinlock.h>
#include <nuttx/timers/watchdog.h>
#include <nuttx/timers/wdg_mcal.h>

#include <stdio.h>

#if defined(WDG_DEV_ERROR_DETECT) && \
    WDG_DEV_ERROR_DETECT == STD_ON || \
    defined(WDG_RUNTIME_ERROR_DETECT) && \
    WDG_RUNTIME_ERROR_DETECT == STD_ON
#  include "Det.h"
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MCAL_WDG_SID_INITIALIZE (0x80u)
#define MCAL_WDG_SID_START      (0x81u)
#define MCAL_WDG_SID_STOP       (0x82u)

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct mcal_wdg_dev_s
{
  struct watchdog_lowerhalf_s lower;       /* NuttX watchdog (FIRST) */
  WdgIf_ModeType mode;
  spinlock_t lock;
  clock_t lastkeep;
  uint32_t timeout_ms;
  uint32_t slow_max_timeout_ms;
  uint32_t fast_max_timeout_ms;
  bool started;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int mcal_wdg_start(FAR struct watchdog_lowerhalf_s *lower);
static int mcal_wdg_stop(FAR struct watchdog_lowerhalf_s *lower);
static int mcal_wdg_keepalive(FAR struct watchdog_lowerhalf_s *lower);
static int mcal_wdg_getstatus(FAR struct watchdog_lowerhalf_s *lower,
                              FAR struct watchdog_status_s *status);
static int mcal_wdg_settimeout(FAR struct watchdog_lowerhalf_s *lower,
                               uint32_t timeout);
static int mcal_wdg_ioctl(FAR struct watchdog_lowerhalf_s *lower,
                          int cmd, unsigned long arg);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct watchdog_ops_s g_mcal_wdg_ops =
{
  .start      = mcal_wdg_start,
  .stop       = mcal_wdg_stop,
  .keepalive  = mcal_wdg_keepalive,
  .getstatus  = mcal_wdg_getstatus,
  .settimeout = mcal_wdg_settimeout,
  .ioctl      = mcal_wdg_ioctl,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int mcal_wdg_start(FAR struct watchdog_lowerhalf_s *lower)
{
  FAR struct mcal_wdg_dev_s *priv = (FAR struct mcal_wdg_dev_s *)lower;
  Std_ReturnType ret;
  irqstate_t flags;

  flags = spin_lock_irqsave(&priv->lock);

  if (priv->started)
    {
      spin_unlock_irqrestore(&priv->lock, flags);
      return OK;
    }

  if (priv->mode == WDGIF_OFF_MODE)
    {
      priv->mode = WDGIF_SLOW_MODE;
      ret = Wdg_SetMode(WDGIF_SLOW_MODE);
    }
  else
    {
      ret = Wdg_SetMode(priv->mode);
    }

  if (ret != E_OK)
    {
      spin_unlock_irqrestore(&priv->lock, flags);
#if defined(WDG_RUNTIME_ERROR_DETECT) && \
    WDG_RUNTIME_ERROR_DETECT == STD_ON
      Det_ReportRuntimeError(WDG_MODULE_ID, WDG_INSTANCE_ID,
                             MCAL_WDG_SID_START,
                             WDG_E_PARAM_MODE);
#endif
      return -EIO;
    }

  Wdg_SetTriggerCondition(priv->timeout_ms);

  priv->lastkeep = clock_systime_ticks();
  priv->started = true;
  spin_unlock_irqrestore(&priv->lock, flags);
  return OK;
}

static int mcal_wdg_stop(FAR struct watchdog_lowerhalf_s *lower)
{
  FAR struct mcal_wdg_dev_s *priv = (FAR struct mcal_wdg_dev_s *)lower;
  Std_ReturnType ret;
  irqstate_t flags;

  flags = spin_lock_irqsave(&priv->lock);

  if (!priv->started)
    {
      spin_unlock_irqrestore(&priv->lock, flags);
      return OK;
    }

  ret = Wdg_SetMode(WDGIF_OFF_MODE);
  if (ret != E_OK)
    {
      spin_unlock_irqrestore(&priv->lock, flags);
#if defined(WDG_RUNTIME_ERROR_DETECT) && \
    WDG_RUNTIME_ERROR_DETECT == STD_ON
      Det_ReportRuntimeError(WDG_MODULE_ID, WDG_INSTANCE_ID,
                             MCAL_WDG_SID_STOP,
                             WDG_E_PARAM_MODE);
#endif
      return -EPERM;
    }

  priv->started = false;
  spin_unlock_irqrestore(&priv->lock, flags);
  return OK;
}

static int mcal_wdg_keepalive(FAR struct watchdog_lowerhalf_s *lower)
{
  FAR struct mcal_wdg_dev_s *priv = (FAR struct mcal_wdg_dev_s *)lower;
  irqstate_t flags;

  flags = spin_lock_irqsave(&priv->lock);

  if (!priv->started)
    {
      spin_unlock_irqrestore(&priv->lock, flags);
      return -EPERM;
    }

  Wdg_SetTriggerCondition(priv->timeout_ms);
  priv->lastkeep = clock_systime_ticks();
  spin_unlock_irqrestore(&priv->lock, flags);
  return OK;
}

static int mcal_wdg_getstatus(FAR struct watchdog_lowerhalf_s *lower,
                              FAR struct watchdog_status_s *status)
{
  FAR struct mcal_wdg_dev_s *priv = (FAR struct mcal_wdg_dev_s *)lower;
  irqstate_t flags;

  flags = spin_lock_irqsave(&priv->lock);

  memset(status, 0, sizeof(*status));
  status->timeout = priv->timeout_ms;

  if (priv->started)
    {
      uint32_t elapsed;

      elapsed = TICK2MSEC(clock_systime_ticks() - priv->lastkeep);
      status->flags |= WDFLAGS_ACTIVE;

      if (elapsed < priv->timeout_ms)
        {
          status->timeleft = priv->timeout_ms - elapsed;
        }
    }

  spin_unlock_irqrestore(&priv->lock, flags);
  return OK;
}

static int mcal_wdg_settimeout(FAR struct watchdog_lowerhalf_s *lower,
                               uint32_t timeout)
{
  FAR struct mcal_wdg_dev_s *priv = (FAR struct mcal_wdg_dev_s *)lower;
  irqstate_t flags;
  uint32_t max;

  flags = spin_lock_irqsave(&priv->lock);

  if (priv->mode == WDGIF_FAST_MODE)
    {
      max = priv->fast_max_timeout_ms;
    }
  else
    {
      max = priv->slow_max_timeout_ms;
    }

  if (timeout == 0 || timeout > max)
    {
      spin_unlock_irqrestore(&priv->lock, flags);
      return -EINVAL;
    }

  priv->timeout_ms = timeout;

  if (priv->started)
    {
      Wdg_SetTriggerCondition(priv->timeout_ms);
      priv->lastkeep = clock_systime_ticks();
    }

  spin_unlock_irqrestore(&priv->lock, flags);
  return OK;
}

static int mcal_wdg_ioctl(FAR struct watchdog_lowerhalf_s *lower,
                          int cmd, unsigned long arg)
{
  FAR struct mcal_wdg_dev_s *priv = (FAR struct mcal_wdg_dev_s *)lower;
  Std_ReturnType ret;
  irqstate_t flags;

  if (cmd != WDIOC_MCAL_SETMODE)
    {
      return -ENOTTY;
    }

  if (arg != WDGIF_OFF_MODE &&
      arg != WDGIF_SLOW_MODE &&
      arg != WDGIF_FAST_MODE)
    {
      return -EINVAL;
    }

  flags = spin_lock_irqsave(&priv->lock);

  ret = Wdg_SetMode(arg);
  if (ret != E_OK)
    {
      spin_unlock_irqrestore(&priv->lock, flags);
      return -EIO;
    }

  priv->mode = arg;

  if (arg == WDGIF_OFF_MODE)
    {
      priv->started = false;
    }
  else
    {
      priv->started = true;
      priv->lastkeep = clock_systime_ticks();
      Wdg_SetTriggerCondition(priv->timeout_ms);
    }

  spin_unlock_irqrestore(&priv->lock, flags);
  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int mcal_wdg_initialize(FAR const Wdg_ConfigType *mcal_config,
                        FAR const struct mcal_wdg_config_s *config,
                        int devno, int num)
{
  FAR struct mcal_wdg_dev_s *priv;
  char devpath[32];
  int i;

  if (mcal_config == NULL || config == NULL || num == 0)
    {
#if defined(WDG_DEV_ERROR_DETECT) && WDG_DEV_ERROR_DETECT == STD_ON
      Det_ReportError(WDG_MODULE_ID, WDG_INSTANCE_ID,
                      MCAL_WDG_SID_INITIALIZE,
                      WDG_E_PARAM_CONFIG);
#endif
      return -EINVAL;
    }

  Wdg_Init(mcal_config);

  for (i = 0; i < num; i++)
    {
      priv = kmm_zalloc(sizeof(*priv));
      if (priv == NULL)
        {
#if defined(WDG_RUNTIME_ERROR_DETECT) && \
    WDG_RUNTIME_ERROR_DETECT == STD_ON
          Det_ReportRuntimeError(WDG_MODULE_ID, WDG_INSTANCE_ID,
                                 MCAL_WDG_SID_INITIALIZE,
                                 WDG_E_INIT_FAILED);
#endif
          return -ENOMEM;
        }

      priv->lower.ops = &g_mcal_wdg_ops;
      priv->mode = config->default_mode;
      priv->slow_max_timeout_ms = config->slow_max_timeout_ms;
      priv->fast_max_timeout_ms = config->fast_max_timeout_ms;
      priv->timeout_ms = config->timeout_ms;
      priv->started = (config->default_mode != WDGIF_OFF_MODE);

      if (priv->started)
        {
          priv->lastkeep = clock_systime_ticks();
        }

      spin_lock_init(&priv->lock);

      snprintf(devpath, sizeof(devpath), "/dev/watchdog%d",
               devno + i);

      if (watchdog_register(devpath, &priv->lower) == NULL)
        {
#if defined(WDG_RUNTIME_ERROR_DETECT) && \
    WDG_RUNTIME_ERROR_DETECT == STD_ON
          Det_ReportRuntimeError(WDG_MODULE_ID, WDG_INSTANCE_ID,
                                 MCAL_WDG_SID_INITIALIZE,
                                 WDG_E_INIT_FAILED);
#endif
          kmm_free(priv);
          return -ENODEV;
        }
    }

  return OK;
}
