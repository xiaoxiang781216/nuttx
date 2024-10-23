/****************************************************************************
 * drivers/rpmsg/rpmsg_test.c
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

#include <string.h>
#include <syslog.h>
#include <time.h>
#include <sys/param.h>
#include <sys/random.h>

#include <nuttx/clock.h>
#include <nuttx/kmalloc.h>
#include <nuttx/list.h>
#include <nuttx/wqueue.h>

#include "rpmsg_test.h"

/****************************************************************************
 * Pre-processor definitions
 ****************************************************************************/

#define RPMSG_TEST_EPT_NAME           "rpmsg-test"

#define RPMSG_TEST_CMD(type, value)   (((type) << 16) | (value))
#define RPMSG_TEST_GET_TYPE(cmd)      ((cmd) >> 16)
#define RPMSG_TEST_GET_VALUE(cmd)     ((cmd) & 0xffff)

/* Rpmsg test type,value and cmd definition  */

#define RPMSG_TEST_TYPE_HOLD_RX        1
#define RPMSG_TEST_VAL_HOLD_RX         0
#define RPMSG_TEST_VAL_HOLD_RX_END     1

#define RPMSG_TEST_TYPE_SEND_NOCOPY    2
#define RPMSG_TEST_VAL_SEND_NOCOPY     0
#define RPMSG_TEST_VAL_SEND_NOCOPY_RSP 1

#define RPMSG_TEST_TYPE_SEND           3
#define RPMSG_TEST_VAL_SEND            0
#define RPMSG_TEST_VAL_SEND_RSP        1

#define RPMSG_TEST_CMD_HOLD_RX         \
  RPMSG_TEST_CMD(RPMSG_TEST_TYPE_HOLD_RX, RPMSG_TEST_VAL_HOLD_RX)
#define RPMSG_TEST_CMD_HOLD_RX_END     \
  RPMSG_TEST_CMD(RPMSG_TEST_TYPE_HOLD_RX, RPMSG_TEST_VAL_HOLD_RX_END)

/* Magic number used in test case */

#define RPMSG_TEST_MAGIC1              0x55
#define RPMSG_TEST_MAGIC2              0xaa

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct rpmsg_test_priv_s
{
  FAR struct rpmsg_endpoint *ept;
  struct list_node           rxhead;
  struct work_s              work;
};

begin_packed_struct struct rpmsg_test_msg_s
{
  uint32_t node[4]; /* Used for the list node */
  uint32_t cmd;
  uint32_t len;
  uint64_t cookie;
  uint8_t  data[1];
} end_packed_struct;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: rpmsg_test_memcmp
 ****************************************************************************/

static bool rpmsg_test_memcmp(FAR const uint8_t *buf, uint8_t ch,
                              size_t size)
{
  size_t i;

  for (i = 0; i < size; i++)
    {
      if (buf[i] != ch)
        {
          return false;
        }
    }

  return true;
}

/****************************************************************************
 * Name: rpmsg_test_hold_rx_work
 ****************************************************************************/

static void rpmsg_test_hold_rx_work(FAR void *arg)
{
  FAR struct rpmsg_test_priv_s *priv = arg;
  FAR struct rpmsg_test_msg_s *msg;
  bool ret;

  /* Check all the rx buffer value (should still be MAGIC2) and release
   * all the held rx buffers.
   */

  syslog(LOG_EMERG, "Rpmsg Test: release rx buffers start\n");

  while (!list_is_empty(&priv->rxhead))
    {
      msg = (FAR struct rpmsg_test_msg_s *)list_remove_head(&priv->rxhead);
      ret = rpmsg_test_memcmp(msg->data, RPMSG_TEST_MAGIC2, msg->len);
      DEBUGASSERT(ret);
      rpmsg_release_rx_buffer(priv->ept, msg);
    }

  syslog(LOG_EMERG, "Rpmsg Test: release rx buffers end\n");
}

/****************************************************************************
 * Name: rpmsg_test_hold_rx_cb
 ****************************************************************************/

static void rpmsg_test_hold_rx_cb(FAR struct rpmsg_endpoint *ept,
                                  FAR void *data, size_t len, uint32_t src,
                                  FAR void *_priv)
{
  FAR struct rpmsg_test_priv_s *priv = _priv;
  FAR struct rpmsg_test_msg_s *msg = data;
  uint32_t value = RPMSG_TEST_GET_VALUE(msg->cmd);
  bool ret;

  /* Hold the rx buffer and fill the rx buffer data with MAGIC2 */

  ret = rpmsg_test_memcmp(msg->data, RPMSG_TEST_MAGIC1, msg->len);
  DEBUGASSERT(ret);
  memset(msg->data, RPMSG_TEST_MAGIC2, msg->len);
  rpmsg_hold_rx_buffer(ept, data);

  list_add_tail(&priv->rxhead, (FAR struct list_node *)data);

  if (value == RPMSG_TEST_VAL_HOLD_RX_END)
    {
      syslog(LOG_EMERG, "Rpmsg Test: hold rx buffer finish\n");

      /* Delay 10ms to release all the rx buffers, to make sure
       * the hold rx buffer api is valid.
       */

      work_queue(LPWORK, &priv->work, rpmsg_test_hold_rx_work, priv,
                 MSEC2TICK(10));
    }
}

/****************************************************************************
 * Name: rpmsg_test_send_nocopy_cb
 ****************************************************************************/

static int rpmsg_test_send_nocopy_cb(FAR struct rpmsg_endpoint *ept,
                                     FAR void *data, size_t len,
                                     uint32_t src, FAR void *priv)
{
  FAR struct rpmsg_test_msg_s *msg = data;
  FAR sem_t *sem = (FAR sem_t *)(uintptr_t)msg->cookie;
  FAR struct rpmsg_test_msg_s *rmsg;
  uint32_t space;
  int ret;

  if ((RPMSG_TEST_GET_VALUE(msg->cmd) == RPMSG_TEST_VAL_SEND_NOCOPY_RSP))
    {
      ret = rpmsg_test_memcmp(msg->data, RPMSG_TEST_MAGIC1, msg->len);
      DEBUGASSERT(ret);
      syslog(LOG_EMERG, "Rpmsg Test: Test rpmsg send nocopy success!\n");
      nxsem_post(sem);
      return ret;
    }

  rmsg = rpmsg_get_tx_payload_buffer(ept, &space, true);
  DEBUGASSERT(rmsg != NULL);

  memcpy(rmsg, msg, space);
  rmsg->cmd = RPMSG_TEST_CMD(RPMSG_TEST_TYPE_SEND_NOCOPY,
                             RPMSG_TEST_VAL_SEND_NOCOPY_RSP);
  ret = rpmsg_send_nocopy(ept, rmsg, space);
  if (ret < 0)
    {
      rpmsg_release_tx_buffer(ept, msg);
    }

  return ret;
}

/****************************************************************************
 * Name: rpmsg_test_send_cb
 ****************************************************************************/

static int rpmsg_test_send_cb(FAR struct rpmsg_endpoint *ept,
                                    FAR void *data, size_t len,
                                    uint32_t src, FAR void *priv)
{
  FAR struct rpmsg_test_msg_s *msg = data;
  FAR sem_t *sem = (FAR sem_t *)(uintptr_t)msg->cookie;
  int ret;

  if (RPMSG_TEST_GET_VALUE(msg->cmd) == RPMSG_TEST_VAL_SEND_RSP)
    {
      ret = rpmsg_test_memcmp(msg->data, RPMSG_TEST_MAGIC1, msg->len);
      DEBUGASSERT(ret);
      syslog(LOG_EMERG, "Rpmsg Test: Test rpmsg send success!\n");
      nxsem_post(sem);
      return ret;
    }

  msg->cmd = RPMSG_TEST_CMD(RPMSG_TEST_TYPE_SEND, RPMSG_TEST_VAL_SEND_RSP);
  return rpmsg_send(ept, msg, len);
}

/****************************************************************************
 * Name: rpmsg_test_ept_cb
 ****************************************************************************/

static int rpmsg_test_ept_cb(FAR struct rpmsg_endpoint *ept,
                             FAR void *data, size_t len, uint32_t src,
                             FAR void *priv)
{
  FAR struct rpmsg_test_msg_s *msg = data;

  switch (RPMSG_TEST_GET_TYPE(msg->cmd))
    {
      case RPMSG_TEST_TYPE_HOLD_RX:
        rpmsg_test_hold_rx_cb(ept, data, len, src, priv);
        break;
      case RPMSG_TEST_TYPE_SEND_NOCOPY:
        rpmsg_test_send_nocopy_cb(ept, data, len, src, priv);
        break;
      case RPMSG_TEST_TYPE_SEND:
        rpmsg_test_send_cb(ept, data, len, src, priv);
        break;
      default:
        break;
    }

  return 0;
}

/****************************************************************************
 * Name: rpmsg_test_hold_rx
 ****************************************************************************/

static int rpmsg_test_hold_rx(FAR struct rpmsg_test_priv_s *priv)
{
  FAR struct rpmsg_endpoint *ept = priv->ept;
  FAR struct rpmsg_test_msg_s *msg;
  FAR struct rpmsg_test_msg_s *nmsg;
  uint32_t space;
  uint32_t nspace;
  int num = 0;
  int ret;

  syslog(LOG_EMERG, "Rpmsg Test: start send\n");

  /* Send all the tx buffer to remote core, remote core will hold this
   * buffer.
   */

  msg = rpmsg_get_tx_payload_buffer(ept, &space, false);
  if (msg == NULL)
    {
      return -ENOMEM;
    }

  for (; ; )
    {
      nmsg = rpmsg_get_tx_payload_buffer(ept, &nspace, false);
      num++;
      msg->cmd = nmsg ? RPMSG_TEST_CMD_HOLD_RX :
                        RPMSG_TEST_CMD_HOLD_RX_END;
      msg->len = space - sizeof(*msg) + 1;
      memset(msg->data, RPMSG_TEST_MAGIC1, msg->len);
      ret = rpmsg_send_nocopy(ept, msg, space);
      DEBUGASSERT(ret >= 0);
      if (nmsg == NULL)
        {
          break;
        }

      msg = nmsg;
      space = nspace;
    }

  syslog(LOG_EMERG, "Rpmsg Test: tx buffer num=%d space=%" PRIu32 "\n",
         num, space);

  syslog(LOG_EMERG, "Rpmsg Test: send finish\n");
  return 0;
}

/****************************************************************************
 * Name: rpmsg_test_send_nocopy
 ****************************************************************************/

static int rpmsg_test_send_nocopy(FAR struct rpmsg_test_priv_s *priv)
{
  FAR struct rpmsg_test_msg_s *msg;
  uint32_t space;
  sem_t sem;
  int ret;

  msg = rpmsg_get_tx_payload_buffer(priv->ept, &space, true);
  DEBUGASSERT(msg != NULL);

  memset(msg, RPMSG_TEST_MAGIC1, space);
  msg->cmd = RPMSG_TEST_CMD(RPMSG_TEST_TYPE_SEND_NOCOPY,
                            RPMSG_TEST_VAL_SEND_NOCOPY);
  msg->len = space - sizeof(struct rpmsg_test_msg_s) + 1;
  msg->cookie = (uint64_t)(uintptr_t)&sem;
  nxsem_init(&sem, 0, 0);

  ret = rpmsg_send_nocopy(priv->ept, msg, space);
  if (ret >= 0)
    {
      nxsem_wait_uninterruptible(&sem);
    }
  else
    {
      rpmsg_release_tx_buffer(priv->ept, msg);
    }

  nxsem_destroy(&sem);
  return ret;
}

/****************************************************************************
 * Name: rpmsg_test_send
 ****************************************************************************/

static int rpmsg_test_send(FAR struct rpmsg_test_priv_s *priv)
{
  FAR struct rpmsg_test_msg_s *msg;
  FAR void *data;
  sem_t sem;
  int datalen;
  int ret;

  datalen = rpmsg_get_tx_buffer_size(priv->ept);
  data = kmm_malloc(datalen);
  DEBUGASSERT(data != NULL);

  msg = (FAR struct rpmsg_test_msg_s *)data;
  memset(msg, RPMSG_TEST_MAGIC1, datalen);
  msg->cmd = RPMSG_TEST_CMD(RPMSG_TEST_TYPE_SEND, RPMSG_TEST_VAL_SEND);
  msg->len = datalen - sizeof(struct rpmsg_test_msg_s) + 1;
  msg->cookie = (uint64_t)(uintptr_t)&sem;
  nxsem_init(&sem, 0, 0);

  ret = rpmsg_send(priv->ept, msg, datalen);
  DEBUGASSERT(ret >= 0);
  nxsem_wait_uninterruptible(&sem);
  nxsem_destroy(&sem);
  kmm_free(data);
  return ret;
}

/****************************************************************************
 * Name: rpmsg_test_release_tx
 ****************************************************************************/

static void rpmsg_test_release_tx(FAR struct rpmsg_test_priv_s *priv)
{
  FAR struct rpmsg_endpoint *ept = priv->ept;
  FAR struct list_node *node;
  struct list_node txhead;
  FAR void **bufs;
  uint32_t space;
  int num = 0;
  int i;

  list_initialize(&txhead);
  while ((node = rpmsg_get_tx_payload_buffer(ept, &space, false)) != NULL)
    {
      num++;
      list_add_tail(&txhead, node);
    }

  syslog(LOG_EMERG, "Rpmsg Test: tx buffer num=%d\n", num);

  bufs = kmm_malloc(num * sizeof(FAR void *));
  DEBUGASSERT(bufs != NULL);

  for (i = 0; i < num; i++)
    {
      bufs[i] = list_remove_head(&txhead);
      rpmsg_release_tx_buffer(priv->ept, bufs[i]);
    }

  /* Restart get tx buffer */

  while ((node = rpmsg_get_tx_payload_buffer(ept, &space, false)) != NULL)
    {
      list_add_tail(&txhead, node);
      for (i = 0; i < num; i++)
        {
          if (node == bufs[i])
            {
              break;
            }
        }

      if (i >= num)
        {
          syslog(LOG_EMERG, "Rpmsg Test: release buffer fail!");
          goto out;
        }
    }

  syslog(LOG_EMERG, "Rpmsg Test: release buffer success!");

  /* Final release tx buffer */

out:
  while ((node = list_remove_head(&txhead)) != NULL)
    {
      rpmsg_release_tx_buffer(priv->ept, node);
    }

  kmm_free(bufs);
  return;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int rpmsg_test(FAR struct rpmsg_endpoint *ept, unsigned long arg)
{
  FAR struct rpmsg_test_priv_s *priv = ept->priv;

  /* Test rx hold buffer case */

  rpmsg_test_hold_rx(priv);

  /* Test send nocopy case */

  rpmsg_test_send_nocopy(priv);

  /* Test send case */

  rpmsg_test_send(priv);

  /* Test release tx case */

  rpmsg_test_release_tx(priv);

  return OK;
}

int rpmsg_test_init(FAR struct rpmsg_device *rdev,
                    FAR struct rpmsg_endpoint *ept)
{
  FAR struct rpmsg_test_priv_s *priv;

  priv = kmm_zalloc(sizeof(*priv));
  if (priv == NULL)
    {
      return -ENOMEM;
    }

  list_initialize(&priv->rxhead);
  priv->ept = ept;
  ept->priv = priv;
  return rpmsg_create_ept(ept, rdev, RPMSG_TEST_EPT_NAME,
                          RPMSG_ADDR_ANY, RPMSG_ADDR_ANY,
                          rpmsg_test_ept_cb, NULL);
}

void rpmsg_test_deinit(FAR struct rpmsg_endpoint *ept)
{
  FAR struct rpmsg_test_priv_s *priv = ept->priv;

  rpmsg_destroy_ept(ept);
  kmm_free(priv);
}
