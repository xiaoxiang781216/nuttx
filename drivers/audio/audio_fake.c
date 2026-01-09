/****************************************************************************
 * drivers/audio/audio_fake.c
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

#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/stat.h>

#include <assert.h>
#include <debug.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <nuttx/audio/audio.h>
#include <nuttx/audio/audio_fake.h>
#include <nuttx/fs/fs.h>
#include <nuttx/fs/ioctl.h>
#include <nuttx/kmalloc.h>
#include <nuttx/mqueue.h>
#include <nuttx/signal.h>
#include <nuttx/kthread.h>

/****************************************************************************
 * Private Types
 ****************************************************************************/

#ifndef M_PI_F
#  define M_PI_F ((float)M_PI)
#endif

#define AUDIO_FAKE_SIN_DATA_FREQ 1000

struct audio_fake_s
{
  struct audio_lowerhalf_s dev; /* Audio lower half (this device) */
#ifndef CONFIG_AUDIO_EXCLUDE_STOP
  volatile bool terminate;      /* True: request to terminate */
#endif
  uint8_t       format;         /* Request audio format */
  uint32_t      channels;       /* Request audio channels */
  uint32_t      sample_rate;    /* Request audio sample rate */
  uint32_t      bps;            /* Data bytes to sec bps (bytes per sec) */
  clock_t       start_tick;     /* Start time in microseconds */
  uint64_t      total_bytes;    /* Total frame datas */
  char          mqname[32];     /* Our message queue name */
  struct file   mq;             /* Message queue for receiving messages */
  struct file   file;           /* Audio file for playback or capture */
  void          *sindata;       /* Sin data buffer, surpport 8bit/16bit/32bit  */
  uint32_t      sinsize;        /* Sin data size */
  FAR const audio_fake_params_t *params;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int audio_fake_getcaps(FAR struct audio_lowerhalf_s *dev, int type,
                              FAR struct audio_caps_s *caps);
#ifdef CONFIG_AUDIO_MULTI_SESSION
static int audio_fake_configure(FAR struct audio_lowerhalf_s *dev,
                                FAR void *session,
                                FAR const struct audio_caps_s *caps);
#else
static int audio_fake_configure(FAR struct audio_lowerhalf_s *dev,
                                FAR const struct audio_caps_s *caps);
#endif
static int audio_fake_shutdown(FAR struct audio_lowerhalf_s *dev);
static int audio_fake_workerthread(int argc, FAR char **argv);
#ifdef CONFIG_AUDIO_MULTI_SESSION
static int audio_fake_start(FAR struct audio_lowerhalf_s *dev,
                            FAR void *session);
#else
static int audio_fake_start(FAR struct audio_lowerhalf_s *dev);
#endif
#ifndef CONFIG_AUDIO_EXCLUDE_STOP
#  ifdef CONFIG_AUDIO_MULTI_SESSION
static int audio_fake_stop(FAR struct audio_lowerhalf_s *dev,
                           FAR void *session);
#  else
static int audio_fake_stop(FAR struct audio_lowerhalf_s *dev);
#  endif
#endif
#ifndef CONFIG_AUDIO_EXCLUDE_PAUSE_RESUME
#  ifdef CONFIG_AUDIO_MULTI_SESSION
static int audio_fake_pause(FAR struct audio_lowerhalf_s *dev,
                            FAR void *session);
static int audio_fake_resume(FAR struct audio_lowerhalf_s *dev,
                             FAR void *session);
#  else
static int audio_fake_pause(FAR struct audio_lowerhalf_s *dev);
static int audio_fake_resume(FAR struct audio_lowerhalf_s *dev);
#  endif
#endif
static int audio_fake_enqueuebuffer(FAR struct audio_lowerhalf_s *dev,
                                    FAR struct ap_buffer_s *apb);
static int audio_fake_ioctl(FAR struct audio_lowerhalf_s *dev, int cmd,
                            unsigned long arg);
#ifdef CONFIG_AUDIO_MULTI_SESSION
static int audio_fake_reserve(FAR struct audio_lowerhalf_s *dev,
                              FAR void **session);
#else
static int audio_fake_reserve(FAR struct audio_lowerhalf_s *dev);
#endif
#ifdef CONFIG_AUDIO_MULTI_SESSION
static int audio_fake_release(FAR struct audio_lowerhalf_s *dev,
                              FAR void *session);
#else
static int audio_fake_release(FAR struct audio_lowerhalf_s *dev);
#endif
static int audio_fake_file_init(FAR struct audio_lowerhalf_s *dev);
static int audio_fake_file_deinit(FAR struct audio_lowerhalf_s *dev);
static int audio_fake_file_write(FAR struct audio_lowerhalf_s *dev,
                                 FAR struct ap_buffer_s *apb);
static int audio_fake_file_read(FAR struct audio_lowerhalf_s *dev,
                                FAR struct ap_buffer_s *apb);
static int audio_fake_process_buffer(FAR struct audio_lowerhalf_s *dev,
                                     FAR struct ap_buffer_s *apb);
static uint8_t audio_fake_subfmt_convert(uint8_t format);
static uint32_t audio_fake_samp_rate_convert(uint32_t samplerate);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const audio_fake_params_t g_devparams[] =
{
#ifdef CONFIG_AUDIO_FAKE_DEVICE_PARAMS
    CONFIG_AUDIO_FAKE_DEVICE_PARAMS
#endif
};

static const struct audio_ops_s g_audioops =
{
    audio_fake_getcaps,       /* getcaps        */
    audio_fake_configure,     /* configure      */
    audio_fake_shutdown,      /* shutdown       */
    audio_fake_start,         /* start          */
#ifndef CONFIG_AUDIO_EXCLUDE_STOP
    audio_fake_stop,          /* stop           */
#endif
#ifndef CONFIG_AUDIO_EXCLUDE_PAUSE_RESUME
    audio_fake_pause,         /* pause          */
    audio_fake_resume,        /* resume         */
#endif
    NULL,                     /* allocbuffer    */
    NULL,                     /* freebuffer     */
    audio_fake_enqueuebuffer, /* enqueue_buffer */
    NULL,                     /* cancel_buffer  */
    audio_fake_ioctl,         /* ioctl          */
    NULL,                     /* read           */
    NULL,                     /* write          */
    audio_fake_reserve,       /* reserve        */
    audio_fake_release        /* release        */
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: audio_fake_samp_rate_convert
 *
 * Description: Convert the samplerate to Nuttx audio samplerate.
 *
 ****************************************************************************/

static uint32_t audio_fake_samp_rate_convert(uint32_t samplerate)
{
  if (samplerate == 8000)
    {
      return AUDIO_SAMP_RATE_8K;
    }
  else if (samplerate == 11025)
    {
      return AUDIO_SAMP_RATE_11K;
    }
  else if (samplerate == 16000)
    {
      return AUDIO_SAMP_RATE_16K;
    }
  else if (samplerate == 22050)
    {
      return AUDIO_SAMP_RATE_22K;
    }
  else if (samplerate == 32000)
    {
      return AUDIO_SAMP_RATE_32K;
    }
  else if (samplerate == 44100)
    {
      return AUDIO_SAMP_RATE_44K;
    }
  else if (samplerate == 48000)
    {
      return AUDIO_SAMP_RATE_48K;
    }
  else if (samplerate != 0)
    {
      auderr("ERROR: Unsupported sample rate %"PRIu32"\n", samplerate);
    }

  return 0;
}

/****************************************************************************
 * Name: audio_fake_subfmt_convert
 *
 * Description: Convert the format to Nuttx audio format.
 *
 ****************************************************************************/

static uint8_t audio_fake_subfmt_convert(uint8_t format)
{
  if (format == 8)
    {
      return AUDIO_SUBFMT_PCM_S8;
    }
  else if (format == 16)
    {
      return AUDIO_SUBFMT_PCM_S16_LE;
    }
  else if (format == 32)
    {
      return AUDIO_SUBFMT_PCM_S32_LE;
    }
  else if (format != AUDIO_SUBFMT_END)
    {
      auderr("ERROR: Unsupported format %d\n", format);
    }

  return 0;
}

/****************************************************************************
 * Name: audio_fake_sin_data_init
 *
 * Description: Initialize a buffer for sine wave data for capture
 *
 ****************************************************************************/

static int audio_fake_sin_data_init(FAR struct audio_lowerhalf_s *dev)
{
  FAR struct audio_fake_s *priv = (FAR struct audio_fake_s *)dev;
  int freq = AUDIO_FAKE_SIN_DATA_FREQ;
  uint8_t format;
  uint32_t nsamples;
  uint32_t i;

  nsamples = (priv->sample_rate *
               priv->channels *
               priv->params->period_time) / 1000;
  format = audio_fake_subfmt_convert(priv->format);
  if (format == AUDIO_SUBFMT_PCM_S8)
    {
      priv->sinsize = nsamples * sizeof(int8_t);
      int8_t *data = (int8_t *)kmm_zalloc(priv->sinsize);
      if (!data)
        {
          auderr("ERROR: Failed to allocate memory for sine data\n");
          return -ENOMEM;
        }

      for (i = 0; i < nsamples; i++)
        {
          float t = (float)i / priv->sample_rate;
          float sample = sinf(2 * M_PI_F * freq * t);
          data[i] = (int8_t)(sample * INT8_MAX);
        }

      priv->sindata = data;
    }
  else if (format == AUDIO_SUBFMT_PCM_S16_LE)
    {
      priv->sinsize = nsamples * sizeof(int16_t);
      int16_t *data = (int16_t *)kmm_zalloc(priv->sinsize);
      if (!data)
        {
          auderr("ERROR: Failed to allocate memory for sine data\n");
          return -ENOMEM;
        }

      for (i = 0; i < nsamples; i++)
        {
          float t = (float)i / priv->sample_rate;
          float sample = sinf(2 * M_PI_F * freq * t);
          data[i] = (int16_t)(sample * INT16_MAX);
        }

      priv->sindata = data;
    }
  else if (format == AUDIO_SUBFMT_PCM_S32_LE)
    {
      priv->sinsize = nsamples * sizeof(int32_t);
      int32_t *data = (int32_t *)kmm_zalloc(priv->sinsize);
      if (!data)
        {
          auderr("ERROR: Failed to allocate memory for sine data\n");
          return -ENOMEM;
        }

      for (i = 0; i < nsamples; i++)
        {
          float t = (float)i / priv->sample_rate;
          float sample = sinf(2 * M_PI_F * freq * t);
          data[i] = (int32_t)(sample * INT32_MAX);
        }

        priv->sindata = data;
    }
  else
    {
      auderr("ERROR: sin_data unsupported format %d\n", format);
      return -EINVAL;
    }

  return 0;
}

/****************************************************************************
 * Name: audio_fake_sin_data_deinit
 *
 * Description: Deinitialize the sine wave data.
 *
 ****************************************************************************/

static void audio_fake_sin_data_deinit(FAR struct audio_lowerhalf_s *dev)
{
  FAR struct audio_fake_s *priv = (FAR struct audio_fake_s *)dev;

  if (priv->sindata)
    {
      kmm_free(priv->sindata);
      priv->sindata = NULL;
    }
}

/****************************************************************************
 * Name: audio_fake_sin_data_read
 *
 * Description: Read capture data from inited sindata.
 *
 ****************************************************************************/

static int audio_fake_sin_data_read(FAR struct audio_lowerhalf_s *dev,
                                    FAR struct ap_buffer_s *apb)
{
  FAR struct audio_fake_s *priv = (FAR struct audio_fake_s *)dev;

  DEBUGASSERT(apb->nmaxbytes == priv->sinsize);

  memcpy(apb->samp, priv->sindata, priv->sinsize);
  apb->nbytes  = priv->sinsize;
  apb->curbyte = 0;
  apb->flags   = 0;

  return 0;
}

/****************************************************************************
 * Name: audio_fake_file_init
 *
 * Description: Initialize the audio file for playback or capture virtual
 * audio driver.
 *
 ****************************************************************************/

static int audio_fake_file_init(FAR struct audio_lowerhalf_s *dev)
{
  FAR struct audio_fake_s *priv = (FAR struct audio_fake_s *)dev;
  char filename[64];
  int ret;

  ret = mkdir(CONFIG_AUDIO_FAKE_DATA_PATH, 0755);
  if (ret < 0 && get_errno() != EEXIST)
    {
      auderr("Failed to create directory\n");
      return -get_errno();
    }

  snprintf(filename, sizeof(filename), "%s/%s_%"PRIu32"_%"PRIu32"_%d.pcm",
           CONFIG_AUDIO_FAKE_DATA_PATH, priv->params->devname,
           priv->sample_rate, priv->channels, priv->format);

  if (priv->params->playback)
    {
      ret = file_open(&priv->file, filename, O_WRONLY | O_TRUNC | O_CREAT |
                      O_CLOEXEC, 0666);
    }
  else
    {
      ret = file_open(&priv->file, filename, O_RDONLY | O_CLOEXEC);
    }

  audwarn("open %s file %s\n", filename, (ret < 0) ? "fail" : "success");

  return ret;
}

/****************************************************************************
 * Name: audio_fake_file_deinit
 *
 * Description: Deinitialize the audio file.
 *
 ****************************************************************************/

static int audio_fake_file_deinit(FAR struct audio_lowerhalf_s *dev)
{
  FAR struct audio_fake_s *priv = (FAR struct audio_fake_s *)dev;

  audinfo("audio_fake_file_deinit, close file\n");
  file_close(&priv->file);

  return 0;
}

/****************************************************************************
 * Name: audio_fake_file_write
 *
 * Description: Write the audio data to file.
 *
 ****************************************************************************/

static int audio_fake_file_write(FAR struct audio_lowerhalf_s *dev,
                                 FAR struct ap_buffer_s *apb)
{
  FAR struct audio_fake_s *priv = (FAR struct audio_fake_s *)dev;
  int ret;

  ret = file_write(&priv->file, apb->samp, apb->nbytes);
  if (ret < 0)
    {
      auderr("Error write data , ret %d\n", ret);
      return ret;
    }

  return ret;
}

/****************************************************************************
 * Name: audio_fake_file_read
 *
 * Description: Read the audio data from file.
 *
 ****************************************************************************/

static int audio_fake_file_read(FAR struct audio_lowerhalf_s *dev,
                                FAR struct ap_buffer_s *apb)
{
  FAR struct audio_fake_s *priv = (FAR struct audio_fake_s *)dev;
  int ret;

  ret = file_read(&priv->file, apb->samp, apb->nmaxbytes);
  if (ret == 0)
    {
      audwarn("read file end\n");
      file_seek(&priv->file, 0, SEEK_SET);
      ret = file_read(&priv->file, apb->samp, apb->nmaxbytes);
    }

  if (ret < 0)
    {
      auderr("Error read data , ret %d\n", ret);
      return ret;
    }

  apb->nbytes  = ret;
  apb->curbyte = 0;
  apb->flags   = 0;

  return ret;
}

/****************************************************************************
 * Name: audio_fake_frame_delay
 *
 * Description: Delay the audio frame.
 *
 ****************************************************************************/

static void audio_fake_frame_delay(FAR struct audio_fake_s *priv,
                                   FAR struct ap_buffer_s *apb)
{
  uint64_t total_time;
  uint64_t diff_time;

  priv->total_bytes += apb->nbytes;
  total_time = (priv->total_bytes * 1000 * 1000) / priv->bps;
  diff_time = TICK2USEC(clock_systime_ticks() - priv->start_tick);
  if (diff_time >= total_time)
    {
      auderr("ERROR: diff_time %" PRIu64 " > total_time %" PRIu64 ".\n",
              diff_time, total_time);
    }
  else
    {
      nxsig_usleep(total_time - diff_time);
    }
}

/****************************************************************************
 * Name: audio_fake_process_buffer
 *
 * Description: Process the audio buffer.
 *
 ****************************************************************************/

static int audio_fake_process_buffer(FAR struct audio_lowerhalf_s *dev,
                                     FAR struct ap_buffer_s *apb)
{
  FAR struct audio_fake_s *priv = (FAR struct audio_fake_s *)dev;
  int ret;

  audinfo("process apb=%p, nbytes=%d\n", apb, apb->nbytes);

  /* Check if this was the last buffer in the stream */
#ifndef CONFIG_AUDIO_EXCLUDE_STOP
  priv->terminate = ((apb->flags & AUDIO_APB_FINAL) != 0);
#endif

  if (priv->params->playback)
    {
      ret = audio_fake_file_write(dev, apb);
      if (ret < 0)
        {
          auderr("Error write data, ret %d\n", ret);
        }
    }
  else
    {
      if (priv->sindata)
        {
          ret = audio_fake_sin_data_read(dev, apb);
        }
      else
        {
          ret = audio_fake_file_read(dev, apb);
        }

      if (ret < 0)
        {
          auderr("Error read data , ret %d\n", ret);
        }
    }

  audio_fake_frame_delay(priv, apb);

#ifdef CONFIG_AUDIO_MULTI_SESSION
  priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_DEQUEUE, apb, OK, NULL);
#else
  priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_DEQUEUE, apb, OK);
#endif

  return ret;
}

/****************************************************************************
 * Name: audio_fake_getcaps
 *
 * Description: Get the audio device capabilities
 *
 ****************************************************************************/

static int audio_fake_getcaps(FAR struct audio_lowerhalf_s *dev, int type,
                              FAR struct audio_caps_s *caps)
{
  FAR struct audio_fake_s *priv = (struct audio_fake_s *)dev;

  audinfo("type=%d\n", type);

  /* Validate the structure */

  DEBUGASSERT(caps->ac_len >= sizeof(struct audio_caps_s));

  /* Fill in the caller's structure based on requested info */

  caps->ac_format.hw  = 0;
  caps->ac_controls.w = 0;

  switch (caps->ac_type)
    {
      /* Caller is querying for the types of units we support */

      case AUDIO_TYPE_QUERY:

        /* Provide our overall capabilities.  The interfacing software
         * must then call us back for specific info for each capability.
         */

        caps->ac_channels = (priv->params->channels[0] << 4) |
                            (priv->params->channels[1] & 0x0f);

        switch (caps->ac_subtype)
          {
            case AUDIO_TYPE_QUERY:

              /* The types of audio units we implement */

              caps->ac_controls.b[0] = priv->params->playback ?
                                       AUDIO_TYPE_OUTPUT : AUDIO_TYPE_INPUT;
              caps->ac_format.hw = 1 << (AUDIO_FMT_PCM - 1);

              break;

            case AUDIO_FMT_PCM:

              caps->ac_controls.b[0] =
                  audio_fake_subfmt_convert(priv->params->format[0]);
              caps->ac_controls.b[1] =
                  audio_fake_subfmt_convert(priv->params->format[1]);
              caps->ac_controls.b[2] =
                  audio_fake_subfmt_convert(priv->params->format[2]);
              caps->ac_controls.b[3] =
                  audio_fake_subfmt_convert(priv->params->format[3]);

              break;

            default:
              caps->ac_controls.b[0] = AUDIO_SUBFMT_END;
              break;
          }

        break;

      /* Provide capabilities of our OUTPUT unit */

      case AUDIO_TYPE_OUTPUT:
      case AUDIO_TYPE_INPUT:

        caps->ac_channels = (priv->params->channels[0] << 4) |
                            (priv->params->channels[1] & 0x0f);

        switch (caps->ac_subtype)
          {
            case AUDIO_TYPE_QUERY:

              caps->ac_channels = (priv->params->channels[0] << 4) |
                                  (priv->params->channels[1] & 0x0f);

              /* Report the Sample rates we support */

              caps->ac_controls.hw[0] =
                  audio_fake_samp_rate_convert(priv->params->samplerate[0]) |
                  audio_fake_samp_rate_convert(priv->params->samplerate[1]) |
                  audio_fake_samp_rate_convert(priv->params->samplerate[2]) |
                  audio_fake_samp_rate_convert(priv->params->samplerate[3]);

              break;

            default:
              break;
          }

        break;

      /* All others we don't support */

      default:

        /* Zero out the fields to indicate no support */

        caps->ac_subtype  = 0;
        caps->ac_channels = 0;

        break;
    }

  /* Return the length of the audio_caps_s struct for validation of
   * proper Audio device type.
   */

  audinfo("Return %d\n", caps->ac_len);
  return caps->ac_len;
}

/****************************************************************************
 * Name: audio_fake_configure
 *
 * Description:
 *   Configure the audio device for the specified  mode of operation.
 *
 ****************************************************************************/

#ifdef CONFIG_AUDIO_MULTI_SESSION
static int audio_fake_configure(FAR struct audio_lowerhalf_s *dev,
                                FAR void *session,
                                FAR const struct audio_caps_s *caps)
#else
static int audio_fake_configure(FAR struct audio_lowerhalf_s *dev,
                                FAR const struct audio_caps_s *caps)
#endif
{
  FAR struct audio_fake_s *priv = (FAR struct audio_fake_s *)dev;
  int ret;
  audinfo("ac_type: %d\n", caps->ac_type);

  if (priv->mqname[0] == '\0')
    {
      struct mq_attr attr;

      /* Create a message queue for the worker thread */

      snprintf(priv->mqname, sizeof(priv->mqname), "/tmp/%" PRIXPTR,
               (uintptr_t)priv);

      attr.mq_maxmsg  = 16;
      attr.mq_msgsize = sizeof(struct audio_msg_s);
      attr.mq_curmsgs = 0;
      attr.mq_flags   = 0;

      ret = file_mq_open(&priv->mq, priv->mqname, O_RDWR | O_CREAT, 0644,
                         &attr);
      if (ret < 0)
        {
          /* Error creating message queue! */

          auderr("ERROR: Couldn't allocate message queue\n");
          return ret;
        }
    }

  /* Process the configure operation */

  switch (caps->ac_type)
    {
      case AUDIO_TYPE_OUTPUT:
      case AUDIO_TYPE_INPUT:
        priv->sample_rate =
            caps->ac_controls.hw[0] | (caps->ac_controls.b[3] << 16);
        priv->channels = caps->ac_channels;
        priv->format   = caps->ac_controls.b[2];
        priv->bps = caps->ac_channels * priv->sample_rate *
                    priv->format / 8;

        audinfo("Audio type: %s\n", (caps->ac_type == AUDIO_TYPE_OUTPUT)
                                        ? "AUDIO_TYPE_OUTPUT"
                                        : "AUDIO_TYPE_INPUT");
        audinfo("Number of channels: %u\n", caps->ac_channels);
        audinfo("Sample rate:        %u\n", caps->ac_controls.hw[0]);
        audinfo("Sample width:       %u\n", caps->ac_controls.b[2]);
        break;

      default:
        audinfo("default case: %d\n", caps->ac_type);
        break;
    }

  audinfo("Return OK\n");
  return OK;
}

/****************************************************************************
 * Name: audio_fake_shutdown
 *
 * Description:
 *   Shutdown the driver and put it in the lowest power state possible.
 *
 ****************************************************************************/

static int audio_fake_shutdown(FAR struct audio_lowerhalf_s *dev)
{
  audinfo("Return OK\n");
  return OK;
}

/****************************************************************************
 * Name: audio_fake_workerthread
 *
 *  This is the thread that feeds data to the chip and keeps the audio
 *  stream going.
 *
 ****************************************************************************/

static int audio_fake_workerthread(int argc, FAR char **argv)
{
  FAR struct audio_fake_s *priv = (FAR struct audio_fake_s *)
                                  ((uintptr_t)strtoul(argv[1], NULL, 16));
  FAR struct ap_buffer_s *apb;
  struct audio_msg_s msg;
  struct mq_attr attr;
  unsigned int prio;
  int msglen;
  int ret;

  audinfo("Entry\n");

  /* Loop as long as we are supposed to be running */

#ifndef CONFIG_AUDIO_EXCLUDE_STOP
  while (!priv->terminate)
#else
  for (; ; )
#endif
    {
      /* Wait for messages from our message queue */

      msglen = file_mq_receive(&priv->mq, (FAR char *)&msg,
                               sizeof(msg), &prio);

      /* Handle the case when we return with no message */

      if (msglen < sizeof(struct audio_msg_s))
        {
          auderr("ERROR: Message too small: %d\n", msglen);
          continue;
        }

      /* Process the message */

      switch (msg.msg_id)
        {
          case AUDIO_MSG_DATA_REQUEST:
            break;

#ifndef CONFIG_AUDIO_EXCLUDE_STOP
          case AUDIO_MSG_STOP:

            /* Consume all buffers on the bufferq after stop */

            for (; ; )
              {
                file_mq_getattr(&priv->mq, &attr);
                if (attr.mq_curmsgs == 0)
                  {
                    break;
                  }

                file_mq_receive(&priv->mq, (FAR char *)&msg, sizeof(msg),
                                &prio);

                /* direct dequeue buffer to application */

                apb = (FAR struct ap_buffer_s *)msg.u.ptr;
#ifdef CONFIG_AUDIO_MULTI_SESSION
                priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_DEQUEUE,
                                apb, OK, NULL);
#else
                priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_DEQUEUE,
                                apb, OK);
#endif
              }

            priv->terminate = true;
            break;
#endif

          case AUDIO_MSG_ENQUEUE:
            apb = (FAR struct ap_buffer_s *)msg.u.ptr;
            ret = audio_fake_process_buffer(&priv->dev, apb);
            if (ret < 0)
              {
                auderr("fake audio process error %d\n", ret);
#ifndef CONFIG_AUDIO_EXCLUDE_STOP
                priv->terminate = true;
#endif
              }
            break;

          default:
            auderr("ERROR: Ignoring message ID %d\n", msg.msg_id);
            break;
        }
    }

  /* Close the message queue */

  file_mq_close(&priv->mq);
  file_mq_unlink(priv->mqname);
  priv->mqname[0] = '\0';

  audio_fake_sin_data_deinit(&priv->dev);
  audio_fake_file_deinit(&priv->dev);

#ifdef CONFIG_AUDIO_MULTI_SESSION
  priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_COMPLETE, NULL, OK, NULL);
#else
  priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_COMPLETE, NULL, OK);
#endif
  audinfo("Exit %s\n", priv->params->devname);

  return OK;
}

/****************************************************************************
 * Name: audio_fake_start
 *
 * Description:
 *   Start the configured operation (audio streaming, volume enabled, etc.).
 *
 ****************************************************************************/

#ifdef CONFIG_AUDIO_MULTI_SESSION
static int audio_fake_start(FAR struct audio_lowerhalf_s *dev,
                            FAR void *session)
#else
static int audio_fake_start(FAR struct audio_lowerhalf_s *dev)
#endif
{
  FAR struct audio_fake_s *priv = (FAR struct audio_fake_s *)dev;
  FAR char *argv[2];
  char arg1[32];
  int ret;

  ret = audio_fake_file_init(dev);
  if (ret < 0)
    {
      if (!priv->params->playback)
        {
          ret = audio_fake_sin_data_init(dev);
          if (ret < 0)
            {
              return ret;
            }
        }
      else
        {
          return ret;
        }
    }

  priv->start_tick = clock_systime_ticks();
  priv->total_bytes = 0;
#ifndef CONFIG_AUDIO_EXCLUDE_STOP
  priv->terminate = false;
#endif

  /* Start our thread for sending data to the device */

  audinfo("Starting worker thread\n");

  snprintf(arg1, 32, "%p", priv);
  argv[0] = arg1;
  argv[1] = NULL;
  ret = kthread_create("audio_fake",
                       CONFIG_AUDIO_FAKE_WORKER_PRIORITY,
                       CONFIG_AUDIO_FAKE_WORKER_STACKSIZE,
                       audio_fake_workerthread, argv);
  if (ret < 0)
    {
      auderr("ERROR: kthread_create failed: %d\n", ret);
      return ret;
    }
  else
    {
      audinfo("Created worker thread\n");
    }

  return OK;
}

/****************************************************************************
 * Name: audio_fake_stop
 *
 * Description: Stop the configured operation (audio streaming, volume
 *              disabled, etc.).
 *
 ****************************************************************************/

#ifndef CONFIG_AUDIO_EXCLUDE_STOP
#ifdef CONFIG_AUDIO_MULTI_SESSION
static int audio_fake_stop(FAR struct audio_lowerhalf_s *dev,
                           FAR void *session)
#else
static int audio_fake_stop(FAR struct audio_lowerhalf_s *dev)
#endif
{
  FAR struct audio_fake_s *priv = (FAR struct audio_fake_s *)dev;
  struct audio_msg_s term_msg;

  /* Send a message to stop all audio streaming */

  term_msg.msg_id = AUDIO_MSG_STOP;
  term_msg.u.data = 0;
  file_mq_send(&priv->mq, (FAR const char *)&term_msg, sizeof(term_msg),
               CONFIG_AUDIO_FAKE_MSG_PRIO);

  audinfo("Return OK\n");
  return OK;
}
#endif

/****************************************************************************
 * Name: audio_fake_pause
 *
 * Description: Pauses the playback.
 *
 ****************************************************************************/

#ifndef CONFIG_AUDIO_EXCLUDE_PAUSE_RESUME
#ifdef CONFIG_AUDIO_MULTI_SESSION
static int audio_fake_pause(FAR struct audio_lowerhalf_s *dev,
                            FAR void *session)
#else
static int audio_fake_pause(FAR struct audio_lowerhalf_s *dev)
#endif
{
  FAR struct audio_fake_s *priv = (FAR struct audio_fake_s *)dev;
  audinfo("%s pause\n", priv->params->devname);
  return OK;
}
#endif /* CONFIG_AUDIO_EXCLUDE_PAUSE_RESUME */

/****************************************************************************
 * Name: audio_fake_resume
 *
 * Description: Resumes the playback.
 *
 ****************************************************************************/

#ifndef CONFIG_AUDIO_EXCLUDE_PAUSE_RESUME
#ifdef CONFIG_AUDIO_MULTI_SESSION
static int audio_fake_resume(FAR struct audio_lowerhalf_s *dev,
                             FAR void *session)
#else
static int audio_fake_resume(FAR struct audio_lowerhalf_s *dev)
#endif
{
  FAR struct audio_fake_s *priv = (FAR struct audio_fake_s *)dev;
  priv->start_tick = clock_systime_ticks();
  priv->total_bytes = 0;
  audinfo("%s resume\n", priv->params->devname);
  return OK;
}
#endif /* CONFIG_AUDIO_EXCLUDE_PAUSE_RESUME */

/****************************************************************************
 * Name: audio_fake_enqueuebuffer
 *
 * Description: Enqueue an Audio Pipeline Buffer for
 * playback/capture processing.
 *
 ****************************************************************************/

static int audio_fake_enqueuebuffer(FAR struct audio_lowerhalf_s *dev,
                                    FAR struct ap_buffer_s *apb)
{
  FAR struct audio_fake_s *priv = (FAR struct audio_fake_s *)dev;
  struct audio_msg_s msg;
  int ret;

  DEBUGASSERT(priv && apb && priv->dev.upper);

  audinfo("apb=%p curbyte=%d nbytes=%d\n", apb, apb->curbyte, apb->nbytes);

  msg.msg_id = AUDIO_MSG_ENQUEUE;
  msg.u.ptr  = apb;

  ret = file_mq_send(&priv->mq, (FAR const char *)&msg, sizeof(msg),
                     CONFIG_AUDIO_FAKE_MSG_PRIO);
  if (ret < 0)
    {
      auderr("ERROR: file_mq_send failed: %d\n", ret);
    }

  audinfo("Return OK\n");
  return ret;
}

/****************************************************************************
 * Name: audio_fake_get_latency
 *
 * Description: calculate latency in frames.
 *
 ****************************************************************************/

static int audio_fake_get_latency(struct audio_lowerhalf_s *dev,
                                  unsigned long arg)
{
  int ret = OK;
  FAR long *latency = (FAR long *)arg;
#ifdef CONFIG_AUDIO_DRIVER_SPECIFIC_BUFFERS
  FAR struct audio_fake_s *priv;
  struct mq_attr attr;
  uint32_t period_frames;

  priv = (FAR struct audio_fake_s *)dev;

  ret = file_mq_getattr(&priv->mq, &attr);
  if (ret < 0)
    {
      *latency = 0;
      auderr("failed to get mq attributes, error: %d\n", ret);
      return ret;
    }

  period_frames = (priv->sample_rate * priv->params->period_time) / 1000;
  *latency = attr.mq_curmsgs * period_frames;

  audinfo("mq_curmsgs: %ld, period_frames: %"PRIu32"\n",
          attr.mq_curmsgs, period_frames);
#else
  *latency = 0;
  audinfo("AUDIOIOC_GETLATENCY not implemented\n");
#endif

  return ret;
}

/****************************************************************************
 * Name: audio_fake_ioctl
 *
 * Description: Perform a device ioctl
 *
 ****************************************************************************/

static int audio_fake_ioctl(FAR struct audio_lowerhalf_s *dev, int cmd,
                            unsigned long arg)
{
#ifdef CONFIG_AUDIO_DRIVER_SPECIFIC_BUFFERS
  FAR struct audio_fake_s *priv = (FAR struct audio_fake_s *)dev;
  FAR struct ap_buffer_info_s *bufinfo;
#endif
  int ret = OK;

  audinfo("cmd=%d arg=%ld\n", cmd, arg);

  /* Deal with ioctls passed from the upper-half driver */

  switch (cmd)
    {
        /* Report our preferred buffer size and quantity */

      case AUDIOIOC_GETBUFFERINFO:
        {
          audinfo("AUDIOIOC_GETBUFFERINFO:\n");
#ifdef CONFIG_AUDIO_DRIVER_SPECIFIC_BUFFERS
          bufinfo = (FAR struct ap_buffer_info_s *)arg;
          bufinfo->buffer_size =
              (priv->bps * priv->params->period_time) / 1000;
          bufinfo->nbuffers = priv->params->periods;
#else
          audwarn("AUDIOIOC_GETBUFFERINFO Return EPERM\n");
          return -EPERM;
#endif
        }
        break;
      case AUDIOIOC_GETLATENCY:
        if (!dev || !arg)
          {
            auderr("invalid params dev: %p, arg: %lu.\n", dev, arg);
            ret = -EINVAL;
          }
        else
          {
            ret = audio_fake_get_latency(dev, arg);
          }
        break;
      default:
        ret = -ENOTTY;
        break;
    }

  audinfo("Return ret: %d\n", ret);
  return ret;
}

/****************************************************************************
 * Name: audio_fake_reserve
 *
 * Description: Reserves a session (the only one we have).
 *
 ****************************************************************************/

#ifdef CONFIG_AUDIO_MULTI_SESSION
static int audio_fake_reserve(FAR struct audio_lowerhalf_s *dev,
                              FAR void **session)
#else
static int audio_fake_reserve(FAR struct audio_lowerhalf_s *dev)
#endif
{
  FAR struct audio_fake_s *priv = (FAR struct audio_fake_s *)dev;
#ifndef CONFIG_AUDIO_EXCLUDE_STOP
  priv->terminate = false;
#endif
  audinfo("%s reserve\n", priv->params->devname);
  return OK;
}

/****************************************************************************
 * Name: audio_fake_release
 *
 * Description: Releases the session (the only one we have).
 *
 ****************************************************************************/

#ifdef CONFIG_AUDIO_MULTI_SESSION
static int audio_fake_release(FAR struct audio_lowerhalf_s *dev,
                              FAR void *session)
#else
static int audio_fake_release(FAR struct audio_lowerhalf_s *dev)
#endif
{
  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: audio_fake_initialize
 *
 * Description:
 *   Initialize and register fake audio device.
 *
 ****************************************************************************/

int audio_fake_initialize(void)
{
  return audio_fake_register(g_devparams, nitems(g_devparams));
}

/****************************************************************************
 * Name: audio_fake_register
 *
 * Description:
 *   Register audio driver nodes using fake_server
 *
 * Input Parameters:
 *   params - fake audio parameters array
 *   nparams - number of fake audio parameters
 *
 * Returned Value:
 *   >0 values represents the number of node register succeed;
 *   <0 values indicate that all nodes have failed to register, and
 *   the value is the error code of the last node register returned.
 *
 ****************************************************************************/

int audio_fake_register(FAR const audio_fake_params_t *params,
                        size_t nparams)
{
  FAR struct audio_fake_s *priv;
  FAR audio_fake_params_t *param;
  size_t good = 0;
  size_t i;
  int ret = 0;

  if (!params || !nparams)
    {
      auderr("ERROR: Invalid fake audio parameters\n");
      return -EINVAL;
    }

  for (i = 0; i < nparams; i++)
    {
      priv = (FAR struct audio_fake_s *)kmm_zalloc(sizeof(*priv));
      if (!priv)
        {
          auderr("ERROR: Failed to allocate fake audio priv\n");
          ret = -ENOMEM;
          continue;
        }

      param = (FAR audio_fake_params_t *)kmm_zalloc(sizeof(*param));
      if (!param)
        {
          auderr("ERROR: Failed to allocate fake audio params\n");
          ret = -ENOMEM;
          kmm_free(priv);
          continue;
        }

      *param = params[i];
      priv->dev.ops = &g_audioops;
      priv->params  = param;

      ret = audio_register(param->devname, &priv->dev);
      if (ret < 0)
        {
          audwarn("WARNING: Failed to register (%s) fake audio device(%d)\n",
                  params->devname, ret);
          kmm_free(priv);
          kmm_free(param);
          continue;
        }

      good++;
    }

  return good ? good : ret;
}
