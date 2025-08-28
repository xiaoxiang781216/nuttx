/****************************************************************************
 * arch/sim/src/sim/sim_hostalsa.h
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

#ifndef __ARCH_SIM_SRC_SIM_HOSTALSA_H
#define __ARCH_SIM_SRC_SIM_HOSTALSA_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <endian.h>

/****************************************************************************
 * Public Types
 ****************************************************************************/

#ifndef __ASOUNDLIB_H

typedef unsigned long snd_pcm_uframes_t;
typedef long snd_pcm_sframes_t;
typedef struct _snd_pcm snd_pcm_t;
typedef struct _snd_pcm_hw_params snd_pcm_hw_params_t;
typedef struct _snd_mixer snd_mixer_t;
typedef struct _snd_mixer_class snd_mixer_class_t;
typedef struct _snd_mixer_elem snd_mixer_elem_t;
typedef struct _snd_mixer_selem_id snd_mixer_selem_id_t;

typedef enum _snd_pcm_format
{
  SND_PCM_FORMAT_UNKNOWN = -1,
  SND_PCM_FORMAT_S8 = 0,
  SND_PCM_FORMAT_U8,
  SND_PCM_FORMAT_S16_LE,
  SND_PCM_FORMAT_S16_BE,
  SND_PCM_FORMAT_U16_LE,
  SND_PCM_FORMAT_U16_BE,
  SND_PCM_FORMAT_S24_LE,
  SND_PCM_FORMAT_S24_BE,
  SND_PCM_FORMAT_U24_LE,
  SND_PCM_FORMAT_U24_BE,
  SND_PCM_FORMAT_S32_LE,
  SND_PCM_FORMAT_S32_BE,
  SND_PCM_FORMAT_U32_LE,
  SND_PCM_FORMAT_U32_BE,
#if __BYTE_ORDER == __LITTLE_ENDIAN
  SND_PCM_FORMAT_S16 = SND_PCM_FORMAT_S16_LE,
  SND_PCM_FORMAT_U16 = SND_PCM_FORMAT_U16_LE,
  SND_PCM_FORMAT_S24 = SND_PCM_FORMAT_S24_LE,
  SND_PCM_FORMAT_U24 = SND_PCM_FORMAT_U24_LE,
  SND_PCM_FORMAT_S32 = SND_PCM_FORMAT_S32_LE,
  SND_PCM_FORMAT_U32 = SND_PCM_FORMAT_U32_LE,
#elif __BYTE_ORDER == __BIG_ENDIAN
  SND_PCM_FORMAT_S16 = SND_PCM_FORMAT_S16_BE,
  SND_PCM_FORMAT_U16 = SND_PCM_FORMAT_U16_BE,
  SND_PCM_FORMAT_S24 = SND_PCM_FORMAT_S24_BE,
  SND_PCM_FORMAT_U24 = SND_PCM_FORMAT_U24_BE,
  SND_PCM_FORMAT_S32 = SND_PCM_FORMAT_S32_BE,
  SND_PCM_FORMAT_U32 = SND_PCM_FORMAT_U32_BE,
#else
#error "Unknown endian"
#endif
} snd_pcm_format_t;

typedef enum _vela_snd_pcm_stream
{
  SND_PCM_STREAM_PLAYBACK = 0,
  SND_PCM_STREAM_CAPTURE,
  SND_PCM_STREAM_LAST = SND_PCM_STREAM_CAPTURE
} snd_pcm_stream_t;

typedef enum _snd_pcm_access
{
  SND_PCM_ACCESS_MMAP_INTERLEAVED = 0,
  SND_PCM_ACCESS_MMAP_NONINTERLEAVED,
  SND_PCM_ACCESS_MMAP_COMPLEX,
  SND_PCM_ACCESS_RW_INTERLEAVED,
  SND_PCM_ACCESS_RW_NONINTERLEAVED,
  SND_PCM_ACCESS_LAST = SND_PCM_ACCESS_RW_NONINTERLEAVED
} snd_pcm_access_t;

typedef enum
{
  SND_MIXER_SCHN_UNKNOWN = -1,
  SND_MIXER_SCHN_FRONT_LEFT,
  SND_MIXER_SCHN_FRONT_RIGHT,
  SND_MIXER_SCHN_REAR_LEFT,
  SND_MIXER_SCHN_REAR_RIGHT,
  SND_MIXER_SCHN_FRONT_CENTER,
  SND_MIXER_SCHN_WOOFER,
  SND_MIXER_SCHN_SIDE_LEFT,
  SND_MIXER_SCHN_SIDE_RIGHT,
  SND_MIXER_SCHN_REAR_CENTER,
  SND_MIXER_SCHN_LAST = 31,
  SND_MIXER_SCHN_MONO = SND_MIXER_SCHN_FRONT_LEFT
} snd_mixer_selem_channel_id_t;

enum snd_mixer_selem_regopt_abstract
{
  SND_MIXER_SABSTRACT_NONE,
  SND_MIXER_SABSTRACT_BASIC,
};

struct snd_mixer_selem_regopt
{
  int ver;
  enum snd_mixer_selem_regopt_abstract abstract;
  const char *device;
  snd_pcm_t *playback_pcm;
  snd_pcm_t *capture_pcm;
};

#endif //__ASOUNDLIB_H

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

int host_snd_pcm_hw_params_malloc(snd_pcm_hw_params_t **hw_params);
int host_snd_pcm_hw_params_any(snd_pcm_t *pcm,
                               snd_pcm_hw_params_t *hw_params);
int host_snd_pcm_hw_params_set_access(snd_pcm_t *pcm,
                                      snd_pcm_hw_params_t *hw_params,
                                      snd_pcm_access_t access);
int host_snd_pcm_hw_params_set_format(snd_pcm_t *pcm,
                                      snd_pcm_hw_params_t *hw_params,
                                      snd_pcm_format_t format);
int host_snd_pcm_hw_params_set_rate(snd_pcm_t *pcm,
                                    snd_pcm_hw_params_t *hw_params,
                                    unsigned int rate, int dir);
int host_snd_pcm_hw_params_set_channels(snd_pcm_t *pcm,
                                        snd_pcm_hw_params_t *hw_params,
                                        unsigned int channels);
int host_snd_pcm_hw_params_set_period_size_near(
    snd_pcm_t *pcm, snd_pcm_hw_params_t *hw_params,
    snd_pcm_uframes_t *frames, int *dir);
int host_snd_pcm_hw_params_set_periods_near(snd_pcm_t *pcm,
                                            snd_pcm_hw_params_t *hw_params,
                                            unsigned int *periods, int *dir);
int host_snd_pcm_hw_params(snd_pcm_t *pcm, snd_pcm_hw_params_t *hw_params);
void host_snd_pcm_hw_params_free(snd_pcm_hw_params_t *hw_params);

int host_snd_pcm_open(snd_pcm_t **pcm, const char *name,
                      snd_pcm_stream_t stream, int mode);
int host_snd_pcm_close(snd_pcm_t *pcm);
int host_snd_pcm_start(snd_pcm_t *pcm);
int host_snd_pcm_writei(snd_pcm_t *pcm, const void *buffer,
                        snd_pcm_uframes_t size);
int host_snd_pcm_readi(snd_pcm_t *pcm, void *buffer, snd_pcm_uframes_t size);
int host_snd_pcm_avail(snd_pcm_t *pcm);
int host_snd_pcm_delay(snd_pcm_t *pcm, snd_pcm_sframes_t *delay);
int host_snd_pcm_drain(snd_pcm_t *pcm);
int host_snd_pcm_prepare(snd_pcm_t *pcm);
const char *host_snd_strerror(int errnum);

int host_snd_mixer_open(snd_mixer_t **mixer, int mode);
int host_snd_mixer_attach(snd_mixer_t *mixer, const char *name);
int host_snd_mixer_selem_register(snd_mixer_t *mixer,
                                  struct snd_mixer_selem_regopt *options,
                                  snd_mixer_class_t **classp);
int host_snd_mixer_load(snd_mixer_t *mixer);
void host_snd_mixer_close(snd_mixer_t *mixer);

int host_snd_mixer_selem_id_malloc(snd_mixer_selem_id_t **sid);
void host_snd_mixer_selem_id_free(snd_mixer_selem_id_t *sid);
void host_snd_mixer_selem_id_set_index(snd_mixer_selem_id_t *sid,
                                       unsigned int index);
void host_snd_mixer_selem_id_set_name(snd_mixer_selem_id_t *sid,
                                      const char *name);

snd_mixer_elem_t *host_snd_mixer_find_selem(snd_mixer_t *mixer,
                                            const snd_mixer_selem_id_t *sid);
int host_snd_mixer_selem_set_playback_volume_range(snd_mixer_elem_t *elem,
                                                   long min, long max);
int host_snd_mixer_selem_set_capture_volume_range(snd_mixer_elem_t *elem,
                                                  long min, long max);
int host_snd_mixer_selem_get_playback_volume(
    snd_mixer_elem_t *elem, snd_mixer_selem_channel_id_t channel,
    long *value);
int host_snd_mixer_selem_get_capture_volume(
    snd_mixer_elem_t *elem, snd_mixer_selem_channel_id_t channel,
    long *value);
int host_snd_mixer_selem_set_playback_volume_all(snd_mixer_elem_t *elem,
                                                 long value);
int host_snd_mixer_selem_set_capture_volume_all(snd_mixer_elem_t *elem,
                                                long value);

#endif /* __ARCH_SIM_SRC_SIM_HOSTALSA_H */