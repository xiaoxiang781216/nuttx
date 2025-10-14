/****************************************************************************
 * drivers/virtio/virtio-crypto.c
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

#include <alloca.h>
#include <debug.h>

#include <crypto/virtio_crypto.h>
#include <nuttx/spinlock.h>
#include <nuttx/virtio/virtio.h>
#include <nuttx/virtio/virtio-crypto.h>

#include "virtio-crypto.h"

/****************************************************************************
 * Private Type Definitions
 ****************************************************************************/

struct virtio_crypto_s
{
  FAR struct virtio_device *vdev;

  /* To protect the vq operations for the controlq and dataq */

  spinlock_t lock;

  /* Maximum number of data queues supported by the device */

  uint32_t max_data_queues;

  /* Number of queue currently used by the driver */

  uint32_t curr_queue;

  /* Specifies the services mask which the device supports,
   * see VIRTIO_CRYPTO_SERVICE_*
   */

  uint32_t crypto_services;

  /* Detailed algorithms mask */

  uint32_t cipher_algo_l;
  uint32_t cipher_algo_h;
  uint32_t hash_algo;
  uint32_t mac_algo_l;
  uint32_t mac_algo_h;
  uint32_t aead_algo;
  uint32_t akcipher_algo;

  /* Maximum length of cipher key */

  uint32_t max_cipher_key_len;

  /* Maximum length of authenticated key */

  uint32_t max_auth_key_len;

  /* Maximum size of per request */

  uint64_t max_size;

  uint8_t dev_id;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/* Crypto driver operation function */

static int virtio_crypto_newsession(FAR uint32_t *sid,
                                    FAR struct cryptoini *cri);
static int virtio_crypto_process(FAR struct cryptop *crp);
static int virtio_crypto_freesession(uint64_t tid);

static int virtio_crypto_fill_crtlq_request(FAR struct cryptoini *cri,
                        FAR struct virtio_crypto_op_ctrl_req_s *ctrl);
static int virtio_crypto_fill_dataq_request(
              FAR struct cryptop *crp, FAR struct virtqueue_buf *vb,
              FAR struct virtio_crypto_op_data_req_s *data,
              FAR struct virtio_crypto_inhdr_s *inhdr,
              FAR int *num_out, FAR int *num_in);

static void virtio_crypto_notify(FAR struct virtqueue *vq);
static int  virtio_crypto_create_vqs(FAR struct virtio_crypto_s *vcrypto);

/* Virtio driver functions */

static int virtio_crypto_probe(FAR struct virtio_device *vdev);
static void virtio_crypto_remove(FAR struct virtio_device *vdev);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct virtio_driver g_virtio_crypto_driver =
{
  LIST_INITIAL_VALUE(g_virtio_crypto_driver.node), /* node */
  VIRTIO_ID_CRYPTO,                                /* device id */
  virtio_crypto_probe,                             /* probe */
  virtio_crypto_remove,                            /* remove */
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int virtio_crypto_fill_crtlq_request(
             FAR struct cryptoini *cri,
             FAR struct virtio_crypto_op_ctrl_req_s *ctrl)
{
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

  memset(ctrl, 0, sizeof(struct virtio_crypto_op_ctrl_req_s));

  /* Pad ctrl header for creating a new session */

  ctrl->header.algo = algo;

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
        ctrl->op_flf.mac.auth_key_len =
              virtio_crypto_get_auth_key_len(algo);
        break;
      case VIRTIO_CRYPTO_SERVICE_AEAD:
        ctrl->header.opcode = VIRTIO_CRYPTO_MAC_CREATE_SESSION;
        ctrl->op_flf.aead.algo = algo;
        ctrl->op_flf.aead.key_len = cri->cri_klen / 8;
        ctrl->op_flf.aead.tag_len = virtio_crypto_get_hash_result_len(algo);
        ctrl->op_flf.aead.op = cri->cri_op == COP_ENCRYPT ?
                VIRTIO_CRYPTO_OP_ENCRYPT : VIRTIO_CRYPTO_OP_DECRYPT;
        break;
      default:
        return -EINVAL;
    }

  return OK;
}

static int virtio_crypto_newsession(FAR uint32_t *sid,
                                    FAR struct cryptoini *cri)
{
  struct virtio_crypto_create_session_input_s input;
  struct virtio_crypto_op_ctrl_req_s ctrl;
  FAR struct virtio_crypto_s *vcrypto;
  FAR struct virtqueue *vq;
  struct virtqueue_buf vb[3];
  unsigned int num_out = 0;
  unsigned int num_in = 0;
  sem_t sem;
  int ret;

  vcrypto = (struct virtio_crypto_s *)crypto_driver_get_priv(*sid);
  if (vcrypto == NULL)
    {
      vrterr("Virtio Crypto newsession: Invalid context\n");
      return -EINVAL;
    }

  vq = vcrypto->vdev->vrings_info[vcrypto->max_data_queues].vq;

  /* Change Nuttx Crypto para into Virtio para */

  ret = virtio_crypto_fill_crtlq_request(cri, &ctrl);
  if (ret < 0)
    {
      vrterr("Virtio Crypto: Invalid parameters in newsession\n");
      return ret;
    }

  nxsem_init(&sem, 0, 0);

  input.status = VIRTIO_CRYPTO_ERR;

  vb[num_out].buf = &ctrl;
  vb[num_out++].len = sizeof(ctrl);

  if (cri->cri_klen > 0)
    {
      vb[num_out].buf = cri->cri_key;
      vb[num_out++].len = cri->cri_klen / 8;
    }

  vb[num_out + num_in].buf = &input;
  vb[num_out + num_in++].len = sizeof(input);

  virtqueue_add_buffer_lock(vq, vb, num_out, num_in, &sem,
                            &vcrypto->lock);
  virtqueue_kick_lock(vq, &vcrypto->lock);

  /* Wait for completion */

  nxsem_wait_uninterruptible(&sem);

  nxsem_destroy(&sem);
  if (input.status != VIRTIO_CRYPTO_OK)
    {
      vrterr("virtio_crypto_s: Create session failed status: %u\n",
                                                     input.status);
      return virtio_crypto_get_errcode(input.status);
    }

  *sid = input.session_id;
  return ret;
}

static int virtio_crypto_freesession(uint64_t tid)
{
  struct virtio_crypto_destroy_session_input_s input;
  struct virtio_crypto_op_ctrl_req_s ctrl;
  FAR struct virtio_crypto_s *vcrypto;
  FAR struct virtqueue *vq;
  struct virtqueue_buf vb[2];
  uint32_t hid = (uint32_t)(tid >> 32);
  sem_t sem;
  int ret = 0;

  vcrypto = crypto_driver_get_priv(hid);
  if (vcrypto == NULL)
    {
      vrterr("Virtio Crypto freesession: Invalid context\n");
      return -EINVAL;
    }

  vq = vcrypto->vdev->vrings_info[vcrypto->max_data_queues].vq;

  memset(&ctrl, 0, sizeof(ctrl));
  nxsem_init(&sem, 0, 0);

  /* Pad ctrl header */

  ctrl.header.opcode = VIRTIO_CRYPTO_CIPHER_DESTROY_SESSION;
  ctrl.op_flf.destroy.session_id = tid & 0xffffffff;

  input.status = VIRTIO_CRYPTO_ERR;

  vb[0].buf = &ctrl;
  vb[0].len = sizeof(ctrl);
  vb[1].buf = &input;
  vb[1].len = sizeof(input);

  virtqueue_add_buffer_lock(vq, vb, 1, 1, &sem, &vcrypto->lock);
  virtqueue_kick_lock(vq, &vcrypto->lock);

  /* Wait for completion */

  nxsem_wait_uninterruptible(&sem);

  nxsem_destroy(&sem);
  if (input.status != VIRTIO_CRYPTO_OK)
    {
      vrterr("virtio_crypto_s: Close session failed status: %u,"
             "session_id: 0x%lu\n", input.status,
             ctrl.op_flf.destroy.session_id);

      ret = virtio_crypto_get_errcode(input.status);
    }

  return ret;
}

static void virtio_crypto_fill_dataq_request_cipher(
              FAR struct cryptop *crp, FAR struct virtqueue_buf *vb,
              FAR struct virtio_crypto_op_data_req_s *data,
              FAR struct virtio_crypto_inhdr_s *inhdr,
              FAR int *num_out, FAR int *num_in)
{
  data->header.opcode = crp->crp_desc->crd_flags & CRD_F_ENCRYPT ?
          VIRTIO_CRYPTO_CIPHER_ENCRYPT : VIRTIO_CRYPTO_CIPHER_DECRYPT;
  data->op_flf.sym.op_type = VIRTIO_CRYPTO_SYM_OP_CIPHER;
  data->op_flf.sym.op_type_flf.cipher.src_data_len = crp->crp_ilen;
  data->op_flf.sym.op_type_flf.cipher.dst_data_len = crp->crp_olen;

  vb[*num_out].buf = data;
  vb[(*num_out)++].len = sizeof(*data);
  if (crp->crp_ivlen > 0 && !(crp->crp_desc->crd_flags & CRD_F_UPDATE))
    {
      vb[*num_out].buf = crp->crp_iv;
      vb[(*num_out)++].len = crp->crp_ivlen;
      data->op_flf.sym.op_type_flf.cipher.iv_len = crp->crp_ivlen;
    }

  vb[*num_out].buf = crp->crp_buf;
  vb[(*num_out)++].len = crp->crp_ilen;
  vb[*num_out + *num_in].buf = crp->crp_dst;
  vb[*num_out + (*num_in)++].len = crp->crp_olen;
  vb[*num_out + *num_in].buf = inhdr;
  vb[*num_out + (*num_in)++].len = sizeof(*inhdr);
}

static void virtio_crypto_fill_dataq_request_hash(
              FAR struct cryptop *crp, FAR struct virtqueue_buf *vb,
              FAR struct virtio_crypto_op_data_req_s *data,
              FAR struct virtio_crypto_inhdr_s *inhdr,
              FAR int *num_out, FAR int *num_in)
{
  data->header.opcode = VIRTIO_CRYPTO_HASH;
  data->op_flf.hash.src_data_len = crp->crp_ilen;

  vb[*num_out].buf = data;
  vb[(*num_out)++].len = sizeof(*data);

  vb[*num_out].buf = crp->crp_buf;
  vb[(*num_out)++].len = crp->crp_ilen;
  if (!(crp->crp_desc->crd_flags & CRD_F_UPDATE))
    {
      vb[*num_out + *num_in].buf = crp->crp_mac;
      vb[*num_out + (*num_in)++].len = crp->crp_olen;
      data->op_flf.hash.hash_result_len = crp->crp_olen;
    }

  vb[*num_out + *num_in].buf = inhdr;
  vb[*num_out + (*num_in)++].len = sizeof(*inhdr);
}

static void virtio_crypto_fill_dataq_request_mac(
              FAR struct cryptop *crp, FAR struct virtqueue_buf *vb,
              FAR struct virtio_crypto_op_data_req_s *data,
              FAR struct virtio_crypto_inhdr_s *inhdr,
              FAR int *num_out, FAR int *num_in)
{
  data->header.opcode = VIRTIO_CRYPTO_MAC;

  vb[*num_out].buf = data;
  vb[(*num_out)++].len = sizeof(*data);

  if (crp->crp_desc->crd_flags & CRD_F_UPDATE)
    {
      vb[*num_out].buf = crp->crp_buf;
      vb[(*num_out)++].len = crp->crp_ilen;
      data->op_flf.mac.hdr.src_data_len = crp->crp_ilen;
    }
  else
    {
      vb[*num_out + *num_in].buf = crp->crp_mac;
      vb[*num_out + (*num_in)++].len = crp->crp_olen;
      data->op_flf.mac.hdr.hash_result_len = crp->crp_olen;
    }

  vb[*num_out + *num_in].buf = inhdr;
  vb[*num_out + (*num_in)++].len = sizeof(*inhdr);
}

static void virtio_crypto_fill_dataq_request_aead(
              FAR struct cryptop *crp, FAR struct virtqueue_buf *vb,
              FAR struct virtio_crypto_op_data_req_s *data,
              FAR struct virtio_crypto_inhdr_s *inhdr,
              FAR int *num_out, FAR int *num_in)
{
  int flags = crp->crp_desc->crd_flags;

  data->header.opcode = flags & CRD_F_ENCRYPT ?
          VIRTIO_CRYPTO_AEAD_ENCRYPT : VIRTIO_CRYPTO_AEAD_DECRYPT;
  data->op_flf.aead.src_data_len = crp->crp_ilen;

  vb[*num_out].buf = data;
  vb[(*num_out)++].len = sizeof(*data);

  if (crp->crp_ivlen > 0)
    {
      vb[*num_out].buf = crp->crp_iv;
      vb[(*num_out)++].len = crp->crp_ivlen;
      data->op_flf.aead.iv_len = crp->crp_ivlen;
    }

  vb[*num_out].buf = crp->crp_buf;
  vb[(*num_out)++].len = crp->crp_ilen;

  if (crp->crp_aadlen > 0 && (flags & CRD_F_UPDATE_AAD))
    {
      vb[*num_out].buf = crp->crp_aad;
      vb[(*num_out)++].len = crp->crp_aadlen;
      data->op_flf.aead.aad_len = crp->crp_aadlen;
    }

  if (flags & CRD_F_UPDATE)
    {
      vb[*num_out + *num_in].buf = crp->crp_dst;
      vb[*num_out + (*num_in)++].len = crp->crp_olen;
      data->op_flf.aead.dst_data_len = crp->crp_olen;
    }
  else
    {
      vb[*num_out + *num_in].buf = crp->crp_mac;
      vb[*num_out + (*num_in)++].len = crp->crp_olen;
      data->op_flf.aead.tag_len = crp->crp_olen;
    }

  vb[*num_out + *num_in].buf = inhdr;
  vb[*num_out + (*num_in)++].len = sizeof(*inhdr);
}

static int virtio_crypto_fill_dataq_request(
              FAR struct cryptop *crp, FAR struct virtqueue_buf *vb,
              FAR struct virtio_crypto_op_data_req_s *data,
              FAR struct virtio_crypto_inhdr_s *inhdr,
              FAR int *num_out, FAR int *num_in)
{
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
  header->session_id = crp->crp_sid & 0xffffffff;
  inhdr->status = VIRTIO_CRYPTO_ERR;

  switch (service)
    {
      case VIRTIO_CRYPTO_SERVICE_CIPHER:
        virtio_crypto_fill_dataq_request_cipher(
                                  crp, vb, data, inhdr,
                                  num_out, num_in);
        break;
      case VIRTIO_CRYPTO_SERVICE_HASH:
        virtio_crypto_fill_dataq_request_hash(
                                  crp, vb, data, inhdr,
                                  num_out, num_in);
        break;
      case VIRTIO_CRYPTO_SERVICE_MAC:
        virtio_crypto_fill_dataq_request_mac(
                                  crp, vb, data, inhdr,
                                  num_out, num_in);
        break;
      case VIRTIO_CRYPTO_SERVICE_AEAD:
        virtio_crypto_fill_dataq_request_aead(
                                  crp, vb, data, inhdr,
                                  num_out, num_in);
        break;
      default:
        return -ENOTSUP;
    }

  return OK;
}

static int virtio_crypto_process(FAR struct cryptop *crp)
{
  struct virtio_crypto_op_data_req_s data;
  struct virtio_crypto_inhdr_s inhdr;
  FAR struct virtio_crypto_s *vcrypto;
  FAR struct virtqueue *vq;
  struct virtqueue_buf vb[6];
  uint32_t hid = (uint32_t)(crp->crp_sid >> 32);
  sem_t sem;
  int num_out = 0;
  int num_in = 0;
  int ret;

  vcrypto = crypto_driver_get_priv(hid);
  if (vcrypto == NULL)
    {
      vrterr("Virtio Crypto process: Invalid context\n");
      return -EINVAL;
    }

  vq = vcrypto->vdev->vrings_info[0].vq;
  nxsem_init(&sem, 0, 0);

  ret = virtio_crypto_fill_dataq_request(crp, vb, &data, &inhdr,
                                         &num_out, &num_in);
  if (ret < 0)
    {
      vrterr("Virtio Crypto: Invalid parameters in process\n");
      return ret;
    }

  virtqueue_add_buffer_lock(vq, vb, num_out, num_in, &sem,
                            &vcrypto->lock);
  virtqueue_kick_lock(vq, &vcrypto->lock);

  /* Wait for completion */

  nxsem_wait_uninterruptible(&sem);

  nxsem_destroy(&sem);
  if (inhdr.status != VIRTIO_CRYPTO_OK)
    {
      ret = virtio_crypto_get_errcode(inhdr.status);
    }

  return ret;
}

static int virtio_crypto_rsa_get_session(FAR struct virtio_crypto_s *vcrypto,
                                         FAR struct cryptkop *krp,
                                         FAR uint32_t *sid)
{
  struct virtio_crypto_create_session_input_s input;
  struct virtio_crypto_op_ctrl_req_s ctrl;
  struct virtqueue_buf vb[3];
  FAR struct virtqueue *vq;
  FAR unsigned char *keybuf;
  FAR struct crparam *n;
  FAR struct crparam *e;
  FAR struct crparam *d;
  FAR struct crparam *p;
  FAR struct crparam *q;
  FAR struct crparam *dp;
  FAR struct crparam *dq;
  FAR struct crparam *u;
  uint32_t keybufsize;
  uint32_t keybits;
  uint32_t keylen;
  sem_t sem;

  vq = vcrypto->vdev->vrings_info[vcrypto->max_data_queues].vq;
  memset(&ctrl, 0, sizeof(struct virtio_crypto_op_ctrl_req_s));

  /* Pad ctrl header for creating a new session */

  ctrl.header.algo = virtio_crypto_asym_get_alg(krp->krp_op);
  ctrl.header.opcode = VIRTIO_CRYPTO_AKCIPHER_CREATE_SESSION;
  ctrl.op_flf.akcipher.algo = ctrl.header.algo;
  ctrl.op_flf.akcipher.key_type =
                        krp->krp_keytype == CRYPTO_KEY_TYPE_PRIVATE ?
                        VIRTIO_CRYPTO_AKCIPHER_KEY_TYPE_PRIVATE :
                        VIRTIO_CRYPTO_AKCIPHER_KEY_TYPE_PUBLIC;
  ctrl.op_flf.akcipher.algo_flf.rsa.padding_algo = krp->krp_padding;
  ctrl.op_flf.akcipher.algo_flf.rsa.hash_algo = krp->krp_hash;

  keybits = krp->krp_param[1].crp_nbits;
  keybufsize = VIRTIO_CRYPTO_RSA_DER_MAX_BYTES(
                keybits, ctrl.op_flf.akcipher.key_type ==
                         VIRTIO_CRYPTO_AKCIPHER_KEY_TYPE_PRIVATE);
  keybuf = (FAR unsigned char *)alloca(keybufsize);
  if (keybuf == NULL)
    {
      return -ENOMEM;
    }

  if (ctrl.op_flf.akcipher.key_type ==
                  VIRTIO_CRYPTO_AKCIPHER_KEY_TYPE_PRIVATE)
    {
      n = &krp->krp_param[1];
      e = &krp->krp_param[2];
      d = &krp->krp_param[3];
      p = &krp->krp_param[4];
      q = &krp->krp_param[5];
      dp = &krp->krp_param[6];
      dq = &krp->krp_param[7];
      u = &krp->krp_param[8];
      keylen = virtio_crypto_write_rsa_key_der(
          (FAR unsigned char *)n->crp_p, n->crp_nbits / 8,
          (FAR unsigned char *)e->crp_p, e->crp_nbits / 8,
          (FAR unsigned char *)d->crp_p, d->crp_nbits / 8,
          (FAR unsigned char *)p->crp_p, p->crp_nbits / 8,
          (FAR unsigned char *)q->crp_p, q->crp_nbits / 8,
          (FAR unsigned char *)dp->crp_p, dq->crp_nbits / 8,
          (FAR unsigned char *)dq->crp_p, dp->crp_nbits / 8,
          (FAR unsigned char *)u->crp_p, u->crp_nbits / 8,
          keybuf, keybufsize);
    }
  else
    {
      n = &krp->krp_param[1];
      e = &krp->krp_param[2];
      keylen = virtio_crypto_write_rsa_pubkey_der(
                (FAR unsigned char *)n->crp_p, n->crp_nbits / 8,
                (FAR unsigned char *)e->crp_p, e->crp_nbits / 8,
                keybuf, keybufsize);
    }

  ctrl.op_flf.akcipher.key_len = keylen;

  nxsem_init(&sem, 0, 0);
  input.status = VIRTIO_CRYPTO_ERR;

  vb[0].buf = &ctrl;
  vb[0].len = sizeof(ctrl);
  vb[1].buf = keybuf + keybufsize - keylen;
  vb[1].len = keylen;
  vb[2].buf = &input;
  vb[2].len = sizeof(input);

  virtqueue_add_buffer_lock(vq, vb, 2, 1, &sem, &vcrypto->lock);

  virtqueue_kick_lock(vq, &vcrypto->lock);

  nxsem_wait_uninterruptible(&sem);

  nxsem_destroy(&sem);
  if (input.status != VIRTIO_CRYPTO_OK)
    {
      vrterr("virtio_crypto_s: Create session failed status: %u\n",
                                                     input.status);
      return virtio_crypto_get_errcode(input.status);
    }

  *sid = input.session_id;
  return OK;
}

static int
virtio_crypto_asym_get_session(FAR struct virtio_crypto_s *vcrypto,
                               FAR struct cryptkop *krp,
                               FAR uint32_t *sid)
{
  switch (virtio_crypto_asym_get_alg(krp->krp_op))
    {
      case VIRTIO_CRYPTO_AKCIPHER_RSA:
        return virtio_crypto_rsa_get_session(vcrypto, krp, sid);
      case VIRTIO_CRYPTO_AKCIPHER_ECDSA:
        /* TODO: Qemu backend not support ECDSA and
         *       Vela vhost need to support ECDSA
         */

        return -ENOTSUP;
    }

  return -EINVAL;
}

static void
virtio_crypto_asym_free_session(FAR struct virtio_crypto_s *vcrypto,
                                FAR struct cryptkop *krp,
                                uint32_t session_id)
{
  struct virtio_crypto_create_session_input_s input;
  struct virtio_crypto_op_ctrl_req_s ctrl;
  struct virtqueue_buf vb[2];
  FAR struct virtqueue *vq;
  sem_t sem;

  vq = vcrypto->vdev->vrings_info[vcrypto->max_data_queues].vq;
  memset(&ctrl, 0, sizeof(ctrl));
  memset(&input, 0, sizeof(input));

  ctrl.header.opcode = VIRTIO_CRYPTO_AKCIPHER_DESTROY_SESSION;
  ctrl.op_flf.destroy.session_id = session_id;

  nxsem_init(&sem, 0, 0);
  input.status = VIRTIO_CRYPTO_ERR;

  vb[0].buf = &ctrl;
  vb[0].len = sizeof(ctrl);
  vb[1].buf = &input;
  vb[1].len = sizeof(input);

  virtqueue_add_buffer_lock(vq, vb, 1, 1, &sem, &vcrypto->lock);
  virtqueue_kick_lock(vq, &vcrypto->lock);

  nxsem_wait_uninterruptible(&sem);

  nxsem_destroy(&sem);
  if (input.status != VIRTIO_CRYPTO_OK)
    {
      vrterr("virtio_crypto_s: Close session failed status: %u,"
             "session_id: 0x%lu\n", input.status,
             ctrl.op_flf.destroy.session_id);
    }
}

static int virtio_crypto_rsa_process(FAR struct virtio_crypto_s *vcrypto,
                                     FAR struct cryptkop *krp,
                                     uint32_t session_id)
{
  struct virtio_crypto_op_data_req_s data;
  struct virtio_crypto_inhdr_s inhdr;
  struct virtqueue_buf vb[4];
  FAR struct virtqueue *vq;
  FAR struct crparam *dst;
  FAR struct crparam *src;
  int iparams = krp->krp_iparams;
  int oparams = krp->krp_oparams;
  int num_out = 0;
  int num_in = 0;
  sem_t sem;

  src = &krp->krp_param[0];
  dst = &krp->krp_param[iparams + oparams - 1];
  vq = vcrypto->vdev->vrings_info[0].vq;
  memset(&data, 0, sizeof(data));
  memset(&inhdr, 0, sizeof(inhdr));

  data.header.session_id = session_id;
  data.header.algo = virtio_crypto_asym_get_alg(krp->krp_op);

  vb[num_out].buf = &data;
  vb[num_out++].len = sizeof(data);

  switch (krp->krp_optype)
    {
      case CRYPTO_OP_DECRYPT:
        data.header.opcode = VIRTIO_CRYPTO_AKCIPHER_DECRYPT;
        data.op_flf.akcipher.src_data_len = src->crp_nbits / 8;
        data.op_flf.akcipher.dst_data_len = dst->crp_nbits / 8;
        vb[num_out].buf = src->crp_p;
        vb[num_out++].len = src->crp_nbits / 8;
        vb[num_out + num_in].buf = dst->crp_p;
        vb[num_out + num_in++].len = dst->crp_nbits / 8;
        break;
      case CRYPTO_OP_ENCRYPT:
        data.header.opcode = VIRTIO_CRYPTO_AKCIPHER_ENCRYPT;
        data.op_flf.akcipher.src_data_len = src->crp_nbits / 8;
        data.op_flf.akcipher.dst_data_len = dst->crp_nbits / 8;
        vb[num_out].buf = src->crp_p;
        vb[num_out++].len = src->crp_nbits / 8;
        vb[num_out + num_in].buf = dst->crp_p;
        vb[num_out + num_in++].len = dst->crp_nbits / 8;
        break;
      case CRYPTO_OP_SIGN:
        data.header.opcode = VIRTIO_CRYPTO_AKCIPHER_SIGN;
        data.op_flf.akcipher.src_data_len = src->crp_nbits / 8;
        data.op_flf.akcipher.dst_data_len = dst->crp_nbits / 8;
        vb[num_out].buf = src->crp_p;
        vb[num_out++].len = src->crp_nbits / 8;
        vb[num_out + num_in].buf = dst->crp_p;
        vb[num_out + num_in++].len = dst->crp_nbits / 8;
        break;
      case CRYPTO_OP_VERIFY:
        data.header.opcode = VIRTIO_CRYPTO_AKCIPHER_VERIFY;
        data.op_flf.akcipher.src_data_len = src->crp_nbits / 8;
        data.op_flf.akcipher.dst_data_len = dst->crp_nbits / 8;
        vb[num_out].buf = src->crp_p;
        vb[num_out++].len = src->crp_nbits / 8;
        vb[num_out].buf = dst->crp_p;
        vb[num_out++].len = dst->crp_nbits / 8;
        break;
      default:
        return -EINVAL;
    }

  nxsem_init(&sem, 0, 0);
  inhdr.status = VIRTIO_CRYPTO_ERR;

  vb[num_out + num_in].buf = &inhdr;
  vb[num_out + num_in++].len = sizeof(inhdr);

  virtqueue_add_buffer_lock(vq, vb, num_out, num_in, &sem, &vcrypto->lock);
  virtqueue_kick_lock(vq, &vcrypto->lock);

  nxsem_wait_uninterruptible(&sem);

  nxsem_destroy(&sem);
  if (inhdr.status != VIRTIO_CRYPTO_OK)
    {
      printf("virtio_crypto_s: Akcipher process failed status: %u,"
             "session_id: 0x%u\n", inhdr.status, session_id);
      return virtio_crypto_get_errcode(inhdr.status);
    }

  return inhdr.status;
}

static int virtio_crypto_asym_process(FAR struct virtio_crypto_s *vcrypto,
                                      FAR struct cryptkop *krp,
                                      uint32_t session_id)
{
  switch (virtio_crypto_asym_get_alg(krp->krp_op))
    {
      case VIRTIO_CRYPTO_AKCIPHER_RSA:
        return virtio_crypto_rsa_process(vcrypto, krp, session_id);
      case VIRTIO_CRYPTO_AKCIPHER_ECDSA:
        /* TODO: Qemu backend not support ECDSA and
         *       Vela vhost need to support ECDSA
         */

        return -ENOTSUP;
    }

  return -EINVAL;
}

static int virtio_crypto_kprocess(FAR struct cryptkop *krp)
{
  FAR struct virtio_crypto_s *vcrypto;
  uint32_t hid;
  uint32_t sid = 0;
  int ret;

  if (krp == NULL)
    {
      vrterr("Virtio Crypto kprocess: Invalid parameters\n");
      return -EINVAL;
    }

  hid = (uint32_t)krp->krp_hid;
  vcrypto = crypto_driver_get_priv(hid);
  if (vcrypto == NULL)
    {
      vrterr("Virtio Crypto kprocess: Invalid context\n");
      return -EINVAL;
    }

  /* create session */

  ret = virtio_crypto_asym_get_session(vcrypto, krp, &sid);
  if (ret < 0)
    {
      return ret;
    }

  /* process akcipher data */

  ret = virtio_crypto_asym_process(vcrypto, krp, sid);
  if (ret < 0)
    {
      vrterr("Virtio Crypto kprocess failed, ret = %d\n", ret);
    }

  /* free session */

  virtio_crypto_asym_free_session(vcrypto, krp, sid);
  return ret;
}

static void virtio_crypto_notify(FAR struct virtqueue *vq)
{
  FAR struct virtio_crypto_s *vcrypto = vq->vq_dev->priv;
  FAR sem_t *sem;

  for (; ; )
    {
      sem = virtqueue_get_buffer_lock(vq, 0, NULL, &vcrypto->lock);
      if (sem == NULL)
        {
          break;
        }

      nxsem_post(sem);
    }
}

static int virtio_crypto_create_vqs(struct virtio_crypto_s *vcrypto)
{
  FAR struct virtio_device *vdev = vcrypto->vdev;
  FAR const char *vqnames[vcrypto->max_data_queues + 1];
  vq_callback callback[vcrypto->max_data_queues + 1];
  uint32_t i;
  int ret;

  /* We expect 1 data virtqueue, followed by
   * possible N-1 data queues used in multiqueue mode,
   * followed by control vq.
   */

  /* Allocate/initialize parameters for data virtqueues */

  for (i = 0; i < vcrypto->max_data_queues; i++)
    {
      vqnames[i]  = "virtio_crypto_dataq";
      callback[i] = virtio_crypto_notify;
    }

  /* Parameters for control virtqueue */

  vqnames[vcrypto->max_data_queues] = "virtio_crypto_ctrlq";
  callback[vcrypto->max_data_queues] = virtio_crypto_notify;

  ret = virtio_create_virtqueues(
          vdev, 0, vcrypto->max_data_queues + 1, vqnames, callback, NULL);
  if (ret < 0)
    {
      vrterr("virtio_device_create_virtqueue failed, ret=%d\n", ret);
    }

  return ret;
}

static int virtio_crypto_probe(FAR struct virtio_device *vdev)
{
  FAR struct virtio_crypto_s *vcrypto;
  int algs[CRYPTO_ALGORITHM_MAX + 1];
  int kalgs[CRK_ALGORITHM_MAX + 1];
  int ret;
  int i;

  vcrypto = kmm_zalloc(sizeof(struct virtio_crypto_s));
  if (vcrypto == NULL)
    {
      vrterr("Virtio Crypto: Probe failed to allocate memory\n");
      return -ENOMEM;
    }

  virtio_read_config_member(vdev, struct virtio_crypto_config_s,
      max_dataqueues, &vcrypto->max_data_queues);
  if (vcrypto->max_data_queues < 1)
    {
      vcrypto->max_data_queues = 1;
    }

  virtio_read_config_member(vdev, struct virtio_crypto_config_s,
      max_cipher_key_len, &vcrypto->max_cipher_key_len);
  virtio_read_config_member(vdev, struct virtio_crypto_config_s,
      max_auth_key_len, &vcrypto->max_auth_key_len);
  virtio_read_config_member(vdev, struct virtio_crypto_config_s,
      max_size, &vcrypto->max_size);
  virtio_read_config_member(vdev, struct virtio_crypto_config_s,
      crypto_services, &vcrypto->crypto_services);
  virtio_read_config_member(vdev, struct virtio_crypto_config_s,
      cipher_algo_l, &vcrypto->cipher_algo_l);
  virtio_read_config_member(vdev, struct virtio_crypto_config_s,
      cipher_algo_h, &vcrypto->cipher_algo_h);
  virtio_read_config_member(vdev, struct virtio_crypto_config_s,
      hash_algo, &vcrypto->hash_algo);
  virtio_read_config_member(vdev, struct virtio_crypto_config_s,
      mac_algo_l, &vcrypto->mac_algo_l);
  virtio_read_config_member(vdev, struct virtio_crypto_config_s,
      mac_algo_h, &vcrypto->mac_algo_h);
  if (vcrypto->crypto_services & (1 << VIRTIO_CRYPTO_SERVICE_AKCIPHER))
    {
      virtio_read_config_member(vdev, struct virtio_crypto_config_s,
          akcipher_algo, &vcrypto->akcipher_algo);
    }

  vcrypto->vdev = vdev;
  vdev->priv = vcrypto;

  /* Use single data queue as default */

  vcrypto->curr_queue = 1;

  vrtinfo("max_queues: %x, max_cipher_key_len: %u,\
           max_auth_key_len: %u, max_size 0x%lx\n",
           vcrypto->max_data_queues, vcrypto->max_cipher_key_len,
           vcrypto->max_auth_key_len, vcrypto->max_size);

  /* Call openamp api to intialize the virtio deivce */

  virtio_set_status(vdev, VIRTIO_CONFIG_STATUS_DRIVER);
  virtio_negotiate_features(vdev,
                           (1UL << VIRTIO_CRYPTO_F_REVISION_1) |
                           (1UL << VIRTIO_CRYPTO_F_CIPHER_STATELESS_MODE) |
                           (1UL << VIRTIO_CRYPTO_F_HASH_STATELESS_MODE) |
                           (1UL << VIRTIO_CRYPTO_F_MAC_STATELESS_MODE) |
                           (1UL << VIRTIO_CRYPTO_F_AEAD_STATELESS_MODE) |
                           (1UL << VIRTIO_CRYPTO_F_AKCIPHER_STATELESS_MODE),
                           NULL);
  virtio_set_status(vdev, VIRTIO_CONFIG_FEATURES_OK);

  ret = virtio_crypto_create_vqs(vcrypto);
  if (ret < 0)
    {
      vrterr("virtio_device_create_virtqueue failed, ret=%d\n", ret);
      goto err;
    }

  virtio_set_status(vdev, VIRTIO_CONFIG_STATUS_DRIVER_OK);

  spin_lock_init(&vcrypto->lock);

  /* Register NuttX driver */

  vcrypto->dev_id = crypto_get_driverid(0);

  crypto_driver_set_priv(vcrypto->dev_id, vcrypto);

  /* All algorithms are supported by default, handled by the host. */

  for (i = 0; i < CRYPTO_ALGORITHM_MAX; i++)
    {
      algs[i] = CRYPTO_ALG_FLAG_SUPPORTED;
    }

  ret = crypto_register(vcrypto->dev_id, algs, virtio_crypto_newsession,
                        virtio_crypto_freesession, virtio_crypto_process);
  if (ret < 0)
    {
      goto err;
    }

  for (i = 0; i < CRK_ALGORITHM_MAX; i++)
    {
      kalgs[i] = CRYPTO_ALG_FLAG_SUPPORTED;
    }

  ret = crypto_kregister(vcrypto->dev_id, kalgs, virtio_crypto_kprocess);
  if (ret < 0)
    {
      for (i = 0; i < CRYPTO_ALGORITHM_MAX; i++)
        {
          crypto_unregister(vcrypto->dev_id, i);
        }

      goto err;
    }

  return ret;

err:
  virtio_delete_virtqueues(vdev);
  virtio_reset_device(vdev);
  kmm_free(vcrypto);
  return ret;
}

static void virtio_crypto_remove(FAR struct virtio_device *vdev)
{
  FAR struct virtio_crypto_s *vcrypto = vdev->priv;
  int i;

  for (i = 0; i < CRYPTO_ALGORITHM_MAX; i++)
    {
      crypto_unregister(vcrypto->dev_id, i);
    }

  virtio_delete_virtqueues(vdev);
  virtio_reset_device(vdev);
  kmm_free(vcrypto);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: virtio_register_crypto_driver
 ****************************************************************************/

int virtio_register_crypto_driver(void)
{
  return virtio_register_driver(&g_virtio_crypto_driver);
}
