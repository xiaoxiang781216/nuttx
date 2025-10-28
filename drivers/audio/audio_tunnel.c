/****************************************************************************
 * drivers/audio/audio_tunnel.c
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
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#include <nuttx/kmalloc.h>
#include <nuttx/mutex.h>
#include <sys/param.h>

#include <nuttx/audio/audio.h>
#include <nuttx/audio/audio_tunnel.h>

/****************************************************************************
 * Private Types
 ****************************************************************************/

#define AUDIO_TUNNEL_PRODUCER   0
#define AUDIO_TUNNEL_CONSUMER   1

struct audio_tunnel_s;

struct audio_peer_s
{
  struct audio_lowerhalf_s dev;        /* Audio lower half (this device) */
  FAR struct audio_tunnel_s *parent;
  char devname[16];                    /* Device name */
  struct dq_queue_s pendq;
};

struct audio_tunnel_s
{
  mutex_t mutex;

  struct audio_info_s info;        /* Formats */
  struct ap_buffer_info_s binfo;

  struct audio_peer_s peers[2];    /* 0 for producer, 1 for consumer */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int audio_tunnel_getcaps(FAR struct audio_lowerhalf_s *dev, int type,
                                FAR struct audio_caps_s *caps);
#ifdef CONFIG_AUDIO_MULTI_SESSION
static int audio_tunnel_reserve(FAR struct audio_lowerhalf_s *dev,
                                FAR void **session);
static int audio_tunnel_configure(FAR struct audio_lowerhalf_s *dev,
                                  FAR void *session,
                                  FAR const struct audio_caps_s *caps);
static int audio_tunnel_start(FAR struct audio_lowerhalf_s *dev,
                              FAR void *session);
static int audio_tunnel_release(FAR struct audio_lowerhalf_s *dev,
                                FAR void *session);

#ifndef CONFIG_AUDIO_EXCLUDE_STOP
static int audio_tunnel_stop(FAR struct audio_lowerhalf_s *dev,
                             FAR void *session);
#endif
#ifndef CONFIG_AUDIO_EXCLUDE_PAUSE_RESUME
static int audio_tunnel_pause(FAR struct audio_lowerhalf_s *dev,
                              FAR void *session);
static int audio_tunnel_resume(FAR struct audio_lowerhalf_s *dev,
                               FAR void *session);
#endif
#else
static int audio_tunnel_reserve(FAR struct audio_lowerhalf_s *dev);
static int audio_tunnel_configure(FAR struct audio_lowerhalf_s *dev,
                                  FAR const struct audio_caps_s *caps);
static int audio_tunnel_start(FAR struct audio_lowerhalf_s *dev);
static int audio_tunnel_release(FAR struct audio_lowerhalf_s *dev);

#ifndef CONFIG_AUDIO_EXCLUDE_STOP
static int audio_tunnel_stop(FAR struct audio_lowerhalf_s *dev);
#endif

#ifndef CONFIG_AUDIO_EXCLUDE_PAUSE_RESUME
static int audio_tunnel_pause(FAR struct audio_lowerhalf_s *dev);
static int audio_tunnel_resume(FAR struct audio_lowerhalf_s *dev);
#endif
#endif
static int audio_tunnel_shutdown(FAR struct audio_lowerhalf_s *dev);
static int audio_tunnel_enqueuebuffer(FAR struct audio_lowerhalf_s *dev,
                                        FAR struct ap_buffer_s *apb);
static int audio_tunnel_ioctl(FAR struct audio_lowerhalf_s *dev, int cmd,
                              unsigned long arg);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct audio_ops_s g_audio_tunnel_ops =
{
  audio_tunnel_getcaps,         /* getcaps        */
  audio_tunnel_configure,       /* configure      */
  audio_tunnel_shutdown,        /* shutdown       */
  audio_tunnel_start,           /* start          */
#ifndef CONFIG_AUDIO_EXCLUDE_STOP
  audio_tunnel_stop,            /* stop           */
#endif
#ifndef CONFIG_AUDIO_EXCLUDE_PAUSE_RESUME
  audio_tunnel_pause,           /* pause          */
  audio_tunnel_resume,          /* resume         */
#endif
  NULL,                         /* allocbuffer    */
  NULL,                         /* freebuffer     */
  audio_tunnel_enqueuebuffer,   /* enqueue_buffer */
  NULL,                         /* cancel_buffer  */
  audio_tunnel_ioctl,           /* ioctl          */
  NULL,                         /* read           */
  NULL,                         /* write          */
  audio_tunnel_reserve,         /* reserve        */
  audio_tunnel_release          /* release        */
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void audio_tunnel_deliver_buffer(FAR struct audio_tunnel_s *tunnel)
{
  FAR struct audio_peer_s *producer =
    &tunnel->peers[AUDIO_TUNNEL_PRODUCER];
  FAR struct audio_peer_s *consumer =
    &tunnel->peers[AUDIO_TUNNEL_CONSUMER];
  FAR struct ap_buffer_s *src;
  FAR struct ap_buffer_s *dst;

  nxmutex_lock(&tunnel->mutex);
  if (dq_empty(&producer->pendq) || dq_empty(&consumer->pendq))
    {
      nxmutex_unlock(&tunnel->mutex);
      audinfo("audio tunnel not ready.\n");
      return;
    }

  src = (FAR struct ap_buffer_s *)dq_remfirst(&producer->pendq);
  dst = (FAR struct ap_buffer_s *)dq_remfirst(&consumer->pendq);

  nxmutex_unlock(&tunnel->mutex);

  memcpy(dst->samp, src->samp, src->nbytes);
  dst->nbytes = src->nbytes;

#ifdef CONFIG_AUDIO_MULTI_SESSION
  consumer->dev.upper(consumer->dev.priv, AUDIO_CALLBACK_DEQUEUE,
                      dst, OK, NULL);
  producer->dev.upper(producer->dev.priv, AUDIO_CALLBACK_DEQUEUE,
                      src, OK, NULL)
#else
  consumer->dev.upper(consumer->dev.priv, AUDIO_CALLBACK_DEQUEUE, dst, OK);
  producer->dev.upper(producer->dev.priv, AUDIO_CALLBACK_DEQUEUE, src, OK);
#endif

  if (src->flags & AUDIO_APB_FINAL)
    {
      producer->dev.upper(producer->dev.priv, AUDIO_CALLBACK_COMPLETE,
                          NULL, OK);
      consumer->dev.upper(consumer->dev.priv, AUDIO_CALLBACK_COMPLETE,
                          NULL, OK);
    }
}

static void audio_tunnel_deliver_message(FAR struct audio_peer_s *peer,
                                         int msg_id)
{
  FAR struct audio_tunnel_s *tunnel = peer->parent;
  struct audio_msg_s msg;

  peer = peer == &tunnel->peers[AUDIO_TUNNEL_PRODUCER] ?
         &tunnel->peers[AUDIO_TUNNEL_CONSUMER] :
         &tunnel->peers[AUDIO_TUNNEL_PRODUCER];

  /* Process the message */

  memset(&msg, 0, sizeof(msg));
  msg.msg_id = msg_id;

  peer->dev.upper(peer->dev.priv, AUDIO_CALLBACK_MESSAGE,
                  (FAR struct ap_buffer_s *)&msg, OK);
}

/****************************************************************************
 * Name: audio_tunnel_getcaps
 *
 * Description:
 *   Get the audio capabilities from stub peer.
 *
 ****************************************************************************/

static int audio_tunnel_getcaps(FAR struct audio_lowerhalf_s *dev, int type,
                                FAR struct audio_caps_s *caps)
{
  FAR struct audio_peer_s *peer = (struct audio_peer_s *)dev;
  FAR struct audio_tunnel_s *tunnel = peer->parent;

  caps->ac_format.hw  = 0;
  caps->ac_controls.w = 0;

  nxmutex_lock(&tunnel->mutex);
  switch (caps->ac_type)
    {
      case AUDIO_TYPE_QUERY:

        caps->ac_channels = tunnel->info.channels;

        switch (caps->ac_subtype)
          {
            case AUDIO_TYPE_QUERY:
              caps->ac_controls.b[0] =
                peer == &tunnel->peers[AUDIO_TUNNEL_PRODUCER] ?
                AUDIO_TYPE_OUTPUT : AUDIO_TYPE_INPUT;

              if (tunnel->info.subformat == 0)
                {
                  nxmutex_unlock(&tunnel->mutex);
                  return -ENOSYS;
                }

              caps->ac_format.hw = 1 << (tunnel->info.format - 1);
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
              caps->ac_controls.b[0] = tunnel->info.subformat;
              caps->ac_controls.b[1] = AUDIO_SUBFMT_END;

              memcpy(&caps->ac_codec, &tunnel->info.codec,
                     sizeof(tunnel->info.codec));
              break;

            default:
              caps->ac_controls.b[0] = AUDIO_SUBFMT_END;
              break;
          }

        break;

      case AUDIO_TYPE_OUTPUT:
      case AUDIO_TYPE_INPUT:
        caps->ac_channels = (tunnel->info.channels << 4) |
                            (tunnel->info.channels & 0x0f);

        switch (caps->ac_subtype)
          {
            case AUDIO_TYPE_QUERY:

              /* Report the Sample rates we support */

              caps->ac_controls.hw[0] =
                tunnel->info.samplerate == 8000 ? AUDIO_SAMP_RATE_8K :
                tunnel->info.samplerate == 12000 ? AUDIO_SAMP_RATE_12K :
                tunnel->info.samplerate == 16000 ? AUDIO_SAMP_RATE_16K :
                tunnel->info.samplerate == 22050 ? AUDIO_SAMP_RATE_22K :
                tunnel->info.samplerate == 24000 ? AUDIO_SAMP_RATE_24K :
                tunnel->info.samplerate == 32000 ? AUDIO_SAMP_RATE_32K :
                tunnel->info.samplerate == 44100 ? AUDIO_SAMP_RATE_44K :
                tunnel->info.samplerate == 48000 ? AUDIO_SAMP_RATE_48K :
                tunnel->info.samplerate == 88200 ? AUDIO_SAMP_RATE_88K :
                tunnel->info.samplerate == 96000 ? AUDIO_SAMP_RATE_96K :
                tunnel->info.samplerate == 192000 ? AUDIO_SAMP_RATE_192K :
                AUDIO_SAMP_RATE_DEF_ALL;
              break;

            default:
              break;
          }

        break;

      default:
        caps->ac_subtype = 0;
        caps->ac_channels = 0;
        break;
    }

  nxmutex_unlock(&tunnel->mutex);

  /* Return the length of the audio_caps_s struct for validation of
   * proper Audio device type.
   */

  audinfo("Return %d\n", caps->ac_len);
  return caps->ac_len;
}

/****************************************************************************
 * Name: audio_tunnel_configure
 *
 * Description:
 *   Configure the driver.
 *
 ****************************************************************************/

#ifdef CONFIG_AUDIO_MULTI_SESSION
static int audio_tunnel_configure(FAR struct audio_lowerhalf_s *dev,
                                  FAR void *session,
                                  FAR const struct audio_caps_s *caps)
#else
static int audio_tunnel_configure(FAR struct audio_lowerhalf_s *dev,
                                  FAR const struct audio_caps_s *caps)
#endif
{
  FAR struct audio_peer_s *peer = (struct audio_peer_s *)dev;
  FAR struct audio_tunnel_s *tunnel = peer->parent;

  audinfo("ac_type: %" PRIu8 "\n", caps->ac_type);

  /* Process the configure operation */

  nxmutex_lock(&tunnel->mutex);
  switch (caps->ac_type)
    {
      case AUDIO_TYPE_OUTPUT:
      case AUDIO_TYPE_INPUT:
        tunnel->info.format = caps->ac_subtype;
        tunnel->info.samplerate = caps->ac_controls.hw[0] |
                                  (caps->ac_controls.b[3] << 16);
        tunnel->info.channels = caps->ac_channels;
        tunnel->info.subformat = caps->ac_format.b[0];
        memcpy(&tunnel->info.codec, &caps->ac_codec,
               sizeof(caps->ac_codec));

        audinfo("Audio type: %s Codec type %" PRIu8 " \
                %" PRIu32 " %" PRIu8 " %" PRIu8 "\n",
                caps->ac_type == AUDIO_TYPE_OUTPUT ? "OUTPUT" : "INPUT",
                caps->ac_subtype,
                tunnel->info.samplerate,
                tunnel->info.channels,
                tunnel->info.subformat);
        break;

      default:
        audinfo("default case: %" PRIu8 "\n", caps->ac_type);
        break;
    }

  nxmutex_unlock(&tunnel->mutex);

  return OK;
}

/****************************************************************************
 * Name: audio_tunnel_shutdown
 *
 * Description:
 *   Shutdown the driver and put it in the lowest power state possible.
 *
 ****************************************************************************/

static int audio_tunnel_shutdown(FAR struct audio_lowerhalf_s *dev)
{
  return OK;
}

/****************************************************************************
 * Name: audio_tunnel_start
 *
 * Description:
 *   Start the driver and put it in the lowest power state possible.
 *
 ****************************************************************************/

#ifdef CONFIG_AUDIO_MULTI_SESSION
static int audio_tunnel_start(FAR struct audio_lowerhalf_s *dev,
                              FAR void *session)
#else
static int audio_tunnel_start(FAR struct audio_lowerhalf_s *dev)
#endif
{
  FAR struct audio_peer_s *peer = (FAR struct audio_peer_s *)dev;
  FAR struct audio_tunnel_s *tunnel = peer->parent;

  audio_tunnel_deliver_message(peer, AUDIO_MSG_START);
  audio_tunnel_deliver_buffer(tunnel);

  return OK;
}

/****************************************************************************
 * Name: audio_tunnel_stop
 *
 * Description:
 *   Stop the configured operation (audio streaming, volume
 *              disabled, etc.).
 *
 ****************************************************************************/

#ifndef CONFIG_AUDIO_EXCLUDE_STOP
#ifdef CONFIG_AUDIO_MULTI_SESSION
static int audio_tunnel_stop(FAR struct audio_lowerhalf_s *dev,
                             FAR void *session)
#else
static int audio_tunnel_stop(FAR struct audio_lowerhalf_s *dev)
#endif
{
  FAR struct audio_peer_s *peer = (FAR struct audio_peer_s *)dev;
  FAR struct audio_tunnel_s *tunnel = peer->parent;

  audio_tunnel_deliver_message(peer, AUDIO_MSG_STOP);

  nxmutex_lock(&tunnel->mutex);
  while (!dq_empty(&peer->pendq))
    {
      FAR struct ap_buffer_s *apb =
        (FAR struct ap_buffer_s *)dq_remfirst(&peer->pendq);
      peer->dev.upper(peer->dev.priv, AUDIO_CALLBACK_DEQUEUE, apb, OK);
    }

  nxmutex_unlock(&tunnel->mutex);
  peer->dev.upper(peer->dev.priv, AUDIO_CALLBACK_COMPLETE, NULL, OK);

  return OK;
}
#endif

/****************************************************************************
 * Name: audio_tunnel_pause
 *
 * Description: Pauses the playback.
 *
 ****************************************************************************/

#ifndef CONFIG_AUDIO_EXCLUDE_PAUSE_RESUME
#ifdef CONFIG_AUDIO_MULTI_SESSION
static int audio_tunnel_pause(FAR struct audio_lowerhalf_s *dev,
                              FAR void *session)
#else
static int audio_tunnel_pause(FAR struct audio_lowerhalf_s *dev)
#endif
{
  FAR struct audio_peer_s *peer = (FAR struct audio_peer_s *)dev;

  audio_tunnel_deliver_message(peer, AUDIO_MSG_PAUSE);
  return OK;
}
#endif /* CONFIG_AUDIO_EXCLUDE_PAUSE_RESUME */

/****************************************************************************
 * Name: audio_tunnel_resume
 *
 * Description: Resumes the playback.
 *
 ****************************************************************************/

#ifndef CONFIG_AUDIO_EXCLUDE_PAUSE_RESUME
#ifdef CONFIG_AUDIO_MULTI_SESSION
static int audio_tunnel_resume(FAR struct audio_lowerhalf_s *dev,
                               FAR void *session)
#else
static int audio_tunnel_resume(FAR struct audio_lowerhalf_s *dev)
#endif
{
  FAR struct audio_peer_s *peer = (FAR struct audio_peer_s *)dev;
  FAR struct audio_tunnel_s *tunnel = peer->parent;

  audio_tunnel_deliver_message(peer, AUDIO_MSG_RESUME);
  audio_tunnel_deliver_buffer(tunnel);

  return OK;
}
#endif /* CONFIG_AUDIO_EXCLUDE_PAUSE_RESUME */

/****************************************************************************
 * Name: audio_tunnel_enqueuebuffer
 *
 * Description: Enqueue an Audio Pipeline Buffer for
 *              playback/capture processing.
 *
 ****************************************************************************/

static int audio_tunnel_enqueuebuffer(FAR struct audio_lowerhalf_s *dev,
                                      FAR struct ap_buffer_s *apb)
{
  FAR struct audio_peer_s *peer = (FAR struct audio_peer_s *)dev;
  FAR struct audio_tunnel_s *tunnel = peer->parent;

  nxmutex_lock(&tunnel->mutex);
  dq_addlast(&apb->dq_entry, &peer->pendq);
  nxmutex_unlock(&tunnel->mutex);

  audio_tunnel_deliver_buffer(tunnel);
  return OK;
}

/****************************************************************************
 * Name: audio_tunnel_ioctl
 *
 * Description: Perform a device ioctl
 *
 ****************************************************************************/

static int audio_tunnel_ioctl(FAR struct audio_lowerhalf_s *dev, int cmd,
                              unsigned long arg)
{
  FAR struct audio_peer_s *peer = (FAR struct audio_peer_s *)dev;
  FAR struct audio_tunnel_s *tunnel = peer->parent;
  FAR struct ap_buffer_info_s *binfo = NULL;
  int ret = OK;

  audinfo("cmd=%d arg=%ld\n", cmd, arg);

  /* Deal with ioctls passed from the upper-half driver */

  nxmutex_lock(&tunnel->mutex);
  switch (cmd)
    {
      case AUDIOIOC_HWRESET:
        audio_tunnel_deliver_message(peer, AUDIO_MSG_IOERR);
        break;

      /* Report our preferred buffer size and quantity */

      case AUDIOIOC_GETBUFFERINFO:
        binfo = (FAR struct ap_buffer_info_s *)arg;
        binfo->nbuffers    = tunnel->binfo.nbuffers;
        binfo->buffer_size = tunnel->binfo.buffer_size;
        break;

      case AUDIOIOC_SETBUFFERINFO:
        binfo = (FAR struct ap_buffer_info_s *)arg;
        tunnel->binfo.nbuffers = binfo->nbuffers;
        tunnel->binfo.buffer_size = binfo->buffer_size;
        break;

      case AUDIOIOC_GETAUDIOINFO:
        memcpy((FAR void *)(uintptr_t)arg, &tunnel->info,
          sizeof(tunnel->info));
        break;

      default:
        ret = -ENOTTY;
        break;
    }

  nxmutex_unlock(&tunnel->mutex);
  audinfo("Return OK\n");
  return ret;
}

#ifdef CONFIG_AUDIO_MULTI_SESSION
static int audio_tunnel_reserve(FAR struct audio_lowerhalf_s *dev,
                                FAR void **session)
#else
static int audio_tunnel_reserve(FAR struct audio_lowerhalf_s *dev)
#endif
{
  return OK;
}

#ifdef CONFIG_AUDIO_MULTI_SESSION
static int audio_tunnel_release(FAR struct audio_lowerhalf_s *dev,
                                FAR void *session)
#else
static int audio_tunnel_release(FAR struct audio_lowerhalf_s *dev)
#endif
{
  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int audio_tunnel_initialize(FAR const char *prefix)
{
  FAR struct audio_tunnel_s *tunnel;
  size_t i;
  int ret;

  /* Allocate the tunnel audio device structure */

  tunnel = (FAR struct audio_tunnel_s *)
              kmm_zalloc(sizeof(struct audio_tunnel_s));
  if (!tunnel)
    {
      auderr("ERROR: Failed to allocate tunnel audio device\n");
      return -ENOMEM;
    }

  nxmutex_init(&tunnel->mutex);

  for (i = 0; i < nitems(tunnel->peers); i++)
    {
      snprintf(tunnel->peers[i].devname, sizeof(tunnel->peers[i].devname),
              "%s%c", prefix, i == AUDIO_TUNNEL_PRODUCER ? 'p' : 'c');
      tunnel->peers[i].dev.ops = &g_audio_tunnel_ops;
      tunnel->peers[i].parent = tunnel;

      dq_init(&tunnel->peers[i].pendq);

      ret = audio_register(tunnel->peers[i].devname, &tunnel->peers[i].dev);
      if (ret < 0)
        {
          goto error;
        }
    }

  return 0;

error:
  while (i-- > 0)
    {
      audio_unregister(tunnel->peers[i].devname, &tunnel->peers[i].dev);
    }

  nxmutex_destroy(&tunnel->mutex);
  kmm_free(tunnel);
  return ret;
}
