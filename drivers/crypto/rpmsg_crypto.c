/****************************************************************************
 * drivers/crypto/rpmsg_crypto.c
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

#include <debug.h>
#include <stdio.h>
#include <sys/param.h>

#include <crypto/cryptodev.h>
#include <crypto/virtio_crypto.h>
#include <nuttx/kmalloc.h>
#include <nuttx/mutex.h>
#include <nuttx/rpmsg/rpmsg.h>

#include "rpmsg_crypto.h"

/****************************************************************************
 * Pre-processor definitions
 ****************************************************************************/

#define RPMSG_CRYPTO_SESSIONS_NUM 2

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct rpmsg_crypto_session_s
{
  bool      used;    /* The session is used */
  sem_t     sem;     /* Semaphore used fo rpmsg */
  uint64_t  tid;     /* The session id */
  int       result;  /* The return value of the remote call */
  FAR void *data;    /* The return data buffer of the remote call */
};

struct rpmsg_crypto_s
{
  struct rpmsg_endpoint ept;          /* Rpmsg endpoint */
  FAR const char       *remotecpu;    /* The server cpu name */
  sem_t                 wait;         /* Wait sem, used for preventing any
                                       * opreation until the connection
                                       * between two cpu established.
                                       */
  uint8_t               devid;        /* The device id registered */
  uint8_t               keydevid;     /* The key management id registered */
  uint32_t              sesnum;       /* The number of sessions */
  FAR struct rpmsg_crypto_session_s
                       *sessions;     /* The session data slots */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int rpmsg_crypto_newsession_handler(FAR struct rpmsg_endpoint *ept,
                                           FAR void *data, size_t len,
                                           uint32_t src, FAR void *priv);
static int rpmsg_crypto_process_handler(FAR struct rpmsg_endpoint *ept,
                                        FAR void *data, size_t len,
                                        uint32_t src, FAR void *priv);
static int rpmsg_crypto_freesession_handler(FAR struct rpmsg_endpoint *ept,
                                            FAR void *data, size_t len,
                                            uint32_t src, FAR void *priv);
static int rpmsg_crypto_keyprocess_handler(FAR struct rpmsg_endpoint *ept,
                                           FAR void *data, size_t len,
                                           uint32_t src, FAR void *priv);

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Rpmsg crypto response handler table */

static const rpmsg_ept_cb g_rpmsg_crypto_handler[] =
{
  [RPMSG_CRYPTO_NEWSESSION]  = rpmsg_crypto_newsession_handler,
  [RPMSG_CRYPTO_PROCESS]     = rpmsg_crypto_process_handler,
  [RPMSG_CRYPTO_FREESESSION] = rpmsg_crypto_freesession_handler,
  [RPMSG_CRYPTO_KEYPROCESS]  = rpmsg_crypto_keyprocess_handler,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: rpmsg_crypto_get_tx_payload_buffer
 *
 * Description:
 *   Get the rpmsg crypto tx payload, the buffer is from the rpmsg
 *   share memory that can be accessed by local and remote cpu.
 *
 * Parameters:
 *   priv  - The rpmsg-crypto handle
 *   len   - The got memroy size
 *
 * Returned Values:
 *   NULL     - failure
 *   not NULL - success
 *
 ****************************************************************************/

static FAR void *
rpmsg_crypto_get_tx_payload_buffer(FAR struct rpmsg_crypto_s *priv,
                                   FAR uint32_t *len)
{
  int sval;

  nxsem_get_value(&priv->wait, &sval);
  if (sval <= 0)
    {
      rpmsg_wait(&priv->ept, &priv->wait);
      rpmsg_post(&priv->ept, &priv->wait);
    }

  return rpmsg_get_tx_payload_buffer(&priv->ept, len, true);
}

/****************************************************************************
 * Name: rpmsg_crypto_send_recv
 *
 * Description:
 *   Send and receive the rpmsg crypto data reserved by
 *   rpmsg_get_tx_payload_buffer().
 *
 * Parameters:
 *   priv    - rpmsg crypto handle
 *   command - the command, RPMSG_CRYPTO_NEWSESSION, RPMSG_CRYPTO_PROCESS,
 *                          RPMSG_CRYPTO_FREESESSION
 *   msg     - the message header
 *   len     - length of the payload
 *   data    - the data
 *
 * Returned Values:
 *   OK on success; A negated errno value is returned on any failure.
 *
 ****************************************************************************/

static int rpmsg_crypto_send_recv(FAR struct rpmsg_crypto_s *priv,
                                  FAR struct rpmsg_crypto_session_s *session,
                                  FAR struct rpmsg_crypto_header_s *msg,
                                  uint32_t command, int len, bool waiting)
{
  int ret;

  msg->command = command;
  msg->result  = -ENXIO;
  msg->cookie  = (uintptr_t)session;

  ret = rpmsg_send_nocopy(&priv->ept, msg, len);
  if (ret < 0)
    {
      rpmsg_release_tx_buffer(&priv->ept, msg);
      return ret;
    }

  if (waiting && rpmsg_wait(&priv->ept, &session->sem) >= 0)
    {
      ret = session->result;
    }

  return ret;
}

static int rpmsg_crypto_alloc_session(FAR struct rpmsg_crypto_s *rdev)
{
  FAR struct rpmsg_crypto_session_s *session;

  if (rdev->sesnum == 0)
    {
      rdev->sesnum = RPMSG_CRYPTO_SESSIONS_NUM;
    }

  session = kmm_realloc(rdev->sessions, rdev->sesnum * 2 *
                        sizeof(struct rpmsg_crypto_session_s));
  if (session == NULL)
    {
      rdev->sesnum = 0;
      return -ENOMEM;
    }

  rdev->sessions = session;
  rdev->sesnum *= 2;
  return OK;
}

static int rpmsg_crypto_get_session(FAR struct rpmsg_crypto_s *rdev)
{
  uint32_t i;

  for (i = 0; i < rdev->sesnum; i++)
    {
      if (!rdev->sessions[i].used)
        {
          break;
        }
    }

  if (i == rdev->sesnum && rpmsg_crypto_alloc_session(rdev) < 0)
    {
      return -ENOMEM;
    }

  rdev->sessions[i].used = true;
  nxsem_init(&rdev->sessions[i].sem, 0, 0);
  return i;
}

static int rpmsg_crypto_put_session(FAR struct rpmsg_crypto_s *rdev,
                                    uint32_t sid)
{
  if (sid > rdev->sesnum)
    {
      return -EINVAL;
    }

  nxsem_destroy(&rdev->sessions[sid].sem);
  rdev->sessions[sid].used = false;
  return OK;
}

static int
rpmsg_crypto_fill_session_request(FAR struct cryptoini *cri,
                                  FAR struct rpmsg_crypto_newsession_s *msg)
{
  FAR struct virtio_crypto_op_ctrl_req_s *ctrl = &msg->ctrl;
  int service = virtio_crypto_get_service(cri->cri_alg);
  int algo = virtio_crypto_get_alg(cri->cri_alg);

  if (service < 0)
    {
      return service;
    }

  if (algo < 0)
    {
      return algo;
    }

  memset(ctrl, 0, sizeof(*ctrl));

  /* Pad ctrl header for creating a new session */

  ctrl->header.algo = algo;
  if (cri->cri_flags & CRD_F_KEYID)
    {
      ctrl->header.flag |= VIRTIO_CRYPTO_KEYID;
    }

  switch (service)
    {
      case VIRTIO_CRYPTO_SERVICE_CIPHER:
        ctrl->header.opcode = VIRTIO_CRYPTO_CIPHER_CREATE_SESSION;
        ctrl->op_flf.sym.op_type = VIRTIO_CRYPTO_SYM_OP_CIPHER;
        ctrl->op_flf.sym.op_flf.cipher.algo = algo;
        ctrl->op_flf.sym.op_flf.cipher.key_len = cri->cri_klen / 8;
        ctrl->op_flf.sym.op_flf.cipher.op =
          cri->cri_op == COP_ENCRYPT ?
          VIRTIO_CRYPTO_OP_ENCRYPT : VIRTIO_CRYPTO_OP_DECRYPT;
        break;
      case VIRTIO_CRYPTO_SERVICE_HASH:
        ctrl->header.opcode = VIRTIO_CRYPTO_HASH_CREATE_SESSION;
        ctrl->op_flf.hash.algo = algo;
        ctrl->op_flf.hash.hash_result_len =
          virtio_crypto_get_hash_result_len(algo);
        break;
      case VIRTIO_CRYPTO_SERVICE_MAC:
        ctrl->header.opcode = VIRTIO_CRYPTO_MAC_CREATE_SESSION;
        ctrl->op_flf.mac.algo = algo;
        ctrl->op_flf.mac.hash_result_len =
          virtio_crypto_get_hash_result_len(algo);
        ctrl->op_flf.mac.auth_key_len = cri->cri_klen / 8;
        break;
      case VIRTIO_CRYPTO_SERVICE_AEAD:
        ctrl->header.opcode = VIRTIO_CRYPTO_MAC_CREATE_SESSION;
        ctrl->op_flf.aead.algo = algo;
        ctrl->op_flf.aead.key_len = cri->cri_klen / 8;
        ctrl->op_flf.aead.tag_len = virtio_crypto_get_tag_len(algo);
        ctrl->op_flf.aead.op = cri->cri_op == COP_ENCRYPT ?
          VIRTIO_CRYPTO_OP_ENCRYPT : VIRTIO_CRYPTO_OP_DECRYPT;
        break;
      default:
        return -EINVAL;
    }

  return OK;
}

static int rpmsg_crypto_newsession(FAR uint32_t *sid,
                                   FAR struct cryptoini *cri)
{
  FAR struct rpmsg_crypto_s *rdev;
  FAR struct rpmsg_crypto_newsession_s *msg;
  uint32_t len = sizeof(*msg);
  uint32_t space;
  int ret;

  if (sid == NULL || cri == NULL)
    {
      return -EINVAL;
    }

  rdev = (FAR struct rpmsg_crypto_s *)crypto_driver_get_priv(*sid);
  if (rdev == NULL)
    {
      rpmsgerr("Invalid context\n");
      return -EINVAL;
    }

  *sid = rpmsg_crypto_get_session(rdev);
  msg = rpmsg_crypto_get_tx_payload_buffer(rdev, &space);
  if (msg == NULL)
    {
      rpmsgerr("No space for payload\n");
      return -ENOMEM;
    }

  /* create newsession msg */

  ret = rpmsg_crypto_fill_session_request(cri, msg);
  if (ret < 0)
    {
      rpmsgerr("Invalid parameters in newsession\n");
      rpmsg_release_tx_buffer(&rdev->ept, msg);
      goto fail;
    }

  if (cri->cri_klen > 0)
    {
      memcpy(msg->buf, cri->cri_key, cri->cri_klen / 8);
      len += cri->cri_klen / 8;
    }

  ret = rpmsg_crypto_send_recv(rdev, &rdev->sessions[*sid],
                               &msg->header, RPMSG_CRYPTO_NEWSESSION,
                               len, true);
  if (ret < 0)
    {
      rpmsgerr("Rpmsg Crypto newsession send failed, ret=%d\n", ret);
      goto fail;
    }

  return rdev->sessions[*sid].result;

fail:
  rpmsg_crypto_put_session(rdev, *sid);
  return ret;
}

static int rpmsg_crypto_fill_data_request(
                        FAR struct cryptop *crp,
                        FAR struct rpmsg_crypto_process_s *msg)
{
  FAR struct virtio_crypto_op_data_req_s *data = &msg->data;
  FAR struct virtio_crypto_op_header_s *header = &data->header;
  FAR struct cryptodesc *crda = crp->crp_desc;
  int service = virtio_crypto_get_service(crda->crd_alg);
  int algo = virtio_crypto_get_alg(crda->crd_alg);

  if (service < 0)
    {
      return service;
    }

  if (algo < 0)
    {
      return algo;
    }

  memset(data, 0, sizeof(struct virtio_crypto_op_data_req_s));
  header->algo = algo;
  switch (service)
    {
      case VIRTIO_CRYPTO_SERVICE_CIPHER:
        header->opcode = crda->crd_flags & CRD_F_ENCRYPT ?
          VIRTIO_CRYPTO_CIPHER_ENCRYPT : VIRTIO_CRYPTO_CIPHER_DECRYPT;
        data->op_flf.sym.op_type = VIRTIO_CRYPTO_SYM_OP_CIPHER;
        data->op_flf.sym.op_type_flf.cipher.src_data_len = crp->crp_ilen;
        data->op_flf.sym.op_type_flf.cipher.dst_data_len = crp->crp_olen;
        crp->crp_opaque = crp->crp_dst;
        break;
      case VIRTIO_CRYPTO_SERVICE_HASH:
        header->opcode = VIRTIO_CRYPTO_HASH;
        data->op_flf.hash.src_data_len = crp->crp_ilen;
        break;
      case VIRTIO_CRYPTO_SERVICE_MAC:
        header->opcode = VIRTIO_CRYPTO_MAC;
        data->op_flf.mac.hdr.src_data_len = crp->crp_ilen;
        break;
      case VIRTIO_CRYPTO_SERVICE_AEAD:
        header->opcode = crda->crd_flags & CRD_F_ENCRYPT ?
          VIRTIO_CRYPTO_AEAD_ENCRYPT : VIRTIO_CRYPTO_AEAD_DECRYPT;
        data->op_flf.aead.iv_len = crp->crp_ivlen;
        data->op_flf.aead.src_data_len = crp->crp_ilen;
        data->op_flf.aead.dst_data_len = crp->crp_olen;
        data->op_flf.aead.aad_len = crp->crp_aadlen;
        data->op_flf.aead.tag_len = virtio_crypto_get_tag_len(algo);
        break;
      default:
        return -ENOTSUP;
    }

  return OK;
}

static FAR struct rpmsg_crypto_process_s *
rpmsg_crypto_get_process_msg(FAR struct rpmsg_crypto_s * rdev,
                             FAR struct rpmsg_crypto_process_s *src)
{
  FAR struct rpmsg_crypto_process_s *msg;
  uint32_t space;

  msg = rpmsg_crypto_get_tx_payload_buffer(rdev, &space);
  if (msg == NULL)
    {
      return NULL;
    }

  msg->header = src->header;
  msg->data = src->data;
  return msg;
}

static int
rpmsg_crypto_process_endec(FAR struct rpmsg_crypto_s *rdev,
                           FAR struct rpmsg_crypto_process_s *info,
                           FAR struct rpmsg_crypto_session_s *session,
                           FAR struct cryptop *crp)
{
  FAR struct rpmsg_crypto_process_s *msg;
  uint32_t space = rpmsg_get_tx_buffer_size(&rdev->ept);
  bool need_iv = !(crp->crp_desc->crd_flags & CRD_F_UPDATE);
  int remaining = crp->crp_ilen;
  uint32_t header_size;
  uint32_t offset = 0;
  uint32_t data_size;
  FAR char *buf;
  int ret;

  while (remaining > 0)
    {
      msg = rpmsg_crypto_get_process_msg(rdev, info);
      if (msg == NULL)
        {
          return -ENOMEM;
        }

      buf = msg->buf;
      header_size = sizeof(*msg);
      data_size = space - sizeof(*msg);
      if (remaining == crp->crp_ilen && need_iv)
        {
          msg->data.op_flf.sym.op_type_flf.cipher.iv_len = crp->crp_ivlen;
          memcpy(buf, crp->crp_iv, crp->crp_ivlen);
          header_size += crp->crp_ivlen;
          data_size -= crp->crp_ivlen;
          buf += crp->crp_ivlen;
        }
      else
        {
          msg->data.op_flf.sym.op_type_flf.cipher.iv_len = 0;
        }

      data_size = MIN(data_size, remaining);
      memcpy(buf, crp->crp_buf + offset, data_size);
      ret = rpmsg_crypto_send_recv(rdev, session, &msg->header,
                                   RPMSG_CRYPTO_PROCESS,
                                   header_size + data_size, true);
      if (ret < 0)
        {
          rpmsgerr("Rpmsg Crypto process failed, ret=%d\n", ret);
          return ret;
        }

      remaining -= data_size;
      offset += data_size;
    }

  return session->result;
}

static int
rpmsg_crypto_process_hash_mac(FAR struct rpmsg_crypto_s *rdev,
                              FAR struct rpmsg_crypto_process_s *info,
                              FAR struct rpmsg_crypto_session_s *session,
                              FAR struct cryptop *crp)
{
  FAR struct rpmsg_crypto_process_s *msg;
  int service = virtio_crypto_get_service(crp->crp_desc->crd_alg);
  uint32_t space = rpmsg_get_tx_buffer_size(&rdev->ept);
  uint32_t flags = crp->crp_desc->crd_flags;
  uint32_t data_size = space - sizeof(*msg);
  int remaining = crp->crp_ilen;
  uint32_t offset = 0;
  int ret;

  do
    {
      msg = rpmsg_crypto_get_process_msg(rdev, info);
      if (msg == NULL)
        {
          return -ENOMEM;
        }

      data_size = MIN(space - sizeof(*msg), remaining);
      memcpy(msg->buf, crp->crp_buf + offset, data_size);
      offset += data_size;
      remaining -= data_size;
      if (remaining == 0 && !(flags & CRD_F_UPDATE))
        {
          if (service == VIRTIO_CRYPTO_SERVICE_HASH)
            {
              msg->data.op_flf.hash.hash_result_len = crp->crp_olen;
            }
          else if (service == VIRTIO_CRYPTO_SERVICE_MAC)
            {
              msg->data.op_flf.mac.hdr.hash_result_len = crp->crp_olen;
            }
        }

      ret = rpmsg_crypto_send_recv(rdev, session, &msg->header,
                                   RPMSG_CRYPTO_PROCESS,
                                   sizeof(*msg) + data_size,
                                   remaining == 0 &&
                                   !(flags & CRD_F_UPDATE));

      if (ret < 0)
        {
          rpmsgerr("Rpmsg Crypto process failed, ret=%d\n", ret);
          return ret;
        }
    }
  while (remaining > 0);

  return session->result;
}

static int rpmsg_crypto_process(FAR struct cryptop *crp)
{
  FAR struct rpmsg_crypto_s *rdev;
  FAR struct rpmsg_crypto_session_s *session;
  struct rpmsg_crypto_process_s info;
  uint32_t hid = (uint32_t)(crp->crp_sid >> 32);
  uint32_t sid = (uint32_t)crp->crp_sid;
  int ret;

  rdev = (FAR struct rpmsg_crypto_s *)crypto_driver_get_priv(hid);
  if (rdev == NULL)
    {
      rpmsgerr("Invalid context\n");
      return -EINVAL;
    }

  if (sid > rdev->sesnum || !rdev->sessions[sid].used)
    {
      rpmsgerr("Invalid sid\n");
      return -EINVAL;
    }

  session = &rdev->sessions[sid];
  ret = rpmsg_crypto_fill_data_request(crp, &info);
  if (ret < 0)
    {
      rpmsgerr("Failed to fill data request\n");
      return ret;
    }

  info.header.tid = session->tid;
  session->data = crp;
  if (info.data.header.opcode == VIRTIO_CRYPTO_CIPHER_ENCRYPT ||
      info.data.header.opcode == VIRTIO_CRYPTO_CIPHER_DECRYPT)
    {
      return rpmsg_crypto_process_endec(rdev, &info, session, crp);
    }
  else if (info.data.header.opcode == VIRTIO_CRYPTO_HASH ||
           info.data.header.opcode == VIRTIO_CRYPTO_MAC)
    {
      return rpmsg_crypto_process_hash_mac(rdev, &info, session, crp);
    }

  return -ENOTSUP;
}

static int rpmsg_crypto_freesession(uint64_t tid)
{
  FAR struct rpmsg_crypto_freesession_s *msg;
  FAR struct rpmsg_crypto_session_s *session;
  FAR struct rpmsg_crypto_s *rdev;
  uint32_t hid = (uint32_t)(tid >> 32);
  uint32_t sid = (uint32_t)tid;
  uint32_t space;
  int ret;

  rdev = (FAR struct rpmsg_crypto_s *)crypto_driver_get_priv(hid);
  if (rdev == NULL)
    {
      rpmsgerr("Invalid context\n");
      return -EINVAL;
    }

  if (sid > rdev->sesnum)
    {
      rpmsgerr("Invalid sid\n");
      return -EINVAL;
    }

  session = &rdev->sessions[sid];
  msg = rpmsg_crypto_get_tx_payload_buffer(rdev, &space);
  if (msg == NULL)
    {
      rpmsgerr("No space for payload\n");
      return -ENOMEM;
    }

  msg->ctrl.header.opcode = VIRTIO_CRYPTO_CIPHER_DESTROY_SESSION;
  msg->header.tid = session->tid;
  ret = rpmsg_crypto_send_recv(rdev, session, &msg->header,
                               RPMSG_CRYPTO_FREESESSION,
                               sizeof(*msg), true);
  if (ret < 0)
    {
      rpmsgerr("Send msg failed\n");
      return ret;
    }

  rpmsg_crypto_put_session(rdev, sid);
  return ret;
}

static int rpmsg_crypto_keyprocess(FAR struct cryptkop *krp)
{
  FAR struct rpmsg_crypto_session_s *session;
  FAR struct rpmsg_crypto_keyprocess_s *msg;
  FAR struct rpmsg_crypto_s *rdev;
  uint32_t data_len = 0;
  uint32_t space;
  uint32_t sid;
  int ret;

  rdev = (FAR struct rpmsg_crypto_s *)crypto_driver_get_priv(krp->krp_hid);
  if (rdev == NULL)
    {
      rpmsgerr("Invalid context\n");
      return -EINVAL;
    }

  msg = rpmsg_crypto_get_tx_payload_buffer(rdev, &space);
  if (msg == NULL)
    {
      rpmsgerr("No space for payload\n");
      return -ENOMEM;
    }

  sid = rpmsg_crypto_get_session(rdev);
  session = &rdev->sessions[sid];
  memset(msg, 0, sizeof(*msg));
  msg->data.cmd = krp->krp_op;
  switch (msg->data.cmd)
    {
      case CRK_ALLOCATE_KEY:
        msg->data.dst_data_len = krp->krp_param[0].crp_nbits / 8;
        break;
      case CRK_VALIDATE_KEYID:
      case CRK_DELETE_KEY:
      case CRK_SAVE_KEY:
      case CRK_LOAD_KEY:
      case CRK_UNLOAD_KEY:
        msg->data.name_len = krp->krp_param[0].crp_nbits / 8;
        break;
      case CRK_IMPORT_KEY:
      case CRK_GENERATE_AES_KEY:
      case CRK_GENERATE_SECP256R1_KEY:
        msg->data.name_len = krp->krp_param[0].crp_nbits / 8;
        msg->data.src_data_len = krp->krp_param[1].crp_nbits / 8;
        break;
      case CRK_EXPORT_KEY:
        msg->data.name_len = krp->krp_param[0].crp_nbits / 8;
        msg->data.dst_data_len = krp->krp_param[1].crp_nbits / 8;
        break;
    }

  if (msg->data.name_len)
    {
      memcpy(msg->buf, krp->krp_param[0].crp_p, msg->data.name_len);
      data_len += msg->data.name_len;
    }

  if (msg->data.src_data_len)
    {
      memcpy(msg->buf + data_len, krp->krp_param[1].crp_p,
                                  msg->data.src_data_len);
      data_len += msg->data.src_data_len;
    }

  if (krp->krp_flags & CRYPTO_F_CBIMM)
    {
      session->data = krp->krp_opaque;
    }
  else
    {
      session->data = krp;
    }

  ret = rpmsg_crypto_send_recv(rdev, session, &msg->header,
                               RPMSG_CRYPTO_KEYPROCESS,
                               sizeof(*msg) + data_len,
                               !(krp->krp_flags & CRYPTO_F_CBIMM));
  if (ret < 0)
    {
      rpmsgerr("Send msg failed\n");
      return ret;
    }

  rpmsg_crypto_put_session(rdev, sid);
  return OK;
}

static int rpmsg_crypto_newsession_handler(FAR struct rpmsg_endpoint *ept,
                                           FAR void *data, size_t len,
                                           uint32_t src, FAR void *priv)
{
  FAR struct rpmsg_crypto_header_s *header = data;
  FAR struct rpmsg_crypto_session_s *session =
    (FAR struct rpmsg_crypto_session_s *)(uintptr_t)header->cookie;

  session->result = header->result;
  if (session->result >= 0)
    {
      session->tid = header->tid;
    }

  return rpmsg_post(ept, &session->sem);
}

static int rpmsg_crypto_process_handler(FAR struct rpmsg_endpoint *ept,
                                        FAR void *data, size_t len,
                                        uint32_t src, FAR void *priv)
{
  FAR struct rpmsg_crypto_header_s *header = data;
  FAR struct rpmsg_crypto_session_s *session =
    (FAR struct rpmsg_crypto_session_s *)(uintptr_t)header->cookie;
  FAR struct rpmsg_crypto_process_s *msg = data;
  FAR struct cryptop *crp = session->data;

  len -= sizeof(*msg);
  session->result = header->result;
  if (session->result == 0 && len > 0)
    {
      if (msg->data.header.opcode == VIRTIO_CRYPTO_CIPHER_ENCRYPT ||
          msg->data.header.opcode == VIRTIO_CRYPTO_CIPHER_DECRYPT)
        {
          memcpy(crp->crp_opaque, msg->buf, len);
          crp->crp_opaque += len;
        }
      else if (msg->data.header.opcode == VIRTIO_CRYPTO_HASH ||
               msg->data.header.opcode == VIRTIO_CRYPTO_MAC)
        {
          memcpy(crp->crp_mac, msg->buf, len);
        }
    }

  return rpmsg_post(ept, &session->sem);
}

static int rpmsg_crypto_freesession_handler(FAR struct rpmsg_endpoint *ept,
                                            FAR void *data, size_t len,
                                            uint32_t src, FAR void *priv)
{
  FAR struct rpmsg_crypto_header_s *header = data;
  FAR struct rpmsg_crypto_session_s *session =
    (FAR struct rpmsg_crypto_session_s *)(uintptr_t)header->cookie;

  session->result = header->result;
  return rpmsg_post(ept, &session->sem);
}

static int rpmsg_crypto_keyprocess_handler(FAR struct rpmsg_endpoint *ept,
                                           FAR void *data, size_t len,
                                           uint32_t src, FAR void *priv)
{
  FAR struct rpmsg_crypto_header_s *header = data;
  FAR struct rpmsg_crypto_session_s *session =
    (FAR struct rpmsg_crypto_session_s *)(uintptr_t)header->cookie;
  FAR struct rpmsg_crypto_keyprocess_s *msg = data;
  FAR struct cryptkop *krp = session->data;

  len -= sizeof(*msg);
  session->result = header->result;
  krp->krp_status = header->result;
  if (session->result == 0 && msg->data.dst_data_len > 0)
    {
      memcpy(krp->krp_param[krp->krp_iparams].crp_p, msg->buf, len);
      if (krp->krp_op == CRK_EXPORT_KEY)
        {
          krp->krp_param[1].crp_nbits = len * 8;
        }
    }

  if ((krp->krp_flags & CRYPTO_F_CBIMM) && krp->krp_callback)
    {
      return krp->krp_callback(krp);
    }

  return rpmsg_post(ept, &session->sem);
}

/****************************************************************************
 * Name: rpmsg_crypto_ept_cb
 *
 * Description:
 *   Rpmsg crypto end point callback function, this function will be called
 *   when receive the remote cpu message.
 *
 * Parameters:
 *   ept  - The rpmsg-crypto end point
 *   data - The received data
 *   len  - The received data length
 *   src  - unknow
 *   priv - unknow
 *
 * Returned Values:
 *   OK on success; A negated errno value is returned on any failure.
 *
 ****************************************************************************/

static int rpmsg_crypto_ept_cb(FAR struct rpmsg_endpoint *ept,
                               FAR void *data, size_t len, uint32_t src,
                               FAR void *priv)
{
  FAR struct rpmsg_crypto_header_s *header = data;
  uint32_t command = header->command;

  if (command < nitems(g_rpmsg_crypto_handler))
    {
      return g_rpmsg_crypto_handler[command](ept, data, len, src, priv);
    }

  rpmsgerr("Failed to find commend handler\n");
  return -EINVAL;
}

/****************************************************************************
 * Name: rpmsg_crypto_ns_bound
 *
 * Description:
 *   Rpmsg crypto end point service bound callback function , called when
 *   remote end point address is received.
 *
 * Parameters:
 *   ept  - The rpmsg-crypto end point
 *
 * Returned Values:
 *   None
 *
 ****************************************************************************/

static void rpmsg_crypto_ns_bound(FAR struct rpmsg_endpoint *ept)
{
  FAR struct rpmsg_crypto_s *priv = ept->priv;

  rpmsg_post(&priv->ept, &priv->wait);
}

/****************************************************************************
 * Name: rpmsg_crypto_created
 *
 * Description:
 *   Rpmsg crypto create function, this function will be called by rptun to
 *   create a rpmsg-crypto end point.
 *
 * Parameters:
 *   rdev  - The rpmsg-crypto end point
 *   priv_ - Rpmsg-crypto handle
 *
 * Returned Values:
 *   None
 *
 ****************************************************************************/

static void rpmsg_crypto_created(FAR struct rpmsg_device *rdev,
                                 FAR void *priv_)
{
  FAR struct rpmsg_crypto_s *priv = priv_;

  if (strcmp(priv->remotecpu, rpmsg_get_cpuname(rdev)) == 0)
    {
      priv->ept.priv = priv;
      priv->ept.ns_bound_cb = rpmsg_crypto_ns_bound;
      rpmsg_create_ept(&priv->ept, rdev, RPMSG_CRYPTO_NAME_PREFIX,
                       RPMSG_ADDR_ANY, RPMSG_ADDR_ANY,
                       rpmsg_crypto_ept_cb, NULL);
    }
}

/****************************************************************************
 * Name: rpmsg_crypto_destroy
 *
 * Description:
 *   Rpmsg crypto destroy function, this function will be called by rptun to
 *   destroy rpmsg-crypto end point.
 *
 * Parameters:
 *   rdev     - The rpmsg-crypto end point
 *   priv_    - Rpmsg-device handle
 *
 * Returned Values:
 *   None
 *
 ****************************************************************************/

static void rpmsg_crypto_destroy(FAR struct rpmsg_device *rdev,
                                 FAR void *priv_)
{
  FAR struct rpmsg_crypto_s *priv = priv_;

  if (strcmp(priv->remotecpu, rpmsg_get_cpuname(rdev)) == 0)
    {
      rpmsg_destroy_ept(&priv->ept);
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: rpmsg_crypto_register
 *
 * Description:
 *   Rpmsg-crypto client initialize function, the client cpu should call
 *   this function in the board initialize process.
 *
 * Parameters:
 *   remotecpu  - the server cpu name
 *   algs       - the crypto driver support symmetrical algorithms
 *   kalgs      - the crypto driver support asymmetrical algorithms
 *
 * Returned Values:
 *   OK on success; A negated errno value is returned on any failure.
 *
 ****************************************************************************/

int rpmsg_crypto_register(FAR const char *remotecpu)
{
  FAR struct rpmsg_crypto_s *rdev;
  int algs[CRYPTO_ALGORITHM_MAX + 1];
  int keyalgs[CRYPTO_ALGORITHM_MAX + 1];
  int ret;
  int i;

  /* Arguments check */

  if (remotecpu == NULL)
    {
      return -EINVAL;
    }

  /* Initialize the rpmsg crypto */

  rdev = kmm_zalloc(sizeof(struct rpmsg_crypto_s));
  if (rdev == NULL)
    {
      rpmsgerr("rpmsg crypto failed to allocate memory\n");
      return -ENOMEM;
    }

  rdev->remotecpu = remotecpu;
  nxsem_init(&rdev->wait, 0, 0);

  /* Register the rpmsg callback */

  ret = rpmsg_register_callback(rdev,
                                rpmsg_crypto_created,
                                rpmsg_crypto_destroy,
                                NULL,
                                NULL);
  if (ret < 0)
    {
      rpmsgerr("rpmsg register callback failed, ret=%d\n", ret);
      goto fail;
    }

  /* Register NuttX driver for cipher */

  rdev->devid = crypto_get_driverid(CRYPTOCAP_F_REMOTE);

  crypto_driver_set_priv(rdev->devid, rdev);

  for (i = 0; i < CRYPTO_ALGORITHM_MAX + 1; i++)
    {
      algs[i] = CRYPTO_ALG_FLAG_SUPPORTED;
    }

  ret = crypto_register(rdev->devid, algs, rpmsg_crypto_newsession,
                        rpmsg_crypto_freesession, rpmsg_crypto_process);

  if (ret < 0)
    {
      rpmsgerr("rpmsg crypto cipher register failed, ret=%d\n", ret);
      goto fail2;
    }

  /* Register NuttX driver for key management */

  rdev->keydevid = crypto_get_driverid(CRYPTOCAP_F_KEY_MGMT);

  crypto_driver_set_priv(rdev->keydevid, rdev);

  keyalgs[CRK_ALLOCATE_KEY] = CRYPTO_ALG_FLAG_SUPPORTED;
  keyalgs[CRK_VALIDATE_KEYID] = CRYPTO_ALG_FLAG_SUPPORTED;
  keyalgs[CRK_IMPORT_KEY] = CRYPTO_ALG_FLAG_SUPPORTED;
  keyalgs[CRK_DELETE_KEY] = CRYPTO_ALG_FLAG_SUPPORTED;
  keyalgs[CRK_EXPORT_KEY] = CRYPTO_ALG_FLAG_SUPPORTED;
  keyalgs[CRK_GENERATE_AES_KEY] = CRYPTO_ALG_FLAG_SUPPORTED;
  keyalgs[CRK_GENERATE_SECP256R1_KEY] = CRYPTO_ALG_FLAG_SUPPORTED;
  keyalgs[CRK_SAVE_KEY] = CRYPTO_ALG_FLAG_SUPPORTED;
  keyalgs[CRK_LOAD_KEY] = CRYPTO_ALG_FLAG_SUPPORTED;
  keyalgs[CRK_UNLOAD_KEY] = CRYPTO_ALG_FLAG_SUPPORTED;

  ret = crypto_kregister(rdev->keydevid, keyalgs, rpmsg_crypto_keyprocess);

  if (ret < 0)
    {
      rpmsgerr("rpmsg crypto key register failed, ret=%d\n", ret);
      goto fail2;
    }

  return ret;

fail:
  kmm_free(rdev);
fail2:
  rpmsg_unregister_callback(rdev,
                            rpmsg_crypto_created,
                            rpmsg_crypto_destroy,
                            NULL,
                            NULL);
  return ret;
}
