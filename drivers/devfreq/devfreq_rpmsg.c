/****************************************************************************
 * drivers/devfreq/devfreq_rpmsg.c
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
#include <stdio.h>
#include <string.h>

#include <sys/param.h>

#include <nuttx/devfreq.h>
#include <nuttx/kmalloc.h>
#include <nuttx/rpmsg/rpmsg.h>
#include <nuttx/semaphore.h>

#include "devfreq_rpmsg.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct devfreq_rpmsg_priv_s
{
  struct rpmsg_endpoint ept;
  FAR const char *cpuname;
  FAR const char *devname;
  FAR const struct devfreq_governor_s *governor;
  FAR struct devfreq_s *devfreq;
  FAR uint32_t *freq_table;
};

struct devfreq_rpmsg_cookie_s
{
  FAR struct devfreq_rpmsg_header_s *msg;
  size_t len;
  sem_t sem;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static FAR const uint32_t *
devfreq_rpmsg_get_table(FAR struct devfreq_s *devfreq);
static int devfreq_rpmsg_target_index(FAR struct devfreq_s *devfreq,
                                      size_t index);
static uint32_t devfreq_rpmsg_get_frequency(FAR struct devfreq_s *devfreq);
static int devfreq_rpmsg_suspend(FAR struct devfreq_s *devfreq);
static int devfreq_rpmsg_resume(FAR struct devfreq_s *devfreq);

static int devfreq_rpmsg_ept_cb(FAR struct rpmsg_endpoint *ept,
                                FAR void *data, size_t len,
                                uint32_t src, FAR void *priv);
static void devfreq_rpmsg_device_created(FAR struct rpmsg_device *rdev,
                                         FAR void *priv);
static void devfreq_rpmsg_device_destroy(FAR struct rpmsg_device *rdev,
                                         FAR void *priv);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct devfreq_driver_s g_devfreq_rpmsg_driver =
{
  .get_table     = devfreq_rpmsg_get_table,
  .target_index  = devfreq_rpmsg_target_index,
  .get_frequency = devfreq_rpmsg_get_frequency,
  .suspend       = devfreq_rpmsg_suspend,
  .resume        = devfreq_rpmsg_resume,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int devfreq_rpmsg_sendrecv(FAR struct devfreq_rpmsg_priv_s *priv,
                                  uint32_t command,
                                  FAR struct devfreq_rpmsg_header_s *msg,
                                  size_t len)
{
  struct devfreq_rpmsg_cookie_s cookie;
  int ret;

  cookie.msg = msg;
  cookie.len = len;
  nxsem_init(&cookie.sem, 0, 0);

  msg->command  = command;
  msg->response = 0;
  msg->result   = -ENXIO;
  msg->cookie   = (uintptr_t)&cookie;

  ret = rpmsg_send(&priv->ept, msg, len);

  if (ret >= 0)
    {
      ret = rpmsg_wait(&priv->ept, &cookie.sem);
      if (ret >= 0)
        {
          ret = msg->result;
        }
    }

  nxsem_destroy(&cookie.sem);
  return ret;
}

static FAR const uint32_t *
devfreq_rpmsg_get_table(FAR struct devfreq_s *devfreq)
{
  FAR struct devfreq_rpmsg_priv_s *priv = devfreq->priv;

  return priv->freq_table;
}

static int devfreq_rpmsg_target_index(FAR struct devfreq_s *devfreq,
                                      size_t index)
{
  FAR struct devfreq_rpmsg_priv_s *priv = devfreq->priv;
  struct devfreq_rpmsg_target_s msg;

  msg.freq = priv->freq_table[index];

  return devfreq_rpmsg_sendrecv(priv, DEVFREQ_RPMSG_TARGET,
                                &msg.header, sizeof(msg));
}

static uint32_t devfreq_rpmsg_get_frequency(FAR struct devfreq_s *devfreq)
{
  FAR struct devfreq_rpmsg_priv_s *priv = devfreq->priv;
  struct devfreq_rpmsg_target_s msg;
  int ret;

  memset(&msg, 0, sizeof(msg));
  ret = devfreq_rpmsg_sendrecv(priv, DEVFREQ_RPMSG_GET_FREQ,
                               &msg.header, sizeof(msg));
  if (ret < 0)
    {
      return 0;
    }

  return msg.freq;
}

static int devfreq_rpmsg_suspend(FAR struct devfreq_s *devfreq)
{
  FAR struct devfreq_rpmsg_priv_s *priv = devfreq->priv;
  struct devfreq_rpmsg_header_s msg;

  return devfreq_rpmsg_sendrecv(priv, DEVFREQ_RPMSG_SUSPEND,
                                &msg, sizeof(msg));
}

static int devfreq_rpmsg_resume(FAR struct devfreq_s *devfreq)
{
  FAR struct devfreq_rpmsg_priv_s *priv = devfreq->priv;
  struct devfreq_rpmsg_header_s msg;

  return devfreq_rpmsg_sendrecv(priv, DEVFREQ_RPMSG_RESUME,
                                &msg, sizeof(msg));
}

static int devfreq_rpmsg_ept_cb(FAR struct rpmsg_endpoint *ept,
                                FAR void *data, size_t len,
                                uint32_t src, FAR void *priv_)
{
  FAR struct devfreq_rpmsg_header_s *header = data;
  FAR struct devfreq_rpmsg_priv_s *priv = priv_;

  if (header->response)
    {
      /* GET_TABLE response carries the frequency table */

      if (header->command != DEVFREQ_RPMSG_GET_TABLE)
        {
          /* Non-GET_TABLE responses go through the cookie mechanism */

          FAR struct devfreq_rpmsg_cookie_s *cookie =
            (FAR struct devfreq_rpmsg_cookie_s *)
              (uintptr_t)header->cookie;

          if (cookie)
            {
              memcpy(cookie->msg, data, MIN(len, cookie->len));
              rpmsg_post(ept, &cookie->sem);
            }

          return 0;
        }
      else if (header->result == 0)
        {
          FAR struct devfreq_rpmsg_table_s *msg = data;
          size_t count;

          if (priv->devfreq != NULL || len < sizeof(*msg))
            {
              return 0;
            }

          /* Table includes DEVFREQ_ENTRY_END, compute count from len */

          count = (len - sizeof(*msg)) / sizeof(uint32_t) + 1;

          priv->freq_table = kmm_malloc(count * sizeof(uint32_t));
          if (priv->freq_table == NULL)
            {
              return -ENOMEM;
            }

          memcpy(priv->freq_table, msg->freq_table,
                 count * sizeof(uint32_t));

          priv->devfreq = devfreq_register(priv->devname,
                            priv->governor,
                            &g_devfreq_rpmsg_driver, priv);
          if (priv->devfreq == NULL)
            {
              kmm_free(priv->freq_table);
              priv->freq_table = NULL;
              return -ENOMEM;
            }
        }
    }

  return 0;
}

static void devfreq_rpmsg_ns_bound(FAR struct rpmsg_endpoint *ept)
{
  struct devfreq_rpmsg_header_s msg;

  /* Request frequency table from server */

  memset(&msg, 0, sizeof(msg));
  msg.command = DEVFREQ_RPMSG_GET_TABLE;
  rpmsg_send(ept, &msg, sizeof(msg));
}

static void devfreq_rpmsg_device_created(FAR struct rpmsg_device *rdev,
                                         FAR void *priv_)
{
  FAR struct devfreq_rpmsg_priv_s *priv = priv_;

  if (strcmp(priv->cpuname, rpmsg_get_cpuname(rdev)) == 0)
    {
      char name[RPMSG_NAME_SIZE];

      snprintf(name, sizeof(name), "%s%s",
               DEVFREQ_RPMSG_EPT_PREFIX, priv->devname);
      priv->ept.priv = priv;
      priv->ept.ns_bound_cb = devfreq_rpmsg_ns_bound;
      rpmsg_create_ept(&priv->ept, rdev, name,
                       RPMSG_ADDR_ANY, RPMSG_ADDR_ANY,
                       devfreq_rpmsg_ept_cb,
                       NULL);
    }
}

static void devfreq_rpmsg_device_destroy(FAR struct rpmsg_device *rdev,
                                         FAR void *priv_)
{
  FAR struct devfreq_rpmsg_priv_s *priv = priv_;

  if (strcmp(priv->cpuname, rpmsg_get_cpuname(rdev)) == 0)
    {
      /* Destroy ept first. rpmsg_destroy_ept will wake any
       * threads blocked in rpmsg_wait.
       */

      rpmsg_destroy_ept(&priv->ept);

      if (priv->devfreq)
        {
          devfreq_unregister(priv->devfreq);
          priv->devfreq = NULL;
        }

      if (priv->freq_table)
        {
          kmm_free(priv->freq_table);
          priv->freq_table = NULL;
        }
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: devfreq_rpmsg_register
 *
 * Description:
 *   Register a devfreq rpmsg proxy device. This creates a local devfreq
 *   device that forwards all operations to a remote CPU via rpmsg.
 *   The frequency table is obtained from the server automatically.
 *
 * Input Parameters:
 *   name       - Local device name (e.g., "cpu")
 *   cpuname    - Remote CPU name (e.g., "ap")
 *   governor   - Governor to use
 *
 * Returned Value:
 *   Opaque handle on success; NULL on failure
 *
 ****************************************************************************/

FAR void *
devfreq_rpmsg_register(FAR const char *name,
                       FAR const char *cpuname,
                       FAR const struct devfreq_governor_s *governor)
{
  FAR struct devfreq_rpmsg_priv_s *priv;

  if (name == NULL || cpuname == NULL)
    {
      return NULL;
    }

  priv = kmm_zalloc(sizeof(*priv));
  if (priv == NULL)
    {
      return NULL;
    }

  priv->cpuname  = cpuname;
  priv->devname  = name;
  priv->governor = governor;

  if (rpmsg_register_callback(priv,
                              devfreq_rpmsg_device_created,
                              devfreq_rpmsg_device_destroy,
                              NULL,
                              NULL) < 0)
    {
      kmm_free(priv);
      return NULL;
    }

  return priv;
}

/****************************************************************************
 * Name: devfreq_rpmsg_unregister
 *
 * Description:
 *   Unregister a devfreq rpmsg proxy device.
 *
 * Input Parameters:
 *   handle - handle returned by devfreq_rpmsg_register
 *
 * Returned Value:
 *   Zero on success; a negated errno on failure
 *
 ****************************************************************************/

int devfreq_rpmsg_unregister(FAR void *handle)
{
  FAR struct devfreq_rpmsg_priv_s *priv = handle;

  if (priv == NULL)
    {
      return -EINVAL;
    }

  /* Unregister callbacks first to prevent new endpoint creation */

  rpmsg_unregister_callback(priv,
                            devfreq_rpmsg_device_created,
                            devfreq_rpmsg_device_destroy,
                            NULL,
                            NULL);

  kmm_free(priv);

  return 0;
}
