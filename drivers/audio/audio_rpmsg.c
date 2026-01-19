/****************************************************************************
 * drivers/audio/audio_rpmsg.c
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
#include <stdint.h>
#include <stdio.h>

#include <nuttx/kmalloc.h>
#include <nuttx/rpmsg/rpmsg.h>
#include <sys/param.h>

#include <nuttx/audio/audio.h>
#include <nuttx/audio/audio_rpmsg.h>

/****************************************************************************
 * Private Types
 ****************************************************************************/

#define AUDIO_RPMSG_CONFIGURE        0
#define AUDIO_RPMSG_IOCTL            1
#define AUDIO_RPMSG_START            2
#define AUDIO_RPMSG_STOP             3
#define AUDIO_RPMSG_PAUSE            4
#define AUDIO_RPMSG_RESUME           5
#define AUDIO_RPMSG_NOTIFY_AVAIL     6
#define AUDIO_RPMSG_SEND_DATA        7
#define AUDIO_RPMSG_ALLOCSHM         8
#define AUDIO_RPMSG_FREESHM          9

#define AUDIO_RPMSG_RESPONSE         (1u << 31)
#define AUDIO_RPMSG_IS_RESPONSE(cmd) (!!((cmd) & AUDIO_RPMSG_RESPONSE))
#define AUDIO_RPMSG_GET_COMMAND(cmd) ((cmd) & ~AUDIO_RPMSG_RESPONSE)

#define AUDIO_RPMSG_EPT_PREFIX       "rpmsg-audio-"

struct audio_rpmsg_cookie_s
{
    sem_t    sem;
    int      result;
    FAR void *data;
};

struct audio_rpmsg_header_s
{
  uint32_t command; /* Client request command */
  int32_t  result;  /* Server exce ret */
  uint64_t cookie;  /* Server return data, copy from ***_handler */
};

struct audio_rpmsg_configure_s
{
  struct audio_rpmsg_header_s header;
  struct audio_caps_s caps;
};

struct audio_rpmsg_ioctl_s
{
  struct audio_rpmsg_header_s header;
  int32_t request;
  uint32_t arglen;
  uint64_t arg;
  uint8_t data[0]; /* Request data */
};

struct audio_rpmsg_msg_s
{
  struct audio_rpmsg_header_s header;
  uint16_t msgid;
};

struct audio_rpmsg_alloc_buffer_s
{
  struct audio_rpmsg_header_s header;
  uint64_t addr;  /* Buffer pointer address */
};

#define audio_rpmsg_free_buffer_s audio_rpmsg_alloc_buffer_s

struct audio_rpmsg_send_data_s
{
  struct audio_rpmsg_header_s header;
  uint32_t flags;  /* Apb flags */
  uint32_t length; /* Msg data length */
  uint8_t data[0]; /* Request data */
};

struct audio_rpmsg_notify_avail_s
{
  struct audio_rpmsg_header_s header;
  uint64_t addr;  /* Buffer pointer to dequeue */
};

struct audio_rpmsg_s
{
  struct audio_lowerhalf_s dev; /* Audio lower half (this device) */
  struct rpmsg_endpoint ept;    /* Rpmsg endpoint */
  FAR const char *remotecpu;    /* Remote cpu name to connect */
  FAR const char *devname;      /* Audio rpmsg device name */
  bool consumer;                /* True: consumer, false: producer */
  bool sharebuf;                /* True: share memory, false: not */
  sem_t wait;                   /* Wait sem, used for preventing any
                                 * opreation until the connection
                                 * between two cpu established.
                                 */
  rmutex_t mutex;

  struct audio_info_s info;     /* Formats */
  struct ap_buffer_info_s binfo;

  FAR void *base;               /* Share memory address. */
  int count;                    /* Local apb allocated buffer count. */
  int refs;                     /* Share Memory reference count. */

  size_t navail;                /* Number of available buffers in peer */
  struct dq_queue_s pendq;
  struct dq_queue_s waitq;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int audio_rpmsg_getcaps(FAR struct audio_lowerhalf_s *dev, int type,
                               FAR struct audio_caps_s *caps);
#ifdef CONFIG_AUDIO_MULTI_SESSION
static int audio_rpmsg_configure(FAR struct audio_lowerhalf_s *dev,
                                 FAR void *session,
                                 FAR const struct audio_caps_s *caps);
#else
static int audio_rpmsg_configure(FAR struct audio_lowerhalf_s *dev,
                                 FAR const struct audio_caps_s *caps);
#endif
static int audio_rpmsg_shutdown(FAR struct audio_lowerhalf_s *dev);

#ifdef CONFIG_AUDIO_MULTI_SESSION
static int audio_rpmsg_start(FAR struct audio_lowerhalf_s *dev,
                             FAR void *session);
#else
static int audio_rpmsg_start(FAR struct audio_lowerhalf_s *dev);
#endif

#ifndef CONFIG_AUDIO_EXCLUDE_STOP
#  ifdef CONFIG_AUDIO_MULTI_SESSION
static int audio_rpmsg_stop(FAR struct audio_lowerhalf_s *dev,
                            FAR void *session);
#  else
static int audio_rpmsg_stop(FAR struct audio_lowerhalf_s *dev);
#  endif
#endif

#ifndef CONFIG_AUDIO_EXCLUDE_PAUSE_RESUME
#  ifdef CONFIG_AUDIO_MULTI_SESSION
static int audio_rpmsg_pause(FAR struct audio_lowerhalf_s *dev,
                             FAR void *session);
static int audio_rpmsg_resume(FAR struct audio_lowerhalf_s *dev,
                              FAR void *session);
#  else
static int audio_rpmsg_pause(FAR struct audio_lowerhalf_s *dev);
static int audio_rpmsg_resume(FAR struct audio_lowerhalf_s *dev);
#  endif
#endif

static int audio_rpmsg_allocbuffer(FAR struct audio_lowerhalf_s *dev,
                                   FAR struct audio_buf_desc_s *bufdesc);
static int audio_rpmsg_freebuffer(FAR struct audio_lowerhalf_s *dev,
                                  FAR struct audio_buf_desc_s *bufdesc);
static int audio_rpmsg_enqueuebuffer(FAR struct audio_lowerhalf_s *dev,
                                     FAR struct ap_buffer_s *apb);
static int audio_rpmsg_ioctl(FAR struct audio_lowerhalf_s *dev, int cmd,
                             unsigned long arg);

#ifdef CONFIG_AUDIO_MULTI_SESSION
static int audio_rpmsg_reserve(FAR struct audio_lowerhalf_s *dev,
                               FAR void **session);
#else
static int audio_rpmsg_reserve(FAR struct audio_lowerhalf_s *dev);
#endif

#ifdef CONFIG_AUDIO_MULTI_SESSION
static int audio_rpmsg_release(FAR struct audio_lowerhalf_s *dev,
                               FAR void *session);
#else
static int audio_rpmsg_release(FAR struct audio_lowerhalf_s *dev);
#endif

static int audio_rpmsg_message_handler(FAR struct rpmsg_endpoint *ept,
                                       FAR void *data, size_t len,
                                       uint32_t src, FAR void *priv);
static int audio_rpmsg_send_data_handler(FAR struct rpmsg_endpoint *ept,
                                         FAR void *data, size_t len,
                                         uint32_t src, FAR void *priv);
static int audio_rpmsg_notify_avail_handler(FAR struct rpmsg_endpoint *ept,
                                            FAR void *data, size_t len,
                                            uint32_t src, FAR void *priv);
static int audio_rpmsg_configure_handler(FAR struct rpmsg_endpoint *ept,
                                         FAR void *data, size_t len,
                                         uint32_t src, FAR void *priv);
static int audio_rpmsg_ioctl_handler(FAR struct rpmsg_endpoint *ept,
                                     FAR void *data, size_t len,
                                     uint32_t src, FAR void *priv);
static int audio_rpmsg_allocshm_handler(FAR struct rpmsg_endpoint *ept,
                                        FAR void *data, size_t len,
                                        uint32_t src, FAR void *priv);
static int audio_rpmsg_freeshm_handler(FAR struct rpmsg_endpoint *ept,
                                       FAR void *data, size_t len,
                                       uint32_t src, FAR void *priv);
static int audio_rpmsg_default_response(FAR struct rpmsg_endpoint *ept,
                                        FAR void *data, size_t len,
                                        uint32_t src, FAR void *priv);
static int audio_rpmsg_ioctl_response(FAR struct rpmsg_endpoint *ept,
                                      FAR void *data, size_t len,
                                      uint32_t src, FAR void *priv);
static int audio_rpmsg_allocshm_response(FAR struct rpmsg_endpoint *ept,
                                         FAR void *data, size_t len,
                                         uint32_t src, FAR void *priv);

/****************************************************************************
 * Private Data
 ****************************************************************************/

#ifdef CONFIG_AUDIO_RPMSG_DEVICE_PARAMS
static const audio_rpmsg_param_t g_audio_rpmsg_params[] =
{
  CONFIG_AUDIO_RPMSG_DEVICE_PARAMS
};
#endif

static const rpmsg_ept_cb g_audio_rpmsg_handler[] =
{
  [AUDIO_RPMSG_CONFIGURE]    = audio_rpmsg_configure_handler,
  [AUDIO_RPMSG_IOCTL]        = audio_rpmsg_ioctl_handler,
  [AUDIO_RPMSG_START]        = audio_rpmsg_message_handler,
  [AUDIO_RPMSG_STOP]         = audio_rpmsg_message_handler,
  [AUDIO_RPMSG_PAUSE]        = audio_rpmsg_message_handler,
  [AUDIO_RPMSG_RESUME]       = audio_rpmsg_message_handler,
  [AUDIO_RPMSG_SEND_DATA]    = audio_rpmsg_send_data_handler,
  [AUDIO_RPMSG_NOTIFY_AVAIL] = audio_rpmsg_notify_avail_handler,
  [AUDIO_RPMSG_ALLOCSHM]     = audio_rpmsg_allocshm_handler,
  [AUDIO_RPMSG_FREESHM]      = audio_rpmsg_freeshm_handler,
};

static const rpmsg_ept_cb g_audio_rpmsg_response[] =
{
  [AUDIO_RPMSG_CONFIGURE]    = audio_rpmsg_default_response,
  [AUDIO_RPMSG_IOCTL]        = audio_rpmsg_ioctl_response,
  [AUDIO_RPMSG_START]        = audio_rpmsg_default_response,
  [AUDIO_RPMSG_STOP]         = audio_rpmsg_default_response,
  [AUDIO_RPMSG_PAUSE]        = audio_rpmsg_default_response,
  [AUDIO_RPMSG_RESUME]       = audio_rpmsg_default_response,
  [AUDIO_RPMSG_SEND_DATA]    = audio_rpmsg_default_response,
  [AUDIO_RPMSG_NOTIFY_AVAIL] = audio_rpmsg_default_response,
  [AUDIO_RPMSG_ALLOCSHM]     = audio_rpmsg_allocshm_response,
};

static const struct audio_ops_s g_audio_rpmsg_ops =
{
    audio_rpmsg_getcaps,       /* getcaps        */
    audio_rpmsg_configure,     /* configure      */
    audio_rpmsg_shutdown,      /* shutdown       */
    audio_rpmsg_start,         /* start          */
#ifndef CONFIG_AUDIO_EXCLUDE_STOP
    audio_rpmsg_stop,          /* stop           */
#endif
#ifndef CONFIG_AUDIO_EXCLUDE_PAUSE_RESUME
    audio_rpmsg_pause,         /* pause          */
    audio_rpmsg_resume,        /* resume         */
#endif
    audio_rpmsg_allocbuffer,   /* allocbuffer    */
    audio_rpmsg_freebuffer,    /* freebuffer     */
    audio_rpmsg_enqueuebuffer, /* enqueue_buffer */
    NULL,                      /* cancel_buffer  */
    audio_rpmsg_ioctl,         /* ioctl          */
    NULL,                      /* read           */
    NULL,                      /* write          */
    audio_rpmsg_reserve,       /* reserve        */
    audio_rpmsg_release        /* release        */
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int audio_rpmsg_local_configure(FAR struct audio_rpmsg_s *aud,
                                       FAR const struct audio_caps_s *caps)
{
  int ret = OK;

  /* Process the configure operation */

  nxrmutex_lock(&aud->mutex);
  switch (caps->ac_type)
    {
      case AUDIO_TYPE_OUTPUT:
      case AUDIO_TYPE_INPUT:
        aud->info.format = caps->ac_subtype;
        aud->info.samplerate = caps->ac_controls.hw[0] |
                                  (caps->ac_controls.b[3] << 16);
        aud->info.channels = caps->ac_channels;
        aud->info.subformat = caps->ac_format.b[0];
        memcpy(&aud->info.codec, &caps->ac_codec,
               sizeof(caps->ac_codec));

        audinfo("Codec type %" PRIu8 " %" PRIu32 " %" PRIu8 " %" PRIu8 "\n",
                aud->info.format,
                aud->info.samplerate,
                aud->info.channels,
                aud->info.subformat);
        break;

      default:
        audinfo("default case: %d\n", caps->ac_type);
        ret = -ENOTTY;
        break;
    }

  nxrmutex_unlock(&aud->mutex);
  return ret;
}

static int audio_rpmsg_local_ioctl(FAR struct audio_rpmsg_s *aud, int cmd,
                                   unsigned long arg)
{
  int ret = OK;

  nxrmutex_lock(&aud->mutex);
  switch (cmd)
    {
      /* Report our preferred buffer size and quantity */

      case AUDIOIOC_GETBUFFERINFO:
        memcpy((FAR void *)(uintptr_t)arg, &aud->binfo,
               sizeof(aud->binfo));
        break;

      case AUDIOIOC_SETBUFFERINFO:
        memcpy(&aud->binfo, (FAR void *)(uintptr_t)arg,
               sizeof(aud->binfo));
        ret = -ENOTTY;
        break;

      case AUDIOIOC_GETAUDIOINFO:
        memcpy((FAR void *)(uintptr_t)arg, &aud->info, sizeof(aud->info));
        break;

      default:
        ret = -ENOTTY;
        break;
    }

  nxrmutex_unlock(&aud->mutex);
  return ret;
}

static void audio_rpmsg_callback(FAR struct audio_rpmsg_s *aud,
                                 int reason, FAR void *data)
{
#ifdef CONFIG_AUDIO_MULTI_SESSION
  aud->dev.upper(aud->dev.priv, reason, data, OK, NULL);
#else
  aud->dev.upper(aud->dev.priv, reason, data, OK);
#endif
}

static int audio_rpmsg_send_response(FAR struct audio_rpmsg_s *aud,
                                     FAR struct audio_rpmsg_header_s *header,
                                     size_t len, int result)
{
  if (header->cookie)
    {
      header->command |= AUDIO_RPMSG_RESPONSE;
      header->result   = result;
      return rpmsg_send(&aud->ept, header, len);
    }

  return OK;
}

/****************************************************************************
 * Name: audio_rpmsg_get_tx_buffer
 *
 * Description:
 *   Get the rpmsg device tx payload, the buffer is from the rpmsg
 *   share memory that can be accessed by local and remote cpu.
 *
 * Parameters:
 *   priv  - The rpmsg-device handle
 *   len   - The got memroy size
 *
 * Returned Values:
 *   NULL     - failure
 *   not NULL - success
 *
 ****************************************************************************/

static FAR void *audio_rpmsg_get_tx_buffer(FAR struct audio_rpmsg_s *aud,
                                           FAR uint32_t *len)
{
  int sval;

  nxsem_get_value(&aud->wait, &sval);
  if (sval <= 0)
    {
      rpmsg_wait(&aud->ept, &aud->wait);
      rpmsg_post(&aud->ept, &aud->wait);
    }

  return rpmsg_get_tx_payload_buffer(&aud->ept, len, true);
}

static int audio_rpmsg_send_data(FAR struct audio_rpmsg_s *aud)
{
  FAR struct audio_rpmsg_send_data_s *req;
  FAR struct ap_buffer_s *apb;
  uint32_t space;
  int ret = 0;

  nxrmutex_lock(&aud->mutex);

  if (aud->navail > 0 && dq_empty(&aud->pendq))
    {
      audwarn("%s no data to send.\n", aud->devname);
    }

  while (!dq_empty(&aud->pendq) && aud->navail > 0)
    {
      req = audio_rpmsg_get_tx_buffer(aud, &space);
      if (req == NULL)
        {
          auderr("%s failed to get tx buffer\n", aud->devname);
          return -ENOMEM;
        }

      if (dq_empty(&aud->pendq) || aud->navail == 0)
        {
          rpmsg_release_tx_buffer(&aud->ept, req);
          break;
        }

      DEBUGASSERT(space > sizeof(*req));
      req->flags = 0;
      req->header.result = 0;
      req->header.command = AUDIO_RPMSG_SEND_DATA;
      apb = (FAR struct ap_buffer_s *)dq_peek(&aud->pendq);
      if (!aud->sharebuf)
        {
          space -= sizeof(*req);
          if (space >= apb->nbytes - apb->curbyte)
            {
              space = apb->nbytes - apb->curbyte;
            }

          memcpy(req->data, apb->samp + apb->curbyte, space);
          req->length = space;

          if ((apb->curbyte + space) == apb->nbytes)
            {
              req->flags = apb->flags;
            }

          ret = rpmsg_send_nocopy(&aud->ept, req, sizeof(*req) + space);
        }
      else
        {
          space = apb->nbytes - apb->curbyte;
          req->length = space;
          req->flags = apb->flags;
          ret = rpmsg_send_nocopy(&aud->ept, req, sizeof(*req));
        }

      if (ret < 0)
        {
          auderr("%s failed to send\n", aud->devname);
          rpmsg_release_tx_buffer(&aud->ept, req);
          break;
        }

      apb->curbyte += space;

      if (apb->curbyte == apb->nbytes)
        {
          dq_remfirst(&aud->pendq);
          if (aud->sharebuf)
            {
              dq_addlast(&apb->dq_entry, &aud->waitq);
            }
          else
            {
              audio_rpmsg_callback(aud, AUDIO_CALLBACK_DEQUEUE, apb);
              if (apb->flags & AUDIO_APB_FINAL)
                {
                  audio_rpmsg_callback(aud, AUDIO_CALLBACK_COMPLETE, apb);
                }
            }

          aud->navail--;
        }
    }

  nxrmutex_unlock(&aud->mutex);
  return ret;
}

static int audio_rpmsg_notify_avail(FAR struct audio_rpmsg_s *aud)
{
  FAR struct audio_rpmsg_notify_avail_s req;
  FAR struct ap_buffer_s *apb;

  nxrmutex_lock(&aud->mutex);
  apb = (FAR struct ap_buffer_s *)dq_tail(&aud->pendq);
  req.addr = up_addrenv_va_to_pa(apb->samp);
  nxrmutex_unlock(&aud->mutex);

  req.header.result = 0;
  req.header.command = AUDIO_RPMSG_NOTIFY_AVAIL;
  return rpmsg_send(&aud->ept, &req, sizeof(req));
}

static int audio_rpmsg_deliver_buffer(FAR struct audio_rpmsg_s *aud)
{
  if (aud->consumer)
    {
      return audio_rpmsg_notify_avail(aud);
    }
  else
    {
      return audio_rpmsg_send_data(aud);
    }
}

static int audio_rpmsg_deliver_message(FAR struct audio_rpmsg_s *aud,
                                       int cmd, int msgid)
{
  struct audio_rpmsg_msg_s req =
    {
      0
    };

  int ret;

  req.header.command = cmd;
  req.msgid = msgid;
  ret = rpmsg_send(&aud->ept, &req, sizeof(req));
  if (ret < 0)
    {
      return ret;
    }

  return OK;
}

/****************************************************************************
 * Name: audio_rpmsg_default_response
 *
 * Description:
 *   This function is used to handle the response from the RPMSG device.
 *   It is used to copy the rpc result to the cookie and post the semaphore.
 *
 ****************************************************************************/

static int audio_rpmsg_default_response(FAR struct rpmsg_endpoint *ept,
                                        FAR void *data, size_t len,
                                        uint32_t src, FAR void *priv)
{
  FAR struct audio_rpmsg_header_s *header = data;
  FAR struct audio_rpmsg_cookie_s *cookie =
    (struct audio_rpmsg_cookie_s *)(uintptr_t)header->cookie;

  cookie->result = header->result;
  rpmsg_post(ept, &cookie->sem);
  return 0;
}

static int audio_rpmsg_ioctl_response(FAR struct rpmsg_endpoint *ept,
                                      FAR void *data, size_t len,
                                      uint32_t src, FAR void *priv)
{
  FAR struct audio_rpmsg_header_s *header = data;
  FAR struct audio_rpmsg_cookie_s *cookie =
    (struct audio_rpmsg_cookie_s *)(uintptr_t)header->cookie;
  FAR struct audio_rpmsg_ioctl_s *req = data;

  cookie->result = header->result;
  if (cookie->result >= 0 && req->arglen > 0)
    {
      memcpy(cookie->data, (FAR void *)(uintptr_t)req->data,
             req->arglen);
    }

  rpmsg_post(ept, &cookie->sem);
  return 0;
}

static int audio_rpmsg_allocshm_response(FAR struct rpmsg_endpoint *ept,
                                         FAR void *data, size_t len,
                                         uint32_t src, FAR void *priv)
{
  FAR struct audio_rpmsg_header_s *header = data;
  FAR struct audio_rpmsg_cookie_s *cookie =
    (struct audio_rpmsg_cookie_s *)(uintptr_t)header->cookie;
  FAR struct audio_rpmsg_alloc_buffer_s *req = data;
  FAR struct audio_rpmsg_alloc_buffer_s *resp =
    (struct audio_rpmsg_alloc_buffer_s *)cookie->data;

  cookie->result = header->result;
  resp->addr = req->addr;

  rpmsg_post(ept, &cookie->sem);
  return 0;
}

static int audio_rpmsg_configure_handler(FAR struct rpmsg_endpoint *ept,
                                         FAR void *data, size_t len,
                                         uint32_t src, FAR void *priv)
{
  FAR struct audio_rpmsg_s *aud = ept->priv;
  FAR struct audio_rpmsg_header_s *header = data;
  FAR struct audio_rpmsg_configure_s *req = data;
  int ret;

  audinfo("ac_type: %d\n", req->caps.ac_type);

  /* Process the configure operation */

  ret = audio_rpmsg_local_configure(aud, &req->caps);
  return audio_rpmsg_send_response(aud, header, sizeof(*req), ret);
}

static int audio_rpmsg_message_handler(FAR struct rpmsg_endpoint *ept,
                                       FAR void *data, size_t len,
                                       uint32_t src, FAR void *priv)
{
  FAR struct audio_rpmsg_s *aud = ept->priv;
  FAR struct audio_rpmsg_msg_s *req = data; /* data from request */
  struct audio_msg_s msg =
    {
      0
    };

  audinfo("%s handle message: %d.\n", aud->devname, req->msgid);

  nxrmutex_lock(&aud->mutex);
  if (req->msgid == AUDIO_MSG_STOP)
    {
      aud->navail = 0;
    }

  nxrmutex_unlock(&aud->mutex);
  msg.msg_id = req->msgid;
  audio_rpmsg_callback(aud, AUDIO_CALLBACK_MESSAGE, &msg);

  return OK;
}

static int audio_rpmsg_ioctl_handler(FAR struct rpmsg_endpoint *ept,
                                     FAR void *data, size_t len,
                                     uint32_t src, FAR void *priv)
{
  FAR struct audio_rpmsg_s *aud = ept->priv;
  FAR struct audio_rpmsg_ioctl_s *req = data;
  int ret = 0;

  audinfo("cmd=%" PRIu32 " arg=%" PRIu64 "\n", req->request, req->arg);

  /* Deal with ioctls passed from the upper-half driver */

  if (req->request == AUDIOIOC_HWRESET)
    {
      struct audio_msg_s msg =
        {
          0
        };

      msg.msg_id = AUDIO_MSG_IOERR;
      audio_rpmsg_callback(aud, AUDIO_CALLBACK_MESSAGE, &msg);
      ret = 0;
    }
  else
    {
      ret = audio_rpmsg_local_ioctl(aud, req->request,
                                    req->arglen > 0 ?
                                    (unsigned long)req->data : req->arg);
    }

  return audio_rpmsg_send_response(aud, &req->header, len,
                                   ret >= 0 ? (int)req->arglen : ret);
}

static int audio_rpmsg_send_data_handler(FAR struct rpmsg_endpoint *ept,
                                         FAR void *data, size_t len,
                                         uint32_t src, FAR void *priv)
{
  FAR struct audio_rpmsg_s *aud = ept->priv;
  FAR struct audio_rpmsg_send_data_s *req = data;
  struct ap_buffer_s *apb;

  /* handle data sent from producer */

  nxrmutex_lock(&aud->mutex);
  if (dq_empty(&aud->pendq))
    {
      nxrmutex_unlock(&aud->mutex);
      audwarn("%s no buffer available\n", aud->devname);
      return 0;
    }

  apb = (FAR struct ap_buffer_s *)dq_peek(&aud->pendq);

  if (!aud->sharebuf)
    {
      DEBUGASSERT((apb->nmaxbytes - apb->curbyte) >= req->length);

      memcpy(apb->samp + apb->curbyte, req->data, req->length);
    }

  apb->curbyte += req->length;

  if (apb->curbyte == apb->nmaxbytes || req->flags & AUDIO_APB_FINAL)
    {
      dq_remfirst(&aud->pendq);
      apb->flags = req->flags;
      apb->nbytes = apb->curbyte;
      audio_rpmsg_callback(aud, AUDIO_CALLBACK_DEQUEUE, apb);
      if (apb->flags & AUDIO_APB_FINAL)
        {
          audio_rpmsg_callback(aud, AUDIO_CALLBACK_COMPLETE, NULL);
        }
    }

  nxrmutex_unlock(&aud->mutex);
  return 0;
}

static int audio_rpmsg_notify_avail_handler(FAR struct rpmsg_endpoint *ept,
                                            FAR void *data, size_t len,
                                            uint32_t src, FAR void *priv)
{
  FAR struct audio_rpmsg_s *aud = ept->priv;
  FAR struct audio_rpmsg_notify_avail_s *req = data;
  FAR struct ap_buffer_s *apb;

  nxrmutex_lock(&aud->mutex);
  aud->navail++;
  if (aud->sharebuf && !dq_empty(&aud->waitq))
    {
      apb = (FAR struct ap_buffer_s *)dq_peek(&aud->waitq);
      if (up_addrenv_va_to_pa(apb->samp) == req->addr)
        {
          dq_remfirst(&aud->waitq);
          audio_rpmsg_callback(aud, AUDIO_CALLBACK_DEQUEUE, apb);
          if (apb->flags & AUDIO_APB_FINAL)
            {
              audio_rpmsg_callback(aud, AUDIO_CALLBACK_COMPLETE, apb);
            }
        }
    }

  nxrmutex_unlock(&aud->mutex);

  return audio_rpmsg_deliver_buffer(aud);
}

static int audio_rpmsg_ept_cb(FAR struct rpmsg_endpoint *ept, FAR void *data,
                              size_t len, uint32_t src, FAR void *priv)
{
  FAR struct audio_rpmsg_header_s *header = data;
  uint32_t cmd = AUDIO_RPMSG_GET_COMMAND(header->command);
  int ret = 0;

  if (cmd < nitems(g_audio_rpmsg_handler))
    {
      if (AUDIO_RPMSG_IS_RESPONSE(header->command))
        {
          ret = g_audio_rpmsg_response[cmd](ept, data, len, src, priv);
        }
      else
        {
          ret = g_audio_rpmsg_handler[cmd](ept, data, len, src, priv);
        }
    }
  else
    {
      auderr("cmd=%" PRIu32 " callback handler is null\n", cmd);
      ret = -EINVAL;
    }

  if (ret < 0)
    {
      auderr("response:%d cmd=%" PRIu32 " fail\n",
        AUDIO_RPMSG_IS_RESPONSE(header->command), cmd);
    }

  return ret;
}

/****************************************************************************
 * Name: audio_rpmsg_ns_bound
 *
 * Description:
 *   Rpmsg device end point service bound callback function , called when
 *   remote end point address is received.
 *
 * Parameters:
 *   ept  - The rpmsg-device end point
 *
 * Returned Values:
 *   None
 *
 ****************************************************************************/

static void audio_rpmsg_ns_bound(FAR struct rpmsg_endpoint *ept)
{
  FAR struct audio_rpmsg_s *aud = ept->priv;

  rpmsg_post(&aud->ept, &aud->wait);
}

/****************************************************************************
 * Name: audio_rpmsg_ns_unbind
 *
 * Description:
 *   This is the unbind callback function.
 *
 * Parameters:
 *   ept - rpmsg_endpoint for communicating with audio rpmsg driver.
 *
 ****************************************************************************/

static void audio_rpmsg_ns_unbind(FAR struct rpmsg_endpoint *ept)
{
  FAR struct audio_rpmsg_s *aud = ept->priv;

  rpmsg_wait(&aud->ept, &aud->wait);
}

/****************************************************************************
 * Name: audio_rpmsg_device_created
 *
 * Description:
 *   Rpmsg device create function, this function will be called by rptun to
 *   create a rpmsg-audio end point.
 *
 * Parameters:
 *   rdev  - The rpmsg-audio end point
 *   priv  - Rpmsg-audio handle
 *
 * Returned Values:
 *   None
 *
 ****************************************************************************/

static void audio_rpmsg_device_created(FAR struct rpmsg_device *rdev,
                                       FAR void *priv)
{
  FAR struct audio_rpmsg_s *aud = (FAR struct audio_rpmsg_s *)priv;
  char eptname[RPMSG_NAME_SIZE];

  if (strcmp(aud->remotecpu, rpmsg_get_cpuname(rdev)) == 0)
    {
      aud->ept.priv = aud;
      aud->ept.ns_bound_cb = audio_rpmsg_ns_bound;
      snprintf(eptname, sizeof(eptname), "%s%s",
               AUDIO_RPMSG_EPT_PREFIX, aud->devname);
      rpmsg_create_ept(&aud->ept, rdev, eptname, RPMSG_ADDR_ANY,
                       RPMSG_ADDR_ANY, audio_rpmsg_ept_cb,
                       audio_rpmsg_ns_unbind);
    }
}

/****************************************************************************
 * Name: audio_rpmsg_device_destroy
 *
 * Description:
 *   Rpmsg device destroy function, this function will be called by rptun to
 *   destroy rpmsg-audio end point.
 *
 * Parameters:
 *   rdev  - The rpmsg-device end point
 *   priv_ - Rpmsg-device handle
 *
 * Returned Values:
 *   None
 *
 ****************************************************************************/

static void audio_rpmsg_device_destroy(FAR struct rpmsg_device *rdev,
                                       FAR void *priv)
{
  FAR struct audio_rpmsg_s *aud = (FAR struct audio_rpmsg_s *)priv;

  if (strcmp(aud->remotecpu, rpmsg_get_cpuname(rdev)) == 0)
    {
      rpmsg_destroy_ept(&aud->ept);
    }
}

/****************************************************************************
 * Name: audio_rpmsg_send_recv
 *
 * Description:
 *   Send and receive the rpmsg data.
 *
 * Parameters:
 *   priv    - audio rpmsg handle
 *   command - the command, details see AUDIO_RPMSG_* in audio_rpmsg.h
 *   copy    - true, send a message across to the remote processor, and the
 *                   tx buffer will be alloced inside function rpmsg_send()
 *             false, send a message in tx buffer reserved by
 *                    rpmsg_get_tx_payload_buffer() across to the remote
 *                    processor.
 *   msg     - the message header
 *   len     - length of the payload
 *   data    - the data
 *
 * Returned Values:
 *   OK on success; A negated errno value is returned on any failure.
 *
 ****************************************************************************/

static int audio_rpmsg_send_recv(FAR struct audio_rpmsg_s *aud,
                                 uint32_t command, bool copy,
                                 FAR struct audio_rpmsg_header_s *req,
                                 size_t len, FAR void *data)
{
  struct audio_rpmsg_cookie_s cookie;
  int ret;

  memset(&cookie, 0, sizeof(cookie));
  nxsem_init(&cookie.sem, 0, 0);

  if (data)
    {
      cookie.data = data;
    }
  else if (copy)
    {
      cookie.data = req;
    }

  req->command = command;
  req->result = -ENXIO;
  req->cookie = (uintptr_t)&cookie;

  if (copy)
    {
      ret = rpmsg_send(&aud->ept, req, len);
    }
  else
    {
      ret = rpmsg_send_nocopy(&aud->ept, req, len);
      if (ret < 0)
        {
          rpmsg_release_tx_buffer(&aud->ept, req);
        }
    }

  if (ret >= 0)
    {
      ret = rpmsg_wait(&aud->ept, &cookie.sem);
      if (ret >= 0)
        {
          ret = cookie.result;
        }
    }

  nxsem_destroy(&cookie.sem);
  return ret;
}

static int audio_rpmsg_allocshm(FAR struct audio_rpmsg_s *aud)
{
  FAR struct rpmsg_endpoint *ept = &aud->ept;
  FAR struct rpmsg_device *rdev = ept->rdev;
  int ret;
  struct audio_rpmsg_alloc_buffer_s req =
    {
      0
    };

  if (aud->refs)
    {
      aud->refs++;
      return 0;
    }

  /* We fix allocating share memory in master core. */

  if (rpmsg_alloc_local(rdev))
    {
      size_t shmsize = aud->binfo.buffer_size * aud->binfo.nbuffers;
      aud->base = rpmsg_alloc_buf(rdev, shmsize, 4);
      if (aud->base == NULL)
        {
          auderr("%s failed to alloc share memory, size:%d!!!\n",
                 aud->devname, shmsize);
          return -ENOMEM;
        }

      memset(aud->base, 0, shmsize);
    }
  else
    {
      ret = audio_rpmsg_send_recv(aud, AUDIO_RPMSG_ALLOCSHM, true,
                                  &req.header, sizeof(req), &req);
      if (ret < 0)
        {
          auderr("%s init shm fail:%d.\n", aud->devname, ret);
          return ret;
        }

      aud->base = (FAR void *)up_addrenv_pa_to_va(req.addr);
    }

  aud->refs++;
  return 0;
}

static void audio_rpmsg_freeshm(FAR struct audio_rpmsg_s *aud)
{
  FAR struct rpmsg_endpoint *ept = &aud->ept;
  FAR struct rpmsg_device *rdev = ept->rdev;
  struct audio_rpmsg_free_buffer_s req =
    {
      0
    };

  auderr("%s all buffer freed.\n", aud->devname);

  if (--aud->refs)
    {
      return;
    }

  if (rpmsg_alloc_local(rdev))
    {
      rpmsg_free_buf(rdev, aud->base);
    }
  else
    {
      req.header.command = AUDIO_RPMSG_FREESHM;
      rpmsg_send(&aud->ept, &req, sizeof(req));
    }

  aud->base = NULL;
}

static FAR struct ap_buffer_s *
audio_rpmsg_alloc_apb(FAR struct audio_rpmsg_s *aud)
{
  FAR struct ap_buffer_s *apb;
  int ret;

  nxrmutex_lock(&aud->mutex);
  if (aud->count == aud->binfo.nbuffers)
    {
      nxrmutex_unlock(&aud->mutex);
      return NULL;
    }

  if (aud->count == 0)
    {
      ret = audio_rpmsg_allocshm(aud);
      if (ret < 0)
        {
          nxrmutex_unlock(&aud->mutex);
          return NULL;
        }
    }

  apb = kumm_zalloc(sizeof(struct ap_buffer_s));
  if (!apb)
    {
      nxrmutex_unlock(&aud->mutex);
      return NULL;
    }

  /* Populate the buffer contents */

  apb->i.channels = 1;
  apb->crefs = 1;
  apb->nmaxbytes = aud->binfo.buffer_size;
#ifdef CONFIG_AUDIO_MULTI_SESSION
  apb->session = bufdesc->session;
#endif
  nxmutex_init(&apb->lock);

  apb->samp = (FAR uint8_t *)(aud->base +
    aud->binfo.buffer_size * aud->count);
  aud->count++;
  nxrmutex_unlock(&aud->mutex);
  return apb;
}

static void audio_rpmsg_free_apb(FAR struct audio_rpmsg_s *aud,
                                FAR struct ap_buffer_s *apb)
{
  int crefs;

  DEBUGASSERT(apb != NULL);

  nxmutex_lock(&apb->lock);
  crefs = --apb->crefs;
  nxmutex_unlock(&apb->lock);
  if (crefs != 0)
    {
      return;
    }

  nxmutex_destroy(&apb->lock);
  kumm_free(apb);

  nxrmutex_lock(&aud->mutex);
  if (--aud->count == 0)
    {
      audio_rpmsg_freeshm(aud);
    }

  nxrmutex_unlock(&aud->mutex);
}

static int audio_rpmsg_freeshm_handler(FAR struct rpmsg_endpoint *ept,
                                       FAR void *data, size_t len,
                                       uint32_t src, FAR void *priv)
{
  FAR struct audio_rpmsg_s *aud = ept->priv;
  FAR struct rpmsg_device *rdev = ept->rdev;

  DEBUGASSERT(rpmsg_alloc_local(rdev));

  nxrmutex_lock(&aud->mutex);
  audio_rpmsg_freeshm(aud);
  nxrmutex_unlock(&aud->mutex);
  return 0;
}

static int audio_rpmsg_allocshm_handler(FAR struct rpmsg_endpoint *ept,
                                        FAR void *data, size_t len,
                                        uint32_t src, FAR void *priv)
{
  FAR struct audio_rpmsg_s *aud = ept->priv;
  FAR struct rpmsg_device *rdev = ept->rdev;
  FAR struct audio_rpmsg_alloc_buffer_s *req = data;
  int ret = 0;

  DEBUGASSERT(rpmsg_alloc_local(rdev));

  audinfo("%s handle allocbuffer:%d\n", aud->devname, aud->refs);

  nxrmutex_lock(&aud->mutex);
  ret = audio_rpmsg_allocshm(aud);
  req->addr = up_addrenv_va_to_pa(aud->base);
  nxrmutex_unlock(&aud->mutex);
  return audio_rpmsg_send_response(aud, &req->header, sizeof(*req), ret);
}

/****************************************************************************
 * Name: audio_rpmsg_getcaps
 *
 * Description:
 *   Get the audio capabilities from device.
 *
 ****************************************************************************/

static int audio_rpmsg_getcaps(FAR struct audio_lowerhalf_s *dev, int type,
                               FAR struct audio_caps_s *caps)
{
  FAR struct audio_rpmsg_s *aud = (FAR struct audio_rpmsg_s *)dev;

  memset(&caps->ac_format, 0, sizeof(caps->ac_format));
  memset(&caps->ac_controls, 0, sizeof(caps->ac_controls));
  memset(&caps->ac_codec, 0, sizeof(caps->ac_codec));

  nxrmutex_lock(&aud->mutex);
  switch (caps->ac_type)
    {
      case AUDIO_TYPE_QUERY:

        caps->ac_channels = aud->info.channels;

        switch (caps->ac_subtype)
          {
            case AUDIO_TYPE_QUERY:
              caps->ac_controls.b[0] =
                aud->consumer ? AUDIO_TYPE_INPUT : AUDIO_TYPE_OUTPUT;

              if (aud->info.subformat == 0)
                {
                  nxrmutex_unlock(&aud->mutex);
                  return -ENOSYS;
                }

              caps->ac_format.hw = 1 << (aud->info.format - 1);
              break;

            case AUDIO_FMT_MP3:
            case AUDIO_FMT_PCM:
            case AUDIO_FMT_AAC:
            case AUDIO_FMT_AC3:
            case AUDIO_FMT_AMR:
            case AUDIO_FMT_DTS:
            case AUDIO_FMT_SBC:
            case AUDIO_FMT_WMA:
            case AUDIO_FMT_WAV:
            case AUDIO_FMT_FLAC:
            case AUDIO_FMT_MIDI:
            case AUDIO_FMT_MPEG:
            case AUDIO_FMT_MSBC:
            case AUDIO_FMT_CVSD:
            case AUDIO_FMT_OPUS:
            case AUDIO_FMT_OGG_VORBIS:
              caps->ac_controls.b[0] = aud->info.subformat;
              caps->ac_controls.b[1] = AUDIO_SUBFMT_END;

              memcpy(&caps->ac_codec, &aud->info.codec,
                     sizeof(aud->info.codec));
              break;

            default:
              caps->ac_controls.b[0] = AUDIO_SUBFMT_END;
              break;
          }

        break;

      case AUDIO_TYPE_OUTPUT:
      case AUDIO_TYPE_INPUT:
        caps->ac_channels = (aud->info.channels << 4) |
                            (aud->info.channels & 0x0f);

        switch (caps->ac_subtype)
          {
            case AUDIO_TYPE_QUERY:

              /* Report the Sample rates we support */

              caps->ac_controls.hw[0] =
                aud->info.samplerate == 8000 ? AUDIO_SAMP_RATE_8K :
                aud->info.samplerate == 12000 ? AUDIO_SAMP_RATE_12K :
                aud->info.samplerate == 16000 ? AUDIO_SAMP_RATE_16K :
                aud->info.samplerate == 22050 ? AUDIO_SAMP_RATE_22K :
                aud->info.samplerate == 24000 ? AUDIO_SAMP_RATE_24K :
                aud->info.samplerate == 32000 ? AUDIO_SAMP_RATE_32K :
                aud->info.samplerate == 44100 ? AUDIO_SAMP_RATE_44K :
                aud->info.samplerate == 48000 ? AUDIO_SAMP_RATE_48K :
                aud->info.samplerate == 88200 ? AUDIO_SAMP_RATE_88K :
                aud->info.samplerate == 96000 ? AUDIO_SAMP_RATE_96K :
                aud->info.samplerate == 192000 ? AUDIO_SAMP_RATE_192K :
                AUDIO_SAMP_RATE_DEF_ALL;
              break;

            default:
              break;
          }

        break;
    }

  nxrmutex_unlock(&aud->mutex);

  /* Return the length of the audio_caps_s struct for validation of
   * proper Audio device type.
   */

  audinfo("Return %d\n", caps->ac_len);
  return caps->ac_len;
}

/****************************************************************************
 * Name: audio_rpmsg_configure
 *
 * Description:
 *   Configure the driver.  Return directly at proxy peer.
 *
 ****************************************************************************/

#ifdef CONFIG_AUDIO_MULTI_SESSION
static int audio_rpmsg_configure(FAR struct audio_lowerhalf_s *dev,
                                 FAR void *session,
                                 FAR const struct audio_caps_s *caps)
#else
static int audio_rpmsg_configure(FAR struct audio_lowerhalf_s *dev,
                                 FAR const struct audio_caps_s *caps)
#endif
{
  FAR struct audio_rpmsg_s *aud = (struct audio_rpmsg_s *)dev;
  FAR struct audio_rpmsg_configure_s *req;
  uint32_t space;
  int ret;

  audinfo("ac_type: %d\n", caps->ac_type);

  ret = audio_rpmsg_local_configure(aud, caps);
  if (ret < 0)
    {
      return ret;
    }

  /* Process the configure rpc */

  req = audio_rpmsg_get_tx_buffer(aud, &space);
  if (req == NULL)
    {
      return -ENOMEM;
    }

  DEBUGASSERT(sizeof(*req) <= space);

  memcpy(&req->caps, caps, sizeof(struct audio_caps_s));
  ret = audio_rpmsg_send_recv(aud, AUDIO_RPMSG_CONFIGURE, false,
                              &req->header, sizeof(*req), NULL);
  if (ret < 0)
    {
      auderr("configure failed, ret=%d\n", ret);
      return ret;
    }

  return OK;
}

static int audio_rpmsg_shutdown(FAR struct audio_lowerhalf_s *dev)
{
  return OK;
}

/****************************************************************************
 * Name: audio_rpmsg_start
 *
 * Description:
 *   Start the driver and put it in the lowest power state possible.
 *
 ****************************************************************************/
#ifdef CONFIG_AUDIO_MULTI_SESSION
static int audio_rpmsg_start(FAR struct audio_lowerhalf_s *dev,
                             FAR void *session)
#else
static int audio_rpmsg_start(FAR struct audio_lowerhalf_s *dev)
#endif
{
  FAR struct audio_rpmsg_s *aud = (FAR struct audio_rpmsg_s *)dev;

  audinfo("audio %s start.\n", aud->devname);

  /* Notify peer to start */

  return audio_rpmsg_deliver_message(aud, AUDIO_RPMSG_START,
                                     AUDIO_MSG_START);
}

/****************************************************************************
 * Name: audio_rpmsg_stop
 *
 * Description:
 *   Stop the configured operation (audio streaming, volume
 *              disabled, etc.).
 *
 ****************************************************************************/

#ifndef CONFIG_AUDIO_EXCLUDE_STOP
#ifdef CONFIG_AUDIO_MULTI_SESSION
static int audio_rpmsg_stop(FAR struct audio_lowerhalf_s *dev,
                            FAR void *session)
#else
static int audio_rpmsg_stop(FAR struct audio_lowerhalf_s *dev)
#endif
{
  FAR struct audio_rpmsg_s *aud = (FAR struct audio_rpmsg_s *)dev;
  FAR struct ap_buffer_s *apb;

  audinfo("audio %s stop.\n", aud->devname);

  nxrmutex_lock(&aud->mutex);
  while (!dq_empty(&aud->waitq))
    {
      apb = (FAR struct ap_buffer_s *)dq_remfirst(&aud->waitq);
      audio_rpmsg_callback(aud, AUDIO_CALLBACK_DEQUEUE, apb);
    }

  while (!dq_empty(&aud->pendq))
    {
      apb = (FAR struct ap_buffer_s *)dq_remfirst(&aud->pendq);
      audio_rpmsg_callback(aud, AUDIO_CALLBACK_DEQUEUE, apb);
    }

  aud->navail = 0;
  audio_rpmsg_callback(aud, AUDIO_CALLBACK_COMPLETE, NULL);
  nxrmutex_unlock(&aud->mutex);

  /* Notify peer to stop */

  return audio_rpmsg_deliver_message(aud, AUDIO_RPMSG_STOP,
                                     AUDIO_MSG_STOP);
}
#endif

/****************************************************************************
 * Name: audio_rpmsg_pause
 *
 * Description: Pauses the playback.
 *
 ****************************************************************************/

#ifndef CONFIG_AUDIO_EXCLUDE_PAUSE_RESUME
#ifdef CONFIG_AUDIO_MULTI_SESSION
static int audio_rpmsg_pause(FAR struct audio_lowerhalf_s *dev,
                             FAR void *session)
#else
static int audio_rpmsg_pause(FAR struct audio_lowerhalf_s *dev)
#endif
{
  FAR struct audio_rpmsg_s *aud = (FAR struct audio_rpmsg_s *)dev;

  audinfo("audio %s pause.\n", aud->devname);

  return audio_rpmsg_deliver_message(aud, AUDIO_RPMSG_PAUSE,
                                     AUDIO_MSG_PAUSE);
}
#endif /* CONFIG_AUDIO_EXCLUDE_PAUSE_RESUME */

/****************************************************************************
 * Name: audio_rpmsg_resume
 *
 * Description: Resumes the playback.
 *
 ****************************************************************************/

#ifndef CONFIG_AUDIO_EXCLUDE_PAUSE_RESUME
#ifdef CONFIG_AUDIO_MULTI_SESSION
static int audio_rpmsg_resume(FAR struct audio_lowerhalf_s *dev,
                              FAR void *session)
#else
static int audio_rpmsg_resume(FAR struct audio_lowerhalf_s *dev)
#endif
{
  FAR struct audio_rpmsg_s *aud = (FAR struct audio_rpmsg_s *)dev;

  audinfo("audio %s resume.\n", aud->devname);

  /* Notify peer to resume */

  return audio_rpmsg_deliver_message(aud, AUDIO_RPMSG_RESUME,
                                     AUDIO_MSG_RESUME);
}
#endif /* CONFIG_AUDIO_EXCLUDE_PAUSE_RESUME */

static int audio_rpmsg_allocbuffer(FAR struct audio_lowerhalf_s *dev,
                                   FAR struct audio_buf_desc_s *bufdesc)
{
  FAR struct audio_rpmsg_s *aud = (FAR struct audio_rpmsg_s *)dev;
  FAR struct ap_buffer_s *apb;

  audinfo("%s alloc buffer\n", aud->devname);

  DEBUGASSERT(bufdesc->u.pbuffer != NULL);

  if (!aud->sharebuf)
    {
      audwarn("%s alloc default apb buffer\n", aud->devname);
      return apb_alloc(bufdesc);
    }

  apb = audio_rpmsg_alloc_apb(aud);
  if (!apb)
    {
      return -ENOMEM;
    }

  *bufdesc->u.pbuffer = apb;
  return sizeof(struct audio_buf_desc_s);
}

static int audio_rpmsg_freebuffer(FAR struct audio_lowerhalf_s *dev,
                                  FAR struct audio_buf_desc_s *bufdesc)
{
  FAR struct audio_rpmsg_s *aud = (FAR struct audio_rpmsg_s *)dev;
  FAR struct ap_buffer_s *apb = (struct ap_buffer_s *)bufdesc->u.pbuffer;

  audinfo("%s free buffer\n", aud->devname);

  if (aud->sharebuf)
    {
      audio_rpmsg_free_apb(aud, apb);
    }
  else
    {
      apb_free(apb);
    }

  return sizeof(struct audio_buf_desc_s);
}

static int audio_rpmsg_enqueuebuffer(FAR struct audio_lowerhalf_s *dev,
                                     FAR struct ap_buffer_s *apb)
{
  FAR struct audio_rpmsg_s *aud = (FAR struct audio_rpmsg_s *)dev;

  nxrmutex_lock(&aud->mutex);
  dq_addlast(&apb->dq_entry, &aud->pendq);
  apb->curbyte = 0;
  nxrmutex_unlock(&aud->mutex);

  return audio_rpmsg_deliver_buffer(aud);
}

/****************************************************************************
 * Name: audio_rpmsg_ioctl_arglen
 *
 * Description:
 *   Get audio rpmsg device ioctl argument length according to the command
 *
 * Parameters:
 *   cmd - the ioctl command
 *   arg - the ioctl arguments
 *
 * Returned Values:
 *   negativate - ioctl command not support
 *   positive   - the argument length
 *
 ****************************************************************************/

static ssize_t audio_rpmsg_ioctl_arglen(int cmd, unsigned long arg)
{
  switch (cmd)
    {
      case AUDIOIOC_HWRESET:
        return 0;
      case AUDIOIOC_GETBUFFERINFO:
      case AUDIOIOC_SETBUFFERINFO:
        return sizeof(struct ap_buffer_info_s);
      case AUDIOIOC_GETAUDIOINFO:
        return sizeof(struct audio_info_s);
      default:
        return -ENOTTY;
    }
}

/****************************************************************************
 * Name: audio_rpmsg_ioctl
 *
 * Description: Perform a device ioctl
 *
 ****************************************************************************/

static int audio_rpmsg_ioctl(FAR struct audio_lowerhalf_s *dev, int cmd,
                             unsigned long arg)
{
  FAR struct audio_rpmsg_s *aud = (FAR struct audio_rpmsg_s *)dev;
  struct audio_rpmsg_ioctl_s *req;
  uint32_t space;
  ssize_t arglen;
  size_t reqlen;
  int ret;

  audinfo("cmd=%d arg=%lu\n", cmd, arg);

  ret = audio_rpmsg_local_ioctl(aud, cmd, arg);
  if (ret != -ENOTTY)
    {
      return ret;
    }

  arglen = audio_rpmsg_ioctl_arglen(cmd, arg);
  if (arglen < 0)
    {
      return arglen;
    }

  /* Deal with ioctls passed from the upper-half driver */

  reqlen = sizeof(*req) + arglen;

  req = audio_rpmsg_get_tx_buffer(aud, &space);
  if (req == NULL)
    {
      return -ENOMEM;
    }

  DEBUGASSERT(reqlen <= space);

  req->request = cmd;
  req->arg = arg;
  req->arglen = arglen;

  if (arglen > 0)
    {
      memcpy(req->data, (FAR void *)(uintptr_t)arg, arglen);
    }

  ret = audio_rpmsg_send_recv(aud, AUDIO_RPMSG_IOCTL,
                              false, &req->header, reqlen,
                              arglen > 0 ? (FAR void *)arg : NULL);
  if (ret < 0)
    {
      auderr("cmd:%d failed, ret=%d\n", cmd, ret);
      return ret;
    }

  audinfo("Return OK\n");
  return ret;
}

#ifdef CONFIG_AUDIO_MULTI_SESSION
static int audio_rpmsg_reserve(FAR struct audio_lowerhalf_s *dev,
                               FAR void **session)
#else
static int audio_rpmsg_reserve(FAR struct audio_lowerhalf_s *dev)
#endif
{
  return OK;
}

#ifdef CONFIG_AUDIO_MULTI_SESSION
static int audio_rpmsg_release(FAR struct audio_lowerhalf_s *dev,
                              FAR void *session)
#else
static int audio_rpmsg_release(FAR struct audio_lowerhalf_s *dev)
#endif
{
  return OK;
}

/****************************************************************************
 * Name: audio_rpmsg_register
 *
 * Description:
 *  register the rpmsg audio driver.
 *
 * Input Parameters:
 *  remotecpu - The name of the cpu to connect with
 *  devname   - The name of the audio device, eg: A2DP
 *  consumer  - true for consumer, false for producer.
 *
 * Returned Value:
 *  0 for Success; a negated value on failure.
 *
 ****************************************************************************/

int audio_rpmsg_register(FAR const char *remotecpu, FAR const char *devname,
                         bool consumer, bool sharebuf)
{
  FAR struct audio_rpmsg_s *aud;
  int ret;

  /* Allocate the rpmsg audio device structure */

  aud = (FAR struct audio_rpmsg_s *)kmm_zalloc(sizeof(*aud));
  if (!aud)
    {
      auderr("ERROR: Failed to allocate rpmsg audio device\n");
      return -ENOMEM;
    }

  nxsem_init(&aud->wait, 0, 0);
  nxrmutex_init(&aud->mutex);

  aud->dev.ops = &g_audio_rpmsg_ops;
  dq_init(&aud->pendq);
  dq_init(&aud->waitq);

  aud->remotecpu = remotecpu;
  aud->devname = devname;
  aud->consumer = consumer;
  aud->sharebuf = sharebuf;
  ret = audio_register(aud->devname, &aud->dev);
  if (ret < 0)
    {
      auderr("WARNING: Failed to register (%s) audio device(%d)\n",
              aud->devname, ret);
      goto error;
    }

  ret = rpmsg_register_callback(aud,
                                audio_rpmsg_device_created,
                                audio_rpmsg_device_destroy,
                                NULL,
                                NULL);
  if (ret < 0)
    {
      audio_unregister(aud->devname, &aud->dev);
      auderr("ERROR: Failed to register rpmsg device, ret=%d\n", ret);
      goto error;
    }

  return OK;

error:
  nxrmutex_destroy(&aud->mutex);
  kmm_free(aud);
  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: audio_rpmsg_initialize
 *
 * Description:
 *   Initialize and register rpmsg audio device.
 *
 ****************************************************************************/

int audio_rpmsg_initialize(void)
{
  size_t i;
  int ret;

  for (i = 0; i < nitems(g_audio_rpmsg_params); i++)
    {
      ret = audio_rpmsg_register(g_audio_rpmsg_params[i].remotecpu,
                                 g_audio_rpmsg_params[i].devname,
                                 g_audio_rpmsg_params[i].consumer,
                                 g_audio_rpmsg_params[i].sharebuf);
      if (ret < 0)
        {
          auderr("Failed to register rpmsg audio device %s\n",
                 g_audio_rpmsg_params[i].devname);
          return ret;
        }
    }

  return OK;
}
