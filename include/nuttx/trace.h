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
#  define trace_diag(tag, level, id, fmt, ...) \
    sched_note_printf_ip(tag, level, id, fmt, ##__VA_ARGS__)
#  define trace_diagv(tag, level, id, fmt, va) \
    sched_note_vprintf_ip(tag, level, id, fmt, va)
#else
#  define trace_begin(tag)
#  define trace_end(tag)
#  define trace_beginex(tag, name)
#  define trace_endex(tag, name)
#  define trace_mark(tag, s)
#  define trace_printf(...)
#  define trace_diag(...)
#  define trace_diagv(...)
#endif

#ifdef CONFIG_TRACE_ANALOG
#  define analog_trace_begin() trace_begin(NXTAG_ANALOG)
#  define analog_trace_end() trace_end(NXTAG_ANALOG)
#  define analog_trace_beginex(name) trace_beginex(NXTAG_ANALOG, name)
#  define analog_trace_endex(name) trace_endex(NXTAG_ANALOG, name)
#  define analog_trace_mark(s) trace_mark(NXTAG_ANALOG, s)
#  define analog_trace_printf(fmt, ...) \
    trace_printf(NXTAG_ANALOG, fmt, ##__VA_ARGS__)
#  define analog_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_ANALOG, level, id, fmt, ##__VA_ARGS__)
#else
#  define analog_trace_begin()
#  define analog_trace_end()
#  define analog_trace_beginex(name)
#  define analog_trace_endex(name)
#  define analog_trace_mark(s)
#  define analog_trace_printf(...)
#  define analog_trace_diag(...)
#endif

#ifdef CONFIG_TRACE_AUDIO
#  define audio_trace_begin() trace_begin(NXTAG_AUDIO)
#  define audio_trace_end() trace_end(NXTAG_AUDIO)
#  define audio_trace_beginex(name) trace_beginex(NXTAG_AUDIO, name)
#  define audio_trace_endex(name) trace_endex(NXTAG_AUDIO, name)
#  define audio_trace_mark(s) trace_mark(NXTAG_AUDIO, s)
#  define audio_trace_printf(fmt, ...) \
    trace_printf(NXTAG_AUDIO, fmt, ##__VA_ARGS__)
#  define audio_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_AUDIO, level, id, fmt, ##__VA_ARGS__)
#else
#  define audio_trace_begin()
#  define audio_trace_end()
#  define audio_trace_beginex(name)
#  define audio_trace_endex(name)
#  define audio_trace_mark(s)
#  define audio_trace_printf(...)
#  define audio_trace_diag(...)
#endif

#ifdef CONFIG_TRACE_BATTERY
#  define battery_trace_begin() trace_begin(NXTAG_BATTERY)
#  define battery_trace_end() trace_end(NXTAG_BATTERY)
#  define battery_trace_beginex(name) trace_beginex(NXTAG_BATTERY, name)
#  define battery_trace_endex(name) trace_endex(NXTAG_BATTERY, name)
#  define battery_trace_mark(s) trace_mark(NXTAG_BATTERY, s)
#  define battery_trace_printf(fmt, ...) \
    trace_printf(NXTAG_BATTERY, fmt, ##__VA_ARGS__)
#  define battery_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_BATTERY, level, id, fmt, ##__VA_ARGS__)
#else
#  define battery_trace_begin()
#  define battery_trace_end()
#  define battery_trace_beginex(name)
#  define battery_trace_endex(name)
#  define battery_trace_mark(s)
#  define battery_trace_printf(...)
#  define battery_trace_diag(...)
#endif

#ifdef CONFIG_TRACE_BINFMT
#  define binfmt_trace_begin() trace_begin(NXTAG_BINFMT)
#  define binfmt_trace_end() trace_end(NXTAG_BINFMT)
#  define binfmt_trace_beginex(name) trace_beginex(NXTAG_BINFMT, name)
#  define binfmt_trace_endex(name) trace_endex(NXTAG_BINFMT, name)
#  define binfmt_trace_mark(s) trace_mark(NXTAG_BINFMT, s)
#  define binfmt_trace_printf(fmt, ...) \
    trace_printf(NXTAG_BINFMT, fmt, ##__VA_ARGS__)
#  define binfmt_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_BINFMT, level, id, fmt, ##__VA_ARGS__)
#else
#  define binfmt_trace_begin()
#  define binfmt_trace_end()
#  define binfmt_trace_beginex(name)
#  define binfmt_trace_endex(name)
#  define binfmt_trace_mark(s)
#  define binfmt_trace_printf(...)
#  define binfmt_trace_diag(...)
#endif

#ifdef CONFIG_TRACE_CAN
#  define can_trace_begin() trace_begin(NXTAG_CAN)
#  define can_trace_end() trace_end(NXTAG_CAN)
#  define can_trace_beginex(name) trace_beginex(NXTAG_CAN, name)
#  define can_trace_endex(name) trace_endex(NXTAG_CAN, name)
#  define can_trace_mark(s) trace_mark(NXTAG_CAN, s)
#  define can_trace_printf(fmt, ...) \
    trace_printf(NXTAG_CAN, fmt, ##__VA_ARGS__)
#  define can_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_CAN, level, id, fmt, ##__VA_ARGS__)
#else
#  define can_trace_begin()
#  define can_trace_end()
#  define can_trace_beginex(name)
#  define can_trace_endex(name)
#  define can_trace_mark(s)
#  define can_trace_printf(...)
#  define can_trace_diag(...)
#endif

#ifdef CONFIG_TRACE_CAPTURE
#  define capture_trace_begin() trace_begin(NXTAG_CAPTURE)
#  define capture_trace_end() trace_end(NXTAG_CAPTURE)
#  define capture_trace_beginex(name) trace_beginex(NXTAG_CAPTURE, name)
#  define capture_trace_endex(name) trace_endex(NXTAG_CAPTURE, name)
#  define capture_trace_mark(s) trace_mark(NXTAG_CAPTURE, s)
#  define capture_trace_printf(fmt, ...) \
    trace_printf(NXTAG_CAPTURE, fmt, ##__VA_ARGS__)
#  define capture_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_CAPTURE, level, id, fmt, ##__VA_ARGS__)
#else
#  define capture_trace_begin()
#  define capture_trace_end()
#  define capture_trace_beginex(name)
#  define capture_trace_endex(name)
#  define capture_trace_mark(s)
#  define capture_trace_printf(...)
#  define capture_trace_diag(...)
#endif

#ifdef CONFIG_TRACE_CONTACTLESS
#  define contactless_trace_begin() trace_begin(NXTAG_CONTACTLESS)
#  define contactless_trace_end() trace_end(NXTAG_CONTACTLESS)
#  define contactless_trace_beginex(name) trace_beginex(NXTAG_CONTACTLESS, name)
#  define contactless_trace_endex(name) trace_endex(NXTAG_CONTACTLESS, name)
#  define contactless_trace_mark(s) trace_mark(NXTAG_CONTACTLESS, s)
#  define contactless_trace_printf(fmt, ...) \
    trace_printf(NXTAG_CONTACTLESS, fmt, ##__VA_ARGS__)
#  define contactless_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_CONTACTLESS, level, id, fmt, ##__VA_ARGS__)
#else
#  define contactless_trace_begin()
#  define contactless_trace_end()
#  define contactless_trace_beginex(name)
#  define contactless_trace_endex(name)
#  define contactless_trace_mark(s)
#  define contactless_trace_printf(...)
#  define contactless_trace_diag(...)
#endif

#ifdef CONFIG_TRACE_CRYPTO
#  define crypto_trace_begin() trace_begin(NXTAG_CRYPTO)
#  define crypto_trace_end() trace_end(NXTAG_CRYPTO)
#  define crypto_trace_beginex(name) trace_beginex(NXTAG_CRYPTO, name)
#  define crypto_trace_endex(name) trace_endex(NXTAG_CRYPTO, name)
#  define crypto_trace_mark(s) trace_mark(NXTAG_CRYPTO, s)
#  define crypto_trace_printf(fmt, ...) \
    trace_printf(NXTAG_CRYPTO, fmt, ##__VA_ARGS__)
#  define crypto_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_CRYPTO, level, id, fmt, ##__VA_ARGS__)
#else
#  define crypto_trace_begin()
#  define crypto_trace_end()
#  define crypto_trace_beginex(name)
#  define crypto_trace_endex(name)
#  define crypto_trace_mark(s)
#  define crypto_trace_printf(...)
#  define crypto_trace_diag(...)
#endif

#ifdef CONFIG_TRACE_DMA
#  define dma_trace_begin() trace_begin(NXTAG_DMA)
#  define dma_trace_end() trace_end(NXTAG_DMA)
#  define dma_trace_beginex(name) trace_beginex(NXTAG_DMA, name)
#  define dma_trace_endex(name) trace_endex(NXTAG_DMA, name)
#  define dma_trace_mark(s) trace_mark(NXTAG_DMA, s)
#  define dma_trace_printf(fmt, ...) \
    trace_printf(NXTAG_DMA, fmt, ##__VA_ARGS__)
#  define dma_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_DMA, level, id, fmt, ##__VA_ARGS__)
#else
#  define dma_trace_begin()
#  define dma_trace_end()
#  define dma_trace_beginex(name)
#  define dma_trace_endex(name)
#  define dma_trace_mark(s)
#  define dma_trace_printf(...)
#  define dma_trace_diag(...)
#endif

#ifdef CONFIG_TRACE_FS
#  define fs_trace_begin() trace_begin(NXTAG_FS)
#  define fs_trace_end() trace_end(NXTAG_FS)
#  define fs_trace_beginex(name) trace_beginex(NXTAG_FS, name)
#  define fs_trace_endex(name) trace_endex(NXTAG_FS, name)
#  define fs_trace_mark(s) trace_mark(NXTAG_FS, s)
#  define fs_trace_printf(fmt, ...) \
    trace_printf(NXTAG_FS, fmt, ##__VA_ARGS__)
#  define fs_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_FS, level, id, fmt, ##__VA_ARGS__)
#else
#  define fs_trace_begin()
#  define fs_trace_end()
#  define fs_trace_beginex(name)
#  define fs_trace_endex(name)
#  define fs_trace_mark(s)
#  define fs_trace_printf(...)
#  define fs_trace_diag(...)
#endif

#ifdef CONFIG_TRACE_GPIO
#  define gpio_trace_begin() trace_begin(NXTAG_GPIO)
#  define gpio_trace_end() trace_end(NXTAG_GPIO)
#  define gpio_trace_beginex(name) trace_beginex(NXTAG_GPIO, name)
#  define gpio_trace_endex(name) trace_endex(NXTAG_GPIO, name)
#  define gpio_trace_mark(s) trace_mark(NXTAG_GPIO, s)
#  define gpio_trace_printf(fmt, ...) \
    trace_printf(NXTAG_GPIO, fmt, ##__VA_ARGS__)
#  define gpio_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_GPIO, level, id, fmt, ##__VA_ARGS__)
#else
#  define gpio_trace_begin()
#  define gpio_trace_end()
#  define gpio_trace_beginex(name)
#  define gpio_trace_endex(name)
#  define gpio_trace_mark(s)
#  define gpio_trace_printf(...)
#  define gpio_trace_diag(...)
#endif

#ifdef CONFIG_TRACE_GRAPHICS
#  define graphics_trace_begin() trace_begin(NXTAG_GRAPHICS)
#  define graphics_trace_end() trace_end(NXTAG_GRAPHICS)
#  define graphics_trace_beginex(name) trace_beginex(NXTAG_GRAPHICS, name)
#  define graphics_trace_endex(name) trace_endex(NXTAG_GRAPHICS, name)
#  define graphics_trace_mark(s) trace_mark(NXTAG_GRAPHICS, s)
#  define graphics_trace_printf(fmt, ...) \
    trace_printf(NXTAG_GRAPHICS, fmt, ##__VA_ARGS__)
#  define graphics_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_GRAPHICS, level, id, fmt, ##__VA_ARGS__)
#else
#  define graphics_trace_begin()
#  define graphics_trace_end()
#  define graphics_trace_beginex(name)
#  define graphics_trace_endex(name)
#  define graphics_trace_mark(s)
#  define graphics_trace_printf(...)
#  define graphics_trace_diag(...)
#endif

#ifdef CONFIG_TRACE_HWTRACING
#  define hwtracing_trace_begin() trace_begin(NXTAG_HWTRACING)
#  define hwtracing_trace_end() trace_end(NXTAG_HWTRACING)
#  define hwtracing_trace_beginex(name) trace_beginex(NXTAG_HWTRACING, name)
#  define hwtracing_trace_endex(name) trace_endex(NXTAG_HWTRACING, name)
#  define hwtracing_trace_mark(s) trace_mark(NXTAG_HWTRACING, s)
#  define hwtracing_trace_printf(fmt, ...) \
    trace_printf(NXTAG_HWTRACING, fmt, ##__VA_ARGS__)
#  define hwtracing_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_HWTRACING, level, id, fmt, ##__VA_ARGS__)
#else
#  define hwtracing_trace_begin()
#  define hwtracing_trace_end()
#  define hwtracing_trace_beginex(name)
#  define hwtracing_trace_endex(name)
#  define hwtracing_trace_mark(s)
#  define hwtracing_trace_printf(...)
#  define hwtracing_trace_diag(...)
#endif

#ifdef CONFIG_TRACE_I2C
#  define i2c_trace_begin() trace_begin(NXTAG_I2C)
#  define i2c_trace_end() trace_end(NXTAG_I2C)
#  define i2c_trace_beginex(name) trace_beginex(NXTAG_I2C, name)
#  define i2c_trace_endex(name) trace_endex(NXTAG_I2C, name)
#  define i2c_trace_mark(s) trace_mark(NXTAG_I2C, s)
#  define i2c_trace_printf(fmt, ...) \
    trace_printf(NXTAG_I2C, fmt, ##__VA_ARGS__)
#  define i2c_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_I2C, level, id, fmt, ##__VA_ARGS__)
#else
#  define i2c_trace_begin()
#  define i2c_trace_end()
#  define i2c_trace_beginex(name)
#  define i2c_trace_endex(name)
#  define i2c_trace_mark(s)
#  define i2c_trace_printf(...)
#  define i2c_trace_diag(...)
#endif

#ifdef CONFIG_TRACE_I2S
#  define i2s_trace_begin() trace_begin(NXTAG_I2S)
#  define i2s_trace_end() trace_end(NXTAG_I2S)
#  define i2s_trace_beginex(name) trace_beginex(NXTAG_I2S, name)
#  define i2s_trace_endex(name) trace_endex(NXTAG_I2S, name)
#  define i2s_trace_mark(s) trace_mark(NXTAG_I2S, s)
#  define i2s_trace_printf(fmt, ...) \
    trace_printf(NXTAG_I2S, fmt, ##__VA_ARGS__)
#  define i2s_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_I2S, level, id, fmt, ##__VA_ARGS__)
#else
#  define i2s_trace_begin()
#  define i2s_trace_end()
#  define i2s_trace_beginex(name)
#  define i2s_trace_endex(name)
#  define i2s_trace_mark(s)
#  define i2s_trace_printf(...)
#  define i2s_trace_diag(...)
#endif

#ifdef CONFIG_TRACE_I3C
#  define i3c_trace_begin() trace_begin(NXTAG_I3C)
#  define i3c_trace_end() trace_end(NXTAG_I3C)
#  define i3c_trace_beginex(name) trace_beginex(NXTAG_I3C, name)
#  define i3c_trace_endex(name) trace_endex(NXTAG_I3C, name)
#  define i3c_trace_mark(s) trace_mark(NXTAG_I3C, s)
#  define i3c_trace_printf(fmt, ...) \
    trace_printf(NXTAG_I3C, fmt, ##__VA_ARGS__)
#  define i3c_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_I3C, level, id, fmt, ##__VA_ARGS__)
#else
#  define i3c_trace_begin()
#  define i3c_trace_end()
#  define i3c_trace_beginex(name)
#  define i3c_trace_endex(name)
#  define i3c_trace_mark(s)
#  define i3c_trace_printf(...)
#  define i3c_trace_diag(...)
#endif

#ifdef CONFIG_TRACE_INPUT
#  define input_trace_begin() trace_begin(NXTAG_INPUT)
#  define input_trace_end() trace_end(NXTAG_INPUT)
#  define input_trace_beginex(name) trace_beginex(NXTAG_INPUT, name)
#  define input_trace_endex(name) trace_endex(NXTAG_INPUT, name)
#  define input_trace_mark(s) trace_mark(NXTAG_INPUT, s)
#  define input_trace_printf(fmt, ...) \
    trace_printf(NXTAG_INPUT, fmt, ##__VA_ARGS__)
#  define input_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_INPUT, level, id, fmt, ##__VA_ARGS__)
#else
#  define input_trace_begin()
#  define input_trace_end()
#  define input_trace_beginex(name)
#  define input_trace_endex(name)
#  define input_trace_mark(s)
#  define input_trace_printf(...)
#  define input_trace_diag(...)
#endif

#ifdef CONFIG_TRACE_IPC
#  define ipc_trace_begin() trace_begin(NXTAG_IPC)
#  define ipc_trace_end() trace_end(NXTAG_IPC)
#  define ipc_trace_beginex(name) trace_beginex(NXTAG_IPC, name)
#  define ipc_trace_endex(name) trace_endex(NXTAG_IPC, name)
#  define ipc_trace_mark(s) trace_mark(NXTAG_IPC, s)
#  define ipc_trace_printf(fmt, ...) \
    trace_printf(NXTAG_IPC, fmt, ##__VA_ARGS__)
#  define ipc_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_IPC, level, id, fmt, ##__VA_ARGS__)
#else
#  define ipc_trace_begin()
#  define ipc_trace_end()
#  define ipc_trace_beginex(name)
#  define ipc_trace_endex(name)
#  define ipc_trace_mark(s)
#  define ipc_trace_printf(...)
#  define ipc_trace_diag(...)
#endif

#ifdef CONFIG_TRACE_IRQ
#  define irq_trace_begin() trace_begin(NXTAG_IRQ)
#  define irq_trace_end() trace_end(NXTAG_IRQ)
#  define irq_trace_beginex(name) trace_beginex(NXTAG_IRQ, name)
#  define irq_trace_endex(name) trace_endex(NXTAG_IRQ, name)
#  define irq_trace_mark(s) trace_mark(NXTAG_IRQ, s)
#  define irq_trace_printf(fmt, ...) \
    trace_printf(NXTAG_IRQ, fmt, ##__VA_ARGS__)
#  define irq_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_IRQ, level, id, fmt, ##__VA_ARGS__)
#else
#  define irq_trace_begin()
#  define irq_trace_end()
#  define irq_trace_beginex(name)
#  define irq_trace_endex(name)
#  define irq_trace_mark(s)
#  define irq_trace_printf(...)
#  define irq_trace_diag(...)
#endif

#ifdef CONFIG_TRACE_LCD
#  define lcd_trace_begin() trace_begin(NXTAG_LCD)
#  define lcd_trace_end() trace_end(NXTAG_LCD)
#  define lcd_trace_beginex(name) trace_beginex(NXTAG_LCD, name)
#  define lcd_trace_endex(name) trace_endex(NXTAG_LCD, name)
#  define lcd_trace_mark(s) trace_mark(NXTAG_LCD, s)
#  define lcd_trace_printf(fmt, ...) \
    trace_printf(NXTAG_LCD, fmt, ##__VA_ARGS__)
#  define lcd_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_LCD, level, id, fmt, ##__VA_ARGS__)
#else
#  define lcd_trace_begin()
#  define lcd_trace_end()
#  define lcd_trace_beginex(name)
#  define lcd_trace_endex(name)
#  define lcd_trace_mark(s)
#  define lcd_trace_printf(...)
#  define lcd_trace_diag(...)
#endif

#ifdef CONFIG_TRACE_LEDS
#  define leds_trace_begin() trace_begin(NXTAG_LEDS)
#  define leds_trace_end() trace_end(NXTAG_LEDS)
#  define leds_trace_beginex(name) trace_beginex(NXTAG_LEDS, name)
#  define leds_trace_endex(name) trace_endex(NXTAG_LEDS, name)
#  define leds_trace_mark(s) trace_mark(NXTAG_LEDS, s)
#  define leds_trace_printf(fmt, ...) \
    trace_printf(NXTAG_LEDS, fmt, ##__VA_ARGS__)
#  define leds_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_LEDS, level, id, fmt, ##__VA_ARGS__)
#else
#  define leds_trace_begin()
#  define leds_trace_end()
#  define leds_trace_beginex(name)
#  define leds_trace_endex(name)
#  define leds_trace_mark(s)
#  define leds_trace_printf(...)
#  define leds_trace_diag(...)
#endif

#ifdef CONFIG_TRACE_LIB
#  define lib_trace_begin() trace_begin(NXTAG_LIB)
#  define lib_trace_end() trace_end(NXTAG_LIB)
#  define lib_trace_beginex(name) trace_beginex(NXTAG_LIB, name)
#  define lib_trace_endex(name) trace_endex(NXTAG_LIB, name)
#  define lib_trace_mark(s) trace_mark(NXTAG_LIB, s)
#  define lib_trace_printf(fmt, ...) \
    trace_printf(NXTAG_LIB, fmt, ##__VA_ARGS__)
#  define lib_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_LIB, level, id, fmt, ##__VA_ARGS__)
#else
#  define lib_trace_begin()
#  define lib_trace_end()
#  define lib_trace_beginex(name)
#  define lib_trace_endex(name)
#  define lib_trace_mark(s)
#  define lib_trace_printf(...)
#  define lib_trace_diag(...)
#endif

#ifdef CONFIG_TRACE_MEMCARD
#  define memcard_trace_begin() trace_begin(NXTAG_MEMCARD)
#  define memcard_trace_end() trace_end(NXTAG_MEMCARD)
#  define memcard_trace_beginex(name) trace_beginex(NXTAG_MEMCARD, name)
#  define memcard_trace_endex(name) trace_endex(NXTAG_MEMCARD, name)
#  define memcard_trace_mark(s) trace_mark(NXTAG_MEMCARD, s)
#  define memcard_trace_printf(fmt, ...) \
    trace_printf(NXTAG_MEMCARD, fmt, ##__VA_ARGS__)
#  define memcard_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_MEMCARD, level, id, fmt, ##__VA_ARGS__)
#else
#  define memcard_trace_begin()
#  define memcard_trace_end()
#  define memcard_trace_beginex(name)
#  define memcard_trace_endex(name)
#  define memcard_trace_mark(s)
#  define memcard_trace_printf(...)
#  define memcard_trace_diag(...)
#endif

#ifdef CONFIG_TRACE_MM
#  define mm_trace_begin() trace_begin(NXTAG_MM)
#  define mm_trace_end() trace_end(NXTAG_MM)
#  define mm_trace_beginex(name) trace_beginex(NXTAG_MM, name)
#  define mm_trace_endex(name) trace_endex(NXTAG_MM, name)
#  define mm_trace_mark(s) trace_mark(NXTAG_MM, s)
#  define mm_trace_printf(fmt, ...) \
    trace_printf(NXTAG_MM, fmt, ##__VA_ARGS__)
#  define mm_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_MM, level, id, fmt, ##__VA_ARGS__)
#else
#  define mm_trace_begin()
#  define mm_trace_end()
#  define mm_trace_beginex(name)
#  define mm_trace_endex(name)
#  define mm_trace_mark(s)
#  define mm_trace_printf(...)
#  define mm_trace_diag(...)
#endif

#ifdef CONFIG_TRACE_MOTOR
#  define motor_trace_begin() trace_begin(NXTAG_MOTOR)
#  define motor_trace_end() trace_end(NXTAG_MOTOR)
#  define motor_trace_beginex(name) trace_beginex(NXTAG_MOTOR, name)
#  define motor_trace_endex(name) trace_endex(NXTAG_MOTOR, name)
#  define motor_trace_mark(s) trace_mark(NXTAG_MOTOR, s)
#  define motor_trace_printf(fmt, ...) \
    trace_printf(NXTAG_MOTOR, fmt, ##__VA_ARGS__)
#  define motor_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_MOTOR, level, id, fmt, ##__VA_ARGS__)
#else
#  define motor_trace_begin()
#  define motor_trace_end()
#  define motor_trace_beginex(name)
#  define motor_trace_endex(name)
#  define motor_trace_mark(s)
#  define motor_trace_printf(...)
#  define motor_trace_diag(...)
#endif

#ifdef CONFIG_TRACE_NET
#  define net_trace_begin() trace_begin(NXTAG_NET)
#  define net_trace_end() trace_end(NXTAG_NET)
#  define net_trace_beginex(name) trace_beginex(NXTAG_NET, name)
#  define net_trace_endex(name) trace_endex(NXTAG_NET, name)
#  define net_trace_mark(s) trace_mark(NXTAG_NET, s)
#  define net_trace_printf(fmt, ...) \
    trace_printf(NXTAG_NET, fmt, ##__VA_ARGS__)
#  define net_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_NET, level, id, fmt, ##__VA_ARGS__)
#else
#  define net_trace_begin()
#  define net_trace_end()
#  define net_trace_beginex(name)
#  define net_trace_endex(name)
#  define net_trace_mark(s)
#  define net_trace_printf(...)
#  define net_trace_diag(...)
#endif

#ifdef CONFIG_TRACE_PAGING
#  define paging_trace_begin() trace_begin(NXTAG_PAGING)
#  define paging_trace_end() trace_end(NXTAG_PAGING)
#  define paging_trace_beginex(name) trace_beginex(NXTAG_PAGING, name)
#  define paging_trace_endex(name) trace_endex(NXTAG_PAGING, name)
#  define paging_trace_mark(s) trace_mark(NXTAG_PAGING, s)
#  define paging_trace_printf(fmt, ...) \
    trace_printf(NXTAG_PAGING, fmt, ##__VA_ARGS__)
#  define paging_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_PAGING, level, id, fmt, ##__VA_ARGS__)
#else
#  define paging_trace_begin()
#  define paging_trace_end()
#  define paging_trace_beginex(name)
#  define paging_trace_endex(name)
#  define paging_trace_mark(s)
#  define paging_trace_printf(...)
#  define paging_trace_diag(...)
#endif

#ifdef CONFIG_TRACE_PCI
#  define pci_trace_begin() trace_begin(NXTAG_PCI)
#  define pci_trace_end() trace_end(NXTAG_PCI)
#  define pci_trace_beginex(name) trace_beginex(NXTAG_PCI, name)
#  define pci_trace_endex(name) trace_endex(NXTAG_PCI, name)
#  define pci_trace_mark(s) trace_mark(NXTAG_PCI, s)
#  define pci_trace_printf(fmt, ...) \
    trace_printf(NXTAG_PCI, fmt, ##__VA_ARGS__)
#  define pci_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_PCI, level, id, fmt, ##__VA_ARGS__)
#else
#  define pci_trace_begin()
#  define pci_trace_end()
#  define pci_trace_beginex(name)
#  define pci_trace_endex(name)
#  define pci_trace_mark(s)
#  define pci_trace_printf(...)
#  define pci_trace_diag(...)
#endif

#ifdef CONFIG_TRACE_POWER
#  define power_trace_begin() trace_begin(NXTAG_POWER)
#  define power_trace_end() trace_end(NXTAG_POWER)
#  define power_trace_beginex(name) trace_beginex(NXTAG_POWER, name)
#  define power_trace_endex(name) trace_endex(NXTAG_POWER, name)
#  define power_trace_mark(s) trace_mark(NXTAG_POWER, s)
#  define power_trace_printf(fmt, ...) \
    trace_printf(NXTAG_POWER, fmt, ##__VA_ARGS__)
#  define power_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_POWER, level, id, fmt, ##__VA_ARGS__)
#else
#  define power_trace_begin()
#  define power_trace_end()
#  define power_trace_beginex(name)
#  define power_trace_endex(name)
#  define power_trace_mark(s)
#  define power_trace_printf(...)
#  define power_trace_diag(...)
#endif

#ifdef CONFIG_TRACE_PTP
#  define ptp_trace_begin() trace_begin(NXTAG_PTP)
#  define ptp_trace_end() trace_end(NXTAG_PTP)
#  define ptp_trace_beginex(name) trace_beginex(NXTAG_PTP, name)
#  define ptp_trace_endex(name) trace_endex(NXTAG_PTP, name)
#  define ptp_trace_mark(s) trace_mark(NXTAG_PTP, s)
#  define ptp_trace_printf(fmt, ...) \
    trace_printf(NXTAG_PTP, fmt, ##__VA_ARGS__)
#  define ptp_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_PTP, level, id, fmt, ##__VA_ARGS__)
#else
#  define ptp_trace_begin()
#  define ptp_trace_end()
#  define ptp_trace_beginex(name)
#  define ptp_trace_endex(name)
#  define ptp_trace_mark(s)
#  define ptp_trace_printf(...)
#  define ptp_trace_diag(...)
#endif

#ifdef CONFIG_TRACE_PWM
#  define pwm_trace_begin() trace_begin(NXTAG_PWM)
#  define pwm_trace_end() trace_end(NXTAG_PWM)
#  define pwm_trace_beginex(name) trace_beginex(NXTAG_PWM, name)
#  define pwm_trace_endex(name) trace_endex(NXTAG_PWM, name)
#  define pwm_trace_mark(s) trace_mark(NXTAG_PWM, s)
#  define pwm_trace_printf(fmt, ...) \
    trace_printf(NXTAG_PWM, fmt, ##__VA_ARGS__)
#  define pwm_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_PWM, level, id, fmt, ##__VA_ARGS__)
#else
#  define pwm_trace_begin()
#  define pwm_trace_end()
#  define pwm_trace_beginex(name)
#  define pwm_trace_endex(name)
#  define pwm_trace_mark(s)
#  define pwm_trace_printf(...)
#  define pwm_trace_diag(...)
#endif

#ifdef CONFIG_TRACE_RC
#  define rc_trace_begin() trace_begin(NXTAG_RC)
#  define rc_trace_end() trace_end(NXTAG_RC)
#  define rc_trace_beginex(name) trace_beginex(NXTAG_RC, name)
#  define rc_trace_endex(name) trace_endex(NXTAG_RC, name)
#  define rc_trace_mark(s) trace_mark(NXTAG_RC, s)
#  define rc_trace_printf(fmt, ...) \
    trace_printf(NXTAG_RC, fmt, ##__VA_ARGS__)
#  define rc_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_RC, level, id, fmt, ##__VA_ARGS__)
#else
#  define rc_trace_begin()
#  define rc_trace_end()
#  define rc_trace_beginex(name)
#  define rc_trace_endex(name)
#  define rc_trace_mark(s)
#  define rc_trace_printf(...)
#  define rc_trace_diag(...)
#endif

#ifdef CONFIG_TRACE_REGMAP
#  define regmap_trace_begin() trace_begin(NXTAG_REGMAP)
#  define regmap_trace_end() trace_end(NXTAG_REGMAP)
#  define regmap_trace_beginex(name) trace_beginex(NXTAG_REGMAP, name)
#  define regmap_trace_endex(name) trace_endex(NXTAG_REGMAP, name)
#  define regmap_trace_mark(s) trace_mark(NXTAG_REGMAP, s)
#  define regmap_trace_printf(fmt, ...) \
    trace_printf(NXTAG_REGMAP, fmt, ##__VA_ARGS__)
#  define regmap_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_REGMAP, level, id, fmt, ##__VA_ARGS__)
#else
#  define regmap_trace_begin()
#  define regmap_trace_end()
#  define regmap_trace_beginex(name)
#  define regmap_trace_endex(name)
#  define regmap_trace_mark(s)
#  define regmap_trace_printf(...)
#  define regmap_trace_diag(...)
#endif

#ifdef CONFIG_TRACE_RESET
#  define reset_trace_begin() trace_begin(NXTAG_RESET)
#  define reset_trace_end() trace_end(NXTAG_RESET)
#  define reset_trace_beginex(name) trace_beginex(NXTAG_RESET, name)
#  define reset_trace_endex(name) trace_endex(NXTAG_RESET, name)
#  define reset_trace_mark(s) trace_mark(NXTAG_RESET, s)
#  define reset_trace_printf(fmt, ...) \
    trace_printf(NXTAG_RESET, fmt, ##__VA_ARGS__)
#  define reset_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_RESET, level, id, fmt, ##__VA_ARGS__)
#else
#  define reset_trace_begin()
#  define reset_trace_end()
#  define reset_trace_beginex(name)
#  define reset_trace_endex(name)
#  define reset_trace_mark(s)
#  define reset_trace_printf(...)
#  define reset_trace_diag(...)
#endif

#ifdef CONFIG_TRACE_RMT
#  define rmt_trace_begin() trace_begin(NXTAG_RMT)
#  define rmt_trace_end() trace_end(NXTAG_RMT)
#  define rmt_trace_beginex(name) trace_beginex(NXTAG_RMT, name)
#  define rmt_trace_endex(name) trace_endex(NXTAG_RMT, name)
#  define rmt_trace_mark(s) trace_mark(NXTAG_RMT, s)
#  define rmt_trace_printf(fmt, ...) \
    trace_printf(NXTAG_RMT, fmt, ##__VA_ARGS__)
#  define rmt_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_RMT, level, id, fmt, ##__VA_ARGS__)
#else
#  define rmt_trace_begin()
#  define rmt_trace_end()
#  define rmt_trace_beginex(name)
#  define rmt_trace_endex(name)
#  define rmt_trace_mark(s)
#  define rmt_trace_printf(...)
#  define rmt_trace_diag(...)
#endif

#ifdef CONFIG_TRACE_RPMSG
#  define rpmsg_trace_begin() trace_begin(NXTAG_RPMSG)
#  define rpmsg_trace_end() trace_end(NXTAG_RPMSG)
#  define rpmsg_trace_beginex(name) trace_beginex(NXTAG_RPMSG, name)
#  define rpmsg_trace_endex(name) trace_endex(NXTAG_RPMSG, name)
#  define rpmsg_trace_mark(s) trace_mark(NXTAG_RPMSG, s)
#  define rpmsg_trace_printf(fmt, ...) \
    trace_printf(NXTAG_RPMSG, fmt, ##__VA_ARGS__)
#  define rpmsg_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_RPMSG, level, id, fmt, ##__VA_ARGS__)
#else
#  define rpmsg_trace_begin()
#  define rpmsg_trace_end()
#  define rpmsg_trace_beginex(name)
#  define rpmsg_trace_endex(name)
#  define rpmsg_trace_mark(s)
#  define rpmsg_trace_printf(...)
#  define rpmsg_trace_diag(...)
#endif

#ifdef CONFIG_TRACE_RPTUN
#  define rptun_trace_begin() trace_begin(NXTAG_RPTUN)
#  define rptun_trace_end() trace_end(NXTAG_RPTUN)
#  define rptun_trace_beginex(name) trace_beginex(NXTAG_RPTUN, name)
#  define rptun_trace_endex(name) trace_endex(NXTAG_RPTUN, name)
#  define rptun_trace_mark(s) trace_mark(NXTAG_RPTUN, s)
#  define rptun_trace_printf(fmt, ...) \
    trace_printf(NXTAG_RPTUN, fmt, ##__VA_ARGS__)
#  define rptun_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_RPTUN, level, id, fmt, ##__VA_ARGS__)
#else
#  define rptun_trace_begin()
#  define rptun_trace_end()
#  define rptun_trace_beginex(name)
#  define rptun_trace_endex(name)
#  define rptun_trace_mark(s)
#  define rptun_trace_printf(...)
#  define rptun_trace_diag(...)
#endif

#ifdef CONFIG_TRACE_RTC
#  define rtc_trace_begin() trace_begin(NXTAG_RTC)
#  define rtc_trace_end() trace_end(NXTAG_RTC)
#  define rtc_trace_beginex(name) trace_beginex(NXTAG_RTC, name)
#  define rtc_trace_endex(name) trace_endex(NXTAG_RTC, name)
#  define rtc_trace_mark(s) trace_mark(NXTAG_RTC, s)
#  define rtc_trace_printf(fmt, ...) \
    trace_printf(NXTAG_RTC, fmt, ##__VA_ARGS__)
#  define rtc_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_RTC, level, id, fmt, ##__VA_ARGS__)
#else
#  define rtc_trace_begin()
#  define rtc_trace_end()
#  define rtc_trace_beginex(name)
#  define rtc_trace_endex(name)
#  define rtc_trace_mark(s)
#  define rtc_trace_printf(...)
#  define rtc_trace_diag(...)
#endif

#ifdef CONFIG_TRACE_SAFETY
#  define safety_trace_begin() trace_begin(NXTAG_SAFETY)
#  define safety_trace_end() trace_end(NXTAG_SAFETY)
#  define safety_trace_beginex(name) trace_beginex(NXTAG_SAFETY, name)
#  define safety_trace_endex(name) trace_endex(NXTAG_SAFETY, name)
#  define safety_trace_mark(s) trace_mark(NXTAG_SAFETY, s)
#  define safety_trace_printf(fmt, ...) \
    trace_printf(NXTAG_SAFETY, fmt, ##__VA_ARGS__)
#  define safety_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_SAFETY, level, id, fmt, ##__VA_ARGS__)
#else
#  define safety_trace_begin()
#  define safety_trace_end()
#  define safety_trace_beginex(name)
#  define safety_trace_endex(name)
#  define safety_trace_mark(s)
#  define safety_trace_printf(...)
#  define safety_trace_diag(...)
#endif

#ifdef CONFIG_TRACE_SCHED
#  define sched_trace_begin() trace_begin(NXTAG_SCHED)
#  define sched_trace_end() trace_end(NXTAG_SCHED)
#  define sched_trace_beginex(name) trace_beginex(NXTAG_SCHED, name)
#  define sched_trace_endex(name) trace_endex(NXTAG_SCHED, name)
#  define sched_trace_mark(s) trace_mark(NXTAG_SCHED, s)
#  define sched_trace_printf(fmt, ...) \
    trace_printf(NXTAG_SCHED, fmt, ##__VA_ARGS__)
#  define sched_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_SCHED, level, id, fmt, ##__VA_ARGS__)
#else
#  define sched_trace_begin()
#  define sched_trace_end()
#  define sched_trace_beginex(name)
#  define sched_trace_endex(name)
#  define sched_trace_mark(s)
#  define sched_trace_printf(...)
#  define sched_trace_diag(...)
#endif

#ifdef CONFIG_TRACE_SENSORS
#  define sensors_trace_begin() trace_begin(NXTAG_SENSORS)
#  define sensors_trace_end() trace_end(NXTAG_SENSORS)
#  define sensors_trace_beginex(name) trace_beginex(NXTAG_SENSORS, name)
#  define sensors_trace_endex(name) trace_endex(NXTAG_SENSORS, name)
#  define sensors_trace_mark(s) trace_mark(NXTAG_SENSORS, s)
#  define sensors_trace_printf(fmt, ...) \
    trace_printf(NXTAG_SENSORS, fmt, ##__VA_ARGS__)
#  define sensors_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_SENSORS, level, id, fmt, ##__VA_ARGS__)
#else
#  define sensors_trace_begin()
#  define sensors_trace_end()
#  define sensors_trace_beginex(name)
#  define sensors_trace_endex(name)
#  define sensors_trace_mark(s)
#  define sensors_trace_printf(...)
#  define sensors_trace_diag(...)
#endif

#ifdef CONFIG_TRACE_SPI
#  define spi_trace_begin() trace_begin(NXTAG_SPI)
#  define spi_trace_end() trace_end(NXTAG_SPI)
#  define spi_trace_beginex(name) trace_beginex(NXTAG_SPI, name)
#  define spi_trace_endex(name) trace_endex(NXTAG_SPI, name)
#  define spi_trace_mark(s) trace_mark(NXTAG_SPI, s)
#  define spi_trace_printf(fmt, ...) \
    trace_printf(NXTAG_SPI, fmt, ##__VA_ARGS__)
#  define spi_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_SPI, level, id, fmt, ##__VA_ARGS__)
#else
#  define spi_trace_begin()
#  define spi_trace_end()
#  define spi_trace_beginex(name)
#  define spi_trace_endex(name)
#  define spi_trace_mark(s)
#  define spi_trace_printf(...)
#  define spi_trace_diag(...)
#endif

#ifdef CONFIG_TRACE_STEPPER
#  define stepper_trace_begin() trace_begin(NXTAG_STEPPER)
#  define stepper_trace_end() trace_end(NXTAG_STEPPER)
#  define stepper_trace_beginex(name) trace_beginex(NXTAG_STEPPER, name)
#  define stepper_trace_endex(name) trace_endex(NXTAG_STEPPER, name)
#  define stepper_trace_mark(s) trace_mark(NXTAG_STEPPER, s)
#  define stepper_trace_printf(fmt, ...) \
    trace_printf(NXTAG_STEPPER, fmt, ##__VA_ARGS__)
#  define stepper_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_STEPPER, level, id, fmt, ##__VA_ARGS__)
#else
#  define stepper_trace_begin()
#  define stepper_trace_end()
#  define stepper_trace_beginex(name)
#  define stepper_trace_endex(name)
#  define stepper_trace_mark(s)
#  define stepper_trace_printf(...)
#  define stepper_trace_diag(...)
#endif

#ifdef CONFIG_TRACE_SYSCALL
#  define syscall_trace_begin() trace_begin(NXTAG_SYSCALL)
#  define syscall_trace_end() trace_end(NXTAG_SYSCALL)
#  define syscall_trace_beginex(name) trace_beginex(NXTAG_SYSCALL, name)
#  define syscall_trace_endex(name) trace_endex(NXTAG_SYSCALL, name)
#  define syscall_trace_mark(s) trace_mark(NXTAG_SYSCALL, s)
#  define syscall_trace_printf(fmt, ...) \
    trace_printf(NXTAG_SYSCALL, fmt, ##__VA_ARGS__)
#  define syscall_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_SYSCALL, level, id, fmt, ##__VA_ARGS__)
#else
#  define syscall_trace_begin()
#  define syscall_trace_end()
#  define syscall_trace_beginex(name)
#  define syscall_trace_endex(name)
#  define syscall_trace_mark(s)
#  define syscall_trace_printf(...)
#  define syscall_trace_diag(...)
#endif

#ifdef CONFIG_TRACE_THERMAL
#  define thermal_trace_begin() trace_begin(NXTAG_THERMAL)
#  define thermal_trace_end() trace_end(NXTAG_THERMAL)
#  define thermal_trace_beginex(name) trace_beginex(NXTAG_THERMAL, name)
#  define thermal_trace_endex(name) trace_endex(NXTAG_THERMAL, name)
#  define thermal_trace_mark(s) trace_mark(NXTAG_THERMAL, s)
#  define thermal_trace_printf(fmt, ...) \
    trace_printf(NXTAG_THERMAL, fmt, ##__VA_ARGS__)
#  define thermal_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_THERMAL, level, id, fmt, ##__VA_ARGS__)
#else
#  define thermal_trace_begin()
#  define thermal_trace_end()
#  define thermal_trace_beginex(name)
#  define thermal_trace_endex(name)
#  define thermal_trace_mark(s)
#  define thermal_trace_printf(...)
#  define thermal_trace_diag(...)
#endif

#ifdef CONFIG_TRACE_TIMER
#  define timer_trace_begin() trace_begin(NXTAG_TIMER)
#  define timer_trace_end() trace_end(NXTAG_TIMER)
#  define timer_trace_beginex(name) trace_beginex(NXTAG_TIMER, name)
#  define timer_trace_endex(name) trace_endex(NXTAG_TIMER, name)
#  define timer_trace_mark(s) trace_mark(NXTAG_TIMER, s)
#  define timer_trace_printf(fmt, ...) \
    trace_printf(NXTAG_TIMER, fmt, ##__VA_ARGS__)
#  define timer_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_TIMER, level, id, fmt, ##__VA_ARGS__)
#else
#  define timer_trace_begin()
#  define timer_trace_end()
#  define timer_trace_beginex(name)
#  define timer_trace_endex(name)
#  define timer_trace_mark(s)
#  define timer_trace_printf(...)
#  define timer_trace_diag(...)
#endif

#ifdef CONFIG_TRACE_USB
#  define usb_trace_begin() trace_begin(NXTAG_USB)
#  define usb_trace_end() trace_end(NXTAG_USB)
#  define usb_trace_beginex(name) trace_beginex(NXTAG_USB, name)
#  define usb_trace_endex(name) trace_endex(NXTAG_USB, name)
#  define usb_trace_mark(s) trace_mark(NXTAG_USB, s)
#  define usb_trace_printf(fmt, ...) \
    trace_printf(NXTAG_USB, fmt, ##__VA_ARGS__)
#  define usb_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_USB, level, id, fmt, ##__VA_ARGS__)
#else
#  define usb_trace_begin()
#  define usb_trace_end()
#  define usb_trace_beginex(name)
#  define usb_trace_endex(name)
#  define usb_trace_mark(s)
#  define usb_trace_printf(...)
#  define usb_trace_diag(...)
#endif

#ifdef CONFIG_TRACE_VHOST
#  define vhost_trace_begin() trace_begin(NXTAG_VHOST)
#  define vhost_trace_end() trace_end(NXTAG_VHOST)
#  define vhost_trace_beginex(name) trace_beginex(NXTAG_VHOST, name)
#  define vhost_trace_endex(name) trace_endex(NXTAG_VHOST, name)
#  define vhost_trace_mark(s) trace_mark(NXTAG_VHOST, s)
#  define vhost_trace_printf(fmt, ...) \
    trace_printf(NXTAG_VHOST, fmt, ##__VA_ARGS__)
#  define vhost_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_VHOST, level, id, fmt, ##__VA_ARGS__)
#else
#  define vhost_trace_begin()
#  define vhost_trace_end()
#  define vhost_trace_beginex(name)
#  define vhost_trace_endex(name)
#  define vhost_trace_mark(s)
#  define vhost_trace_printf(...)
#  define vhost_trace_diag(...)
#endif

#ifdef CONFIG_TRACE_VIDEO
#  define video_trace_begin() trace_begin(NXTAG_VIDEO)
#  define video_trace_end() trace_end(NXTAG_VIDEO)
#  define video_trace_beginex(name) trace_beginex(NXTAG_VIDEO, name)
#  define video_trace_endex(name) trace_endex(NXTAG_VIDEO, name)
#  define video_trace_mark(s) trace_mark(NXTAG_VIDEO, s)
#  define video_trace_printf(fmt, ...) \
    trace_printf(NXTAG_VIDEO, fmt, ##__VA_ARGS__)
#  define video_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_VIDEO, level, id, fmt, ##__VA_ARGS__)
#else
#  define video_trace_begin()
#  define video_trace_end()
#  define video_trace_beginex(name)
#  define video_trace_endex(name)
#  define video_trace_mark(s)
#  define video_trace_printf(...)
#  define video_trace_diag(...)
#endif

#ifdef CONFIG_TRACE_VIRTIO
#  define virtio_trace_begin() trace_begin(NXTAG_VIRTIO)
#  define virtio_trace_end() trace_end(NXTAG_VIRTIO)
#  define virtio_trace_beginex(name) trace_beginex(NXTAG_VIRTIO, name)
#  define virtio_trace_endex(name) trace_endex(NXTAG_VIRTIO, name)
#  define virtio_trace_mark(s) trace_mark(NXTAG_VIRTIO, s)
#  define virtio_trace_printf(fmt, ...) \
    trace_printf(NXTAG_VIRTIO, fmt, ##__VA_ARGS__)
#  define virtio_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_VIRTIO, level, id, fmt, ##__VA_ARGS__)
#else
#  define virtio_trace_begin()
#  define virtio_trace_end()
#  define virtio_trace_beginex(name)
#  define virtio_trace_endex(name)
#  define virtio_trace_mark(s)
#  define virtio_trace_printf(...)
#  define virtio_trace_diag(...)
#endif

#ifdef CONFIG_TRACE_WATCHDOG
#  define watchdog_trace_begin() trace_begin(NXTAG_WATCHDOG)
#  define watchdog_trace_end() trace_end(NXTAG_WATCHDOG)
#  define watchdog_trace_beginex(name) trace_beginex(NXTAG_WATCHDOG, name)
#  define watchdog_trace_endex(name) trace_endex(NXTAG_WATCHDOG, name)
#  define watchdog_trace_mark(s) trace_mark(NXTAG_WATCHDOG, s)
#  define watchdog_trace_printf(fmt, ...) \
    trace_printf(NXTAG_WATCHDOG, fmt, ##__VA_ARGS__)
#  define watchdog_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_WATCHDOG, level, id, fmt, ##__VA_ARGS__)
#else
#  define watchdog_trace_begin()
#  define watchdog_trace_end()
#  define watchdog_trace_beginex(name)
#  define watchdog_trace_endex(name)
#  define watchdog_trace_mark(s)
#  define watchdog_trace_printf(...)
#  define watchdog_trace_diag(...)
#endif

#ifdef CONFIG_TRACE_WIRELESS
#  define wireless_trace_begin() trace_begin(NXTAG_WIRELESS)
#  define wireless_trace_end() trace_end(NXTAG_WIRELESS)
#  define wireless_trace_beginex(name) trace_beginex(NXTAG_WIRELESS, name)
#  define wireless_trace_endex(name) trace_endex(NXTAG_WIRELESS, name)
#  define wireless_trace_mark(s) trace_mark(NXTAG_WIRELESS, s)
#  define wireless_trace_printf(fmt, ...) \
    trace_printf(NXTAG_WIRELESS, fmt, ##__VA_ARGS__)
#  define wireless_trace_diag(level, id, fmt, ...) \
    trace_diag(NXTAG_WIRELESS, level, id, fmt, ##__VA_ARGS__)
#else
#  define wireless_trace_begin()
#  define wireless_trace_end()
#  define wireless_trace_beginex(name)
#  define wireless_trace_endex(name)
#  define wireless_trace_mark(s)
#  define wireless_trace_printf(...)
#  define wireless_trace_diag(...)
#endif
/* Diagnostic IDs */

#define DIAG_MM_ALLOC_FAIL       0
#define DIAG_MM_FREE_FAIL        1

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
