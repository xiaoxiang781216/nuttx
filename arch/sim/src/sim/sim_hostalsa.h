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