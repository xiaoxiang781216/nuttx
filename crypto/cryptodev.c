/****************************************************************************
 * crypto/cryptodev.c
 *
 * SPDX-License-Identifier: BSD-2-Clause
 * SPDX-FileCopyrightText: Copyright (c) 2001 Theo de Raadt
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Effort sponsored in part by the Defense Advanced Research Projects
 * Agency (DARPA) and Air Force Research Laboratory, Air Force
 * Materiel Command, USAF, under agreement number F30602-01-2-0537.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>
#include <sys/param.h>
#include <stdbool.h>
#include <string.h>
#include <poll.h>
#include <errno.h>

#include <nuttx/atomic.h>
#include <nuttx/kmalloc.h>
#include <nuttx/fs/fs.h>
#include <nuttx/mm/mempool.h>
#include <nuttx/crypto/crypto.h>
#include <nuttx/drivers/drivers.h>
#include <nuttx/percpu.h>
#include <nuttx/spinlock.h>

#include <crypto/xform.h>
#include <crypto/cryptodev.h>
#include <crypto/cryptosoft.h>

/****************************************************************************
 * Public Data
 ****************************************************************************/

DECLARE_PER_CPU_BMP(FAR struct cryptocap *, crypto_drivers);
#define crypto_drivers this_cpu_var_bmp(crypto_drivers)

DECLARE_PER_CPU_BMP(int, crypto_drivers_num);
#define crypto_drivers_num this_cpu_var_bmp(crypto_drivers_num)

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct csession
{
  TAILQ_ENTRY(csession) next;
  uint64_t sid;
  uint32_t ses;

  uint32_t cipher;
  uint32_t mac;
  bool txform;
  bool thash;

  uint8_t key[AALG_MAX_RESULT_LEN];
  int keylen;
  u_char tmp_iv[EALG_MAX_BLOCK_LEN];

  uint8_t mackey[AALG_MAX_RESULT_LEN];
  int mackeylen;
  int error;
};

struct fcrypt
{
  TAILQ_HEAD(csessionlist, csession) csessions;
  TAILQ_HEAD(cryptopinflight, cryptop) crp_inflight;
  TAILQ_HEAD(cryptkopinflight, cryptkop) crpk_inflight;
  TAILQ_HEAD(cryptoplist, cryptop) crp_ret;
  TAILQ_HEAD(cryptkoplist, cryptkop) crpk_ret;
  atomic_t refcount;
  spinlock_t lock;
  int sesn;
  FAR struct pollfd *fds;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/* Character driver methods */

static ssize_t cryptof_read(FAR struct file *filep,
                            FAR char *buffer, size_t len);
static ssize_t cryptof_write(FAR struct file *filep,
                             FAR const char *buffer, size_t len);
static int cryptof_ioctl(FAR struct file *filep,
                         int cmd, unsigned long arg);
static int cryptof_poll(FAR struct file *filep,
                        FAR struct pollfd *fds, bool setup);
static int cryptof_open(FAR struct file *filep);
static int cryptof_close(FAR struct file *filep);

static int cryptoopen(FAR struct file *filep);
static ssize_t cryptoread(FAR struct file *filep,
                          FAR char *buffer, size_t len);
static ssize_t cryptowrite(FAR struct file *filep,
                           FAR const char *buffer, size_t len);
static int cryptoclose(FAR struct file *filep);
static int cryptoioctl(FAR struct file *filep, int cmd, unsigned long arg);

static FAR struct csession *csefind(FAR struct fcrypt *, u_int);
static int csedelete(FAR struct fcrypt *, FAR struct csession *);
static FAR struct csession *cseadd(FAR struct fcrypt *,
                                   FAR struct csession *);
static FAR struct csession *csecreate(FAR struct fcrypt *, uint64_t,
                                      caddr_t, uint64_t,
                                      caddr_t, uint64_t, uint32_t,
                                      uint32_t, bool, bool);
static int csefree(FAR struct csession *);

static int cryptodev_op(FAR struct fcrypt *,
                        FAR struct csession *,
                        FAR struct crypt_op *);
static int cryptodev_cancel(FAR struct fcrypt *, uint32_t);
static int cryptodevop_cb(FAR struct cryptop *);
static int cryptodev_getopstatus(FAR struct fcrypt *,
                                 FAR struct crypt_op *);
static int cryptodev_key(FAR struct fcrypt *, FAR struct crypt_kop *);
static int cryptodevkey_cancel(FAR struct fcrypt *,
                               FAR struct crypt_kop *);
static int cryptodevkey_cb(FAR struct cryptkop *);
static int cryptodev_getkeystatus(FAR struct fcrypt *,
                                  FAR struct crypt_kop *);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct file_operations g_cryptofops =
{
  cryptof_open,        /* open   */
  cryptof_close,       /* close  */
  cryptof_read,        /* read   */
  cryptof_write,       /* write  */
  NULL,                /* seek   */
  cryptof_ioctl,       /* ioctl  */
  NULL,                /* truncate */
  cryptof_poll         /* poll   */
};

static const struct file_operations g_cryptoops =
{
  cryptoopen,          /* open   */
  cryptoclose,         /* close  */
  cryptoread,          /* read   */
  cryptowrite,         /* write  */
  NULL,                /* seek   */
  cryptoioctl,         /* ioctl  */
};

static struct inode g_cryptoinode =
{
  .i_flags = FSNODEFLAG_TYPE_DRIVER,
  .i_crefs = 1,
  .u.i_ops = &g_cryptofops
};

#if CONFIG_CRYPTO_SESSION_POOL_COUNT > 0
MEMPOOL_DEFINE(g_csession_pool, sizeof(struct csession),
               CONFIG_CRYPTO_SESSION_POOL_COUNT,
               CONFIG_CRYPTO_SESSION_POOL_COUNT, 0);
#  define csession_alloc()                                          \
    ((FAR struct csession *)mempool_zallocate(&g_csession_pool, 0))
#  define csession_free(p)                                          \
    do                                                              \
      {                                                             \
        explicit_bzero((p), sizeof(struct csession));               \
        mempool_release(&g_csession_pool, (p));                     \
      }                                                             \
    while (0)
#else
#  define csession_alloc()                                          \
    ((FAR struct csession *)kmm_zalloc(sizeof(struct csession)))
#  define csession_free(p)                                          \
    do                                                              \
      {                                                             \
        explicit_bzero((p), sizeof(struct csession));               \
        kmm_free((p));                                              \
      }                                                             \
    while (0)
#endif

#if CONFIG_CRYPTO_CRYPTOP_POOL_COUNT > 0
MEMPOOL_DEFINE(g_cryptop_pool, sizeof(struct cryptop),
               CONFIG_CRYPTO_CRYPTOP_POOL_COUNT,
               CONFIG_CRYPTO_CRYPTOP_POOL_COUNT, 0);
#  define cryptop_pool_alloc()                                      \
    ((FAR struct cryptop *)mempool_zallocate(&g_cryptop_pool, 0))
#  define cryptop_pool_free(p)                                      \
    mempool_release(&g_cryptop_pool, (p))
#else
#  define cryptop_pool_alloc()                                      \
    ((FAR struct cryptop *)kmm_zalloc(sizeof(struct cryptop)))
#  define cryptop_pool_free(p)  kmm_free((p))
#endif

#define cryptop_free(p)                                             \
    do                                                              \
      {                                                             \
        if ((p)->crp_flags & CRYPTO_F_REL)                          \
          {                                                         \
            cryptop_pool_free((p));                                 \
          }                                                         \
        else                                                        \
          {                                                         \
            kmm_free((p));                                          \
          }                                                         \
      }                                                             \
    while (0)

#if CONFIG_CRYPTO_CRYPTKOP_POOL_COUNT > 0
MEMPOOL_DEFINE(g_cryptkop_pool, sizeof(struct cryptkop),
               CONFIG_CRYPTO_CRYPTKOP_POOL_COUNT,
               CONFIG_CRYPTO_CRYPTKOP_POOL_COUNT, 0);
#  define cryptkop_pool_alloc()                                     \
    ((FAR struct cryptkop *)mempool_zallocate(&g_cryptkop_pool, 0))
#  define cryptkop_pool_release(p)                                  \
    mempool_release(&g_cryptkop_pool, (p))
#else
#  define cryptkop_pool_alloc()                                     \
    ((FAR struct cryptkop *)kmm_zalloc(sizeof(struct cryptkop)))
#  define cryptkop_pool_release(p)  kmm_free((p))
#endif

#define cryptkop_free(p)                                            \
    do                                                              \
      {                                                             \
        cryptkop_free_params((p));                                  \
        if ((p)->krp_flags & CRYPTO_F_REL)                          \
          {                                                         \
            cryptkop_pool_release((p));                             \
          }                                                         \
        else                                                        \
          {                                                         \
            kmm_free((p));                                          \
          }                                                         \
      }                                                             \
    while (0)

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void cryptkop_free_params(FAR struct cryptkop *krp)
{
  int i;

  if (krp->krp_flags & CRYPTO_F_REL)
    {
      return;
    }

  for (i = 0; i < CRK_MAXPARAM; i++)
    {
      if (krp->krp_param[i].crp_p)
        {
          explicit_bzero(krp->krp_param[i].crp_p,
                         (krp->krp_param[i].crp_nbits + 7) / 8);
          kmm_free(krp->krp_param[i].crp_p);
        }
    }
}

static FAR struct fcrypt *fcrypt_get(FAR struct fcrypt *fcr)
{
  atomic_add(&fcr->refcount, 1);
  return fcr;
}

static void fcrypt_put(FAR struct fcrypt *fcr)
{
  FAR struct csession *cse;
  FAR struct cryptop *crp;
  FAR struct cryptkop *krp;

  if (atomic_sub(&fcr->refcount, 1) != 1)
    {
      return;
    }

  while ((cse = TAILQ_FIRST(&fcr->csessions)))
    {
      TAILQ_REMOVE(&fcr->csessions, cse, next);
      csefree(cse);
    }

  while ((crp = TAILQ_FIRST(&fcr->crp_ret)))
    {
      TAILQ_REMOVE(&fcr->crp_ret, crp, crp_next);
      cryptop_free(crp);
    }

  while ((krp = TAILQ_FIRST(&fcr->crpk_ret)))
    {
      TAILQ_REMOVE(&fcr->crpk_ret, krp, krp_next);
      cryptkop_free(krp);
    }

  kmm_free(fcr);
}

static void cryptodevkey_cancel_dispatch(uint32_t reqid, u_int op)
{
  struct cryptkop krp;

  memset(&krp, 0, sizeof(krp));
  krp.krp_op = op;
  krp.krp_flags = CRYPTO_F_CANCEL;
  krp.krp_reqid = reqid;
  crypto_kinvoke(&krp);
}

static void cryptodev_cancel_dispatch(uint64_t sid, uint32_t reqid)
{
  struct cryptop crp;
  struct cryptodesc crd;

  memset(&crp, 0, sizeof(crp));
  memset(&crd, 0, sizeof(crd));

  crp.crp_sid = sid;
  crp.crp_reqid = reqid;
  crp.crp_flags = CRYPTO_F_CANCEL;
  crp.crp_desc = &crd;
  crypto_invoke(&crp);
}

/* ARGSUSED */

static ssize_t cryptof_read(FAR struct file *filep,
                            FAR char *buffer, size_t len)
{
  return -EIO;
}

/* ARGSUSED */

static ssize_t cryptof_write(FAR struct file *filep,
                             FAR const char *buffer, size_t len)
{
  return -EIO;
}

/* ARGSUSED */

static int cryptof_ioctl(FAR struct file *filep,
                         int cmd, unsigned long arg)
{
  struct cryptoini cria;
  struct cryptoini crie;
  FAR struct fcrypt *fcr = filep->f_priv;
  FAR struct csession *cse;
  FAR struct session_op *sop;
  FAR struct crypt_op *cop;
  FAR struct crypt_kop *kop;
  bool txform = false;
  bool thash = false;
  uint64_t sid;
  uint32_t ses;
  int error = 0;

  switch (cmd)
    {
      case CIOCGSESSION:
        sop = (FAR struct session_op *)arg;
        switch (sop->cipher)
          {
            case 0:
              break;
            case CRYPTO_3DES_CBC:
            case CRYPTO_BLF_CBC:
            case CRYPTO_CAST_CBC:
            case CRYPTO_AES_CBC:
            case CRYPTO_AES_192_CBC:
            case CRYPTO_AES_256_CBC:
            case CRYPTO_AES_CMAC:
            case CRYPTO_AES_CTR:
            case CRYPTO_AES_XTS:
            case CRYPTO_AES_GCM_16:
            case CRYPTO_AES_OFB:
            case CRYPTO_AES_CFB_8:
            case CRYPTO_AES_CFB_128:
            case CRYPTO_NULL:
              txform = true;
              break;
            default:
              return -EINVAL;
          }

        switch (sop->mac)
          {
            case 0:
              break;
            case CRYPTO_MD5_HMAC:
            case CRYPTO_SHA1_HMAC:
            case CRYPTO_RIPEMD160_HMAC:
            case CRYPTO_SHA2_256_HMAC:
            case CRYPTO_SHA2_384_HMAC:
            case CRYPTO_SHA2_512_HMAC:
            case CRYPTO_AES_128_GMAC:
            case CRYPTO_AES_192_GMAC:
            case CRYPTO_AES_256_GMAC:
            case CRYPTO_AES_128_CMAC:
            case CRYPTO_MD5:
            case CRYPTO_POLY1305:
            case CRYPTO_RIPEMD160:
            case CRYPTO_SHA1:
            case CRYPTO_SHA2_224:
            case CRYPTO_SHA2_256:
            case CRYPTO_SHA2_384:
            case CRYPTO_SHA2_512:
            case CRYPTO_CRC8:
            case CRYPTO_CRC16:
            case CRYPTO_CRC32:
            case CRYPTO_PBKDF2_HMAC_SHA1:
            case CRYPTO_PBKDF2_HMAC_SHA256:
              thash = true;
              break;
            default:
              return -EINVAL;
          }

        bzero(&crie, sizeof(crie));
        bzero(&cria, sizeof(cria));

        if (txform)
          {
            crie.cri_alg = sop->cipher;
            crie.cri_klen = sop->keylen * 8;
            crie.cri_op = sop->op;
            if (sop->flags & SOP_F_KEYID)
              {
                crie.cri_flags |= CRD_F_KEYID;
              }

            crie.cri_key = sop->key;
            if (thash)
              {
                crie.cri_next = &cria;
              }
          }

        if (thash)
          {
            cria.cri_alg = sop->mac;
            cria.cri_sid = -1;
            cria.cri_klen = sop->mackeylen * 8;
            if (sop->flags & SOP_F_KEYID)
              {
                cria.cri_flags |= CRD_F_KEYID;
              }

            cria.cri_key = sop->mackey;
          }

        error = crypto_newsession(&sid, txform ? &crie : &cria, 1);
        if (error)
          {
            break;
          }

        cse = csecreate(fcr, sid,
              sop->key, sop->keylen,
              sop->mackey, sop->mackeylen,
              sop->cipher, sop->mac, txform, thash);

        if (cse == NULL)
          {
            crypto_freesession(sid);
            error = -EINVAL;
            break;
          }

        sop->ses = cse->ses;

        break;
      case CIOCFSESSION:
        ses = *(FAR uint32_t *)arg;
        cse = csefind(fcr, ses);
        if (cse == NULL)
          {
            return -EINVAL;
          }

        csedelete(fcr, cse);
        error = csefree(cse);
        break;
      case CIOCCRYPT:
        cop = (FAR struct crypt_op *)arg;
        if (cop->flags & COP_FLAG_CANCEL)
          {
            error = cryptodev_cancel(fcr, cop->reqid);
            break;
          }

        cse = csefind(fcr, cop->ses);
        if (cse == NULL)
          {
            return -EINVAL;
          }

        error = cryptodev_op(fcr, cse, cop);
        break;
      case CIOCCRYPTRET:
        error = cryptodev_getopstatus(fcr,
                                     (FAR struct crypt_op *)arg);
        break;
      case CIOCKEY:
        kop = (FAR struct crypt_kop *)arg;
        if (kop->crk_flags & CRYPTO_F_CANCEL)
          {
            error = cryptodevkey_cancel(fcr, kop);
            break;
          }

        error = cryptodev_key(fcr, kop);
        break;
      case CIOCKEYRET:
        error = cryptodev_getkeystatus(fcr, (FAR struct crypt_kop *)arg);
        break;
      case CIOCASYMFEAT:
        error = crypto_getfeat((FAR int *)arg);
        break;
      default:
        error = -ENOTTY;
    }

  return error;
}

static int cryptodev_op(FAR struct fcrypt *fcr,
                        FAR struct csession *cse,
                        FAR struct crypt_op *cop)
{
  FAR struct cryptop *scrp = NULL;
  struct cryptop crp;
  struct cryptodesc crda;
  struct cryptodesc crde;
  irqstate_t iflags;
  int error = OK;
  uint32_t hid;

  bzero(&crp, sizeof(struct cryptop));
  bzero(&crda, sizeof(struct cryptodesc));
  bzero(&crde, sizeof(struct cryptodesc));

  if (cop->flags & COP_FLAG_CBIMM)
    {
      if (cop->flags & COP_FLAG_REL)
        {
          scrp = cryptop_pool_alloc();
          if (scrp == NULL)
            {
              return -ENOMEM;
            }

          scrp->crp_dst   = cop->dst;
          scrp->crp_mac   = cop->mac;
          scrp->crp_flags = CRYPTO_F_CBIMM | CRYPTO_F_REL;
        }
      else
        {
          scrp = kmm_zalloc(sizeof(*scrp) + cop->olen);
          if (scrp == NULL)
            {
              return -ENOMEM;
            }

          if (cop->dst && cop->olen > 0)
            {
              scrp->crp_dst = (caddr_t)(scrp + 1);
            }
          else if (cop->mac && cop->olen > 0)
            {
              scrp->crp_mac = (caddr_t)(scrp + 1);
            }

          scrp->crp_flags = CRYPTO_F_CBIMM;
        }

      scrp->crp_reqid    = cop->reqid;
      scrp->crp_olen     = cop->olen;
      scrp->crp_sid      = cse->sid;
      scrp->crp_opaque   = fcrypt_get(fcr);
      scrp->crp_callback = cryptodevop_cb;
      crp.crp_opaque     = scrp;
      crp.crp_flags      = CRYPTO_F_CBIMM;

      iflags = spin_lock_irqsave(&fcr->lock);
      TAILQ_INSERT_TAIL(&fcr->crp_inflight, scrp, crp_next);
      spin_unlock_irqrestore(&fcr->lock, iflags);
    }

  if (cse->thash)
    {
      crp.crp_desc = &crda;
      crda.crd_skip = 0;
      crda.crd_len = cop->len;
      crda.crd_inject = 0;

      crda.crd_alg = cse->mac;
      crda.crd_key = (caddr_t)cse->mackey;
      crda.crd_klen = cse->mackeylen * 8;
      if (cop->flags & COP_FLAG_UPDATE)
        {
          crda.crd_flags |= CRD_F_UPDATE;
        }
      else
        {
          crda.crd_flags &= ~CRD_F_UPDATE;
        }

      if (cop->flags & COP_FLAG_UPDATE_AAD)
        {
          crda.crd_flags |= CRD_F_UPDATE_AAD;
        }
    }

  if (cse->txform)
    {
      if (cse->thash)
        {
          crda.crd_next = &crde;
        }
      else
        {
          crp.crp_desc = &crde;
        }

      if (cop->op == COP_ENCRYPT)
        {
          crde.crd_flags |= CRD_F_ENCRYPT;
        }
      else
        {
          crde.crd_flags &= ~CRD_F_ENCRYPT;
        }

      if (cop->flags & COP_FLAG_UPDATE)
        {
          crde.crd_flags |= CRD_F_UPDATE;
        }

      crde.crd_len = cop->len;
      crde.crd_inject = 0;
      crde.crd_alg = cse->cipher;
      crde.crd_key = (caddr_t)cse->key;
      crde.crd_klen = cse->keylen * 8;
    }

  crp.crp_ilen = cop->len;
  crp.crp_olen = cop->olen;
  crp.crp_buf  = cop->src;
  crp.crp_sid  = cse->sid;

  if (cop->iv)
    {
      crp.crp_iv = cop->iv;
      crp.crp_ivlen = cop->ivlen;
    }

  if (cop->dst)
    {
      crp.crp_dst = cop->dst;
    }

  if (cop->mac)
    {
      crp.crp_mac = cop->mac;
    }

  if (cop->aad && cop->aadlen > 0)
    {
      crp.crp_aad = cop->aad;
      crp.crp_aadlen = cop->aadlen;
    }

  if (cop->iterations)
    {
      crp.crp_iter = cop->iterations;
    }

  /* try the fast path first */

  crp.crp_flags |= CRYPTO_F_IOV | CRYPTO_F_NOQUEUE;
  hid = (crp.crp_sid >> 32) & 0xffffffff;
  if (hid >= crypto_drivers_num ||
      (crypto_drivers[hid].cc_flags & CRYPTOCAP_F_SOFTWARE) ||
      crypto_drivers[hid].cc_process == NULL)
    {
      crp.crp_flags = CRYPTO_F_IOV;
      crypto_invoke(&crp);
    }
  else
    {
      crypto_drivers[hid].cc_operations++;
      crypto_drivers[hid].cc_bytes += crp.crp_ilen;
      error = crypto_drivers[hid].cc_process(&crp);
    }

  if ((cop->flags & COP_FLAG_UPDATE) == 0)
    {
      crde.crd_flags &= ~CRD_F_IV_EXPLICIT;
    }

  cop->status = crp.crp_etype;

  if (error && scrp)
    {
      /* Submission failed: the callback will not run, so clean up here. */

      iflags = spin_lock_irqsave(&fcr->lock);
      TAILQ_REMOVE(&fcr->crp_inflight, scrp, crp_next);
      spin_unlock_irqrestore(&fcr->lock, iflags);

      fcrypt_put(fcr);
      cryptop_free(scrp);
    }

  return error;
}

/****************************************************************************
 * Name: cryptodev_cancel
 *
 * Description:
 *   Cancel a pending symmetric async operation identified by reqid.
 *
 *   - If the op is still in-flight, flag it so its completion callback
 *     discards the result when it arrives (the backend still owns the
 *     cryptop, so it cannot be freed here).
 *   - If the result already reached the completion queue, remove and free.
 *   - Otherwise there is no such op: return -ENOENT.
 *
 ****************************************************************************/

static int cryptodev_cancel(FAR struct fcrypt *fcr, uint32_t reqid)
{
  FAR struct cryptop *crp;
  irqstate_t flags;
  uint64_t sid;

  flags = spin_lock_irqsave(&fcr->lock);

  /* Still in-flight: flag it; the completion callback drops the result. */

  TAILQ_FOREACH(crp, &fcr->crp_inflight, crp_next)
    {
      if (crp->crp_reqid == reqid)
        {
          sid = crp->crp_sid;
          crp->crp_flags |= CRYPTO_F_CANCEL;
          spin_unlock_irqrestore(&fcr->lock, flags);

          cryptodev_cancel_dispatch(sid, reqid);
          return OK;
        }
    }

  TAILQ_FOREACH(crp, &fcr->crp_ret, crp_next)
    {
      if (crp->crp_reqid == reqid)
        {
          TAILQ_REMOVE(&fcr->crp_ret, crp, crp_next);
          spin_unlock_irqrestore(&fcr->lock, flags);
          cryptop_free(crp);
          return OK;
        }
    }

  spin_unlock_irqrestore(&fcr->lock, flags);
  return -ENOENT;
}

static int cryptodevop_cb(FAR struct cryptop *crp)
{
  FAR struct fcrypt *fcr = crp->crp_opaque;
  FAR struct pollfd *fds;
  irqstate_t flags;

  flags = spin_lock_irqsave(&fcr->lock);
  TAILQ_REMOVE(&fcr->crp_inflight, crp, crp_next);

  /* Discard the result if a concurrent cancel flagged this op. */

  if (crp->crp_flags & CRYPTO_F_CANCEL)
    {
      spin_unlock_irqrestore(&fcr->lock, flags);
      cryptop_free(crp);
      fcrypt_put(fcr);
      return OK;
    }

  TAILQ_INSERT_TAIL(&fcr->crp_ret, crp, crp_next);
  fds = fcr->fds;
  spin_unlock_irqrestore(&fcr->lock, flags);

  if (fds != NULL)
    {
      poll_notify(&fds, 1, POLLIN);
    }

  fcrypt_put(fcr);
  return OK;
}

static FAR struct cryptop *
fcrypt_dequeue_op(FAR struct fcrypt *fcr, uint32_t reqid)
{
  FAR struct cryptop *crp;
  irqstate_t flags;

  flags = spin_lock_irqsave(&fcr->lock);
  TAILQ_FOREACH(crp, &fcr->crp_ret, crp_next)
    {
      if (crp->crp_reqid == reqid)
        {
          TAILQ_REMOVE(&fcr->crp_ret, crp, crp_next);
          break;
        }
    }

  spin_unlock_irqrestore(&fcr->lock, flags);
  return crp;
}

static int cryptodev_getopstatus(FAR struct fcrypt *fcr,
                                 FAR struct crypt_op *cop)
{
  FAR struct cryptop *crp = fcrypt_dequeue_op(fcr, cop->reqid);

  if (crp == NULL)
    {
      return -EAGAIN;
    }

  if (!(crp->crp_flags & CRYPTO_F_REL))
    {
      if (cop->dst && crp->crp_dst)
        {
          memcpy(cop->dst, crp->crp_dst, crp->crp_olen);
        }

      if (cop->mac && crp->crp_mac)
        {
          memcpy(cop->mac, crp->crp_mac, crp->crp_olen);
        }
    }

  cop->status = crp->crp_etype;
  cryptop_free(crp);

  return OK;
}

static int cryptodev_key(FAR struct fcrypt *fcr, FAR struct crypt_kop *kop)
{
  FAR struct cryptkop *krp = NULL;
  irqstate_t iflags;
  int error = -EINVAL;
  int in;
  int out;
  int size;
  int i;

  if (kop->crk_iparams + kop->crk_oparams > CRK_MAXPARAM)
    {
      return -EFBIG;
    }

  in = kop->crk_iparams;
  out = kop->crk_oparams;
  switch (kop->crk_op)
    {
      case CRK_MOD_EXP:
        if (in == 3 && out == 1)
          {
            break;
          }

        return -EINVAL;
      case CRK_MOD_EXP_CRT:
        if (in == 9 && out == 1)
          {
            break;
          }

        return -EINVAL;
      case CRK_DSA_SIGN:
        if (in == 5 && out == 2)
          {
            break;
          }

        return -EINVAL;
      case CRK_DSA_VERIFY:
        if (in == 7 && out == 0)
          {
            break;
          }

        return -EINVAL;
      case CRK_DH_COMPUTE_KEY:
        if (in == 3 && out == 1)
          {
            break;
          }

        return -EINVAL;
      case CRK_DH_MAKE_PUBLIC:
        if (in == 2 && out == 2)
          {
            break;
          }

        return -EINVAL;
      case CRK_RSA_PKCS15_SIGN:
        if (in == 9 && out == 1)
          {
            break;
          }

        return -EINVAL;
      case CRK_RSA_PKCS15_VERIFY:
        if (in == 5 && out == 0)
          {
            break;
          }

        return -EINVAL;
      case CRK_RSA_PSS_SIGN:
        if (in == 9 && out == 1)
          {
            break;
          }

        return -EINVAL;
      case CRK_RSA_PSS_VERIFY:
        if (in == 4 && out == 0)
          {
            break;
          }

        return -EINVAL;
      case CRK_ECDSA_SECP256R1_SIGN:
        if (in == 2 && out == 2)
          {
            break;
          }

        return -EINVAL;
      case CRK_ECDSA_SECP256R1_VERIFY:
        if (in == 6 && out == 0)
          {
            break;
          }

        return -EINVAL;
      case CRK_ECDSA_SECP256R1_GENKEY:
        if (in == 0 && out == 4)
          {
            break;
          }

        return -EINVAL;
      case CRK_ECDH_GEN_PUBLIC:
        if (in == 0 && out == 4)
          {
            break;
          }

        return -EINVAL;
      case CRK_ECDH_COMPUTE_SHARED:
        if (in == 3 && out == 1)
          {
            break;
          }

        return -EINVAL;

      /* key management */

      case CRK_ALLOCATE_KEY:

      /* outparam: keyid */

        if (in == 0 && out == 1)
          {
            break;
          }

        return -EINVAL;
      case CRK_VALIDATE_KEYID:
      case CRK_DELETE_KEY:
      case CRK_SAVE_KEY:
      case CRK_LOAD_KEY:
      case CRK_UNLOAD_KEY:

      /* inparam: keyid */

        if (in == 1 && out == 0)
          {
            break;
          }

        return -EINVAL;
      case CRK_IMPORT_KEY:

      /* inparam: keyid, raw data */

      case CRK_GENERATE_AES_KEY:

      /* inparam: keyid, keylen 16/24/32(128/192/256) */

        if (in == 2 && out == 0)
          {
            break;
          }

        return -EINVAL;
      case CRK_EXPORT_KEY:
      case CRK_EXPORT_PUBLIC_KEY:

      /* inparam: keyid, outparam: key data */

        if (in == 1 && out == 1)
          {
            break;
          }

        return -EINVAL;
      case CRK_GENERATE_RSA_KEY:
      case CRK_GENERATE_SECP256R1_KEY:

      /* 1 inparam : keypair id or private keyid
       * 2 inparam : private keyid, public keyid
       */

        if ((in == 1 || in == 2) && out == 0)
          {
            break;
          }

        return -EINVAL;
      default:
        return -EINVAL;
    }

  if (kop->crk_flags & CRYPTO_F_REL)
    {
      krp = cryptkop_pool_alloc();
    }
  else
    {
      krp = kmm_zalloc(sizeof *krp);
    }

  if (krp == NULL)
    {
      return -ENOMEM;
    }

  for (i = 0; i < CRK_MAXPARAM; i++)
    {
      krp->krp_param[i].crp_nbits = kop->crk_param[i].crp_nbits;
      if (kop->crk_param[i].crp_nbits > 65536)
        {
          /* XXX how big do we need to support? */

          goto fail;
        }
    }

  krp->krp_op = kop->crk_op;
  krp->krp_iparams = kop->crk_iparams;
  krp->krp_oparams = kop->crk_oparams;
  krp->krp_keytype = kop->crk_keytype;
  krp->krp_optype = kop->crk_optype;
  krp->krp_padding = kop->crk_padding;
  krp->krp_hash = kop->crk_hash;
  krp->krp_status = 0;
  krp->krp_flags = kop->crk_flags;
  krp->krp_reqid = kop->crk_reqid;

  if (krp->krp_flags & CRYPTO_F_CBIMM)
    {
      krp->krp_fcr = fcr;
      krp->krp_callback = cryptodevkey_cb;
      fcrypt_get(fcr);

      iflags = spin_lock_irqsave(&fcr->lock);
      TAILQ_INSERT_TAIL(&fcr->crpk_inflight, krp, krp_next);
      spin_unlock_irqrestore(&fcr->lock, iflags);
    }

  for (i = 0; i < krp->krp_iparams + krp->krp_oparams; i++)
    {
      if (krp->krp_flags & CRYPTO_F_REL)
        {
          krp->krp_param[i].crp_p = kop->crk_param[i].crp_p;
        }
      else
        {
          size = (krp->krp_param[i].crp_nbits + 7) / 8;
          if (size == 0)
            {
              continue;
            }

          krp->krp_param[i].crp_p = kmm_zalloc(size);
          if (i >= krp->krp_iparams)
            {
              continue;
            }

          memcpy(krp->krp_param[i].crp_p, kop->crk_param[i].crp_p, size);
        }
    }

  error = crypto_kinvoke(krp);
  if (error || krp->krp_status)
    {
      if (krp->krp_flags & CRYPTO_F_CBIMM)
        {
          iflags = spin_lock_irqsave(&fcr->lock);
          TAILQ_REMOVE(&fcr->crpk_inflight, krp, krp_next);
          spin_unlock_irqrestore(&fcr->lock, iflags);
        }

      goto fail;
    }

  /* Async path: krp stays alive, callback will enqueue it */

  if (krp->krp_flags & CRYPTO_F_CBIMM)
    {
      return OK;
    }

  if (!(krp->krp_flags & CRYPTO_F_REL))
    {
      for (i = krp->krp_iparams;
           i < krp->krp_iparams + krp->krp_oparams; i++)
        {
          size = (krp->krp_param[i].crp_nbits + 7) / 8;
          if (size == 0)
            {
              continue;
            }

          kop->crk_param[i].crp_nbits = krp->krp_param[i].crp_nbits;
          memcpy(kop->crk_param[i].crp_p, krp->krp_param[i].crp_p, size);
        }
    }

fail:
  if (krp)
    {
      if (krp->krp_flags & CRYPTO_F_CBIMM)
        {
          fcrypt_put(fcr);
        }

      kop->crk_status = krp->krp_status;
      cryptkop_free(krp);
    }

  return error;
}

/****************************************************************************
 * Name: cryptodevkey_cancel
 *
 * Description:
 *   Cancel a pending asymmetric (key) async operation identified by reqid.
 *
 *   - If the op is still in-flight, flag it so its completion callback
 *     discards the result, and forward the cancel to the backend so it can
 *     drop a still-queued operation before it consumes hardware.
 *   - If the result already reached the completion queue, remove and free.
 *   - Otherwise there is no such op: return -ENOENT.
 *
 ****************************************************************************/

static int cryptodevkey_cancel(FAR struct fcrypt *fcr,
                               FAR struct crypt_kop *kop)
{
  FAR struct cryptkop *krp;
  irqstate_t flags;

  flags = spin_lock_irqsave(&fcr->lock);

  /* Still in-flight: flag it; the completion callback drops the result. */

  TAILQ_FOREACH(krp, &fcr->crpk_inflight, krp_next)
    {
      if (krp->krp_reqid == kop->crk_reqid)
        {
          krp->krp_flags |= CRYPTO_F_CANCEL;
          spin_unlock_irqrestore(&fcr->lock, flags);

          cryptodevkey_cancel_dispatch(kop->crk_reqid, kop->crk_op);
          return OK;
        }
    }

  TAILQ_FOREACH(krp, &fcr->crpk_ret, krp_next)
    {
      if (krp->krp_reqid == kop->crk_reqid)
        {
          TAILQ_REMOVE(&fcr->crpk_ret, krp, krp_next);
          spin_unlock_irqrestore(&fcr->lock, flags);
          cryptkop_free(krp);
          return OK;
        }
    }

  spin_unlock_irqrestore(&fcr->lock, flags);
  return -ENOENT;
}

static int cryptodevkey_cb(FAR struct cryptkop *krp)
{
  FAR struct fcrypt *fcr = krp->krp_fcr;
  FAR struct pollfd *fds;
  irqstate_t flags;

  flags = spin_lock_irqsave(&fcr->lock);
  TAILQ_REMOVE(&fcr->crpk_inflight, krp, krp_next);

  /* Discard the result if a concurrent cancel flagged this op. */

  if (krp->krp_flags & CRYPTO_F_CANCEL)
    {
      spin_unlock_irqrestore(&fcr->lock, flags);
      cryptkop_free(krp);
      fcrypt_put(fcr);
      return OK;
    }

  TAILQ_INSERT_TAIL(&fcr->crpk_ret, krp, krp_next);
  fds = fcr->fds;
  spin_unlock_irqrestore(&fcr->lock, flags);

  if (fds != NULL)
    {
      poll_notify(&fds, 1, POLLIN);
    }

  fcrypt_put(fcr);
  return OK;
}

static FAR struct cryptkop *
fcrypt_dequeue_key(FAR struct fcrypt *fcr, uint32_t reqid)
{
  FAR struct cryptkop *krp;
  irqstate_t flags;

  flags = spin_lock_irqsave(&fcr->lock);
  TAILQ_FOREACH(krp, &fcr->crpk_ret, krp_next)
    {
      if (krp->krp_reqid == reqid)
        {
          TAILQ_REMOVE(&fcr->crpk_ret, krp, krp_next);
          break;
        }
    }

  spin_unlock_irqrestore(&fcr->lock, flags);
  return krp;
}

static int cryptodev_getkeystatus(struct fcrypt *fcr, struct crypt_kop *ret)
{
  FAR struct cryptkop *krp = fcrypt_dequeue_key(fcr, ret->crk_reqid);
  int size;
  int i;

  if (krp == NULL)
    {
      return -EAGAIN;
    }

  ret->crk_op = krp->krp_op;
  ret->crk_status = krp->krp_status;
  ret->crk_iparams = krp->krp_iparams;
  ret->crk_oparams = krp->krp_oparams;

  if (!(krp->krp_flags & CRYPTO_F_REL))
    {
      for (i = 0; i < krp->krp_iparams + krp->krp_oparams; i++)
        {
          size = (krp->krp_param[i].crp_nbits + 7) / 8;

          if (i < ret->crk_iparams || size == 0)
            {
              continue;
            }

          ret->crk_param[i].crp_nbits = krp->krp_param[i].crp_nbits;
          memcpy(ret->crk_param[i].crp_p, krp->krp_param[i].crp_p, size);
        }
    }

  cryptkop_free(krp);
  return OK;
}

/* ARGSUSED */

static int cryptof_poll(FAR struct file *filep,
                        FAR struct pollfd *fds, bool setup)
{
  FAR struct fcrypt *fcr = filep->f_priv;
  bool ready = false;
  irqstate_t flags;
  int ret = OK;

  if (fcr == NULL || fds == NULL)
    {
      return -EINVAL;
    }

  flags = spin_lock_irqsave(&fcr->lock);
  if (setup)
    {
      if (!TAILQ_EMPTY(&fcr->crp_ret) ||
          !TAILQ_EMPTY(&fcr->crpk_ret))
        {
          ready = true;
        }
      else if (fcr->fds)
        {
          ret = -EBUSY;
        }
      else
        {
          fcr->fds = fds;
        }
    }
  else
    {
      fcr->fds = NULL;
    }

  spin_unlock_irqrestore(&fcr->lock, flags);

  if (ready)
    {
      poll_notify(&fds, 1, POLLIN);
    }

  return ret;
}

/* ARGSUSED */

static int cryptof_close(FAR struct file *filep)
{
  FAR struct fcrypt *fcr = filep->f_priv;
  irqstate_t flags;

  flags = spin_lock_irqsave(&fcr->lock);
  fcr->fds = NULL;
  spin_unlock_irqrestore(&fcr->lock, flags);

  filep->f_priv = NULL;
  fcrypt_put(fcr);
  return 0;
}

/* Clone csessions into a new fd */

static int cryptof_open(FAR struct file *filep)
{
  FAR struct fcrypt *fcr = filep->f_priv;
  FAR struct fcrypt *fcrd = NULL;
  FAR struct csession *cse;
  FAR struct csession *csed;
  struct cryptoini cria;
  struct cryptoini crie;
  uint64_t sid;
  int ret = 0;

  if (fcr == NULL)
    {
      return -EINVAL;
    }

  /* A 'struct fcrypt' is bound to the fd of the cryptodev,
   * which stores a list of sessions. Each encryption operation
   * would create a context and get a session id, which can be
   * used to find the specific encryption operation. Therefore,
   * in order to complete the copy operation, it is necessary to
   * create same session based on the copy fd and obtain the newly
   * generated session id.
   */

  fcrd = kmm_zalloc(sizeof(struct fcrypt));
  if (fcrd == NULL)
    {
      return -ENOMEM;
    }

  TAILQ_INIT(&fcrd->csessions);
  TAILQ_INIT(&fcrd->crp_inflight);
  TAILQ_INIT(&fcrd->crpk_inflight);
  TAILQ_INIT(&fcrd->crp_ret);
  TAILQ_INIT(&fcrd->crpk_ret);
  atomic_set(&fcrd->refcount, 1);
  spin_lock_init(&fcrd->lock);
  TAILQ_FOREACH(cse, &fcr->csessions, next)
    {
      bzero(&crie, sizeof(crie));
      bzero(&cria, sizeof(cria));
      if (cse->txform)
        {
          crie.cri_alg = cse->cipher;
          crie.cri_klen = cse->keylen * 8;
          crie.cri_key = (caddr_t)cse->key;
          if (cse->thash)
            {
              crie.cri_next = &cria;
            }
        }

      if (cse->thash)
        {
          cria.cri_alg = cse->mac;
          cria.cri_sid = cse->sid;
          cria.cri_klen = cse->mackeylen * 8;
          cria.cri_key = (caddr_t)cse->mackey;
        }

      ret = crypto_newsession(&sid, cse->txform ? &crie : &cria, 1);
      if (ret < 0)
        {
          goto bail;
        }

      csed = csecreate(fcrd, sid,
                        (caddr_t)cse->key, cse->keylen,
                        (caddr_t)cse->mackey, cse->mackeylen,
                        cse->cipher, cse->mac, cse->txform,
                        cse->thash);
      if (csed == NULL)
        {
          crypto_freesession(sid);
          ret = -EINVAL;
          goto bail;
        }

      csed->ses = cse->ses;
    }

  filep->f_priv = fcrd;
  return 0;

bail:
  while ((csed = TAILQ_FIRST(&fcrd->csessions)))
    {
      TAILQ_REMOVE(&fcrd->csessions, csed, next);
      csefree(csed);
    }

  kmm_free(fcrd);
  return ret;
}

static int cryptoopen(FAR struct file *filep)
{
  return 0;
}

static ssize_t cryptoread(FAR struct file *filep,
                          FAR char *buffer, size_t len)
{
  return 0;
}

static ssize_t cryptowrite(FAR struct file *filep,
                           FAR const char *buffer, size_t len)
{
  return len;
}

static int cryptoclose(FAR struct file *filep)
{
  return 0;
}

static int cryptoioctl(FAR struct file *filep, int cmd, unsigned long arg)
{
  FAR struct fcrypt *fcr;
  int fd;
  int error = 0;

  switch (cmd)
    {
      case CRIOGET:
        fcr = kmm_zalloc(sizeof(struct fcrypt));
        if (fcr == NULL)
          {
            return -ENOMEM;
          }

        TAILQ_INIT(&fcr->csessions);
        TAILQ_INIT(&fcr->crp_inflight);
        TAILQ_INIT(&fcr->crpk_inflight);
        TAILQ_INIT(&fcr->crp_ret);
        TAILQ_INIT(&fcr->crpk_ret);
        atomic_set(&fcr->refcount, 1);
        spin_lock_init(&fcr->lock);

        fd = file_allocate_from_inode(&g_cryptoinode, 0, 0, fcr, 0);
        if (fd < 0)
          {
            kmm_free(fcr);
            return fd;
          }

        fcr->sesn = 0;
        *(FAR uint32_t *)arg = fd;
        break;
      default:
        error = -EINVAL;
        break;
    }

  return error;
}

static FAR struct csession *csefind(FAR struct fcrypt *fcr, u_int ses)
{
  FAR struct csession *cse;

  TAILQ_FOREACH(cse, &fcr->csessions, next)
  if (cse->ses == ses)
    {
      return cse;
    }

  return NULL;
}

static int csedelete(FAR struct fcrypt *fcr, FAR struct csession *cse_del)
{
  FAR struct csession *cse;

  TAILQ_FOREACH(cse, &fcr->csessions, next)
    {
      if (cse == cse_del)
        {
          TAILQ_REMOVE(&fcr->csessions, cse, next);
          return 1;
        }
    }

  return 0;
}

static FAR struct csession *cseadd(FAR struct fcrypt *fcr,
                                   FAR struct csession *cse)
{
  TAILQ_INSERT_TAIL(&fcr->csessions, cse, next);
  cse->ses = fcr->sesn++;
  return cse;
}

static FAR struct csession *csecreate(FAR struct fcrypt *fcr, uint64_t sid,
                                      caddr_t key, uint64_t keylen,
                                      caddr_t mackey, uint64_t mackeylen,
                                      uint32_t cipher, uint32_t mac,
                                      bool txform, bool thash)
{
  FAR struct csession *cse;

  cse = csession_alloc();
  if (cse == NULL)
    {
      return NULL;
    }

  cse->sid = sid;
  cse->cipher = cipher;
  cse->mac = mac;
  cse->txform = txform;
  cse->thash = thash;
  cse->keylen = keylen;
  cse->mackeylen = mackeylen;

  if (key != NULL && keylen > 0)
    {
      memcpy(cse->key, key,
             MIN(keylen, AALG_MAX_RESULT_LEN));
    }

  if (mackey != NULL && mackeylen > 0)
    {
      memcpy(cse->mackey, mackey,
             MIN(mackeylen, AALG_MAX_RESULT_LEN));
    }

  cseadd(fcr, cse);
  return cse;
}

static int csefree(FAR struct csession *cse)
{
  int error;

  error = crypto_freesession(cse->sid);
  csession_free(cse);
  return error;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void devcrypto_register(void)
{
  register_driver("/dev/crypto", &g_cryptoops, 0666, NULL);

#ifdef CONFIG_CRYPTO_CRYPTODEV_SOFTWARE_CRYPTO
  swcr_init();
#endif

#ifdef CONFIG_CRYPTO_CRYPTODEV_SOFTWARE_KEYMGMT
  swkey_init();
#endif

#ifdef CONFIG_CRYPTO_CRYPTODEV_HARDWARE
  hwcr_init();
#endif
}
