/****************************************************************************
 * drivers/devfreq/devfreq_rpmsg_server.c
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

#include <debug.h>
#include <errno.h>
#include <string.h>

#include <sys/param.h>

#include <nuttx/devfreq.h>
#include <nuttx/kmalloc.h>
#include <nuttx/rpmsg/rpmsg.h>

#include "devfreq_rpmsg.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct devfreq_rpmsg_server_s
{
  struct rpmsg_endpoint ept;
  FAR struct devfreq_s *devfreq;
  FAR struct qos_request_s *qos;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int devfreq_rpmsg_ept_cb(FAR struct rpmsg_endpoint *ept,
                                FAR void *data, size_t len,
                                uint32_t src, FAR void *priv);

static int devfreq_rpmsg_target_handler(FAR struct rpmsg_endpoint *ept,
                                        FAR void *data, size_t len,
                                        uint32_t src, FAR void *priv);
static int devfreq_rpmsg_getfreq_handler(FAR struct rpmsg_endpoint *ept,
                                         FAR void *data, size_t len,
                                         uint32_t src, FAR void *priv);
static int devfreq_rpmsg_suspend_handler(FAR struct rpmsg_endpoint *ept,
                                         FAR void *data, size_t len,
                                         uint32_t src, FAR void *priv);
static int devfreq_rpmsg_resume_handler(FAR struct rpmsg_endpoint *ept,
                                        FAR void *data, size_t len,
                                        uint32_t src, FAR void *priv);
static int devfreq_rpmsg_gettable_handler(FAR struct rpmsg_endpoint *ept,
                                          FAR void *data, size_t len,
                                          uint32_t src, FAR void *priv);

static bool devfreq_rpmsg_server_match(FAR struct rpmsg_device *rdev,
                                       FAR void *priv,
                                       FAR const char *name,
                                       uint32_t dest);
static void devfreq_rpmsg_server_bind(FAR struct rpmsg_device *rdev,
                                      FAR void *priv,
                                      FAR const char *name,
                                      uint32_t dest);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const rpmsg_ept_cb g_devfreq_rpmsg_handler[] =
{
  [DEVFREQ_RPMSG_TARGET]    = devfreq_rpmsg_target_handler,
  [DEVFREQ_RPMSG_GET_FREQ]  = devfreq_rpmsg_getfreq_handler,
  [DEVFREQ_RPMSG_SUSPEND]   = devfreq_rpmsg_suspend_handler,
  [DEVFREQ_RPMSG_RESUME]    = devfreq_rpmsg_resume_handler,
  [DEVFREQ_RPMSG_GET_TABLE] = devfreq_rpmsg_gettable_handler,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static bool devfreq_rpmsg_server_match(FAR struct rpmsg_device *rdev,
                                       FAR void *priv,
                                       FAR const char *name,
                                       uint32_t dest)
{
  return strncmp(name, DEVFREQ_RPMSG_EPT_PREFIX,
                 DEVFREQ_RPMSG_EPT_PREFIX_LEN) == 0;
}

static void
devfreq_rpmsg_server_ept_release(FAR struct rpmsg_endpoint *ept)
{
  FAR struct devfreq_rpmsg_server_s *server = ept->priv;

  if (server->qos)
    {
      devfreq_qos_remove_request(server->devfreq, server->qos);
    }

  kmm_free(server);
}

static void devfreq_rpmsg_server_bind(FAR struct rpmsg_device *rdev,
                                      FAR void *priv,
                                      FAR const char *name,
                                      uint32_t dest)
{
  FAR struct devfreq_rpmsg_server_s *server;
  FAR const char *devname = name + DEVFREQ_RPMSG_EPT_PREFIX_LEN;
  FAR struct devfreq_s *devfreq;

  devfreq = devfreq_find_by_name(devname);
  if (devfreq == NULL)
    {
      return;
    }

  server = kmm_zalloc(sizeof(*server));
  if (server == NULL)
    {
      return;
    }

  server->devfreq = devfreq;
  server->ept.priv = server;
  server->ept.release_cb = devfreq_rpmsg_server_ept_release;

  if (rpmsg_create_ept(&server->ept, rdev, name,
                       RPMSG_ADDR_ANY, RPMSG_ADDR_ANY,
                       devfreq_rpmsg_ept_cb,
                       rpmsg_destroy_ept) < 0)
    {
      kmm_free(server);
    }
}

static int devfreq_rpmsg_ept_cb(FAR struct rpmsg_endpoint *ept,
                                FAR void *data, size_t len,
                                uint32_t src, FAR void *priv)
{
  FAR struct devfreq_rpmsg_header_s *header = data;
  uint32_t cmd = header->command;
  int ret = -EINVAL;

  if (header->response)
    {
      return ret;
    }

  if (cmd < nitems(g_devfreq_rpmsg_handler) &&
      g_devfreq_rpmsg_handler[cmd])
    {
      header->response = 1;
      ret = g_devfreq_rpmsg_handler[cmd](ept, data, len, src, priv);
    }

  return ret;
}

static int devfreq_rpmsg_target_handler(FAR struct rpmsg_endpoint *ept,
                                        FAR void *data, size_t len,
                                        uint32_t src, FAR void *priv)
{
  FAR struct devfreq_rpmsg_server_s *server = ept->priv;
  FAR struct devfreq_rpmsg_target_s *msg = data;
  FAR struct devfreq_s *devfreq = server->devfreq;
  uint32_t freq = msg->freq;

  /* Use QoS mechanism to set frequency constraint.
   * The rpmsg protocol currently only carries a single target frequency,
   * so min=max=freq. To support independent min/max QoS from remote
   * clients, the rpmsg message would need to be extended.
   */

  if (server->qos == NULL)
    {
      server->qos = devfreq_qos_add_request(devfreq, freq, freq);
      if (server->qos == NULL)
        {
          msg->header.result = -ENOMEM;
        }
      else
        {
          msg->header.result = 0;
        }
    }
  else
    {
      msg->header.result = devfreq_qos_update_request(devfreq,
                             server->qos, freq, freq);
    }

  return rpmsg_send(ept, data, len);
}

static int devfreq_rpmsg_getfreq_handler(FAR struct rpmsg_endpoint *ept,
                                         FAR void *data, size_t len,
                                         uint32_t src, FAR void *priv)
{
  FAR struct devfreq_rpmsg_server_s *server = ept->priv;
  FAR struct devfreq_rpmsg_target_s *msg = data;

  msg->freq = devfreq_get_frequency(server->devfreq);
  msg->header.result = 0;

  return rpmsg_send(ept, data, sizeof(*msg));
}

static int devfreq_rpmsg_suspend_handler(FAR struct rpmsg_endpoint *ept,
                                         FAR void *data, size_t len,
                                         uint32_t src, FAR void *priv)
{
  FAR struct devfreq_rpmsg_server_s *server = ept->priv;
  FAR struct devfreq_rpmsg_header_s *msg = data;

  msg->result = devfreq_suspend(server->devfreq);

  return rpmsg_send(ept, data, len);
}

static int devfreq_rpmsg_resume_handler(FAR struct rpmsg_endpoint *ept,
                                        FAR void *data, size_t len,
                                        uint32_t src, FAR void *priv)
{
  FAR struct devfreq_rpmsg_server_s *server = ept->priv;
  FAR struct devfreq_rpmsg_header_s *msg = data;

  msg->result = devfreq_resume(server->devfreq);

  return rpmsg_send(ept, data, len);
}

static int
devfreq_rpmsg_gettable_handler(FAR struct rpmsg_endpoint *ept,
                               FAR void *data, size_t len,
                               uint32_t src, FAR void *priv)
{
  FAR struct devfreq_rpmsg_server_s *server = ept->priv;
  FAR struct devfreq_s *devfreq = server->devfreq;
  FAR const uint32_t *table = devfreq->freq_table;
  FAR struct devfreq_rpmsg_table_s *msg;
  uint32_t paylen;
  size_t count = 0;
  size_t msglen;

  /* Count table size including DEVFREQ_ENTRY_END */

  while (table[count++] != DEVFREQ_ENTRY_END);

  /* freq_table[1] in struct already provides space for 1 element */

  msglen = sizeof(*msg) + (count - 1) * sizeof(uint32_t);
  msg = rpmsg_get_tx_payload_buffer(ept, &paylen, true);
  if (msg == NULL || paylen < msglen)
    {
      FAR struct devfreq_rpmsg_header_s *hdr = data;

      if (msg)
        {
          rpmsg_release_tx_buffer(ept, msg);
        }

      hdr->result = -ENOMEM;
      return rpmsg_send(ept, data, len);
    }

  memcpy(msg, data, sizeof(msg->header));
  msg->header.result = 0;
  memcpy(msg->freq_table, table, count * sizeof(uint32_t));

  return rpmsg_send_nocopy(ept, msg, msglen);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: devfreq_rpmsg_server_init
 *
 * Description:
 *   Initialize the devfreq rpmsg server. This allows remote CPUs to
 *   control any registered devfreq device via rpmsg. The target device
 *   is resolved by name from the endpoint name at bind time.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   Zero on success; a negated errno on failure
 *
 ****************************************************************************/

int devfreq_rpmsg_server_init(void)
{
  return rpmsg_register_callback(NULL,
                                 NULL,
                                 NULL,
                                 devfreq_rpmsg_server_match,
                                 devfreq_rpmsg_server_bind);
}
