/****************************************************************************
 * drivers/crypto/rpmsg_crypto_server.c
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
#include <errno.h>
#include <sys/endian.h>
#include <sys/param.h>

#include <crypto/cryptodev.h>
#include <crypto/virtio_crypto.h>
#include <nuttx/kmalloc.h>
#include <nuttx/rpmsg/rpmsg.h>

#include "rpmsg_crypto.h"

/****************************************************************************
 * Pre-processor definitions
 ****************************************************************************/

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct rpmsg_crypto_server_data_s
{
  uint64_t tid;
  struct   cryptop crp;
  struct   cryptodesc crda;
  struct   cryptodesc crde;
};

struct rpmsg_crypto_server_s
{
  struct rpmsg_endpoint ept;          /* Rpmsg end point */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/* Functions handle the messages from the client cpu */

static int rpmsg_crypto_newsession_handler(FAR struct rpmsg_endpoint *ept,
                                           FAR void *data, size_t len,
                                           uint32_t src, FAR void *priv);
static int rpmsg_crypto_process_handler(FAR struct rpmsg_endpoint *ept,
                                        FAR void *data, size_t len,
                                        uint32_t src, FAR void *priv);
static int rpmsg_crypto_freesession_handler(FAR struct rpmsg_endpoint *ept,
                                            FAR void *data, size_t len,
                                            uint32_t src, FAR void *priv);
static int rpmsg_crypto_kprocess_handler(FAR struct rpmsg_endpoint *ept,
                                         FAR void *data, size_t len,
                                         uint32_t src, FAR void *priv);
static int rpmsg_crypto_keyprocess_handler(FAR struct rpmsg_endpoint *ept,
                                           FAR void *data, size_t len,
                                           uint32_t src, FAR void *priv);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const rpmsg_ept_cb g_rpmsg_crypto_handler[] =
{
  [RPMSG_CRYPTO_NEWSESSION]  = rpmsg_crypto_newsession_handler,
  [RPMSG_CRYPTO_PROCESS]     = rpmsg_crypto_process_handler,
  [RPMSG_CRYPTO_FREESESSION] = rpmsg_crypto_freesession_handler,
  [RPMSG_CRYPTO_KPROCESS]    = rpmsg_crypto_kprocess_handler,
  [RPMSG_CRYPTO_KEYPROCESS]  = rpmsg_crypto_keyprocess_handler,
};

/****************************************************************************
 * Public Data
 ****************************************************************************/

extern int cryptodevallowsoft;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: rpmsg_crypto_newsession_handler
 ****************************************************************************/

static int rpmsg_crypto_newsession_handler(FAR struct rpmsg_endpoint *ept,
                                           FAR void *data, size_t len,
                                           uint32_t src, FAR void *priv)
{
  FAR struct rpmsg_crypto_newsession_s *msg = data;
  FAR struct rpmsg_crypto_server_data_s *rcsdata;
  struct cryptoini cria;
  struct cryptoini crie;
  int keylen = 0;
  int txform = 0;
  int thash = 0;

  rcsdata = kmm_zalloc(sizeof(struct rpmsg_crypto_server_data_s));
  if (rcsdata == NULL)
    {
      rpmsgerr("Failed to get new session data\n");
      msg->header.result = -ENOMEM;
      goto out;
    }

  bzero(&crie, sizeof(crie));
  bzero(&cria, sizeof(cria));

  if (msg->ctrl.header.opcode == VIRTIO_CRYPTO_CIPHER_CREATE_SESSION)
    {
      txform = virtio_crypto_get_cipher_alg(msg->ctrl.header.algo);
      if (txform < 0)
        {
          msg->header.result = txform;
          kmm_free(rcsdata);
          goto out;
        }
      else if (txform == CRYPTO_AES_CBC)
        {
          if (msg->ctrl.op_flf.sym.op_flf.cipher.key_len == 24)
            {
              txform = CRYPTO_AES_192_CBC;
            }
          else if (msg->ctrl.op_flf.sym.op_flf.cipher.key_len == 32)
            {
              txform = CRYPTO_AES_256_CBC;
            }
        }

      keylen = msg->ctrl.op_flf.sym.op_flf.cipher.key_len;
    }
  else if (msg->ctrl.header.opcode == VIRTIO_CRYPTO_HASH_CREATE_SESSION)
    {
      thash = virtio_crypto_get_hash_alg(msg->ctrl.header.algo);
      if (thash < 0)
        {
          msg->header.result = thash;
          kmm_free(rcsdata);
          goto out;
        }
    }
  else if (msg->ctrl.header.opcode == VIRTIO_CRYPTO_MAC_CREATE_SESSION)
    {
      thash = virtio_crypto_get_mac_alg(msg->ctrl.header.algo);
      if (thash < 0)
        {
          msg->header.result = thash;
          kmm_free(rcsdata);
          goto out;
        }
      else if (thash == CRYPTO_AES_128_CMAC)
        {
          txform = CRYPTO_AES_CMAC;
        }

      keylen = msg->ctrl.op_flf.mac.auth_key_len;
    }

  if (txform)
    {
      crie.cri_alg = txform;
      crie.cri_klen = keylen * 8;
      crie.cri_key = msg->buf;
      if (msg->ctrl.header.flag & VIRTIO_CRYPTO_KEYID)
        {
          crie.cri_flags |= CRD_F_KEYID;
        }

      if (thash)
        {
          crie.cri_next = &cria;
        }
    }

  if (thash)
    {
      cria.cri_alg = thash;
      cria.cri_sid = -1;
      cria.cri_klen = keylen * 8;
      cria.cri_key = msg->buf;
      if (msg->ctrl.header.flag & VIRTIO_CRYPTO_KEYID)
        {
          cria.cri_flags |= CRD_F_KEYID;
        }
    }

  msg->header.result = crypto_newsession(&rcsdata->tid, txform ?
                                         &crie : &cria,
                                         !cryptodevallowsoft);
  if (msg->header.result != 0)
    {
      kmm_free(rcsdata);
      goto out;
    }

  msg->header.tid = (uintptr_t)rcsdata;
out:
  return rpmsg_send(ept, msg, sizeof(*msg));
}

static int
rpmsg_crypto_process_endec(FAR struct rpmsg_endpoint *ept,
                           FAR struct rpmsg_crypto_process_s *msg,
                           int msglen)
{
  FAR struct rpmsg_crypto_server_data_s *rcsdata =
    (FAR struct rpmsg_crypto_server_data_s *)(uintptr_t)msg->header.tid;
  FAR struct cryptodesc *crde = &rcsdata->crde;
  FAR struct cryptop *crp = &rcsdata->crp;
  FAR struct rpmsg_crypto_process_s *rsp;
  uint32_t ivlen = msg->data.op_flf.sym.op_type_flf.cipher.iv_len;
  uint32_t space;

  rsp = rpmsg_get_tx_payload_buffer(ept, &space, true);
  if (rsp == NULL)
    {
      rpmsgerr("get tx payload failed or no enough space\n");
      return -ENOMEM;
    }

  crde->crd_alg = virtio_crypto_get_cipher_alg(msg->data.header.algo);
  if (crde->crd_alg < 0)
    {
      rsp->header.result = crde->crd_alg;
      goto out;
    }

  if (msg->data.header.opcode == VIRTIO_CRYPTO_CIPHER_ENCRYPT)
    {
      crde->crd_flags |= CRD_F_ENCRYPT;
    }
  else
    {
      crde->crd_flags &= ~CRD_F_ENCRYPT;
    }

  crp->crp_sid = rcsdata->tid;
  crp->crp_desc = crde;

  if (ivlen)
    {
      crp->crp_ivlen = ivlen;
      crde->crd_flags &= ~CRD_F_UPDATE;
    }
  else
    {
      crp->crp_ivlen = 0;
      crde->crd_flags |= CRD_F_UPDATE;
    }

  crp->crp_ilen = msglen - sizeof(*msg) - crp->crp_ivlen;
  crde->crd_len = crp->crp_ilen;
  crp->crp_olen = crp->crp_ilen;
  crp->crp_iv = msg->buf;
  crp->crp_buf = msg->buf + crp->crp_ivlen;
  crp->crp_dst = rsp->buf;
  rsp->header = msg->header;
  rsp->data = msg->data;
  rsp->header.result = crypto_invoke(crp);
  if (rsp->header.result == 0)
    {
      rsp->header.result = crp->crp_etype;
    }

out:
  return rpmsg_send_nocopy(ept, rsp, sizeof(*rsp) + crp->crp_olen);
}

static int
rpmsg_crypto_process_hash(FAR struct rpmsg_endpoint *ept,
                          FAR struct rpmsg_crypto_process_s *msg,
                          int msglen)
{
  FAR struct rpmsg_crypto_server_data_s *rcsdata =
    (FAR struct rpmsg_crypto_server_data_s *)(uintptr_t)msg->header.tid;
  FAR struct cryptodesc *crda = &rcsdata->crda;
  FAR struct cryptop *crp = &rcsdata->crp;
  FAR struct rpmsg_crypto_process_s *rsp;
  uint32_t result_len = msg->data.op_flf.hash.hash_result_len;
  uint32_t space;

  crda->crd_alg = virtio_crypto_get_hash_alg(msg->data.header.algo);
  if (crda->crd_alg < 0)
    {
      return -EINVAL;
    }

  crp->crp_sid = rcsdata->tid;
  crp->crp_desc = crda;

  if (result_len)
    {
      /* output result */

      rsp = rpmsg_get_tx_payload_buffer(ept, &space, true);
      if (rsp == NULL)
        {
          rpmsgerr("get tx payload failed or no enough space\n");
          return -ENOMEM;
        }

      crda->crd_flags = 0;
      rsp->header = msg->header;
      rsp->data = msg->data;
      crp->crp_olen = result_len;
      crp->crp_mac = rsp->buf;
      rsp->header.result = crypto_invoke(crp);
      if (rsp->header.result == 0)
        {
          rsp->header.result = crp->crp_etype;
        }

      return rpmsg_send_nocopy(ept, rsp, sizeof(*rsp) + crp->crp_olen);
    }

  crda->crd_flags |= CRD_F_UPDATE;
  crp->crp_ilen = msglen - sizeof(*msg);
  crda->crd_len = crp->crp_ilen;
  crp->crp_buf = msg->buf;
  return crypto_invoke(crp);
}

static int
rpmsg_crypto_process_mac(FAR struct rpmsg_endpoint *ept,
                         FAR struct rpmsg_crypto_process_s *msg,
                         int msglen)
{
  FAR struct rpmsg_crypto_server_data_s *rcsdata =
    (FAR struct rpmsg_crypto_server_data_s *)(uintptr_t)msg->header.tid;
  FAR struct cryptodesc *crda = &rcsdata->crda;
  FAR struct cryptodesc *crde = &rcsdata->crde;
  FAR struct cryptop *crp = &rcsdata->crp;
  FAR struct rpmsg_crypto_process_s *rsp;
  uint32_t result_len = msg->data.op_flf.mac.hdr.hash_result_len;
  uint32_t space;

  crda->crd_alg = virtio_crypto_get_mac_alg(msg->data.header.algo);
  if (crda->crd_alg < 0)
    {
      return -EINVAL;
    }
  else if (crda->crd_alg == CRYPTO_AES_128_CMAC)
    {
      crde->crd_alg = CRYPTO_AES_CMAC;
      crda->crd_next = crde;
    }

  crp->crp_sid = rcsdata->tid;
  crp->crp_desc = crda;

  if (result_len)
    {
      /* output result */

      rsp = rpmsg_get_tx_payload_buffer(ept, &space, true);
      if (rsp == NULL)
        {
          rpmsgerr("get tx payload failed or no enough space\n");
          return -ENOMEM;
        }

      crda->crd_flags = 0;
      rsp->header = msg->header;
      rsp->data = msg->data;
      crp->crp_buf = NULL;
      crp->crp_ilen = 0;
      crp->crp_olen = result_len;
      crp->crp_mac = rsp->buf;
      rsp->header.result = crypto_invoke(crp);
      if (rsp->header.result == 0)
        {
          rsp->header.result = crp->crp_etype;
        }

      return rpmsg_send_nocopy(ept, rsp, sizeof(*rsp) + crp->crp_olen);
    }

  crda->crd_flags |= CRD_F_UPDATE;
  crp->crp_ilen = msglen - sizeof(*msg);
  crda->crd_len = crp->crp_ilen;
  crde->crd_len = crp->crp_ilen;
  crp->crp_buf = msg->buf;
  return crypto_invoke(crp);
}

static void rpmsg_crypto_read_binary(FAR unsigned char **p,
                                     FAR unsigned char *mpi,
                                     size_t len)
{
  uint32_t *left;
  uint32_t *right;
  uint32_t tmp;

  memset(mpi, 0, len);

  if (len != 0)
    {
      memcpy(mpi, *p, len);
    }

  for (left = (uint32_t *)mpi,
       right = (uint32_t *)(mpi + len - sizeof(*right));
       left <= right; left++, right--)
    {
      tmp    = betoh32(*left);
      *left  = betoh32(*right);
      *right = tmp;
    }

  *p += len;
}

static int
rpmsg_crypto_kprocess_rsa(FAR struct rpmsg_endpoint *ept,
                          FAR struct rpmsg_crypto_kprocess_s *msg,
                          size_t len, uint32_t src, FAR void *priv)
{
  FAR struct rpmsg_crypto_kprocess_s *rsp = NULL;
  FAR struct crparam *output = NULL;
  FAR struct crparam *input = NULL;
  FAR struct crparam *n = NULL;
  FAR struct crparam *e = NULL;
  FAR struct crparam *d = NULL;
  FAR struct crparam *p = NULL;
  FAR struct crparam *q = NULL;
  FAR struct crparam *dp = NULL;
  FAR struct crparam *dq = NULL;
  FAR struct crparam *qp = NULL;
  FAR unsigned char *end = NULL;
  FAR unsigned char *pos = NULL;
  struct cryptkop krp;
  uint32_t data_len = 0;
  uint32_t space;
  uint32_t keylen;
  size_t slen;
  int version;
  int ret;

  memset(&krp, 0, sizeof(krp));
  keylen = msg->ctrl.op_flf.akcipher.key_len;
  krp.krp_padding = msg->ctrl.op_flf.akcipher.algo_flf.rsa.padding_algo;
  krp.krp_hash = msg->ctrl.op_flf.akcipher.algo_flf.rsa.hash_algo;

  /* Import key */

  pos = (FAR unsigned char *)msg->buf;
  end = pos + keylen;

  if (msg->ctrl.op_flf.akcipher.key_type ==
                         VIRTIO_CRYPTO_AKCIPHER_KEY_TYPE_PRIVATE)
    {
      n = &krp.krp_param[1];
      e = &krp.krp_param[2];
      d = &krp.krp_param[3];
      p = &krp.krp_param[4];
      q = &krp.krp_param[5];
      dp = &krp.krp_param[6];
      dq = &krp.krp_param[7];
      qp = &krp.krp_param[8];
      ret = asn1_get_tag(&pos, end, &slen, ASN1_CONSTRUCTED | ASN1_SEQUENCE);
      if (ret != 0)
        {
          return -EINVAL;
        }

      end = pos + slen;
      ret = asn1_get_int(&pos, end, &version);
      if (ret != 0)
        {
          return -EINVAL;
        }

      /* Import N */

      ret = asn1_get_tag(&pos, end, &slen, ASN1_INTEGER);
      if (ret != 0)
        {
          return ret;
        }

      n->crp_p = alloca(slen);
      n->crp_nbits = slen * 8;
      rpmsg_crypto_read_binary(&pos, (FAR unsigned char *)n->crp_p, slen);

      /* Import E */

      ret = asn1_get_tag(&pos, end, &slen, ASN1_INTEGER);
      if (ret != 0)
        {
          return ret;
        }

      e->crp_p = alloca(slen);
      e->crp_nbits = slen * 8;
      rpmsg_crypto_read_binary(&pos, (FAR unsigned char *)e->crp_p, slen);

      /* Import D */

      ret = asn1_get_tag(&pos, end, &slen, ASN1_INTEGER);
      if (ret != 0)
        {
          return ret;
        }

      d->crp_p = alloca(slen);
      d->crp_nbits = slen * 8;
      rpmsg_crypto_read_binary(&pos, (FAR unsigned char *)d->crp_p, slen);

      /* Import P */

      ret = asn1_get_tag(&pos, end, &slen, ASN1_INTEGER);
      if (ret != 0)
        {
          return ret;
        }

      p->crp_p = alloca(slen);
      p->crp_nbits = slen * 8;
      rpmsg_crypto_read_binary(&pos, (FAR unsigned char *)p->crp_p, slen);

      /* Import Q */

      ret = asn1_get_tag(&pos, end, &slen, ASN1_INTEGER);
      if (ret != 0)
        {
          return ret;
        }

      q->crp_p = alloca(slen);
      q->crp_nbits = slen * 8;
      rpmsg_crypto_read_binary(&pos, (FAR unsigned char *)q->crp_p, slen);

      /* Import DP */

      ret = asn1_get_tag(&pos, end, &slen, ASN1_INTEGER);
      if (ret != 0)
        {
          return ret;
        }

      dp->crp_p = alloca(slen);
      dp->crp_nbits = slen * 8;
      rpmsg_crypto_read_binary(&pos, (FAR unsigned char *)dp->crp_p, slen);

      /* Import DQ */

      ret = asn1_get_tag(&pos, end, &slen, ASN1_INTEGER);
      if (ret != 0)
        {
          return ret;
        }

      dq->crp_p = alloca(slen);
      dq->crp_nbits = slen * 8;
      rpmsg_crypto_read_binary(&pos, (FAR unsigned char *)dq->crp_p, slen);

      /* Import QP */

      ret = asn1_get_tag(&pos, end, &slen, ASN1_INTEGER);
      if (ret != 0)
        {
          return ret;
        }

      qp->crp_p = alloca(slen);
      qp->crp_nbits = slen * 8;
      rpmsg_crypto_read_binary(&pos, (FAR unsigned char *)qp->crp_p, slen);
    }
  else if (msg->ctrl.op_flf.akcipher.key_type ==
                         VIRTIO_CRYPTO_AKCIPHER_KEY_TYPE_PUBLIC)
    {
      n = &krp.krp_param[1];
      e = &krp.krp_param[2];
      ret = asn1_get_tag(&pos, end, &slen, ASN1_CONSTRUCTED | ASN1_SEQUENCE);
      if (ret != 0)
        {
          return -EINVAL;
        }

      /* Import N */

      ret = asn1_get_tag(&pos, end, &slen, ASN1_INTEGER);
      if (ret != 0)
        {
          return ret;
        }

      n->crp_p = alloca(slen);
      n->crp_nbits = slen * 8;
      rpmsg_crypto_read_binary(&pos, (FAR unsigned char *)n->crp_p, slen);

      /* Import E */

      ret = asn1_get_tag(&pos, end, &slen, ASN1_INTEGER);
      if (ret != 0)
        {
          return ret;
        }

      e->crp_p = alloca(slen);
      e->crp_nbits = slen * 8;
      rpmsg_crypto_read_binary(&pos, (FAR unsigned char *)e->crp_p, slen);
    }

  input = &krp.krp_param[0];
  input->crp_p = msg->buf + keylen;
  input->crp_nbits = msg->data.op_flf.akcipher.src_data_len * 8;
  rsp = rpmsg_get_tx_payload_buffer(ept, &space, true);
  if (rsp == NULL)
    {
      rpmsgerr("get tx payload failed or no enough space\n");
      return -ENOMEM;
    }

  /* padding processing data */

  rsp->header = msg->header;
  rsp->ctrl = msg->ctrl;
  rsp->data = msg->data;
  krp.krp_flags = msg->data.header.flag;
  switch (msg->data.header.opcode)
    {
      case VIRTIO_CRYPTO_AKCIPHER_DECRYPT:
        if (dp && dp->crp_nbits != 0)
          {
            krp.krp_op = CRK_MOD_EXP_CRT;
            krp.krp_iparams = 9;
            krp.krp_oparams = 1;
          }
        else
          {
            krp.krp_op = CRK_MOD_EXP;
            krp.krp_iparams = 3;
            krp.krp_oparams = 1;
          }

        krp.krp_optype = CRYPTO_OP_DECRYPT;
        output = &krp.krp_param[krp.krp_iparams + krp.krp_oparams - 1];
        output->crp_p = rsp->buf;
        output->crp_nbits = msg->data.op_flf.akcipher.dst_data_len * 8;
        data_len += msg->data.op_flf.akcipher.dst_data_len;
        break;
      case VIRTIO_CRYPTO_AKCIPHER_ENCRYPT:
        krp.krp_op = CRK_MOD_EXP;
        krp.krp_iparams = 3;
        krp.krp_oparams = 1;
        krp.krp_optype = CRYPTO_OP_ENCRYPT;
        output = &krp.krp_param[krp.krp_iparams + krp.krp_oparams - 1];
        output->crp_p = rsp->buf;
        output->crp_nbits = msg->data.op_flf.akcipher.dst_data_len * 8;
        data_len += msg->data.op_flf.akcipher.dst_data_len;
        break;
      case VIRTIO_CRYPTO_AKCIPHER_SIGN:
        if (krp.krp_padding == CRYPTO_RSA_PKCS1_PADDING)
          {
            krp.krp_op = CRK_RSA_PKCS15_SIGN;
          }
        else if (krp.krp_padding == CRYPTO_RSA_OAEP_PADDING)
          {
            krp.krp_op = CRK_RSA_PSS_SIGN;
          }

        krp.krp_iparams = 9;
        krp.krp_oparams = 1;
        krp.krp_optype = CRYPTO_OP_SIGN;
        output = &krp.krp_param[krp.krp_iparams + krp.krp_oparams - 1];
        output->crp_p = rsp->buf;
        output->crp_nbits = msg->data.op_flf.akcipher.dst_data_len * 8;
        data_len += msg->data.op_flf.akcipher.dst_data_len;
        break;
      case VIRTIO_CRYPTO_AKCIPHER_VERIFY:
        if (krp.krp_padding == CRYPTO_RSA_PKCS1_PADDING)
          {
            krp.krp_op = CRK_RSA_PKCS15_VERIFY;
            output = &krp.krp_param[krp.krp_iparams + krp.krp_oparams - 1];
            krp.krp_iparams = 5;
            krp.krp_oparams = 0;
          }
        else if (krp.krp_padding == CRYPTO_RSA_OAEP_PADDING)
          {
            krp.krp_op = CRK_RSA_PSS_VERIFY;
            krp.krp_iparams = 4;
            krp.krp_oparams = 0;
            output = &krp.krp_param[krp.krp_iparams + krp.krp_oparams - 1];
          }

        krp.krp_optype = CRYPTO_OP_VERIFY;
        output->crp_p = msg->buf + keylen + input->crp_nbits / 8;
        output->crp_nbits = msg->data.op_flf.akcipher.dst_data_len * 8;
        break;
      default:
        rpmsg_release_tx_buffer(ept, rsp);
        return -EINVAL;
    }

  rsp->header.result = crypto_kinvoke(&krp);
  return rpmsg_send_nocopy(ept, rsp, sizeof(*rsp) + data_len);
}

/****************************************************************************
 * Name: rpmsg_crypto_process_handler
 ****************************************************************************/

static int rpmsg_crypto_process_handler(FAR struct rpmsg_endpoint *ept,
                                        FAR void *data, size_t len,
                                        uint32_t src, FAR void *priv)
{
  FAR struct rpmsg_crypto_process_s *msg = data;
  int ret = -EINVAL;

  if (msg->data.header.opcode == VIRTIO_CRYPTO_CIPHER_ENCRYPT ||
      msg->data.header.opcode == VIRTIO_CRYPTO_CIPHER_DECRYPT)
    {
      ret = rpmsg_crypto_process_endec(ept, msg, len);
    }
  else if (msg->data.header.opcode == VIRTIO_CRYPTO_HASH)
    {
      ret = rpmsg_crypto_process_hash(ept, msg, len);
    }
  else if (msg->data.header.opcode == VIRTIO_CRYPTO_MAC)
    {
      ret = rpmsg_crypto_process_mac(ept, msg, len);
    }

  return ret;
}

/****************************************************************************
 * Name: rpmsg_crypto_freesession_handler
 ****************************************************************************/

static int rpmsg_crypto_freesession_handler(FAR struct rpmsg_endpoint *ept,
                                            FAR void *data, size_t len,
                                            uint32_t src, FAR void *priv)
{
  FAR struct rpmsg_crypto_freesession_s *msg = data;
  FAR struct rpmsg_crypto_server_data_s *rcsdata =
    (FAR struct rpmsg_crypto_server_data_s *)(uintptr_t)msg->header.tid;

  msg->header.result = crypto_freesession(rcsdata->tid);
  kmm_free(rcsdata);
  return rpmsg_send(ept, msg, sizeof(*msg));
}

/****************************************************************************
 * Name: rpmsg_crypto_kprocess_handler
 ****************************************************************************/

static int rpmsg_crypto_kprocess_handler(FAR struct rpmsg_endpoint *ept,
                                         FAR void *data, size_t len,
                                         uint32_t src, FAR void *priv)
{
  FAR struct rpmsg_crypto_kprocess_s *msg = data;

  switch (msg->ctrl.header.algo)
    {
      case VIRTIO_CRYPTO_AKCIPHER_RSA:
        return rpmsg_crypto_kprocess_rsa(ept, msg, len, src, priv);
      case VIRTIO_CRYPTO_AKCIPHER_ECDSA:
        /* TODO: Need to support ECDSA
         */

        return -ENOTSUP;
    }

  return -EINVAL;
}

/****************************************************************************
 * Name: rpmsg_crypto_keyprocess_handler
 ****************************************************************************/

static int rpmsg_crypto_keyprocess_handler(FAR struct rpmsg_endpoint *ept,
                                           FAR void *data, size_t len,
                                           uint32_t src, FAR void *priv)
{
  FAR struct rpmsg_crypto_keyprocess_s *msg = data;
  FAR struct rpmsg_crypto_keyprocess_s *rsp;
  struct cryptkop krp;
  uint32_t space;

  rsp = rpmsg_get_tx_payload_buffer(ept, &space, true);
  if (rsp == NULL)
    {
      rpmsgerr("get tx payload failed or no enough space\n");
      return -ENOMEM;
    }

  rsp->header = msg->header;
  rsp->data = msg->data;
  memset(&krp, 0, sizeof(krp));
  krp.krp_op = msg->data.cmd;
  switch (msg->data.cmd)
    {
      case CRK_ALLOCATE_KEY:
        krp.krp_iparams = 0;
        krp.krp_oparams = 1;
        krp.krp_param[0].crp_nbits = msg->data.dst_data_len * 8;
        krp.krp_param[0].crp_p = rsp->buf;
        break;
      case CRK_VALIDATE_KEYID:
      case CRK_DELETE_KEY:
      case CRK_SAVE_KEY:
      case CRK_LOAD_KEY:
      case CRK_UNLOAD_KEY:
        krp.krp_iparams = 1;
        krp.krp_oparams = 0;
        krp.krp_param[0].crp_nbits = msg->data.name_len * 8;
        krp.krp_param[0].crp_p = msg->buf;
        break;
      case CRK_IMPORT_KEY:
      case CRK_GENERATE_AES_KEY:
      case CRK_GENERATE_SECP256R1_KEY:
        krp.krp_iparams = 2;
        krp.krp_oparams = 0;
        krp.krp_param[0].crp_nbits = msg->data.name_len * 8;
        krp.krp_param[0].crp_p = msg->buf;
        krp.krp_param[1].crp_nbits = msg->data.src_data_len * 8;
        krp.krp_param[1].crp_p = msg->buf + msg->data.name_len;
        break;
      case CRK_EXPORT_KEY:
        krp.krp_iparams = 1;
        krp.krp_oparams = 1;
        krp.krp_param[0].crp_nbits = msg->data.name_len * 8;
        krp.krp_param[0].crp_p = msg->buf;
        krp.krp_param[1].crp_nbits = msg->data.dst_data_len * 8;
        krp.krp_param[1].crp_p = rsp->buf;
        break;
    }

  rsp->header.result = crypto_kinvoke(&krp);
  if (rsp->header.result == 0)
    {
      rsp->header.result = krp.krp_status;
      if (krp.krp_op == CRK_EXPORT_KEY)
        {
          rsp->data.dst_data_len = krp.krp_param[1].crp_nbits / 8;
        }
    }

  return rpmsg_send_nocopy(ept, rsp, sizeof(*rsp) + rsp->data.dst_data_len);
}

/****************************************************************************
 * Name: rpmsg_crypto_ept_cb
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

  rpmsgerr("Rpmsg Crypto Server: Failed to find commend handler\n");
  return -EINVAL;
}

/****************************************************************************
 * Name: rpmsgdev_ns_match
 ****************************************************************************/

static bool rpmsg_crypto_ns_match(FAR struct rpmsg_device *rdev,
                                  FAR void *priv, FAR const char *name,
                                  uint32_t dest)
{
  return !strncmp(name, RPMSG_CRYPTO_NAME_PREFIX,
                        RPMSG_CRYPTO_NAME_PREFIX_LEN);
}

/****************************************************************************
 * Name: rpmsg_crypto_ept_release
 ****************************************************************************/

static void rpmsg_crypto_ept_release(FAR struct rpmsg_endpoint *ept)
{
  FAR struct rpmsg_crypto_server_s *server = ept->priv;

  kmm_free(server);
}

/****************************************************************************
 * Name: rpmsg_crypto_ns_bind
 ****************************************************************************/

static void rpmsg_crypto_ns_bind(FAR struct rpmsg_device *rdev,
                                 FAR void *priv, FAR const char *name,
                                 uint32_t dest)
{
  FAR struct rpmsg_crypto_server_s *server;
  int ret;

  server = kmm_zalloc(sizeof(*server));
  if (server == NULL)
    {
      return;
    }

  server->ept.priv = server;
  server->ept.release_cb = rpmsg_crypto_ept_release;
  ret = rpmsg_create_ept(&server->ept, rdev, name,
                         RPMSG_ADDR_ANY, dest,
                         rpmsg_crypto_ept_cb, rpmsg_destroy_ept);
  if (ret < 0)
    {
      kmm_free(server);
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: rpmsg_crypto_server_init
 *
 * Description:
 *   Rpmsg-crypto server initialize function, the server cpu should call
 *   this function.
 *
 * Parameters:
 *   None
 *
 * Returned Values:
 *   OK on success; A negated errno value is returned on any failure.
 *
 ****************************************************************************/

int rpmsg_crypto_server_init(void)
{
  return rpmsg_register_callback(NULL,
                                 NULL,
                                 NULL,
                                 rpmsg_crypto_ns_match,
                                 rpmsg_crypto_ns_bind);
}
