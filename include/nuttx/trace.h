/****************************************************************************
 * include/nuttx/trace.h
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

#ifndef __INCLUDE_NUTTX_TRACE_H
#define __INCLUDE_NUTTX_TRACE_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <string.h>

#include <nuttx/arch.h>
#include <nuttx/sched_note.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifdef CONFIG_TRACE
#  define trace_begin(tag) sched_note_begin(tag)
#  define trace_end(tag) sched_note_end(tag)
#  define trace_beginex(tag, name) sched_note_beginex(tag, name)
#  define trace_endex(tag, name) sched_note_endex(tag, name)
#  define trace_mark(tag, s) sched_note_mark(tag, s)
#  define trace_printf(tag, fmt, ...) \
    sched_note_printf(tag, LOG_DEBUG, fmt, ##__VA_ARGS__)
#  define trace_async_begin(tag, name, cookie) \
    sched_note_async_begin(tag, name, cookie)
#  define trace_async_end(tag, name, cookie) \
    sched_note_async_end(tag, name, cookie)
#  define trace_async_begin_track(tag, track, name, cookie) \
    sched_note_async_begin_track(tag, track, name, cookie)
#  define trace_async_end_track(tag, track, name, cookie) \
    sched_note_async_end_track(tag, track, name, cookie)
#else
#  define trace_begin(tag)
#  define trace_end(tag)
#  define trace_beginex(tag, name)
#  define trace_endex(tag, name)
#  define trace_mark(tag, s)
#  define trace_printf(...)
#  define trace_async_begin(tag, name, cookie)
#  define trace_async_end(tag, name, cookie)
#  define trace_async_begin_track(tag, track, name, cookie)
#  define trace_async_end_track(tag, track, name, cookie)
#endif

#ifdef CONFIG_DIAG
#  define trace_diag(tag, level, id, fmt, ...) \
    sched_note_printf_ip(tag, level, id, fmt, ##__VA_ARGS__)
#  define trace_diagv(tag, level, id, fmt, va) \
    sched_note_vprintf_ip(tag, level, id, fmt, va)
#else
#  define trace_diag(tag, level, id, fmt, ...) \
    do \
      { \
        if (0) \
          { \
            (void)(tag); (void)(id); \
            syslog(level, fmt, ##__VA_ARGS__); \
          } \
      } \
    while (0)
#  define trace_diagv(tag, level, id, fmt, va) \
    do \
      { \
        if (0) \
          { \
            (void)(tag); (void)(id); \
            vsyslog(level, fmt, va); \
          } \
      } \
    while (0)
#endif

#ifdef CONFIG_TRACE_ANALOG
#  define analog_trace_begin() trace_begin(NXTAG_ANALOG)
#  define analog_trace_end() trace_end(NXTAG_ANALOG)
#  define analog_trace_beginex(name) trace_beginex(NXTAG_ANALOG, name)
#  define analog_trace_endex(name) trace_endex(NXTAG_ANALOG, name)
#  define analog_trace_mark(s) trace_mark(NXTAG_ANALOG, s)
#  define analog_trace_printf(fmt, ...) \
    trace_printf(NXTAG_ANALOG, fmt, ##__VA_ARGS__)
#  define analog_trace_async_begin(name, cookie) \
    trace_async_begin(NXTAG_ANALOG, name, cookie)
#  define analog_trace_async_end(name, cookie) \
    trace_async_end(NXTAG_ANALOG, name, cookie)
#  define analog_trace_async_begin_track(track, name, cookie) \
    trace_async_begin_track(NXTAG_ANALOG, track, name, cookie)
#  define analog_trace_async_end_track(track, name, cookie) \
    trace_async_end_track(NXTAG_ANALOG, track, name, cookie)
#else
#  define analog_trace_begin()
#  define analog_trace_end()
#  define analog_trace_beginex(name)
#  define analog_trace_endex(name)
#  define analog_trace_mark(s)
#  define analog_trace_printf(...)
#  define analog_trace_async_begin(name, cookie)
#  define analog_trace_async_end(name, cookie)
#  define analog_trace_async_begin_track(track, name, cookie)
#  define analog_trace_async_end_track(track, name, cookie)
#endif

#ifdef CONFIG_TRACE_AUDIO
#  define audio_trace_begin() trace_begin(NXTAG_AUDIO)
#  define audio_trace_end() trace_end(NXTAG_AUDIO)
#  define audio_trace_beginex(name) trace_beginex(NXTAG_AUDIO, name)
#  define audio_trace_endex(name) trace_endex(NXTAG_AUDIO, name)
#  define audio_trace_mark(s) trace_mark(NXTAG_AUDIO, s)
#  define audio_trace_printf(fmt, ...) \
    trace_printf(NXTAG_AUDIO, fmt, ##__VA_ARGS__)
#  define audio_trace_async_begin(name, cookie) \
    trace_async_begin(NXTAG_AUDIO, name, cookie)
#  define audio_trace_async_end(name, cookie) \
    trace_async_end(NXTAG_AUDIO, name, cookie)
#  define audio_trace_async_begin_track(track, name, cookie) \
    trace_async_begin_track(NXTAG_AUDIO, track, name, cookie)
#  define audio_trace_async_end_track(track, name, cookie) \
    trace_async_end_track(NXTAG_AUDIO, track, name, cookie)
#else
#  define audio_trace_begin()
#  define audio_trace_end()
#  define audio_trace_beginex(name)
#  define audio_trace_endex(name)
#  define audio_trace_mark(s)
#  define audio_trace_printf(...)
#  define audio_trace_async_begin(name, cookie)
#  define audio_trace_async_end(name, cookie)
#  define audio_trace_async_begin_track(track, name, cookie)
#  define audio_trace_async_end_track(track, name, cookie)
#endif

#ifdef CONFIG_TRACE_BATTERY
#  define battery_trace_begin() trace_begin(NXTAG_BATTERY)
#  define battery_trace_end() trace_end(NXTAG_BATTERY)
#  define battery_trace_beginex(name) trace_beginex(NXTAG_BATTERY, name)
#  define battery_trace_endex(name) trace_endex(NXTAG_BATTERY, name)
#  define battery_trace_mark(s) trace_mark(NXTAG_BATTERY, s)
#  define battery_trace_printf(fmt, ...) \
    trace_printf(NXTAG_BATTERY, fmt, ##__VA_ARGS__)
#  define battery_trace_async_begin(name, cookie) \
    trace_async_begin(NXTAG_BATTERY, name, cookie)
#  define battery_trace_async_end(name, cookie) \
    trace_async_end(NXTAG_BATTERY, name, cookie)
#  define battery_trace_async_begin_track(track, name, cookie) \
    trace_async_begin_track(NXTAG_BATTERY, track, name, cookie)
#  define battery_trace_async_end_track(track, name, cookie) \
    trace_async_end_track(NXTAG_BATTERY, track, name, cookie)
#else
#  define battery_trace_begin()
#  define battery_trace_end()
#  define battery_trace_beginex(name)
#  define battery_trace_endex(name)
#  define battery_trace_mark(s)
#  define battery_trace_printf(...)
#  define battery_trace_async_begin(name, cookie)
#  define battery_trace_async_end(name, cookie)
#  define battery_trace_async_begin_track(track, name, cookie)
#  define battery_trace_async_end_track(track, name, cookie)
#endif

#ifdef CONFIG_TRACE_BINFMT
#  define binfmt_trace_begin() trace_begin(NXTAG_BINFMT)
#  define binfmt_trace_end() trace_end(NXTAG_BINFMT)
#  define binfmt_trace_beginex(name) trace_beginex(NXTAG_BINFMT, name)
#  define binfmt_trace_endex(name) trace_endex(NXTAG_BINFMT, name)
#  define binfmt_trace_mark(s) trace_mark(NXTAG_BINFMT, s)
#  define binfmt_trace_printf(fmt, ...) \
    trace_printf(NXTAG_BINFMT, fmt, ##__VA_ARGS__)
#  define binfmt_trace_async_begin(name, cookie) \
    trace_async_begin(NXTAG_BINFMT, name, cookie)
#  define binfmt_trace_async_end(name, cookie) \
    trace_async_end(NXTAG_BINFMT, name, cookie)
#  define binfmt_trace_async_begin_track(track, name, cookie) \
    trace_async_begin_track(NXTAG_BINFMT, track, name, cookie)
#  define binfmt_trace_async_end_track(track, name, cookie) \
    trace_async_end_track(NXTAG_BINFMT, track, name, cookie)
#else
#  define binfmt_trace_begin()
#  define binfmt_trace_end()
#  define binfmt_trace_beginex(name)
#  define binfmt_trace_endex(name)
#  define binfmt_trace_mark(s)
#  define binfmt_trace_printf(...)
#  define binfmt_trace_async_begin(name, cookie)
#  define binfmt_trace_async_end(name, cookie)
#  define binfmt_trace_async_begin_track(track, name, cookie)
#  define binfmt_trace_async_end_track(track, name, cookie)
#endif

#ifdef CONFIG_TRACE_CAN
#  define can_trace_begin() trace_begin(NXTAG_CAN)
#  define can_trace_end() trace_end(NXTAG_CAN)
#  define can_trace_beginex(name) trace_beginex(NXTAG_CAN, name)
#  define can_trace_endex(name) trace_endex(NXTAG_CAN, name)
#  define can_trace_mark(s) trace_mark(NXTAG_CAN, s)
#  define can_trace_printf(fmt, ...) \
    trace_printf(NXTAG_CAN, fmt, ##__VA_ARGS__)
#  define can_trace_async_begin(name, cookie) \
    trace_async_begin(NXTAG_CAN, name, cookie)
#  define can_trace_async_end(name, cookie) \
    trace_async_end(NXTAG_CAN, name, cookie)
#  define can_trace_async_begin_track(track, name, cookie) \
    trace_async_begin_track(NXTAG_CAN, track, name, cookie)
#  define can_trace_async_end_track(track, name, cookie) \
    trace_async_end_track(NXTAG_CAN, track, name, cookie)
#else
#  define can_trace_begin()
#  define can_trace_end()
#  define can_trace_beginex(name)
#  define can_trace_endex(name)
#  define can_trace_mark(s)
#  define can_trace_printf(...)
#  define can_trace_async_begin(name, cookie)
#  define can_trace_async_end(name, cookie)
#  define can_trace_async_begin_track(track, name, cookie)
#  define can_trace_async_end_track(track, name, cookie)
#endif

#ifdef CONFIG_TRACE_CAPTURE
#  define capture_trace_begin() trace_begin(NXTAG_CAPTURE)
#  define capture_trace_end() trace_end(NXTAG_CAPTURE)
#  define capture_trace_beginex(name) trace_beginex(NXTAG_CAPTURE, name)
#  define capture_trace_endex(name) trace_endex(NXTAG_CAPTURE, name)
#  define capture_trace_mark(s) trace_mark(NXTAG_CAPTURE, s)
#  define capture_trace_printf(fmt, ...) \
    trace_printf(NXTAG_CAPTURE, fmt, ##__VA_ARGS__)
#  define capture_trace_async_begin(name, cookie) \
    trace_async_begin(NXTAG_CAPTURE, name, cookie)
#  define capture_trace_async_end(name, cookie) \
    trace_async_end(NXTAG_CAPTURE, name, cookie)
#  define capture_trace_async_begin_track(track, name, cookie) \
    trace_async_begin_track(NXTAG_CAPTURE, track, name, cookie)
#  define capture_trace_async_end_track(track, name, cookie) \
    trace_async_end_track(NXTAG_CAPTURE, track, name, cookie)
#else
#  define capture_trace_begin()
#  define capture_trace_end()
#  define capture_trace_beginex(name)
#  define capture_trace_endex(name)
#  define capture_trace_mark(s)
#  define capture_trace_printf(...)
#  define capture_trace_async_begin(name, cookie)
#  define capture_trace_async_end(name, cookie)
#  define capture_trace_async_begin_track(track, name, cookie)
#  define capture_trace_async_end_track(track, name, cookie)
#endif

#ifdef CONFIG_TRACE_CONTACTLESS
#  define contactless_trace_begin() trace_begin(NXTAG_CONTACTLESS)
#  define contactless_trace_end() trace_end(NXTAG_CONTACTLESS)
#  define contactless_trace_beginex(name) trace_beginex(NXTAG_CONTACTLESS, name)
#  define contactless_trace_endex(name) trace_endex(NXTAG_CONTACTLESS, name)
#  define contactless_trace_mark(s) trace_mark(NXTAG_CONTACTLESS, s)
#  define contactless_trace_printf(fmt, ...) \
    trace_printf(NXTAG_CONTACTLESS, fmt, ##__VA_ARGS__)
#  define contactless_trace_async_begin(name, cookie) \
    trace_async_begin(NXTAG_CONTACTLESS, name, cookie)
#  define contactless_trace_async_end(name, cookie) \
    trace_async_end(NXTAG_CONTACTLESS, name, cookie)
#  define contactless_trace_async_begin_track(track, name, cookie) \
    trace_async_begin_track(NXTAG_CONTACTLESS, track, name, cookie)
#  define contactless_trace_async_end_track(track, name, cookie) \
    trace_async_end_track(NXTAG_CONTACTLESS, track, name, cookie)
#else
#  define contactless_trace_begin()
#  define contactless_trace_end()
#  define contactless_trace_beginex(name)
#  define contactless_trace_endex(name)
#  define contactless_trace_mark(s)
#  define contactless_trace_printf(...)
#  define contactless_trace_async_begin(name, cookie)
#  define contactless_trace_async_end(name, cookie)
#  define contactless_trace_async_begin_track(track, name, cookie)
#  define contactless_trace_async_end_track(track, name, cookie)
#endif

#ifdef CONFIG_TRACE_CRYPTO
#  define crypto_trace_begin() trace_begin(NXTAG_CRYPTO)
#  define crypto_trace_end() trace_end(NXTAG_CRYPTO)
#  define crypto_trace_beginex(name) trace_beginex(NXTAG_CRYPTO, name)
#  define crypto_trace_endex(name) trace_endex(NXTAG_CRYPTO, name)
#  define crypto_trace_mark(s) trace_mark(NXTAG_CRYPTO, s)
#  define crypto_trace_printf(fmt, ...) \
    trace_printf(NXTAG_CRYPTO, fmt, ##__VA_ARGS__)
#  define crypto_trace_async_begin(name, cookie) \
    trace_async_begin(NXTAG_CRYPTO, name, cookie)
#  define crypto_trace_async_end(name, cookie) \
    trace_async_end(NXTAG_CRYPTO, name, cookie)
#  define crypto_trace_async_begin_track(track, name, cookie) \
    trace_async_begin_track(NXTAG_CRYPTO, track, name, cookie)
#  define crypto_trace_async_end_track(track, name, cookie) \
    trace_async_end_track(NXTAG_CRYPTO, track, name, cookie)
#else
#  define crypto_trace_begin()
#  define crypto_trace_end()
#  define crypto_trace_beginex(name)
#  define crypto_trace_endex(name)
#  define crypto_trace_mark(s)
#  define crypto_trace_printf(...)
#  define crypto_trace_async_begin(name, cookie)
#  define crypto_trace_async_end(name, cookie)
#  define crypto_trace_async_begin_track(track, name, cookie)
#  define crypto_trace_async_end_track(track, name, cookie)
#endif

#ifdef CONFIG_TRACE_DMA
#  define dma_trace_begin() trace_begin(NXTAG_DMA)
#  define dma_trace_end() trace_end(NXTAG_DMA)
#  define dma_trace_beginex(name) trace_beginex(NXTAG_DMA, name)
#  define dma_trace_endex(name) trace_endex(NXTAG_DMA, name)
#  define dma_trace_mark(s) trace_mark(NXTAG_DMA, s)
#  define dma_trace_printf(fmt, ...) \
    trace_printf(NXTAG_DMA, fmt, ##__VA_ARGS__)
#  define dma_trace_async_begin(name, cookie) \
    trace_async_begin(NXTAG_DMA, name, cookie)
#  define dma_trace_async_end(name, cookie) \
    trace_async_end(NXTAG_DMA, name, cookie)
#  define dma_trace_async_begin_track(track, name, cookie) \
    trace_async_begin_track(NXTAG_DMA, track, name, cookie)
#  define dma_trace_async_end_track(track, name, cookie) \
    trace_async_end_track(NXTAG_DMA, track, name, cookie)
#else
#  define dma_trace_begin()
#  define dma_trace_end()
#  define dma_trace_beginex(name)
#  define dma_trace_endex(name)
#  define dma_trace_mark(s)
#  define dma_trace_printf(...)
#  define dma_trace_async_begin(name, cookie)
#  define dma_trace_async_end(name, cookie)
#  define dma_trace_async_begin_track(track, name, cookie)
#  define dma_trace_async_end_track(track, name, cookie)
#endif

#ifdef CONFIG_TRACE_FS
#  define fs_trace_begin() trace_begin(NXTAG_FS)
#  define fs_trace_end() trace_end(NXTAG_FS)
#  define fs_trace_beginex(name) trace_beginex(NXTAG_FS, name)
#  define fs_trace_endex(name) trace_endex(NXTAG_FS, name)
#  define fs_trace_mark(s) trace_mark(NXTAG_FS, s)
#  define fs_trace_printf(fmt, ...) \
    trace_printf(NXTAG_FS, fmt, ##__VA_ARGS__)
#  define fs_trace_async_begin(name, cookie) \
    trace_async_begin(NXTAG_FS, name, cookie)
#  define fs_trace_async_end(name, cookie) \
    trace_async_end(NXTAG_FS, name, cookie)
#  define fs_trace_async_begin_track(track, name, cookie) \
    trace_async_begin_track(NXTAG_FS, track, name, cookie)
#  define fs_trace_async_end_track(track, name, cookie) \
    trace_async_end_track(NXTAG_FS, track, name, cookie)
#else
#  define fs_trace_begin()
#  define fs_trace_end()
#  define fs_trace_beginex(name)
#  define fs_trace_endex(name)
#  define fs_trace_mark(s)
#  define fs_trace_printf(...)
#  define fs_trace_async_begin(name, cookie)
#  define fs_trace_async_end(name, cookie)
#  define fs_trace_async_begin_track(track, name, cookie)
#  define fs_trace_async_end_track(track, name, cookie)
#endif

#ifdef CONFIG_TRACE_GPIO
#  define gpio_trace_begin() trace_begin(NXTAG_GPIO)
#  define gpio_trace_end() trace_end(NXTAG_GPIO)
#  define gpio_trace_beginex(name) trace_beginex(NXTAG_GPIO, name)
#  define gpio_trace_endex(name) trace_endex(NXTAG_GPIO, name)
#  define gpio_trace_mark(s) trace_mark(NXTAG_GPIO, s)
#  define gpio_trace_printf(fmt, ...) \
    trace_printf(NXTAG_GPIO, fmt, ##__VA_ARGS__)
#  define gpio_trace_async_begin(name, cookie) \
    trace_async_begin(NXTAG_GPIO, name, cookie)
#  define gpio_trace_async_end(name, cookie) \
    trace_async_end(NXTAG_GPIO, name, cookie)
#  define gpio_trace_async_begin_track(track, name, cookie) \
    trace_async_begin_track(NXTAG_GPIO, track, name, cookie)
#  define gpio_trace_async_end_track(track, name, cookie) \
    trace_async_end_track(NXTAG_GPIO, track, name, cookie)
#else
#  define gpio_trace_begin()
#  define gpio_trace_end()
#  define gpio_trace_beginex(name)
#  define gpio_trace_endex(name)
#  define gpio_trace_mark(s)
#  define gpio_trace_printf(...)
#  define gpio_trace_async_begin(name, cookie)
#  define gpio_trace_async_end(name, cookie)
#  define gpio_trace_async_begin_track(track, name, cookie)
#  define gpio_trace_async_end_track(track, name, cookie)
#endif

#ifdef CONFIG_TRACE_GRAPHICS
#  define graphics_trace_begin() trace_begin(NXTAG_GRAPHICS)
#  define graphics_trace_end() trace_end(NXTAG_GRAPHICS)
#  define graphics_trace_beginex(name) trace_beginex(NXTAG_GRAPHICS, name)
#  define graphics_trace_endex(name) trace_endex(NXTAG_GRAPHICS, name)
#  define graphics_trace_mark(s) trace_mark(NXTAG_GRAPHICS, s)
#  define graphics_trace_printf(fmt, ...) \
    trace_printf(NXTAG_GRAPHICS, fmt, ##__VA_ARGS__)
#  define graphics_trace_async_begin(name, cookie) \
    trace_async_begin(NXTAG_GRAPHICS, name, cookie)
#  define graphics_trace_async_end(name, cookie) \
    trace_async_end(NXTAG_GRAPHICS, name, cookie)
#  define graphics_trace_async_begin_track(track, name, cookie) \
    trace_async_begin_track(NXTAG_GRAPHICS, track, name, cookie)
#  define graphics_trace_async_end_track(track, name, cookie) \
    trace_async_end_track(NXTAG_GRAPHICS, track, name, cookie)
#else
#  define graphics_trace_begin()
#  define graphics_trace_end()
#  define graphics_trace_beginex(name)
#  define graphics_trace_endex(name)
#  define graphics_trace_mark(s)
#  define graphics_trace_printf(...)
#  define graphics_trace_async_begin(name, cookie)
#  define graphics_trace_async_end(name, cookie)
#  define graphics_trace_async_begin_track(track, name, cookie)
#  define graphics_trace_async_end_track(track, name, cookie)
#endif

#ifdef CONFIG_TRACE_HWTRACING
#  define hwtracing_trace_begin() trace_begin(NXTAG_HWTRACING)
#  define hwtracing_trace_end() trace_end(NXTAG_HWTRACING)
#  define hwtracing_trace_beginex(name) trace_beginex(NXTAG_HWTRACING, name)
#  define hwtracing_trace_endex(name) trace_endex(NXTAG_HWTRACING, name)
#  define hwtracing_trace_mark(s) trace_mark(NXTAG_HWTRACING, s)
#  define hwtracing_trace_printf(fmt, ...) \
    trace_printf(NXTAG_HWTRACING, fmt, ##__VA_ARGS__)
#  define hwtracing_trace_async_begin(name, cookie) \
    trace_async_begin(NXTAG_HWTRACING, name, cookie)
#  define hwtracing_trace_async_end(name, cookie) \
    trace_async_end(NXTAG_HWTRACING, name, cookie)
#  define hwtracing_trace_async_begin_track(track, name, cookie) \
    trace_async_begin_track(NXTAG_HWTRACING, track, name, cookie)
#  define hwtracing_trace_async_end_track(track, name, cookie) \
    trace_async_end_track(NXTAG_HWTRACING, track, name, cookie)
#else
#  define hwtracing_trace_begin()
#  define hwtracing_trace_end()
#  define hwtracing_trace_beginex(name)
#  define hwtracing_trace_endex(name)
#  define hwtracing_trace_mark(s)
#  define hwtracing_trace_printf(...)
#  define hwtracing_trace_async_begin(name, cookie)
#  define hwtracing_trace_async_end(name, cookie)
#  define hwtracing_trace_async_begin_track(track, name, cookie)
#  define hwtracing_trace_async_end_track(track, name, cookie)
#endif

#ifdef CONFIG_TRACE_I2C
#  define i2c_trace_begin() trace_begin(NXTAG_I2C)
#  define i2c_trace_end() trace_end(NXTAG_I2C)
#  define i2c_trace_beginex(name) trace_beginex(NXTAG_I2C, name)
#  define i2c_trace_endex(name) trace_endex(NXTAG_I2C, name)
#  define i2c_trace_mark(s) trace_mark(NXTAG_I2C, s)
#  define i2c_trace_printf(fmt, ...) \
    trace_printf(NXTAG_I2C, fmt, ##__VA_ARGS__)
#  define i2c_trace_async_begin(name, cookie) \
    trace_async_begin(NXTAG_I2C, name, cookie)
#  define i2c_trace_async_end(name, cookie) \
    trace_async_end(NXTAG_I2C, name, cookie)
#  define i2c_trace_async_begin_track(track, name, cookie) \
    trace_async_begin_track(NXTAG_I2C, track, name, cookie)
#  define i2c_trace_async_end_track(track, name, cookie) \
    trace_async_end_track(NXTAG_I2C, track, name, cookie)
#else
#  define i2c_trace_begin()
#  define i2c_trace_end()
#  define i2c_trace_beginex(name)
#  define i2c_trace_endex(name)
#  define i2c_trace_mark(s)
#  define i2c_trace_printf(...)
#  define i2c_trace_async_begin(name, cookie)
#  define i2c_trace_async_end(name, cookie)
#  define i2c_trace_async_begin_track(track, name, cookie)
#  define i2c_trace_async_end_track(track, name, cookie)
#endif

#ifdef CONFIG_TRACE_I2S
#  define i2s_trace_begin() trace_begin(NXTAG_I2S)
#  define i2s_trace_end() trace_end(NXTAG_I2S)
#  define i2s_trace_beginex(name) trace_beginex(NXTAG_I2S, name)
#  define i2s_trace_endex(name) trace_endex(NXTAG_I2S, name)
#  define i2s_trace_mark(s) trace_mark(NXTAG_I2S, s)
#  define i2s_trace_printf(fmt, ...) \
    trace_printf(NXTAG_I2S, fmt, ##__VA_ARGS__)
#  define i2s_trace_async_begin(name, cookie) \
    trace_async_begin(NXTAG_I2S, name, cookie)
#  define i2s_trace_async_end(name, cookie) \
    trace_async_end(NXTAG_I2S, name, cookie)
#  define i2s_trace_async_begin_track(track, name, cookie) \
    trace_async_begin_track(NXTAG_I2S, track, name, cookie)
#  define i2s_trace_async_end_track(track, name, cookie) \
    trace_async_end_track(NXTAG_I2S, track, name, cookie)
#else
#  define i2s_trace_begin()
#  define i2s_trace_end()
#  define i2s_trace_beginex(name)
#  define i2s_trace_endex(name)
#  define i2s_trace_mark(s)
#  define i2s_trace_printf(...)
#  define i2s_trace_async_begin(name, cookie)
#  define i2s_trace_async_end(name, cookie)
#  define i2s_trace_async_begin_track(track, name, cookie)
#  define i2s_trace_async_end_track(track, name, cookie)
#endif

#ifdef CONFIG_TRACE_I3C
#  define i3c_trace_begin() trace_begin(NXTAG_I3C)
#  define i3c_trace_end() trace_end(NXTAG_I3C)
#  define i3c_trace_beginex(name) trace_beginex(NXTAG_I3C, name)
#  define i3c_trace_endex(name) trace_endex(NXTAG_I3C, name)
#  define i3c_trace_mark(s) trace_mark(NXTAG_I3C, s)
#  define i3c_trace_printf(fmt, ...) \
    trace_printf(NXTAG_I3C, fmt, ##__VA_ARGS__)
#  define i3c_trace_async_begin(name, cookie) \
    trace_async_begin(NXTAG_I3C, name, cookie)
#  define i3c_trace_async_end(name, cookie) \
    trace_async_end(NXTAG_I3C, name, cookie)
#  define i3c_trace_async_begin_track(track, name, cookie) \
    trace_async_begin_track(NXTAG_I3C, track, name, cookie)
#  define i3c_trace_async_end_track(track, name, cookie) \
    trace_async_end_track(NXTAG_I3C, track, name, cookie)
#else
#  define i3c_trace_begin()
#  define i3c_trace_end()
#  define i3c_trace_beginex(name)
#  define i3c_trace_endex(name)
#  define i3c_trace_mark(s)
#  define i3c_trace_printf(...)
#  define i3c_trace_async_begin(name, cookie)
#  define i3c_trace_async_end(name, cookie)
#  define i3c_trace_async_begin_track(track, name, cookie)
#  define i3c_trace_async_end_track(track, name, cookie)
#endif
#ifdef CONFIG_TRACE_INPUT
#  define input_trace_begin() trace_begin(NXTAG_INPUT)
#  define input_trace_end() trace_end(NXTAG_INPUT)
#  define input_trace_beginex(name) trace_beginex(NXTAG_INPUT, name)
#  define input_trace_endex(name) trace_endex(NXTAG_INPUT, name)
#  define input_trace_mark(s) trace_mark(NXTAG_INPUT, s)
#  define input_trace_printf(fmt, ...) \
    trace_printf(NXTAG_INPUT, fmt, ##__VA_ARGS__)
#  define input_trace_async_begin(name, cookie) \
    trace_async_begin(NXTAG_INPUT, name, cookie)
#  define input_trace_async_end(name, cookie) \
    trace_async_end(NXTAG_INPUT, name, cookie)
#  define input_trace_async_begin_track(track, name, cookie) \
    trace_async_begin_track(NXTAG_INPUT, track, name, cookie)
#  define input_trace_async_end_track(track, name, cookie) \
    trace_async_end_track(NXTAG_INPUT, track, name, cookie)
#else
#  define input_trace_begin()
#  define input_trace_end()
#  define input_trace_beginex(name)
#  define input_trace_endex(name)
#  define input_trace_mark(s)
#  define input_trace_printf(...)
#  define input_trace_async_begin(name, cookie)
#  define input_trace_async_end(name, cookie)
#  define input_trace_async_begin_track(track, name, cookie)
#  define input_trace_async_end_track(track, name, cookie)
#endif
#ifdef CONFIG_TRACE_IPC
#  define ipc_trace_begin() trace_begin(NXTAG_IPC)
#  define ipc_trace_end() trace_end(NXTAG_IPC)
#  define ipc_trace_beginex(name) trace_beginex(NXTAG_IPC, name)
#  define ipc_trace_endex(name) trace_endex(NXTAG_IPC, name)
#  define ipc_trace_mark(s) trace_mark(NXTAG_IPC, s)
#  define ipc_trace_printf(fmt, ...) \
    trace_printf(NXTAG_IPC, fmt, ##__VA_ARGS__)
#  define ipc_trace_async_begin(name, cookie) \
    trace_async_begin(NXTAG_IPC, name, cookie)
#  define ipc_trace_async_end(name, cookie) \
    trace_async_end(NXTAG_IPC, name, cookie)
#  define ipc_trace_async_begin_track(track, name, cookie) \
    trace_async_begin_track(NXTAG_IPC, track, name, cookie)
#  define ipc_trace_async_end_track(track, name, cookie) \
    trace_async_end_track(NXTAG_IPC, track, name, cookie)
#else
#  define ipc_trace_begin()
#  define ipc_trace_end()
#  define ipc_trace_beginex(name)
#  define ipc_trace_endex(name)
#  define ipc_trace_mark(s)
#  define ipc_trace_printf(...)
#  define ipc_trace_async_begin(name, cookie)
#  define ipc_trace_async_end(name, cookie)
#  define ipc_trace_async_begin_track(track, name, cookie)
#  define ipc_trace_async_end_track(track, name, cookie)
#endif
#ifdef CONFIG_TRACE_IRQ
#  define irq_trace_begin() trace_begin(NXTAG_IRQ)
#  define irq_trace_end() trace_end(NXTAG_IRQ)
#  define irq_trace_beginex(name) trace_beginex(NXTAG_IRQ, name)
#  define irq_trace_endex(name) trace_endex(NXTAG_IRQ, name)
#  define irq_trace_mark(s) trace_mark(NXTAG_IRQ, s)
#  define irq_trace_printf(fmt, ...) \
    trace_printf(NXTAG_IRQ, fmt, ##__VA_ARGS__)
#  define irq_trace_async_begin(name, cookie) \
    trace_async_begin(NXTAG_IRQ, name, cookie)
#  define irq_trace_async_end(name, cookie) \
    trace_async_end(NXTAG_IRQ, name, cookie)
#  define irq_trace_async_begin_track(track, name, cookie) \
    trace_async_begin_track(NXTAG_IRQ, track, name, cookie)
#  define irq_trace_async_end_track(track, name, cookie) \
    trace_async_end_track(NXTAG_IRQ, track, name, cookie)
#else
#  define irq_trace_begin()
#  define irq_trace_end()
#  define irq_trace_beginex(name)
#  define irq_trace_endex(name)
#  define irq_trace_mark(s)
#  define irq_trace_printf(...)
#  define irq_trace_async_begin(name, cookie)
#  define irq_trace_async_end(name, cookie)
#  define irq_trace_async_begin_track(track, name, cookie)
#  define irq_trace_async_end_track(track, name, cookie)
#endif
#ifdef CONFIG_TRACE_LCD
#  define lcd_trace_begin() trace_begin(NXTAG_LCD)
#  define lcd_trace_end() trace_end(NXTAG_LCD)
#  define lcd_trace_beginex(name) trace_beginex(NXTAG_LCD, name)
#  define lcd_trace_endex(name) trace_endex(NXTAG_LCD, name)
#  define lcd_trace_mark(s) trace_mark(NXTAG_LCD, s)
#  define lcd_trace_printf(fmt, ...) \
    trace_printf(NXTAG_LCD, fmt, ##__VA_ARGS__)
#  define lcd_trace_async_begin(name, cookie) \
    trace_async_begin(NXTAG_LCD, name, cookie)
#  define lcd_trace_async_end(name, cookie) \
    trace_async_end(NXTAG_LCD, name, cookie)
#  define lcd_trace_async_begin_track(track, name, cookie) \
    trace_async_begin_track(NXTAG_LCD, track, name, cookie)
#  define lcd_trace_async_end_track(track, name, cookie) \
    trace_async_end_track(NXTAG_LCD, track, name, cookie)
#else
#  define lcd_trace_begin()
#  define lcd_trace_end()
#  define lcd_trace_beginex(name)
#  define lcd_trace_endex(name)
#  define lcd_trace_mark(s)
#  define lcd_trace_printf(...)
#  define lcd_trace_async_begin(name, cookie)
#  define lcd_trace_async_end(name, cookie)
#  define lcd_trace_async_begin_track(track, name, cookie)
#  define lcd_trace_async_end_track(track, name, cookie)
#endif
#ifdef CONFIG_TRACE_LEDS
#  define leds_trace_begin() trace_begin(NXTAG_LEDS)
#  define leds_trace_end() trace_end(NXTAG_LEDS)
#  define leds_trace_beginex(name) trace_beginex(NXTAG_LEDS, name)
#  define leds_trace_endex(name) trace_endex(NXTAG_LEDS, name)
#  define leds_trace_mark(s) trace_mark(NXTAG_LEDS, s)
#  define leds_trace_printf(fmt, ...) \
    trace_printf(NXTAG_LEDS, fmt, ##__VA_ARGS__)
#  define leds_trace_async_begin(name, cookie) \
    trace_async_begin(NXTAG_LEDS, name, cookie)
#  define leds_trace_async_end(name, cookie) \
    trace_async_end(NXTAG_LEDS, name, cookie)
#  define leds_trace_async_begin_track(track, name, cookie) \
    trace_async_begin_track(NXTAG_LEDS, track, name, cookie)
#  define leds_trace_async_end_track(track, name, cookie) \
    trace_async_end_track(NXTAG_LEDS, track, name, cookie)
#else
#  define leds_trace_begin()
#  define leds_trace_end()
#  define leds_trace_beginex(name)
#  define leds_trace_endex(name)
#  define leds_trace_mark(s)
#  define leds_trace_printf(...)
#  define leds_trace_async_begin(name, cookie)
#  define leds_trace_async_end(name, cookie)
#  define leds_trace_async_begin_track(track, name, cookie)
#  define leds_trace_async_end_track(track, name, cookie)
#endif
#ifdef CONFIG_TRACE_LIB
#  define lib_trace_begin() trace_begin(NXTAG_LIB)
#  define lib_trace_end() trace_end(NXTAG_LIB)
#  define lib_trace_beginex(name) trace_beginex(NXTAG_LIB, name)
#  define lib_trace_endex(name) trace_endex(NXTAG_LIB, name)
#  define lib_trace_mark(s) trace_mark(NXTAG_LIB, s)
#  define lib_trace_printf(fmt, ...) \
    trace_printf(NXTAG_LIB, fmt, ##__VA_ARGS__)
#  define lib_trace_async_begin(name, cookie) \
    trace_async_begin(NXTAG_LIB, name, cookie)
#  define lib_trace_async_end(name, cookie) \
    trace_async_end(NXTAG_LIB, name, cookie)
#  define lib_trace_async_begin_track(track, name, cookie) \
    trace_async_begin_track(NXTAG_LIB, track, name, cookie)
#  define lib_trace_async_end_track(track, name, cookie) \
    trace_async_end_track(NXTAG_LIB, track, name, cookie)
#else
#  define lib_trace_begin()
#  define lib_trace_end()
#  define lib_trace_beginex(name)
#  define lib_trace_endex(name)
#  define lib_trace_mark(s)
#  define lib_trace_printf(...)
#  define lib_trace_async_begin(name, cookie)
#  define lib_trace_async_end(name, cookie)
#  define lib_trace_async_begin_track(track, name, cookie)
#  define lib_trace_async_end_track(track, name, cookie)
#endif
#ifdef CONFIG_TRACE_MEMCARD
#  define memcard_trace_begin() trace_begin(NXTAG_MEMCARD)
#  define memcard_trace_end() trace_end(NXTAG_MEMCARD)
#  define memcard_trace_beginex(name) trace_beginex(NXTAG_MEMCARD, name)
#  define memcard_trace_endex(name) trace_endex(NXTAG_MEMCARD, name)
#  define memcard_trace_mark(s) trace_mark(NXTAG_MEMCARD, s)
#  define memcard_trace_printf(fmt, ...) \
    trace_printf(NXTAG_MEMCARD, fmt, ##__VA_ARGS__)
#  define memcard_trace_async_begin(name, cookie) \
    trace_async_begin(NXTAG_MEMCARD, name, cookie)
#  define memcard_trace_async_end(name, cookie) \
    trace_async_end(NXTAG_MEMCARD, name, cookie)
#  define memcard_trace_async_begin_track(track, name, cookie) \
    trace_async_begin_track(NXTAG_MEMCARD, track, name, cookie)
#  define memcard_trace_async_end_track(track, name, cookie) \
    trace_async_end_track(NXTAG_MEMCARD, track, name, cookie)
#else
#  define memcard_trace_begin()
#  define memcard_trace_end()
#  define memcard_trace_beginex(name)
#  define memcard_trace_endex(name)
#  define memcard_trace_mark(s)
#  define memcard_trace_printf(...)
#  define memcard_trace_async_begin(name, cookie)
#  define memcard_trace_async_end(name, cookie)
#  define memcard_trace_async_begin_track(track, name, cookie)
#  define memcard_trace_async_end_track(track, name, cookie)
#endif
#ifdef CONFIG_TRACE_MM
#  define mm_trace_begin() trace_begin(NXTAG_MM)
#  define mm_trace_end() trace_end(NXTAG_MM)
#  define mm_trace_beginex(name) trace_beginex(NXTAG_MM, name)
#  define mm_trace_endex(name) trace_endex(NXTAG_MM, name)
#  define mm_trace_mark(s) trace_mark(NXTAG_MM, s)
#  define mm_trace_printf(fmt, ...) \
    trace_printf(NXTAG_MM, fmt, ##__VA_ARGS__)
#  define mm_trace_async_begin(name, cookie) \
    trace_async_begin(NXTAG_MM, name, cookie)
#  define mm_trace_async_end(name, cookie) \
    trace_async_end(NXTAG_MM, name, cookie)
#  define mm_trace_async_begin_track(track, name, cookie) \
    trace_async_begin_track(NXTAG_MM, track, name, cookie)
#  define mm_trace_async_end_track(track, name, cookie) \
    trace_async_end_track(NXTAG_MM, track, name, cookie)
#else
#  define mm_trace_begin()
#  define mm_trace_end()
#  define mm_trace_beginex(name)
#  define mm_trace_endex(name)
#  define mm_trace_mark(s)
#  define mm_trace_printf(...)
#  define mm_trace_async_begin(name, cookie)
#  define mm_trace_async_end(name, cookie)
#  define mm_trace_async_begin_track(track, name, cookie)
#  define mm_trace_async_end_track(track, name, cookie)
#endif

#ifdef CONFIG_TRACE_MOTOR
#  define motor_trace_begin() trace_begin(NXTAG_MOTOR)
#  define motor_trace_end() trace_end(NXTAG_MOTOR)
#  define motor_trace_beginex(name) trace_beginex(NXTAG_MOTOR, name)
#  define motor_trace_endex(name) trace_endex(NXTAG_MOTOR, name)
#  define motor_trace_mark(s) trace_mark(NXTAG_MOTOR, s)
#  define motor_trace_printf(fmt, ...) \
    trace_printf(NXTAG_MOTOR, fmt, ##__VA_ARGS__)
#  define motor_trace_async_begin(name, cookie) \
    trace_async_begin(NXTAG_MOTOR, name, cookie)
#  define motor_trace_async_end(name, cookie) \
    trace_async_end(NXTAG_MOTOR, name, cookie)
#  define motor_trace_async_begin_track(track, name, cookie) \
    trace_async_begin_track(NXTAG_MOTOR, track, name, cookie)
#  define motor_trace_async_end_track(track, name, cookie) \
    trace_async_end_track(NXTAG_MOTOR, track, name, cookie)
#else
#  define motor_trace_begin()
#  define motor_trace_end()
#  define motor_trace_beginex(name)
#  define motor_trace_endex(name)
#  define motor_trace_mark(s)
#  define motor_trace_printf(...)
#  define motor_trace_async_begin(name, cookie)
#  define motor_trace_async_end(name, cookie)
#  define motor_trace_async_begin_track(track, name, cookie)
#  define motor_trace_async_end_track(track, name, cookie)
#endif

#ifdef CONFIG_TRACE_NET
#  define net_trace_begin() trace_begin(NXTAG_NET)
#  define net_trace_end() trace_end(NXTAG_NET)
#  define net_trace_beginex(name) trace_beginex(NXTAG_NET, name)
#  define net_trace_endex(name) trace_endex(NXTAG_NET, name)
#  define net_trace_mark(s) trace_mark(NXTAG_NET, s)
#  define net_trace_printf(fmt, ...) \
    trace_printf(NXTAG_NET, fmt, ##__VA_ARGS__)
#  define net_trace_async_begin(name, cookie) \
    trace_async_begin(NXTAG_NET, name, cookie)
#  define net_trace_async_end(name, cookie) \
    trace_async_end(NXTAG_NET, name, cookie)
#  define net_trace_async_begin_track(track, name, cookie) \
    trace_async_begin_track(NXTAG_NET, track, name, cookie)
#  define net_trace_async_end_track(track, name, cookie) \
    trace_async_end_track(NXTAG_NET, track, name, cookie)
#else
#  define net_trace_begin()
#  define net_trace_end()
#  define net_trace_beginex(name)
#  define net_trace_endex(name)
#  define net_trace_mark(s)
#  define net_trace_printf(...)
#  define net_trace_async_begin(name, cookie)
#  define net_trace_async_end(name, cookie)
#  define net_trace_async_begin_track(track, name, cookie)
#  define net_trace_async_end_track(track, name, cookie)
#endif

#ifdef CONFIG_TRACE_PAGING
#  define paging_trace_begin() trace_begin(NXTAG_PAGING)
#  define paging_trace_end() trace_end(NXTAG_PAGING)
#  define paging_trace_beginex(name) trace_beginex(NXTAG_PAGING, name)
#  define paging_trace_endex(name) trace_endex(NXTAG_PAGING, name)
#  define paging_trace_mark(s) trace_mark(NXTAG_PAGING, s)
#  define paging_trace_printf(fmt, ...) \
    trace_printf(NXTAG_PAGING, fmt, ##__VA_ARGS__)
#  define paging_trace_async_begin(name, cookie) \
    trace_async_begin(NXTAG_PAGING, name, cookie)
#  define paging_trace_async_end(name, cookie) \
    trace_async_end(NXTAG_PAGING, name, cookie)
#  define paging_trace_async_begin_track(track, name, cookie) \
    trace_async_begin_track(NXTAG_PAGING, track, name, cookie)
#  define paging_trace_async_end_track(track, name, cookie) \
    trace_async_end_track(NXTAG_PAGING, track, name, cookie)
#else
#  define paging_trace_begin()
#  define paging_trace_end()
#  define paging_trace_beginex(name)
#  define paging_trace_endex(name)
#  define paging_trace_mark(s)
#  define paging_trace_printf(...)
#  define paging_trace_async_begin(name, cookie)
#  define paging_trace_async_end(name, cookie)
#  define paging_trace_async_begin_track(track, name, cookie)
#  define paging_trace_async_end_track(track, name, cookie)
#endif

#ifdef CONFIG_TRACE_PCI
#  define pci_trace_begin() trace_begin(NXTAG_PCI)
#  define pci_trace_end() trace_end(NXTAG_PCI)
#  define pci_trace_beginex(name) trace_beginex(NXTAG_PCI, name)
#  define pci_trace_endex(name) trace_endex(NXTAG_PCI, name)
#  define pci_trace_mark(s) trace_mark(NXTAG_PCI, s)
#  define pci_trace_printf(fmt, ...) \
    trace_printf(NXTAG_PCI, fmt, ##__VA_ARGS__)
#  define pci_trace_async_begin(name, cookie) \
    trace_async_begin(NXTAG_PCI, name, cookie)
#  define pci_trace_async_end(name, cookie) \
    trace_async_end(NXTAG_PCI, name, cookie)
#  define pci_trace_async_begin_track(track, name, cookie) \
    trace_async_begin_track(NXTAG_PCI, track, name, cookie)
#  define pci_trace_async_end_track(track, name, cookie) \
    trace_async_end_track(NXTAG_PCI, track, name, cookie)
#else
#  define pci_trace_begin()
#  define pci_trace_end()
#  define pci_trace_beginex(name)
#  define pci_trace_endex(name)
#  define pci_trace_mark(s)
#  define pci_trace_printf(...)
#  define pci_trace_async_begin(name, cookie)
#  define pci_trace_async_end(name, cookie)
#  define pci_trace_async_begin_track(track, name, cookie)
#  define pci_trace_async_end_track(track, name, cookie)
#endif

#ifdef CONFIG_TRACE_POWER
#  define power_trace_begin() trace_begin(NXTAG_POWER)
#  define power_trace_end() trace_end(NXTAG_POWER)
#  define power_trace_beginex(name) trace_beginex(NXTAG_POWER, name)
#  define power_trace_endex(name) trace_endex(NXTAG_POWER, name)
#  define power_trace_mark(s) trace_mark(NXTAG_POWER, s)
#  define power_trace_printf(fmt, ...) \
    trace_printf(NXTAG_POWER, fmt, ##__VA_ARGS__)
#  define power_trace_async_begin(name, cookie) \
    trace_async_begin(NXTAG_POWER, name, cookie)
#  define power_trace_async_end(name, cookie) \
    trace_async_end(NXTAG_POWER, name, cookie)
#  define power_trace_async_begin_track(track, name, cookie) \
    trace_async_begin_track(NXTAG_POWER, track, name, cookie)
#  define power_trace_async_end_track(track, name, cookie) \
    trace_async_end_track(NXTAG_POWER, track, name, cookie)
#else
#  define power_trace_begin()
#  define power_trace_end()
#  define power_trace_beginex(name)
#  define power_trace_endex(name)
#  define power_trace_mark(s)
#  define power_trace_printf(...)
#  define power_trace_async_begin(name, cookie)
#  define power_trace_async_end(name, cookie)
#  define power_trace_async_begin_track(track, name, cookie)
#  define power_trace_async_end_track(track, name, cookie)
#endif

#ifdef CONFIG_TRACE_PTP
#  define ptp_trace_begin() trace_begin(NXTAG_PTP)
#  define ptp_trace_end() trace_end(NXTAG_PTP)
#  define ptp_trace_beginex(name) trace_beginex(NXTAG_PTP, name)
#  define ptp_trace_endex(name) trace_endex(NXTAG_PTP, name)
#  define ptp_trace_mark(s) trace_mark(NXTAG_PTP, s)
#  define ptp_trace_printf(fmt, ...) \
    trace_printf(NXTAG_PTP, fmt, ##__VA_ARGS__)
#  define ptp_trace_async_begin(name, cookie) \
    trace_async_begin(NXTAG_PTP, name, cookie)
#  define ptp_trace_async_end(name, cookie) \
    trace_async_end(NXTAG_PTP, name, cookie)
#  define ptp_trace_async_begin_track(track, name, cookie) \
    trace_async_begin_track(NXTAG_PTP, track, name, cookie)
#  define ptp_trace_async_end_track(track, name, cookie) \
    trace_async_end_track(NXTAG_PTP, track, name, cookie)
#else
#  define ptp_trace_begin()
#  define ptp_trace_end()
#  define ptp_trace_beginex(name)
#  define ptp_trace_endex(name)
#  define ptp_trace_mark(s)
#  define ptp_trace_printf(...)
#  define ptp_trace_async_begin(name, cookie)
#  define ptp_trace_async_end(name, cookie)
#  define ptp_trace_async_begin_track(track, name, cookie)
#  define ptp_trace_async_end_track(track, name, cookie)
#endif

#ifdef CONFIG_TRACE_PWM
#  define pwm_trace_begin() trace_begin(NXTAG_PWM)
#  define pwm_trace_end() trace_end(NXTAG_PWM)
#  define pwm_trace_beginex(name) trace_beginex(NXTAG_PWM, name)
#  define pwm_trace_endex(name) trace_endex(NXTAG_PWM, name)
#  define pwm_trace_mark(s) trace_mark(NXTAG_PWM, s)
#  define pwm_trace_printf(fmt, ...) \
    trace_printf(NXTAG_PWM, fmt, ##__VA_ARGS__)
#  define pwm_trace_async_begin(name, cookie) \
    trace_async_begin(NXTAG_PWM, name, cookie)
#  define pwm_trace_async_end(name, cookie) \
    trace_async_end(NXTAG_PWM, name, cookie)
#  define pwm_trace_async_begin_track(track, name, cookie) \
    trace_async_begin_track(NXTAG_PWM, track, name, cookie)
#  define pwm_trace_async_end_track(track, name, cookie) \
    trace_async_end_track(NXTAG_PWM, track, name, cookie)
#else
#  define pwm_trace_begin()
#  define pwm_trace_end()
#  define pwm_trace_beginex(name)
#  define pwm_trace_endex(name)
#  define pwm_trace_mark(s)
#  define pwm_trace_printf(...)
#  define pwm_trace_async_begin(name, cookie)
#  define pwm_trace_async_end(name, cookie)
#  define pwm_trace_async_begin_track(track, name, cookie)
#  define pwm_trace_async_end_track(track, name, cookie)
#endif

#ifdef CONFIG_TRACE_RC
#  define rc_trace_begin() trace_begin(NXTAG_RC)
#  define rc_trace_end() trace_end(NXTAG_RC)
#  define rc_trace_beginex(name) trace_beginex(NXTAG_RC, name)
#  define rc_trace_endex(name) trace_endex(NXTAG_RC, name)
#  define rc_trace_mark(s) trace_mark(NXTAG_RC, s)
#  define rc_trace_printf(fmt, ...) \
    trace_printf(NXTAG_RC, fmt, ##__VA_ARGS__)
#  define rc_trace_async_begin(name, cookie) \
    trace_async_begin(NXTAG_RC, name, cookie)
#  define rc_trace_async_end(name, cookie) \
    trace_async_end(NXTAG_RC, name, cookie)
#  define rc_trace_async_begin_track(track, name, cookie) \
    trace_async_begin_track(NXTAG_RC, track, name, cookie)
#  define rc_trace_async_end_track(track, name, cookie) \
    trace_async_end_track(NXTAG_RC, track, name, cookie)
#else
#  define rc_trace_begin()
#  define rc_trace_end()
#  define rc_trace_beginex(name)
#  define rc_trace_endex(name)
#  define rc_trace_mark(s)
#  define rc_trace_printf(...)
#  define rc_trace_async_begin(name, cookie)
#  define rc_trace_async_end(name, cookie)
#  define rc_trace_async_begin_track(track, name, cookie)
#  define rc_trace_async_end_track(track, name, cookie)
#endif

#ifdef CONFIG_TRACE_REGMAP
#  define regmap_trace_begin() trace_begin(NXTAG_REGMAP)
#  define regmap_trace_end() trace_end(NXTAG_REGMAP)
#  define regmap_trace_beginex(name) trace_beginex(NXTAG_REGMAP, name)
#  define regmap_trace_endex(name) trace_endex(NXTAG_REGMAP, name)
#  define regmap_trace_mark(s) trace_mark(NXTAG_REGMAP, s)
#  define regmap_trace_printf(fmt, ...) \
    trace_printf(NXTAG_REGMAP, fmt, ##__VA_ARGS__)
#  define regmap_trace_async_begin(name, cookie) \
    trace_async_begin(NXTAG_REGMAP, name, cookie)
#  define regmap_trace_async_end(name, cookie) \
    trace_async_end(NXTAG_REGMAP, name, cookie)
#  define regmap_trace_async_begin_track(track, name, cookie) \
    trace_async_begin_track(NXTAG_REGMAP, track, name, cookie)
#  define regmap_trace_async_end_track(track, name, cookie) \
    trace_async_end_track(NXTAG_REGMAP, track, name, cookie)
#else
#  define regmap_trace_begin()
#  define regmap_trace_end()
#  define regmap_trace_beginex(name)
#  define regmap_trace_endex(name)
#  define regmap_trace_mark(s)
#  define regmap_trace_printf(...)
#  define regmap_trace_async_begin(name, cookie)
#  define regmap_trace_async_end(name, cookie)
#  define regmap_trace_async_begin_track(track, name, cookie)
#  define regmap_trace_async_end_track(track, name, cookie)
#endif

#ifdef CONFIG_TRACE_RESET
#  define reset_trace_begin() trace_begin(NXTAG_RESET)
#  define reset_trace_end() trace_end(NXTAG_RESET)
#  define reset_trace_beginex(name) trace_beginex(NXTAG_RESET, name)
#  define reset_trace_endex(name) trace_endex(NXTAG_RESET, name)
#  define reset_trace_mark(s) trace_mark(NXTAG_RESET, s)
#  define reset_trace_printf(fmt, ...) \
    trace_printf(NXTAG_RESET, fmt, ##__VA_ARGS__)
#  define reset_trace_async_begin(name, cookie) \
    trace_async_begin(NXTAG_RESET, name, cookie)
#  define reset_trace_async_end(name, cookie) \
    trace_async_end(NXTAG_RESET, name, cookie)
#  define reset_trace_async_begin_track(track, name, cookie) \
    trace_async_begin_track(NXTAG_RESET, track, name, cookie)
#  define reset_trace_async_end_track(track, name, cookie) \
    trace_async_end_track(NXTAG_RESET, track, name, cookie)
#else
#  define reset_trace_begin()
#  define reset_trace_end()
#  define reset_trace_beginex(name)
#  define reset_trace_endex(name)
#  define reset_trace_mark(s)
#  define reset_trace_printf(...)
#  define reset_trace_async_begin(name, cookie)
#  define reset_trace_async_end(name, cookie)
#  define reset_trace_async_begin_track(track, name, cookie)
#  define reset_trace_async_end_track(track, name, cookie)
#endif

#ifdef CONFIG_TRACE_RMT
#  define rmt_trace_begin() trace_begin(NXTAG_RMT)
#  define rmt_trace_end() trace_end(NXTAG_RMT)
#  define rmt_trace_beginex(name) trace_beginex(NXTAG_RMT, name)
#  define rmt_trace_endex(name) trace_endex(NXTAG_RMT, name)
#  define rmt_trace_mark(s) trace_mark(NXTAG_RMT, s)
#  define rmt_trace_printf(fmt, ...) \
    trace_printf(NXTAG_RMT, fmt, ##__VA_ARGS__)
#  define rmt_trace_async_begin(name, cookie) \
    trace_async_begin(NXTAG_RMT, name, cookie)
#  define rmt_trace_async_end(name, cookie) \
    trace_async_end(NXTAG_RMT, name, cookie)
#  define rmt_trace_async_begin_track(track, name, cookie) \
    trace_async_begin_track(NXTAG_RMT, track, name, cookie)
#  define rmt_trace_async_end_track(track, name, cookie) \
    trace_async_end_track(NXTAG_RMT, track, name, cookie)
#else
#  define rmt_trace_begin()
#  define rmt_trace_end()
#  define rmt_trace_beginex(name)
#  define rmt_trace_endex(name)
#  define rmt_trace_mark(s)
#  define rmt_trace_printf(...)
#  define rmt_trace_async_begin(name, cookie)
#  define rmt_trace_async_end(name, cookie)
#  define rmt_trace_async_begin_track(track, name, cookie)
#  define rmt_trace_async_end_track(track, name, cookie)
#endif

#ifdef CONFIG_TRACE_RPMSG
#  define rpmsg_trace_begin() trace_begin(NXTAG_RPMSG)
#  define rpmsg_trace_end() trace_end(NXTAG_RPMSG)
#  define rpmsg_trace_beginex(name) trace_beginex(NXTAG_RPMSG, name)
#  define rpmsg_trace_endex(name) trace_endex(NXTAG_RPMSG, name)
#  define rpmsg_trace_mark(s) trace_mark(NXTAG_RPMSG, s)
#  define rpmsg_trace_printf(fmt, ...) \
    trace_printf(NXTAG_RPMSG, fmt, ##__VA_ARGS__)
#  define rpmsg_trace_async_begin(name, cookie) \
    trace_async_begin(NXTAG_RPMSG, name, cookie)
#  define rpmsg_trace_async_end(name, cookie) \
    trace_async_end(NXTAG_RPMSG, name, cookie)
#  define rpmsg_trace_async_begin_track(track, name, cookie) \
    trace_async_begin_track(NXTAG_RPMSG, track, name, cookie)
#  define rpmsg_trace_async_end_track(track, name, cookie) \
    trace_async_end_track(NXTAG_RPMSG, track, name, cookie)
#else
#  define rpmsg_trace_begin()
#  define rpmsg_trace_end()
#  define rpmsg_trace_beginex(name)
#  define rpmsg_trace_endex(name)
#  define rpmsg_trace_mark(s)
#  define rpmsg_trace_printf(...)
#  define rpmsg_trace_async_begin(name, cookie)
#  define rpmsg_trace_async_end(name, cookie)
#  define rpmsg_trace_async_begin_track(track, name, cookie)
#  define rpmsg_trace_async_end_track(track, name, cookie)
#endif

#ifdef CONFIG_TRACE_RPTUN
#  define rptun_trace_begin() trace_begin(NXTAG_RPTUN)
#  define rptun_trace_end() trace_end(NXTAG_RPTUN)
#  define rptun_trace_beginex(name) trace_beginex(NXTAG_RPTUN, name)
#  define rptun_trace_endex(name) trace_endex(NXTAG_RPTUN, name)
#  define rptun_trace_mark(s) trace_mark(NXTAG_RPTUN, s)
#  define rptun_trace_printf(fmt, ...) \
    trace_printf(NXTAG_RPTUN, fmt, ##__VA_ARGS__)
#  define rptun_trace_async_begin(name, cookie) \
    trace_async_begin(NXTAG_RPTUN, name, cookie)
#  define rptun_trace_async_end(name, cookie) \
    trace_async_end(NXTAG_RPTUN, name, cookie)
#  define rptun_trace_async_begin_track(track, name, cookie) \
    trace_async_begin_track(NXTAG_RPTUN, track, name, cookie)
#  define rptun_trace_async_end_track(track, name, cookie) \
    trace_async_end_track(NXTAG_RPTUN, track, name, cookie)
#else
#  define rptun_trace_begin()
#  define rptun_trace_end()
#  define rptun_trace_beginex(name)
#  define rptun_trace_endex(name)
#  define rptun_trace_mark(s)
#  define rptun_trace_printf(...)
#  define rptun_trace_async_begin(name, cookie)
#  define rptun_trace_async_end(name, cookie)
#  define rptun_trace_async_begin_track(track, name, cookie)
#  define rptun_trace_async_end_track(track, name, cookie)
#endif

#ifdef CONFIG_TRACE_RTC
#  define rtc_trace_begin() trace_begin(NXTAG_RTC)
#  define rtc_trace_end() trace_end(NXTAG_RTC)
#  define rtc_trace_beginex(name) trace_beginex(NXTAG_RTC, name)
#  define rtc_trace_endex(name) trace_endex(NXTAG_RTC, name)
#  define rtc_trace_mark(s) trace_mark(NXTAG_RTC, s)
#  define rtc_trace_printf(fmt, ...) \
    trace_printf(NXTAG_RTC, fmt, ##__VA_ARGS__)
#  define rtc_trace_async_begin(name, cookie) \
    trace_async_begin(NXTAG_RTC, name, cookie)
#  define rtc_trace_async_end(name, cookie) \
    trace_async_end(NXTAG_RTC, name, cookie)
#  define rtc_trace_async_begin_track(track, name, cookie) \
    trace_async_begin_track(NXTAG_RTC, track, name, cookie)
#  define rtc_trace_async_end_track(track, name, cookie) \
    trace_async_end_track(NXTAG_RTC, track, name, cookie)
#else
#  define rtc_trace_begin()
#  define rtc_trace_end()
#  define rtc_trace_beginex(name)
#  define rtc_trace_endex(name)
#  define rtc_trace_mark(s)
#  define rtc_trace_printf(...)
#  define rtc_trace_async_begin(name, cookie)
#  define rtc_trace_async_end(name, cookie)
#  define rtc_trace_async_begin_track(track, name, cookie)
#  define rtc_trace_async_end_track(track, name, cookie)
#endif

#ifdef CONFIG_TRACE_SAFETY
#  define safety_trace_begin() trace_begin(NXTAG_SAFETY)
#  define safety_trace_end() trace_end(NXTAG_SAFETY)
#  define safety_trace_beginex(name) trace_beginex(NXTAG_SAFETY, name)
#  define safety_trace_endex(name) trace_endex(NXTAG_SAFETY, name)
#  define safety_trace_mark(s) trace_mark(NXTAG_SAFETY, s)
#  define safety_trace_printf(fmt, ...) \
    trace_printf(NXTAG_SAFETY, fmt, ##__VA_ARGS__)
#  define safety_trace_async_begin(name, cookie) \
    trace_async_begin(NXTAG_SAFETY, name, cookie)
#  define safety_trace_async_end(name, cookie) \
    trace_async_end(NXTAG_SAFETY, name, cookie)
#  define safety_trace_async_begin_track(track, name, cookie) \
    trace_async_begin_track(NXTAG_SAFETY, track, name, cookie)
#  define safety_trace_async_end_track(track, name, cookie) \
    trace_async_end_track(NXTAG_SAFETY, track, name, cookie)
#else
#  define safety_trace_begin()
#  define safety_trace_end()
#  define safety_trace_beginex(name)
#  define safety_trace_endex(name)
#  define safety_trace_mark(s)
#  define safety_trace_printf(...)
#  define safety_trace_async_begin(name, cookie)
#  define safety_trace_async_end(name, cookie)
#  define safety_trace_async_begin_track(track, name, cookie)
#  define safety_trace_async_end_track(track, name, cookie)
#endif

#ifdef CONFIG_TRACE_SCHED
#  define sched_trace_begin() trace_begin(NXTAG_SCHED)
#  define sched_trace_end() trace_end(NXTAG_SCHED)
#  define sched_trace_beginex(name) trace_beginex(NXTAG_SCHED, name)
#  define sched_trace_endex(name) trace_endex(NXTAG_SCHED, name)
#  define sched_trace_mark(s) trace_mark(NXTAG_SCHED, s)
#  define sched_trace_printf(fmt, ...) \
    trace_printf(NXTAG_SCHED, fmt, ##__VA_ARGS__)
#  define sched_trace_async_begin(name, cookie) \
    trace_async_begin(NXTAG_SCHED, name, cookie)
#  define sched_trace_async_end(name, cookie) \
    trace_async_end(NXTAG_SCHED, name, cookie)
#  define sched_trace_async_begin_track(track, name, cookie) \
    trace_async_begin_track(NXTAG_SCHED, track, name, cookie)
#  define sched_trace_async_end_track(track, name, cookie) \
    trace_async_end_track(NXTAG_SCHED, track, name, cookie)
#else
#  define sched_trace_begin()
#  define sched_trace_end()
#  define sched_trace_beginex(name)
#  define sched_trace_endex(name)
#  define sched_trace_mark(s)
#  define sched_trace_printf(...)
#  define sched_trace_async_begin(name, cookie)
#  define sched_trace_async_end(name, cookie)
#  define sched_trace_async_begin_track(track, name, cookie)
#  define sched_trace_async_end_track(track, name, cookie)
#endif

#ifdef CONFIG_TRACE_SENSORS
#  define sensor_trace_begin() trace_begin(NXTAG_SENSORS)
#  define sensor_trace_end() trace_end(NXTAG_SENSORS)
#  define sensor_trace_beginex(name) trace_beginex(NXTAG_SENSORS, name)
#  define sensor_trace_endex(name) trace_endex(NXTAG_SENSORS, name)
#  define sensor_trace_mark(s) trace_mark(NXTAG_SENSORS, s)
#  define sensor_trace_printf(fmt, ...) \
    trace_printf(NXTAG_SENSORS, fmt, ##__VA_ARGS__)
#  define sensor_trace_async_begin(name, cookie) \
    trace_async_begin(NXTAG_SENSORS, name, cookie)
#  define sensor_trace_async_end(name, cookie) \
    trace_async_end(NXTAG_SENSORS, name, cookie)
#  define sensor_trace_async_begin_track(track, name, cookie) \
    trace_async_begin_track(NXTAG_SENSORS, track, name, cookie)
#  define sensor_trace_async_end_track(track, name, cookie) \
    trace_async_end_track(NXTAG_SENSORS, track, name, cookie)
#else
#  define sensor_trace_end()
#  define sensor_trace_beginex(name)
#  define sensor_trace_endex(name)
#  define sensor_trace_mark(s)
#  define sensor_trace_printf(...)
#  define sensor_trace_async_begin(name, cookie)
#  define sensor_trace_async_end(name, cookie)
#  define sensor_trace_async_begin_track(track, name, cookie)
#  define sensor_trace_async_end_track(track, name, cookie)
#endif

#ifdef CONFIG_TRACE_SPI
#  define spi_trace_begin() trace_begin(NXTAG_SPI)
#  define spi_trace_end() trace_end(NXTAG_SPI)
#  define spi_trace_beginex(name) trace_beginex(NXTAG_SPI, name)
#  define spi_trace_endex(name) trace_endex(NXTAG_SPI, name)
#  define spi_trace_mark(s) trace_mark(NXTAG_SPI, s)
#  define spi_trace_printf(fmt, ...) \
    trace_printf(NXTAG_SPI, fmt, ##__VA_ARGS__)
#  define spi_trace_async_begin(name, cookie) \
    trace_async_begin(NXTAG_SPI, name, cookie)
#  define spi_trace_async_end(name, cookie) \
    trace_async_end(NXTAG_SPI, name, cookie)
#  define spi_trace_async_begin_track(track, name, cookie) \
    trace_async_begin_track(NXTAG_SPI, track, name, cookie)
#  define spi_trace_async_end_track(track, name, cookie) \
    trace_async_end_track(NXTAG_SPI, track, name, cookie)
#else
#  define spi_trace_begin()
#  define spi_trace_end()
#  define spi_trace_beginex(name)
#  define spi_trace_endex(name)
#  define spi_trace_mark(s)
#  define spi_trace_printf(...)
#  define spi_trace_async_begin(name, cookie)
#  define spi_trace_async_end(name, cookie)
#  define spi_trace_async_begin_track(track, name, cookie)
#  define spi_trace_async_end_track(track, name, cookie)
#endif

#ifdef CONFIG_TRACE_STEPPER
#  define stepper_trace_begin() trace_begin(NXTAG_STEPPER)
#  define stepper_trace_end() trace_end(NXTAG_STEPPER)
#  define stepper_trace_beginex(name) trace_beginex(NXTAG_STEPPER, name)
#  define stepper_trace_endex(name) trace_endex(NXTAG_STEPPER, name)
#  define stepper_trace_mark(s) trace_mark(NXTAG_STEPPER, s)
#  define stepper_trace_printf(fmt, ...) \
    trace_printf(NXTAG_STEPPER, fmt, ##__VA_ARGS__)
#  define stepper_trace_async_begin(name, cookie) \
    trace_async_begin(NXTAG_STEPPER, name, cookie)
#  define stepper_trace_async_end(name, cookie) \
    trace_async_end(NXTAG_STEPPER, name, cookie)
#  define stepper_trace_async_begin_track(track, name, cookie) \
    trace_async_begin_track(NXTAG_STEPPER, track, name, cookie)
#  define stepper_trace_async_end_track(track, name, cookie) \
    trace_async_end_track(NXTAG_STEPPER, track, name, cookie)
#else
#  define stepper_trace_begin()
#  define stepper_trace_end()
#  define stepper_trace_beginex(name)
#  define stepper_trace_endex(name)
#  define stepper_trace_mark(s)
#  define stepper_trace_printf(...)
#  define stepper_trace_async_begin(name, cookie)
#  define stepper_trace_async_end(name, cookie)
#  define stepper_trace_async_begin_track(track, name, cookie)
#  define stepper_trace_async_end_track(track, name, cookie)
#endif

#ifdef CONFIG_TRACE_SYSCALL
#  define syscall_trace_begin() trace_begin(NXTAG_SYSCALL)
#  define syscall_trace_end() trace_end(NXTAG_SYSCALL)
#  define syscall_trace_beginex(name) trace_beginex(NXTAG_SYSCALL, name)
#  define syscall_trace_endex(name) trace_endex(NXTAG_SYSCALL, name)
#  define syscall_trace_mark(s) trace_mark(NXTAG_SYSCALL, s)
#  define syscall_trace_printf(fmt, ...) \
    trace_printf(NXTAG_SYSCALL, fmt, ##__VA_ARGS__)
#  define syscall_trace_async_begin(name, cookie) \
    trace_async_begin(NXTAG_SYSCALL, name, cookie)
#  define syscall_trace_async_end(name, cookie) \
    trace_async_end(NXTAG_SYSCALL, name, cookie)
#  define syscall_trace_async_begin_track(track, name, cookie) \
    trace_async_begin_track(NXTAG_SYSCALL, track, name, cookie)
#  define syscall_trace_async_end_track(track, name, cookie) \
    trace_async_end_track(NXTAG_SYSCALL, track, name, cookie)
#else
#  define syscall_trace_begin()
#  define syscall_trace_end()
#  define syscall_trace_beginex(name)
#  define syscall_trace_endex(name)
#  define syscall_trace_mark(s)
#  define syscall_trace_printf(...)
#  define syscall_trace_async_begin(name, cookie)
#  define syscall_trace_async_end(name, cookie)
#  define syscall_trace_async_begin_track(track, name, cookie)
#  define syscall_trace_async_end_track(track, name, cookie)
#endif

#ifdef CONFIG_TRACE_TEE
#  define tee_trace_begin() trace_begin(NXTAG_TEE)
#  define tee_trace_end() trace_end(NXTAG_TEE)
#  define tee_trace_beginex(name) trace_beginex(NXTAG_TEE, name)
#  define tee_trace_endex(name) trace_endex(NXTAG_TEE, name)
#  define tee_trace_mark(s) trace_mark(NXTAG_TEE, s)
#  define tee_trace_printf(fmt, ...) \
    trace_printf(NXTAG_TEE, fmt, ##__VA_ARGS__)
#  define tee_trace_async_begin(name, cookie) \
    trace_async_begin(NXTAG_TEE, name, cookie)
#  define tee_trace_async_end(name, cookie) \
    trace_async_end(NXTAG_TEE, name, cookie)
#  define tee_trace_async_begin_track(track, name, cookie) \
    trace_async_begin_track(NXTAG_TEE, track, name, cookie)
#  define tee_trace_async_end_track(track, name, cookie) \
    trace_async_end_track(NXTAG_TEE, track, name, cookie)
#else
#  define tee_trace_begin()
#  define tee_trace_end()
#  define tee_trace_beginex(name)
#  define tee_trace_endex(name)
#  define tee_trace_mark(s)
#  define tee_trace_printf(...)
#  define tee_trace_async_begin(name, cookie)
#  define tee_trace_async_end(name, cookie)
#  define tee_trace_async_begin_track(track, name, cookie)
#  define tee_trace_async_end_track(track, name, cookie)
#endif

#ifdef CONFIG_TRACE_THERMAL
#  define thermal_trace_begin() trace_begin(NXTAG_THERMAL)
#  define thermal_trace_end() trace_end(NXTAG_THERMAL)
#  define thermal_trace_beginex(name) trace_beginex(NXTAG_THERMAL, name)
#  define thermal_trace_endex(name) trace_endex(NXTAG_THERMAL, name)
#  define thermal_trace_mark(s) trace_mark(NXTAG_THERMAL, s)
#  define thermal_trace_printf(fmt, ...) \
    trace_printf(NXTAG_THERMAL, fmt, ##__VA_ARGS__)
#  define thermal_trace_async_begin(name, cookie) \
    trace_async_begin(NXTAG_THERMAL, name, cookie)
#  define thermal_trace_async_end(name, cookie) \
    trace_async_end(NXTAG_THERMAL, name, cookie)
#  define thermal_trace_async_begin_track(track, name, cookie) \
    trace_async_begin_track(NXTAG_THERMAL, track, name, cookie)
#  define thermal_trace_async_end_track(track, name, cookie) \
    trace_async_end_track(NXTAG_THERMAL, track, name, cookie)
#else
#  define thermal_trace_begin()
#  define thermal_trace_end()
#  define thermal_trace_beginex(name)
#  define thermal_trace_endex(name)
#  define thermal_trace_mark(s)
#  define thermal_trace_printf(...)
#  define thermal_trace_async_begin(name, cookie)
#  define thermal_trace_async_end(name, cookie)
#  define thermal_trace_async_begin_track(track, name, cookie)
#  define thermal_trace_async_end_track(track, name, cookie)
#endif

#ifdef CONFIG_TRACE_TIMER
#  define timer_trace_begin() trace_begin(NXTAG_TIMER)
#  define timer_trace_end() trace_end(NXTAG_TIMER)
#  define timer_trace_beginex(name) trace_beginex(NXTAG_TIMER, name)
#  define timer_trace_endex(name) trace_endex(NXTAG_TIMER, name)
#  define timer_trace_mark(s) trace_mark(NXTAG_TIMER, s)
#  define timer_trace_printf(fmt, ...) \
    trace_printf(NXTAG_TIMER, fmt, ##__VA_ARGS__)
#  define timer_trace_async_begin(name, cookie) \
    trace_async_begin(NXTAG_TIMER, name, cookie)
#  define timer_trace_async_end(name, cookie) \
    trace_async_end(NXTAG_TIMER, name, cookie)
#  define timer_trace_async_begin_track(track, name, cookie) \
    trace_async_begin_track(NXTAG_TIMER, track, name, cookie)
#  define timer_trace_async_end_track(track, name, cookie) \
    trace_async_end_track(NXTAG_TIMER, track, name, cookie)
#else
#  define timer_trace_begin()
#  define timer_trace_end()
#  define timer_trace_beginex(name)
#  define timer_trace_endex(name)
#  define timer_trace_mark(s)
#  define timer_trace_printf(...)
#  define timer_trace_async_begin(name, cookie)
#  define timer_trace_async_end(name, cookie)
#  define timer_trace_async_begin_track(track, name, cookie)
#  define timer_trace_async_end_track(track, name, cookie)
#endif

#ifdef CONFIG_TRACE_USB
#  define usb_trace_begin() trace_begin(NXTAG_USB)
#  define usb_trace_end() trace_end(NXTAG_USB)
#  define usb_trace_beginex(name) trace_beginex(NXTAG_USB, name)
#  define usb_trace_endex(name) trace_endex(NXTAG_USB, name)
#  define usb_trace_mark(s) trace_mark(NXTAG_USB, s)
#  define usb_trace_printf(fmt, ...) \
    trace_printf(NXTAG_USB, fmt, ##__VA_ARGS__)
#  define usb_trace_async_begin(name, cookie) \
    trace_async_begin(NXTAG_USB, name, cookie)
#  define usb_trace_async_end(name, cookie) \
    trace_async_end(NXTAG_USB, name, cookie)
#  define usb_trace_async_begin_track(track, name, cookie) \
    trace_async_begin_track(NXTAG_USB, track, name, cookie)
#  define usb_trace_async_end_track(track, name, cookie) \
    trace_async_end_track(NXTAG_USB, track, name, cookie)
#else
#  define usb_trace_begin()
#  define usb_trace_end()
#  define usb_trace_beginex(name)
#  define usb_trace_endex(name)
#  define usb_trace_mark(s)
#  define usb_trace_printf(...)
#  define usb_trace_async_begin(name, cookie)
#  define usb_trace_async_end(name, cookie)
#  define usb_trace_async_begin_track(track, name, cookie)
#  define usb_trace_async_end_track(track, name, cookie)
#endif

#ifdef CONFIG_TRACE_VHOST
#  define vhost_trace_begin() trace_begin(NXTAG_VHOST)
#  define vhost_trace_end() trace_end(NXTAG_VHOST)
#  define vhost_trace_beginex(name) trace_beginex(NXTAG_VHOST, name)
#  define vhost_trace_endex(name) trace_endex(NXTAG_VHOST, name)
#  define vhost_trace_mark(s) trace_mark(NXTAG_VHOST, s)
#  define vhost_trace_printf(fmt, ...) \
    trace_printf(NXTAG_VHOST, fmt, ##__VA_ARGS__)
#  define vhost_trace_async_begin(name, cookie) \
    trace_async_begin(NXTAG_VHOST, name, cookie)
#  define vhost_trace_async_end(name, cookie) \
    trace_async_end(NXTAG_VHOST, name, cookie)
#  define vhost_trace_async_begin_track(track, name, cookie) \
    trace_async_begin_track(NXTAG_VHOST, track, name, cookie)
#  define vhost_trace_async_end_track(track, name, cookie) \
    trace_async_end_track(NXTAG_VHOST, track, name, cookie)
#else
#  define vhost_trace_begin()
#  define vhost_trace_end()
#  define vhost_trace_beginex(name)
#  define vhost_trace_endex(name)
#  define vhost_trace_mark(s)
#  define vhost_trace_printf(...)
#  define vhost_trace_async_begin(name, cookie)
#  define vhost_trace_async_end(name, cookie)
#  define vhost_trace_async_begin_track(track, name, cookie)
#  define vhost_trace_async_end_track(track, name, cookie)
#endif

#ifdef CONFIG_TRACE_VIDEO
#  define video_trace_begin() trace_begin(NXTAG_VIDEO)
#  define video_trace_end() trace_end(NXTAG_VIDEO)
#  define video_trace_beginex(name) trace_beginex(NXTAG_VIDEO, name)
#  define video_trace_endex(name) trace_endex(NXTAG_VIDEO, name)
#  define video_trace_mark(s) trace_mark(NXTAG_VIDEO, s)
#  define video_trace_printf(fmt, ...) \
    trace_printf(NXTAG_VIDEO, fmt, ##__VA_ARGS__)
#  define video_trace_async_begin(name, cookie) \
    trace_async_begin(NXTAG_VIDEO, name, cookie)
#  define video_trace_async_end(name, cookie) \
    trace_async_end(NXTAG_VIDEO, name, cookie)
#  define video_trace_async_begin_track(track, name, cookie) \
    trace_async_begin_track(NXTAG_VIDEO, track, name, cookie)
#  define video_trace_async_end_track(track, name, cookie) \
    trace_async_end_track(NXTAG_VIDEO, track, name, cookie)
#else
#  define video_trace_begin()
#  define video_trace_end()
#  define video_trace_beginex(name)
#  define video_trace_endex(name)
#  define video_trace_mark(s)
#  define video_trace_printf(...)
#  define video_trace_async_begin(name, cookie)
#  define video_trace_async_end(name, cookie)
#  define video_trace_async_begin_track(track, name, cookie)
#  define video_trace_async_end_track(track, name, cookie)
#endif

#ifdef CONFIG_TRACE_VIRTIO
#  define virtio_trace_begin() trace_begin(NXTAG_VIRTIO)
#  define virtio_trace_end() trace_end(NXTAG_VIRTIO)
#  define virtio_trace_beginex(name) trace_beginex(NXTAG_VIRTIO, name)
#  define virtio_trace_endex(name) trace_endex(NXTAG_VIRTIO, name)
#  define virtio_trace_mark(s) trace_mark(NXTAG_VIRTIO, s)
#  define virtio_trace_printf(fmt, ...) \
    trace_printf(NXTAG_VIRTIO, fmt, ##__VA_ARGS__)
#  define virtio_trace_async_begin(name, cookie) \
    trace_async_begin(NXTAG_VIRTIO, name, cookie)
#  define virtio_trace_async_end(name, cookie) \
    trace_async_end(NXTAG_VIRTIO, name, cookie)
#  define virtio_trace_async_begin_track(track, name, cookie) \
    trace_async_begin_track(NXTAG_VIRTIO, track, name, cookie)
#  define virtio_trace_async_end_track(track, name, cookie) \
    trace_async_end_track(NXTAG_VIRTIO, track, name, cookie)
#else
#  define virtio_trace_begin()
#  define virtio_trace_end()
#  define virtio_trace_beginex(name)
#  define virtio_trace_endex(name)
#  define virtio_trace_mark(s)
#  define virtio_trace_printf(...)
#  define virtio_trace_async_begin(name, cookie)
#  define virtio_trace_async_end(name, cookie)
#  define virtio_trace_async_begin_track(track, name, cookie)
#  define virtio_trace_async_end_track(track, name, cookie)
#endif

#ifdef CONFIG_TRACE_WATCHDOG
#  define watchdog_trace_begin() trace_begin(NXTAG_WATCHDOG)
#  define watchdog_trace_end() trace_end(NXTAG_WATCHDOG)
#  define watchdog_trace_beginex(name) trace_beginex(NXTAG_WATCHDOG, name)
#  define watchdog_trace_endex(name) trace_endex(NXTAG_WATCHDOG, name)
#  define watchdog_trace_mark(s) trace_mark(NXTAG_WATCHDOG, s)
#  define watchdog_trace_printf(fmt, ...) \
    trace_printf(NXTAG_WATCHDOG, fmt, ##__VA_ARGS__)
#  define watchdog_trace_async_begin(name, cookie) \
    trace_async_begin(NXTAG_WATCHDOG, name, cookie)
#  define watchdog_trace_async_end(name, cookie) \
    trace_async_end(NXTAG_WATCHDOG, name, cookie)
#  define watchdog_trace_async_begin_track(track, name, cookie) \
    trace_async_begin_track(NXTAG_WATCHDOG, track, name, cookie)
#  define watchdog_trace_async_end_track(track, name, cookie) \
    trace_async_end_track(NXTAG_WATCHDOG, track, name, cookie)
#else
#  define watchdog_trace_begin()
#  define watchdog_trace_end()
#  define watchdog_trace_beginex(name)
#  define watchdog_trace_endex(name)
#  define watchdog_trace_mark(s)
#  define watchdog_trace_printf(...)
#  define watchdog_trace_async_begin(name, cookie)
#  define watchdog_trace_async_end(name, cookie)
#  define watchdog_trace_async_begin_track(track, name, cookie)
#  define watchdog_trace_async_end_track(track, name, cookie)
#endif

#ifdef CONFIG_TRACE_WIRELESS
#  define wireless_trace_begin() trace_begin(NXTAG_WIRELESS)
#  define wireless_trace_end() trace_end(NXTAG_WIRELESS)
#  define wireless_trace_beginex(name) trace_beginex(NXTAG_WIRELESS, name)
#  define wireless_trace_endex(name) trace_endex(NXTAG_WIRELESS, name)
#  define wireless_trace_mark(s) trace_mark(NXTAG_WIRELESS, s)
#  define wireless_trace_printf(fmt, ...) \
    trace_printf(NXTAG_WIRELESS, fmt, ##__VA_ARGS__)
#  define wireless_trace_async_begin(name, cookie) \
    trace_async_begin(NXTAG_WIRELESS, name, cookie)
#  define wireless_trace_async_end(name, cookie) \
    trace_async_end(NXTAG_WIRELESS, name, cookie)
#  define wireless_trace_async_begin_track(track, name, cookie) \
    trace_async_begin_track(NXTAG_WIRELESS, track, name, cookie)
#  define wireless_trace_async_end_track(track, name, cookie) \
    trace_async_end_track(NXTAG_WIRELESS, track, name, cookie)
#else
#  define wireless_trace_begin()
#  define wireless_trace_end()
#  define wireless_trace_beginex(name)
#  define wireless_trace_endex(name)
#  define wireless_trace_mark(s)
#  define wireless_trace_printf(...)
#  define wireless_trace_async_begin(name, cookie)
#  define wireless_trace_async_end(name, cookie)
#  define wireless_trace_async_begin_track(track, name, cookie)
#  define wireless_trace_async_end_track(track, name, cookie)
#endif

/* xxx_trace_diag — per-module DFX fault reporting wrappers.
 * Controlled solely by CONFIG_DIAG via the underlying trace_diag macro.
 */

#define analog_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_ANALOG, level, id, fmt, ##__VA_ARGS__)
#define audio_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_AUDIO, level, id, fmt, ##__VA_ARGS__)
#define battery_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_BATTERY, level, id, fmt, ##__VA_ARGS__)
#define binfmt_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_BINFMT, level, id, fmt, ##__VA_ARGS__)
#define can_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_CAN, level, id, fmt, ##__VA_ARGS__)
#define capture_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_CAPTURE, level, id, fmt, ##__VA_ARGS__)
#define contactless_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_CONTACTLESS, level, id, fmt, ##__VA_ARGS__)
#define crypto_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_CRYPTO, level, id, fmt, ##__VA_ARGS__)
#define dma_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_DMA, level, id, fmt, ##__VA_ARGS__)
#define fs_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_FS, level, id, fmt, ##__VA_ARGS__)
#define gpio_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_GPIO, level, id, fmt, ##__VA_ARGS__)
#define graphics_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_GRAPHICS, level, id, fmt, ##__VA_ARGS__)
#define hwtracing_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_HWTRACING, level, id, fmt, ##__VA_ARGS__)
#define i2c_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_I2C, level, id, fmt, ##__VA_ARGS__)
#define i2s_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_I2S, level, id, fmt, ##__VA_ARGS__)
#define i3c_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_I3C, level, id, fmt, ##__VA_ARGS__)
#define input_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_INPUT, level, id, fmt, ##__VA_ARGS__)
#define ipc_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_IPC, level, id, fmt, ##__VA_ARGS__)
#define irq_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_IRQ, level, id, fmt, ##__VA_ARGS__)
#define lcd_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_LCD, level, id, fmt, ##__VA_ARGS__)
#define leds_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_LEDS, level, id, fmt, ##__VA_ARGS__)
#define lib_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_LIB, level, id, fmt, ##__VA_ARGS__)
#define memcard_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_MEMCARD, level, id, fmt, ##__VA_ARGS__)
#define mm_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_MM, level, id, fmt, ##__VA_ARGS__)
#define motor_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_MOTOR, level, id, fmt, ##__VA_ARGS__)
#define net_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_NET, level, id, fmt, ##__VA_ARGS__)
#define paging_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_PAGING, level, id, fmt, ##__VA_ARGS__)
#define pci_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_PCI, level, id, fmt, ##__VA_ARGS__)
#define power_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_POWER, level, id, fmt, ##__VA_ARGS__)
#define ptp_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_PTP, level, id, fmt, ##__VA_ARGS__)
#define pwm_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_PWM, level, id, fmt, ##__VA_ARGS__)
#define rc_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_RC, level, id, fmt, ##__VA_ARGS__)
#define regmap_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_REGMAP, level, id, fmt, ##__VA_ARGS__)
#define reset_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_RESET, level, id, fmt, ##__VA_ARGS__)
#define rmt_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_RMT, level, id, fmt, ##__VA_ARGS__)
#define rpmsg_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_RPMSG, level, id, fmt, ##__VA_ARGS__)
#define rptun_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_RPTUN, level, id, fmt, ##__VA_ARGS__)
#define rtc_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_RTC, level, id, fmt, ##__VA_ARGS__)
#define safety_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_SAFETY, level, id, fmt, ##__VA_ARGS__)
#define sched_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_SCHED, level, id, fmt, ##__VA_ARGS__)
#define sensor_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_SENSORS, level, id, fmt, ##__VA_ARGS__)
#define spi_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_SPI, level, id, fmt, ##__VA_ARGS__)
#define stepper_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_STEPPER, level, id, fmt, ##__VA_ARGS__)
#define syscall_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_SYSCALL, level, id, fmt, ##__VA_ARGS__)
#define tee_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_TEE, level, id, fmt, ##__VA_ARGS__)
#define thermal_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_THERMAL, level, id, fmt, ##__VA_ARGS__)
#define timer_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_TIMER, level, id, fmt, ##__VA_ARGS__)
#define usb_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_USB, level, id, fmt, ##__VA_ARGS__)
#define vhost_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_VHOST, level, id, fmt, ##__VA_ARGS__)
#define video_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_VIDEO, level, id, fmt, ##__VA_ARGS__)
#define virtio_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_VIRTIO, level, id, fmt, ##__VA_ARGS__)
#define watchdog_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_WATCHDOG, level, id, fmt, ##__VA_ARGS__)
#define wireless_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_WIRELESS, level, id, fmt, ##__VA_ARGS__)

/* Helper: capture backtrace into buffer, no-op if unsupported.
 * The buffer is cleared before capturing so that unused slots are
 * guaranteed to be NULL instead of holding stale stack data.
 */

#if defined(CONFIG_ARCH_HAVE_BACKTRACE) && defined(CONFIG_DIAG)
#  define DIAG_BACKTRACE(tcb, buf, size) \
     do \
       { \
         memset(buf, 0, sizeof(buf)); \
         up_backtrace(tcb, buf, size, 0); \
       } \
     while (0)
#else
#  define DIAG_BACKTRACE(tcb, buf, size) do {} while (0)
#endif

/* Diagnostic IDs – cross-feature base */

#define DIAG_SYSTEM_BASE                 961010000

/* System module fault code bases (within DIAG_SYSTEM_BASE) */

#define DIAG_SYSTEM_SCHED_BASE           (DIAG_SYSTEM_BASE + 0)        /* 961010000 */
#define DIAG_SCHED_ASSERT                (DIAG_SYSTEM_SCHED_BASE + 0)  /* 961010000 */
#define DIAG_SCHED_PREEMPTION_TIMEOUT    (DIAG_SYSTEM_SCHED_BASE + 3)  /* 961010003 */
#define DIAG_SCHED_HOLDER_PANIC          (DIAG_SYSTEM_SCHED_BASE + 4)  /* 961010004 */
#define DIAG_SCHED_WQUEUE_TIMEOUT        (DIAG_SYSTEM_SCHED_BASE + 5)  /* 961010005 */
#define DIAG_SCHED_WDOG_TIMEOUT          (DIAG_SYSTEM_SCHED_BASE + 6)  /* 961010006 */
#define DIAG_SCHED_THREAD_TIMEOUT        (DIAG_SYSTEM_SCHED_BASE + 7)  /* 961010007 */
#define DIAG_SCHED_IRQDISABLE_TIMEOUT    (DIAG_SYSTEM_SCHED_BASE + 8)  /* 961010008 */
#define DIAG_SCHED_BUSYWAIT_TIMEOUT      (DIAG_SYSTEM_SCHED_BASE + 9)  /* 961010009 */
#define DIAG_SCHED_BOOT                  (DIAG_SYSTEM_SCHED_BASE + 10) /* 961010010 */
#define DIAG_SCHED_REBOOT                (DIAG_SYSTEM_SCHED_BASE + 11) /* 961010011 */

#define DIAG_SYSTEM_MM_BASE              (DIAG_SYSTEM_BASE + 100)     /* 961010100 */
#define DIAG_MM_ALLOC_FAIL               (DIAG_SYSTEM_MM_BASE + 0)    /* 961010100 */
#define DIAG_MM_FREE_FAIL                (DIAG_SYSTEM_MM_BASE + 1)    /* 961010101 */
#define DIAG_MM_STACK_OVERFLOW           (DIAG_SYSTEM_MM_BASE + 2)    /* 961010102 */

#define DIAG_SYSTEM_IRQ_BASE             (DIAG_SYSTEM_BASE + 200)     /* 961010200 */
#define DIAG_IRQ_HANDLER_TIMEOUT         (DIAG_SYSTEM_IRQ_BASE + 1)   /* 961010201 */

#define DIAG_SYSTEM_IPC_BASE             (DIAG_SYSTEM_BASE + 300)     /* 961010300 */
#define DIAG_SYSTEM_RPMSG_BASE           (DIAG_SYSTEM_BASE + 400)     /* 961010400 */

/* Rpmsg fault codes (within DIAG_SYSTEM_RPMSG_BASE).  Each module owns a
 * decade (10 IDs), as labelled below, so a new code can be added without
 * disturbing the others.  Failures in the same category share a single ID
 * and are distinguished by the "Stage:"/"Reason:" field in the log string.
 */

/* Rpmsg framework (BASE + 0..9) */

#define DIAG_RPMSG_RECURSIVE_LIMIT       (DIAG_SYSTEM_RPMSG_BASE + 0)   /* 961010400 */
#define DIAG_RPMSG_REGISTER_FAIL         (DIAG_SYSTEM_RPMSG_BASE + 1)   /* 961010401 */

/* Rptun (BASE + 10..19) */

#define DIAG_RPTUN_INIT_FAIL             (DIAG_SYSTEM_RPMSG_BASE + 10)  /* 961010410 */
#define DIAG_RPTUN_PEER_NOT_READY        (DIAG_SYSTEM_RPMSG_BASE + 11)  /* 961010411 */
#define DIAG_RPTUN_REMOTE_PANIC          (DIAG_SYSTEM_RPMSG_BASE + 12)  /* 961010412 */

/* Rpmsg virtio transport (BASE + 20..29) */

#define DIAG_RPMSG_VIRTIO_INIT_FAIL      (DIAG_SYSTEM_RPMSG_BASE + 20)  /* 961010420 */
#define DIAG_RPMSG_VIRTIO_TX_FAIL        (DIAG_SYSTEM_RPMSG_BASE + 21)  /* 961010421 */
#define DIAG_RPMSG_VIRTIO_LOST_IRQ       (DIAG_SYSTEM_RPMSG_BASE + 22)  /* 961010422 */

/* Rpmsg port-uart transport (BASE + 30..39) */

#define DIAG_RPMSG_UART_INIT_FAIL        (DIAG_SYSTEM_RPMSG_BASE + 30)  /* 961010430 */
#define DIAG_RPMSG_UART_RX_ERROR         (DIAG_SYSTEM_RPMSG_BASE + 31)  /* 961010431 */

/* Rpmsg port-spi transport (BASE + 40..49) */

#define DIAG_RPMSG_SPI_RX_ERROR          (DIAG_SYSTEM_RPMSG_BASE + 40)  /* 961010440 */
#define DIAG_RPMSG_SPI_TX_ERROR          (DIAG_SYSTEM_RPMSG_BASE + 41)  /* 961010441 */

/* Rpmsg router (BASE + 50..59) */

#define DIAG_RPMSG_ROUTER_INIT_FAIL      (DIAG_SYSTEM_RPMSG_BASE + 50)  /* 961010450 */
#define DIAG_RPMSG_ROUTER_COMM_FAIL      (DIAG_SYSTEM_RPMSG_BASE + 51)  /* 961010451 */

/* Rpmsg socket service (BASE + 60..69) */

#define DIAG_RPMSG_SOCKET_RX_ERROR       (DIAG_SYSTEM_RPMSG_BASE + 60)  /* 961010460 */
#define DIAG_RPMSG_SOCKET_TX_ERROR       (DIAG_SYSTEM_RPMSG_BASE + 61)  /* 961010461 */

/* Rpmsg char service (BASE + 70..79) */

#define DIAG_RPMSG_CHAR_INIT_FAIL        (DIAG_SYSTEM_RPMSG_BASE + 70)  /* 961010470 */

/* Rpmsg rtc service (BASE + 80..89) */

#define DIAG_RPMSG_RTC_ERROR             (DIAG_SYSTEM_RPMSG_BASE + 80)  /* 961010480 */

#define DIAG_SYSTEM_BINDER_BASE          (DIAG_SYSTEM_BASE + 500)     /* 961010500 */
#define DIAG_SYSTEM_SENSOR_BASE          (DIAG_SYSTEM_BASE + 600)     /* 961010600 */
#define DIAG_SYSTEM_PM_BASE              (DIAG_SYSTEM_BASE + 700)     /* 961010700 */

#define DIAG_SYSTEM_TEE_BASE             (DIAG_SYSTEM_BASE + 800)     /* 961010800 */

#define DIAG_TEE_TRANSPORT_FAIL          (DIAG_SYSTEM_TEE_BASE + 0)   /* 961010800 */
#define DIAG_TEE_TRANSPORT_HANG          (DIAG_SYSTEM_TEE_BASE + 1)   /* 961010801 */
#define DIAG_TEE_RPC_HANG                (DIAG_SYSTEM_TEE_BASE + 2)   /* 961010802 */
#define DIAG_TEE_SUPPLICANT_HANG         (DIAG_SYSTEM_TEE_BASE + 3)   /* 961010803 */
#define DIAG_TEE_SHM_FAIL                (DIAG_SYSTEM_TEE_BASE + 4)   /* 961010804 */
#define DIAG_TEE_OPTEED_HANG             (DIAG_SYSTEM_TEE_BASE + 5)   /* 961010805 */
#define DIAG_TEE_THREAD_FAIL             (DIAG_SYSTEM_TEE_BASE + 6)   /* 961010806 */
#define DIAG_TEE_COMM_BREAK              (DIAG_SYSTEM_TEE_BASE + 7)   /* 961010807 */
#define DIAG_TEE_COMPAT_PANIC            (DIAG_SYSTEM_TEE_BASE + 8)   /* 961010808 */
#define DIAG_TEE_STORAGE_FAIL            (DIAG_SYSTEM_TEE_BASE + 9)   /* 961010809 */
#define DIAG_TEE_RPMB_FAIL               (DIAG_SYSTEM_TEE_BASE + 10)  /* 961010810 */
#define DIAG_TEE_TA_INIT_FAIL            (DIAG_SYSTEM_TEE_BASE + 11)  /* 961010811 */
#define DIAG_TEE_TA_ENTRY_ERR            (DIAG_SYSTEM_TEE_BASE + 12)  /* 961010812 */
#define DIAG_TEE_TA_PANIC                (DIAG_SYSTEM_TEE_BASE + 13)  /* 961010813 */

/* DIAG_TEE_TRANSPORT_FAIL (961010800) ErrorCode definitions.
 * Strictly the open/connect phase: socket creation and connect.
 */

#define DIAG_TEE_OPEN_ERR_SOCKET         1  /* psock_socket failed */
#define DIAG_TEE_OPEN_ERR_CONNECT        2  /* psock_connect failed */

/* DIAG_TEE_TRANSPORT_HANG (961010801) ErrorCode definitions.
 * All call-phase transport failures. SEND is an immediate error; PEER_RESET
 * means opteed is gone (EOF/ECONNRESET/EPIPE/ENOTCONN); TIMEOUT (elapsed ~=
 * configured timeout) is the real "call does not return" signature.
 */

#define DIAG_TEE_CALL_ERR_SEND           1  /* sendmsg returned error */
#define DIAG_TEE_CALL_ERR_PEER_RESET     2  /* peer closed / link down */
#define DIAG_TEE_CALL_ERR_RECV           3  /* other recv error */
#define DIAG_TEE_CALL_ERR_TIMEOUT        4  /* recv timed out (call hang) */

#define DIAG_SYSTEM_OTA_BASE             (DIAG_SYSTEM_BASE + 900)     /* 961010900 */

#define DIAG_SYSTEM_WATCHDOG_BASE        (DIAG_SYSTEM_BASE + 1000)    /* 961011000 */

#define DIAG_SYSTEM_FS_BASE              (DIAG_SYSTEM_BASE + 1100)    /* 961011100 */
#define DIAG_FS_MOUNT_FAIL               (DIAG_SYSTEM_FS_BASE + 0)    /* 961011100 */

#define DIAG_SYSTEM_DRIVER_BASE          (DIAG_SYSTEM_BASE + 1400)    /* 961011400 */

/* Network module diagnostic IDs */

#define DIAG_NET_BASE                    961030000
#define DIAG_NET_DHCP_FAIL               (DIAG_NET_BASE + 1)  /* 961030001 */
#define DIAG_NET_DNS_FAIL                (DIAG_NET_BASE + 2)  /* 961030002 */
#define DIAG_NET_NTP_FAIL                (DIAG_NET_BASE + 3)  /* 961030003 */
#define DIAG_NET_CONN_STATE_ABNORM       (DIAG_NET_BASE + 4)  /* 961030004 */
#define DIAG_NET_WIFI_SCAN_FAIL          (DIAG_NET_BASE + 5)  /* 961030005 */
#define DIAG_NET_WIFI_CONNECT_FAIL       (DIAG_NET_BASE + 6)  /* 961030006 */
#define DIAG_NET_CARRIER_OFF             (DIAG_NET_BASE + 7)  /* 961030007 */
#define DIAG_NET_NIC_START_FAIL          (DIAG_NET_BASE + 10) /* 961030010 */
#define DIAG_NET_DNS_LATENCY_HIGH        (DIAG_NET_BASE + 13) /* 961030013 */
#define DIAG_NET_TCP_LATENCY_HIGH        (DIAG_NET_BASE + 14) /* 961030014 */

/* DIAG_NET_DHCP_FAIL (961030001) ErrorCode definitions */

#define DIAG_NET_DHCP_ERR_DISCOVER_SEND   1  /* DISCOVER send failed */
#define DIAG_NET_DHCP_ERR_DISCOVER_RECV   2  /* DISCOVER recv socket error */
#define DIAG_NET_DHCP_ERR_NO_OFFER        3  /* No OFFER response (timeout) */
#define DIAG_NET_DHCP_ERR_REQUEST_SEND    4  /* REQUEST send failed */
#define DIAG_NET_DHCP_ERR_NAK             5  /* Received NAK (server rejected) */
#define DIAG_NET_DHCP_ERR_REQUEST_RECV    6  /* REQUEST recv socket error */
#define DIAG_NET_DHCP_ERR_NO_ACK          7  /* No ACK response (timeout) */
#define DIAG_NET_DHCP_ERR_IP_CONFLICT     8  /* IP conflict (ACD detected) */
#define DIAG_NET_DHCP_ERR_INIT_FAIL       9  /* DHCP client init failed */
#define DIAG_NET_DHCP_ERR_LEASE_LOST      10 /* Lease lost (renew failed) */
#define DIAG_NET_DHCP_ERR_IPV4LL_FALLBACK 11 /* Fallback to IPv4LL */

/* DIAG_NET_DNS_FAIL (961030002) ErrorCode definitions */

#define DIAG_NET_DNS_ERR_TIMEOUT          1  /* Query timeout (retries exhausted) */
#define DIAG_NET_DNS_ERR_NO_SERVER        2  /* No DNS server configured */
#define DIAG_NET_DNS_ERR_PARSE            3  /* Response parse error */

/* DIAG_NET_CONN_STATE_ABNORM (961030004) ErrorCode definitions */

#define DIAG_NET_ERR_ARP_UNREACHABLE      1  /* ARP no response (target unreachable) */
#define DIAG_NET_ERR_GW_MAC_CHANGED       2  /* Gateway MAC address changed */
#define DIAG_NET_ERR_IOB_STALL            3  /* IOB allocation stalled for too long */
#define DIAG_NET_ERR_TCP_RETRANS          4  /* TCP retransmission limit exceeded */
#define DIAG_NET_ERR_UDP_SEND_FAIL        5  /* UDP send failure */

/* DIAG_NET_DNS_LATENCY_HIGH (961030013) threshold (ms) */

#define DIAG_NET_DNS_LATENCY_THRESHOLD_MS 1000

#endif /* __INCLUDE_NUTTX_TRACE_H */
