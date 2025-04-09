/****************************************************************************
 * include/nuttx/audio/audio_fake.h
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

#ifndef __INCLUDE_NUTTX_AUDIO_AUDIO_FAKE_H
#define __INCLUDE_NUTTX_AUDIO_AUDIO_FAKE_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef CONFIG_AUDIO_FAKE

/****************************************************************************
 * Public Data
 ****************************************************************************/

#ifdef __cplusplus
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

typedef struct audio_fake_params
{
  FAR const char *devname;
  bool       playback;        /* True: playback, False: recording */
  uint32_t   samplerate[4];   /* Array of sample rate,eg. [44100, 48000, 32000, 22050] */
  uint8_t    channels[2];     /* Range of channels, [min_channel, max_channel] */
  uint8_t    format[4];       /* Array of format, eg. [8, 16, 32] */
  uint32_t   period_time;     /* Period time in milliseconds */
  uint32_t   periods;         /* Number of periods */
} audio_fake_params_t;

int audio_fake_initialize(void);
int audio_fake_register(FAR const audio_fake_params_t *params,
                        size_t nparams);

#undef EXTERN
#ifdef __cplusplus
}
#endif

#endif /* CONFIG_AUDIO_FAKE */
#endif /* __INCLUDE_NUTTX_AUDIO_AUDIO_FAKE_H */
