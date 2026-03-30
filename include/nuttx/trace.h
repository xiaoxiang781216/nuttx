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
#  define trace_event(tag, level, id, fmt, ...) \
    sched_note_printf_ip(tag, level, id, fmt, 0, ##__VA_ARGS__)
#else
#  define trace_begin(tag)
#  define trace_end(tag)
#  define trace_beginex(tag, name)
#  define trace_endex(tag, name)
#  define trace_mark(tag, s)
#  define trace_event(...)
#endif

#ifdef CONFIG_TRACE_APP
#  define app_trace_begin() trace_begin(NXTAG_APP)
#  define app_trace_end() trace_end(NXTAG_APP)
#  define app_trace_beginex(name) trace_beginex(NXTAG_APP, name)
#  define app_trace_endex(name) trace_endex(NXTAG_APP, name)
#  define app_trace_mark(s) trace_mark(NXTAG_APP, s)
#  define app_trace_event(level, id, fmt, ...) \
    trace_event(NXTAG_APP, level, id, fmt, ##__VA_ARGS__)
#else
#  define app_trace_begin()
#  define app_trace_end()
#  define app_trace_beginex(name)
#  define app_trace_endex(name)
#  define app_trace_mark(s)
#  define app_trace_event(...)
#endif

#ifdef CONFIG_TRACE_ANALOG
#  define analog_trace_begin() trace_begin(NXTAG_ANALOG)
#  define analog_trace_end() trace_end(NXTAG_ANALOG)
#  define analog_trace_beginex(name) trace_beginex(NXTAG_ANALOG, name)
#  define analog_trace_endex(name) trace_endex(NXTAG_ANALOG, name)
#  define analog_trace_mark(s) trace_mark(NXTAG_ANALOG, s)
#  define analog_trace_event(level, id, fmt, ...) \
    trace_event(NXTAG_ANALOG, level, id, fmt, ##__VA_ARGS__)
#else
#  define analog_trace_begin()
#  define analog_trace_end()
#  define analog_trace_beginex(name)
#  define analog_trace_endex(name)
#  define analog_trace_mark(s)
#  define analog_trace_event(...)
#endif

#ifdef CONFIG_TRACE_AUDIO
#  define audio_trace_begin() trace_begin(NXTAG_AUDIO)
#  define audio_trace_end() trace_end(NXTAG_AUDIO)
#  define audio_trace_beginex(name) trace_beginex(NXTAG_AUDIO, name)
#  define audio_trace_endex(name) trace_endex(NXTAG_AUDIO, name)
#  define audio_trace_mark(s) trace_mark(NXTAG_AUDIO, s)
#  define audio_trace_event(level, id, fmt, ...) \
    trace_event(NXTAG_AUDIO, level, id, fmt, ##__VA_ARGS__)
#else
#  define audio_trace_begin()
#  define audio_trace_end()
#  define audio_trace_beginex(name)
#  define audio_trace_endex(name)
#  define audio_trace_mark(s)
#  define audio_trace_event(...)
#endif

#ifdef CONFIG_TRACE_BATTERY
#  define battery_trace_begin() trace_begin(NXTAG_BATTERY)
#  define battery_trace_end() trace_end(NXTAG_BATTERY)
#  define battery_trace_beginex(name) trace_beginex(NXTAG_BATTERY, name)
#  define battery_trace_endex(name) trace_endex(NXTAG_BATTERY, name)
#  define battery_trace_mark(s) trace_mark(NXTAG_BATTERY, s)
#  define battery_trace_event(level, id, fmt, ...) \
    trace_event(NXTAG_BATTERY, level, id, fmt, ##__VA_ARGS__)
#else
#  define battery_trace_begin()
#  define battery_trace_end()
#  define battery_trace_beginex(name)
#  define battery_trace_endex(name)
#  define battery_trace_mark(s)
#  define battery_trace_event(...)
#endif

#ifdef CONFIG_TRACE_BINFMT
#  define binfmt_trace_begin() trace_begin(NXTAG_BINFMT)
#  define binfmt_trace_end() trace_end(NXTAG_BINFMT)
#  define binfmt_trace_beginex(name) trace_beginex(NXTAG_BINFMT, name)
#  define binfmt_trace_endex(name) trace_endex(NXTAG_BINFMT, name)
#  define binfmt_trace_mark(s) trace_mark(NXTAG_BINFMT, s)
#  define binfmt_trace_event(level, id, fmt, ...) \
    trace_event(NXTAG_BINFMT, level, id, fmt, ##__VA_ARGS__)
#else
#  define binfmt_trace_begin()
#  define binfmt_trace_end()
#  define binfmt_trace_beginex(name)
#  define binfmt_trace_endex(name)
#  define binfmt_trace_mark(s)
#  define binfmt_trace_event(...)
#endif

#ifdef CONFIG_TRACE_CAN
#  define can_trace_begin() trace_begin(NXTAG_CAN)
#  define can_trace_end() trace_end(NXTAG_CAN)
#  define can_trace_beginex(name) trace_beginex(NXTAG_CAN, name)
#  define can_trace_endex(name) trace_endex(NXTAG_CAN, name)
#  define can_trace_mark(s) trace_mark(NXTAG_CAN, s)
#  define can_trace_event(level, id, fmt, ...) \
    trace_event(NXTAG_CAN, level, id, fmt, ##__VA_ARGS__)
#else
#  define can_trace_begin()
#  define can_trace_end()
#  define can_trace_beginex(name)
#  define can_trace_endex(name)
#  define can_trace_mark(s)
#  define can_trace_event(...)
#endif

#ifdef CONFIG_TRACE_CAPTURE
#  define capture_trace_begin() trace_begin(NXTAG_CAPTURE)
#  define capture_trace_end() trace_end(NXTAG_CAPTURE)
#  define capture_trace_beginex(name) trace_beginex(NXTAG_CAPTURE, name)
#  define capture_trace_endex(name) trace_endex(NXTAG_CAPTURE, name)
#  define capture_trace_mark(s) trace_mark(NXTAG_CAPTURE, s)
#  define capture_trace_event(level, id, fmt, ...) \
    trace_event(NXTAG_CAPTURE, level, id, fmt, ##__VA_ARGS__)
#else
#  define capture_trace_begin()
#  define capture_trace_end()
#  define capture_trace_beginex(name)
#  define capture_trace_endex(name)
#  define capture_trace_mark(s)
#  define capture_trace_event(...)
#endif

#ifdef CONFIG_TRACE_CONTACTLESS
#  define contactless_trace_begin() trace_begin(NXTAG_CONTACTLESS)
#  define contactless_trace_end() trace_end(NXTAG_CONTACTLESS)
#  define contactless_trace_beginex(name) trace_beginex(NXTAG_CONTACTLESS, name)
#  define contactless_trace_endex(name) trace_endex(NXTAG_CONTACTLESS, name)
#  define contactless_trace_mark(s) trace_mark(NXTAG_CONTACTLESS, s)
#  define contactless_trace_event(level, id, fmt, ...) \
    trace_event(NXTAG_CONTACTLESS, level, id, fmt, ##__VA_ARGS__)
#else
#  define contactless_trace_begin()
#  define contactless_trace_end()
#  define contactless_trace_beginex(name)
#  define contactless_trace_endex(name)
#  define contactless_trace_mark(s)
#  define contactless_trace_event(...)
#endif

#ifdef CONFIG_TRACE_CRYPTO
#  define crypto_trace_begin() trace_begin(NXTAG_CRYPTO)
#  define crypto_trace_end() trace_end(NXTAG_CRYPTO)
#  define crypto_trace_beginex(name) trace_beginex(NXTAG_CRYPTO, name)
#  define crypto_trace_endex(name) trace_endex(NXTAG_CRYPTO, name)
#  define crypto_trace_mark(s) trace_mark(NXTAG_CRYPTO, s)
#  define crypto_trace_event(level, id, fmt, ...) \
    trace_event(NXTAG_CRYPTO, level, id, fmt, ##__VA_ARGS__)
#else
#  define crypto_trace_begin()
#  define crypto_trace_end()
#  define crypto_trace_beginex(name)
#  define crypto_trace_endex(name)
#  define crypto_trace_mark(s)
#  define crypto_trace_event(...)
#endif

#ifdef CONFIG_TRACE_DMA
#  define dma_trace_begin() trace_begin(NXTAG_DMA)
#  define dma_trace_end() trace_end(NXTAG_DMA)
#  define dma_trace_beginex(name) trace_beginex(NXTAG_DMA, name)
#  define dma_trace_endex(name) trace_endex(NXTAG_DMA, name)
#  define dma_trace_mark(s) trace_mark(NXTAG_DMA, s)
#  define dma_trace_event(level, id, fmt, ...) \
    trace_event(NXTAG_DMA, level, id, fmt, ##__VA_ARGS__)
#else
#  define dma_trace_begin()
#  define dma_trace_end()
#  define dma_trace_beginex(name)
#  define dma_trace_endex(name)
#  define dma_trace_mark(s)
#  define dma_trace_event(...)
#endif

#ifdef CONFIG_TRACE_FS
#  define fs_trace_begin() trace_begin(NXTAG_FS)
#  define fs_trace_end() trace_end(NXTAG_FS)
#  define fs_trace_beginex(name) trace_beginex(NXTAG_FS, name)
#  define fs_trace_endex(name) trace_endex(NXTAG_FS, name)
#  define fs_trace_mark(s) trace_mark(NXTAG_FS, s)
#  define fs_trace_event(level, id, fmt, ...) \
    trace_event(NXTAG_FS, level, id, fmt, ##__VA_ARGS__)
#else
#  define fs_trace_begin()
#  define fs_trace_end()
#  define fs_trace_beginex(name)
#  define fs_trace_endex(name)
#  define fs_trace_mark(s)
#  define fs_trace_event(...)
#endif

#ifdef CONFIG_TRACE_GPIO
#  define gpio_trace_begin() trace_begin(NXTAG_GPIO)
#  define gpio_trace_end() trace_end(NXTAG_GPIO)
#  define gpio_trace_beginex(name) trace_beginex(NXTAG_GPIO, name)
#  define gpio_trace_endex(name) trace_endex(NXTAG_GPIO, name)
#  define gpio_trace_mark(s) trace_mark(NXTAG_GPIO, s)
#  define gpio_trace_event(level, id, fmt, ...) \
    trace_event(NXTAG_GPIO, level, id, fmt, ##__VA_ARGS__)
#else
#  define gpio_trace_begin()
#  define gpio_trace_end()
#  define gpio_trace_beginex(name)
#  define gpio_trace_endex(name)
#  define gpio_trace_mark(s)
#  define gpio_trace_event(...)
#endif

#ifdef CONFIG_TRACE_GRAPHICS
#  define graphics_trace_begin() trace_begin(NXTAG_GRAPHICS)
#  define graphics_trace_end() trace_end(NXTAG_GRAPHICS)
#  define graphics_trace_beginex(name) trace_beginex(NXTAG_GRAPHICS, name)
#  define graphics_trace_endex(name) trace_endex(NXTAG_GRAPHICS, name)
#  define graphics_trace_mark(s) trace_mark(NXTAG_GRAPHICS, s)
#  define graphics_trace_event(level, id, fmt, ...) \
    trace_event(NXTAG_GRAPHICS, level, id, fmt, ##__VA_ARGS__)
#else
#  define graphics_trace_begin()
#  define graphics_trace_end()
#  define graphics_trace_beginex(name)
#  define graphics_trace_endex(name)
#  define graphics_trace_mark(s)
#  define graphics_trace_event(...)
#endif

#ifdef CONFIG_TRACE_HWTRACING
#  define hwtracing_trace_begin() trace_begin(NXTAG_HWTRACING)
#  define hwtracing_trace_end() trace_end(NXTAG_HWTRACING)
#  define hwtracing_trace_beginex(name) trace_beginex(NXTAG_HWTRACING, name)
#  define hwtracing_trace_endex(name) trace_endex(NXTAG_HWTRACING, name)
#  define hwtracing_trace_mark(s) trace_mark(NXTAG_HWTRACING, s)
#  define hwtracing_trace_event(level, id, fmt, ...) \
    trace_event(NXTAG_HWTRACING, level, id, fmt, ##__VA_ARGS__)
#else
#  define hwtracing_trace_begin()
#  define hwtracing_trace_end()
#  define hwtracing_trace_beginex(name)
#  define hwtracing_trace_endex(name)
#  define hwtracing_trace_mark(s)
#  define hwtracing_trace_event(...)
#endif

#ifdef CONFIG_TRACE_I2C
#  define i2c_trace_begin() trace_begin(NXTAG_I2C)
#  define i2c_trace_end() trace_end(NXTAG_I2C)
#  define i2c_trace_beginex(name) trace_beginex(NXTAG_I2C, name)
#  define i2c_trace_endex(name) trace_endex(NXTAG_I2C, name)
#  define i2c_trace_mark(s) trace_mark(NXTAG_I2C, s)
#  define i2c_trace_event(level, id, fmt, ...) \
    trace_event(NXTAG_I2C, level, id, fmt, ##__VA_ARGS__)
#else
#  define i2c_trace_begin()
#  define i2c_trace_end()
#  define i2c_trace_beginex(name)
#  define i2c_trace_endex(name)
#  define i2c_trace_mark(s)
#  define i2c_trace_event(...)
#endif

#ifdef CONFIG_TRACE_I2S
#  define i2s_trace_begin() trace_begin(NXTAG_I2S)
#  define i2s_trace_end() trace_end(NXTAG_I2S)
#  define i2s_trace_beginex(name) trace_beginex(NXTAG_I2S, name)
#  define i2s_trace_endex(name) trace_endex(NXTAG_I2S, name)
#  define i2s_trace_mark(s) trace_mark(NXTAG_I2S, s)
#  define i2s_trace_event(level, id, fmt, ...) \
    trace_event(NXTAG_I2S, level, id, fmt, ##__VA_ARGS__)
#else
#  define i2s_trace_begin()
#  define i2s_trace_end()
#  define i2s_trace_beginex(name)
#  define i2s_trace_endex(name)
#  define i2s_trace_mark(s)
#  define i2s_trace_event(...)
#endif

#ifdef CONFIG_TRACE_I3C
#  define i3c_trace_begin() trace_begin(NXTAG_I3C)
#  define i3c_trace_end() trace_end(NXTAG_I3C)
#  define i3c_trace_beginex(name) trace_beginex(NXTAG_I3C, name)
#  define i3c_trace_endex(name) trace_endex(NXTAG_I3C, name)
#  define i3c_trace_mark(s) trace_mark(NXTAG_I3C, s)
#  define i3c_trace_event(level, id, fmt, ...) \
    trace_event(NXTAG_I3C, level, id, fmt, ##__VA_ARGS__)
#else
#  define i3c_trace_begin()
#  define i3c_trace_end()
#  define i3c_trace_beginex(name)
#  define i3c_trace_endex(name)
#  define i3c_trace_mark(s)
#  define i3c_trace_event(...)
#endif

#ifdef CONFIG_TRACE_INPUT
#  define input_trace_begin() trace_begin(NXTAG_INPUT)
#  define input_trace_end() trace_end(NXTAG_INPUT)
#  define input_trace_beginex(name) trace_beginex(NXTAG_INPUT, name)
#  define input_trace_endex(name) trace_endex(NXTAG_INPUT, name)
#  define input_trace_mark(s) trace_mark(NXTAG_INPUT, s)
#  define input_trace_event(level, id, fmt, ...) \
    trace_event(NXTAG_INPUT, level, id, fmt, ##__VA_ARGS__)
#else
#  define input_trace_begin()
#  define input_trace_end()
#  define input_trace_beginex(name)
#  define input_trace_endex(name)
#  define input_trace_mark(s)
#  define input_trace_event(...)
#endif

#ifdef CONFIG_TRACE_IPC
#  define ipc_trace_begin() trace_begin(NXTAG_IPC)
#  define ipc_trace_end() trace_end(NXTAG_IPC)
#  define ipc_trace_beginex(name) trace_beginex(NXTAG_IPC, name)
#  define ipc_trace_endex(name) trace_endex(NXTAG_IPC, name)
#  define ipc_trace_mark(s) trace_mark(NXTAG_IPC, s)
#  define ipc_trace_event(level, id, fmt, ...) \
    trace_event(NXTAG_IPC, level, id, fmt, ##__VA_ARGS__)
#else
#  define ipc_trace_begin()
#  define ipc_trace_end()
#  define ipc_trace_beginex(name)
#  define ipc_trace_endex(name)
#  define ipc_trace_mark(s)
#  define ipc_trace_event(...)
#endif

#ifdef CONFIG_TRACE_IRQ
#  define irq_trace_begin() trace_begin(NXTAG_IRQ)
#  define irq_trace_end() trace_end(NXTAG_IRQ)
#  define irq_trace_beginex(name) trace_beginex(NXTAG_IRQ, name)
#  define irq_trace_endex(name) trace_endex(NXTAG_IRQ, name)
#  define irq_trace_mark(s) trace_mark(NXTAG_IRQ, s)
#  define irq_trace_event(level, id, fmt, ...) \
    trace_event(NXTAG_IRQ, level, id, fmt, ##__VA_ARGS__)
#else
#  define irq_trace_begin()
#  define irq_trace_end()
#  define irq_trace_beginex(name)
#  define irq_trace_endex(name)
#  define irq_trace_mark(s)
#  define irq_trace_event(...)
#endif

#ifdef CONFIG_TRACE_LCD
#  define lcd_trace_begin() trace_begin(NXTAG_LCD)
#  define lcd_trace_end() trace_end(NXTAG_LCD)
#  define lcd_trace_beginex(name) trace_beginex(NXTAG_LCD, name)
#  define lcd_trace_endex(name) trace_endex(NXTAG_LCD, name)
#  define lcd_trace_mark(s) trace_mark(NXTAG_LCD, s)
#  define lcd_trace_event(level, id, fmt, ...) \
    trace_event(NXTAG_LCD, level, id, fmt, ##__VA_ARGS__)
#else
#  define lcd_trace_begin()
#  define lcd_trace_end()
#  define lcd_trace_beginex(name)
#  define lcd_trace_endex(name)
#  define lcd_trace_mark(s)
#  define lcd_trace_event(...)
#endif

#ifdef CONFIG_TRACE_LEDS
#  define leds_trace_begin() trace_begin(NXTAG_LEDS)
#  define leds_trace_end() trace_end(NXTAG_LEDS)
#  define leds_trace_beginex(name) trace_beginex(NXTAG_LEDS, name)
#  define leds_trace_endex(name) trace_endex(NXTAG_LEDS, name)
#  define leds_trace_mark(s) trace_mark(NXTAG_LEDS, s)
#  define leds_trace_event(level, id, fmt, ...) \
    trace_event(NXTAG_LEDS, level, id, fmt, ##__VA_ARGS__)
#else
#  define leds_trace_begin()
#  define leds_trace_end()
#  define leds_trace_beginex(name)
#  define leds_trace_endex(name)
#  define leds_trace_mark(s)
#  define leds_trace_event(...)
#endif

#ifdef CONFIG_TRACE_LIB
#  define lib_trace_begin() trace_begin(NXTAG_LIB)
#  define lib_trace_end() trace_end(NXTAG_LIB)
#  define lib_trace_beginex(name) trace_beginex(NXTAG_LIB, name)
#  define lib_trace_endex(name) trace_endex(NXTAG_LIB, name)
#  define lib_trace_mark(s) trace_mark(NXTAG_LIB, s)
#  define lib_trace_event(level, id, fmt, ...) \
    trace_event(NXTAG_LIB, level, id, fmt, ##__VA_ARGS__)
#else
#  define lib_trace_begin()
#  define lib_trace_end()
#  define lib_trace_beginex(name)
#  define lib_trace_endex(name)
#  define lib_trace_mark(s)
#  define lib_trace_event(...)
#endif

#ifdef CONFIG_TRACE_MEMCARD
#  define memcard_trace_begin() trace_begin(NXTAG_MEMCARD)
#  define memcard_trace_end() trace_end(NXTAG_MEMCARD)
#  define memcard_trace_beginex(name) trace_beginex(NXTAG_MEMCARD, name)
#  define memcard_trace_endex(name) trace_endex(NXTAG_MEMCARD, name)
#  define memcard_trace_mark(s) trace_mark(NXTAG_MEMCARD, s)
#  define memcard_trace_event(level, id, fmt, ...) \
    trace_event(NXTAG_MEMCARD, level, id, fmt, ##__VA_ARGS__)
#else
#  define memcard_trace_begin()
#  define memcard_trace_end()
#  define memcard_trace_beginex(name)
#  define memcard_trace_endex(name)
#  define memcard_trace_mark(s)
#  define memcard_trace_event(...)
#endif

#ifdef CONFIG_TRACE_MM
#  define mm_trace_begin() trace_begin(NXTAG_MM)
#  define mm_trace_end() trace_end(NXTAG_MM)
#  define mm_trace_beginex(name) trace_beginex(NXTAG_MM, name)
#  define mm_trace_endex(name) trace_endex(NXTAG_MM, name)
#  define mm_trace_mark(s) trace_mark(NXTAG_MM, s)
#  define mm_trace_event(level, id, fmt, ...) \
    trace_event(NXTAG_MM, level, id, fmt, ##__VA_ARGS__)
#else
#  define mm_trace_begin()
#  define mm_trace_end()
#  define mm_trace_beginex(name)
#  define mm_trace_endex(name)
#  define mm_trace_mark(s)
#  define mm_trace_event(...)
#endif

#ifdef CONFIG_TRACE_MOTOR
#  define motor_trace_begin() trace_begin(NXTAG_MOTOR)
#  define motor_trace_end() trace_end(NXTAG_MOTOR)
#  define motor_trace_beginex(name) trace_beginex(NXTAG_MOTOR, name)
#  define motor_trace_endex(name) trace_endex(NXTAG_MOTOR, name)
#  define motor_trace_mark(s) trace_mark(NXTAG_MOTOR, s)
#  define motor_trace_event(level, id, fmt, ...) \
    trace_event(NXTAG_MOTOR, level, id, fmt, ##__VA_ARGS__)
#else
#  define motor_trace_begin()
#  define motor_trace_end()
#  define motor_trace_beginex(name)
#  define motor_trace_endex(name)
#  define motor_trace_mark(s)
#  define motor_trace_event(...)
#endif

#ifdef CONFIG_TRACE_NET
#  define net_trace_begin() trace_begin(NXTAG_NET)
#  define net_trace_end() trace_end(NXTAG_NET)
#  define net_trace_beginex(name) trace_beginex(NXTAG_NET, name)
#  define net_trace_endex(name) trace_endex(NXTAG_NET, name)
#  define net_trace_mark(s) trace_mark(NXTAG_NET, s)
#  define net_trace_event(level, id, fmt, ...) \
    trace_event(NXTAG_NET, level, id, fmt, ##__VA_ARGS__)
#else
#  define net_trace_begin()
#  define net_trace_end()
#  define net_trace_beginex(name)
#  define net_trace_endex(name)
#  define net_trace_mark(s)
#  define net_trace_event(...)
#endif

#ifdef CONFIG_TRACE_PAGING
#  define paging_trace_begin() trace_begin(NXTAG_PAGING)
#  define paging_trace_end() trace_end(NXTAG_PAGING)
#  define paging_trace_beginex(name) trace_beginex(NXTAG_PAGING, name)
#  define paging_trace_endex(name) trace_endex(NXTAG_PAGING, name)
#  define paging_trace_mark(s) trace_mark(NXTAG_PAGING, s)
#  define paging_trace_event(level, id, fmt, ...) \
    trace_event(NXTAG_PAGING, level, id, fmt, ##__VA_ARGS__)
#else
#  define paging_trace_begin()
#  define paging_trace_end()
#  define paging_trace_beginex(name)
#  define paging_trace_endex(name)
#  define paging_trace_mark(s)
#  define paging_trace_event(...)
#endif

#ifdef CONFIG_TRACE_PCI
#  define pci_trace_begin() trace_begin(NXTAG_PCI)
#  define pci_trace_end() trace_end(NXTAG_PCI)
#  define pci_trace_beginex(name) trace_beginex(NXTAG_PCI, name)
#  define pci_trace_endex(name) trace_endex(NXTAG_PCI, name)
#  define pci_trace_mark(s) trace_mark(NXTAG_PCI, s)
#  define pci_trace_event(level, id, fmt, ...) \
    trace_event(NXTAG_PCI, level, id, fmt, ##__VA_ARGS__)
#else
#  define pci_trace_begin()
#  define pci_trace_end()
#  define pci_trace_beginex(name)
#  define pci_trace_endex(name)
#  define pci_trace_mark(s)
#  define pci_trace_event(...)
#endif

#ifdef CONFIG_TRACE_POWER
#  define power_trace_begin() trace_begin(NXTAG_POWER)
#  define power_trace_end() trace_end(NXTAG_POWER)
#  define power_trace_beginex(name) trace_beginex(NXTAG_POWER, name)
#  define power_trace_endex(name) trace_endex(NXTAG_POWER, name)
#  define power_trace_mark(s) trace_mark(NXTAG_POWER, s)
#  define power_trace_event(level, id, fmt, ...) \
    trace_event(NXTAG_POWER, level, id, fmt, ##__VA_ARGS__)
#else
#  define power_trace_begin()
#  define power_trace_end()
#  define power_trace_beginex(name)
#  define power_trace_endex(name)
#  define power_trace_mark(s)
#  define power_trace_event(...)
#endif

#ifdef CONFIG_TRACE_PTP
#  define ptp_trace_begin() trace_begin(NXTAG_PTP)
#  define ptp_trace_end() trace_end(NXTAG_PTP)
#  define ptp_trace_beginex(name) trace_beginex(NXTAG_PTP, name)
#  define ptp_trace_endex(name) trace_endex(NXTAG_PTP, name)
#  define ptp_trace_mark(s) trace_mark(NXTAG_PTP, s)
#  define ptp_trace_event(level, id, fmt, ...) \
    trace_event(NXTAG_PTP, level, id, fmt, ##__VA_ARGS__)
#else
#  define ptp_trace_begin()
#  define ptp_trace_end()
#  define ptp_trace_beginex(name)
#  define ptp_trace_endex(name)
#  define ptp_trace_mark(s)
#  define ptp_trace_event(...)
#endif

#ifdef CONFIG_TRACE_PWM
#  define pwm_trace_begin() trace_begin(NXTAG_PWM)
#  define pwm_trace_end() trace_end(NXTAG_PWM)
#  define pwm_trace_beginex(name) trace_beginex(NXTAG_PWM, name)
#  define pwm_trace_endex(name) trace_endex(NXTAG_PWM, name)
#  define pwm_trace_mark(s) trace_mark(NXTAG_PWM, s)
#  define pwm_trace_event(level, id, fmt, ...) \
    trace_event(NXTAG_PWM, level, id, fmt, ##__VA_ARGS__)
#else
#  define pwm_trace_begin()
#  define pwm_trace_end()
#  define pwm_trace_beginex(name)
#  define pwm_trace_endex(name)
#  define pwm_trace_mark(s)
#  define pwm_trace_event(...)
#endif

#ifdef CONFIG_TRACE_RC
#  define rc_trace_begin() trace_begin(NXTAG_RC)
#  define rc_trace_end() trace_end(NXTAG_RC)
#  define rc_trace_beginex(name) trace_beginex(NXTAG_RC, name)
#  define rc_trace_endex(name) trace_endex(NXTAG_RC, name)
#  define rc_trace_mark(s) trace_mark(NXTAG_RC, s)
#  define rc_trace_event(level, id, fmt, ...) \
    trace_event(NXTAG_RC, level, id, fmt, ##__VA_ARGS__)
#else
#  define rc_trace_begin()
#  define rc_trace_end()
#  define rc_trace_beginex(name)
#  define rc_trace_endex(name)
#  define rc_trace_mark(s)
#  define rc_trace_event(...)
#endif

#ifdef CONFIG_TRACE_REGMAP
#  define regmap_trace_begin() trace_begin(NXTAG_REGMAP)
#  define regmap_trace_end() trace_end(NXTAG_REGMAP)
#  define regmap_trace_beginex(name) trace_beginex(NXTAG_REGMAP, name)
#  define regmap_trace_endex(name) trace_endex(NXTAG_REGMAP, name)
#  define regmap_trace_mark(s) trace_mark(NXTAG_REGMAP, s)
#  define regmap_trace_event(level, id, fmt, ...) \
    trace_event(NXTAG_REGMAP, level, id, fmt, ##__VA_ARGS__)
#else
#  define regmap_trace_begin()
#  define regmap_trace_end()
#  define regmap_trace_beginex(name)
#  define regmap_trace_endex(name)
#  define regmap_trace_mark(s)
#  define regmap_trace_event(...)
#endif

#ifdef CONFIG_TRACE_RESET
#  define reset_trace_begin() trace_begin(NXTAG_RESET)
#  define reset_trace_end() trace_end(NXTAG_RESET)
#  define reset_trace_beginex(name) trace_beginex(NXTAG_RESET, name)
#  define reset_trace_endex(name) trace_endex(NXTAG_RESET, name)
#  define reset_trace_mark(s) trace_mark(NXTAG_RESET, s)
#  define reset_trace_event(level, id, fmt, ...) \
    trace_event(NXTAG_RESET, level, id, fmt, ##__VA_ARGS__)
#else
#  define reset_trace_begin()
#  define reset_trace_end()
#  define reset_trace_beginex(name)
#  define reset_trace_endex(name)
#  define reset_trace_mark(s)
#  define reset_trace_event(...)
#endif

#ifdef CONFIG_TRACE_RMT
#  define rmt_trace_begin() trace_begin(NXTAG_RMT)
#  define rmt_trace_end() trace_end(NXTAG_RMT)
#  define rmt_trace_beginex(name) trace_beginex(NXTAG_RMT, name)
#  define rmt_trace_endex(name) trace_endex(NXTAG_RMT, name)
#  define rmt_trace_mark(s) trace_mark(NXTAG_RMT, s)
#  define rmt_trace_event(level, id, fmt, ...) \
    trace_event(NXTAG_RMT, level, id, fmt, ##__VA_ARGS__)
#else
#  define rmt_trace_begin()
#  define rmt_trace_end()
#  define rmt_trace_beginex(name)
#  define rmt_trace_endex(name)
#  define rmt_trace_mark(s)
#  define rmt_trace_event(...)
#endif

#ifdef CONFIG_TRACE_RPMSG
#  define rpmsg_trace_begin() trace_begin(NXTAG_RPMSG)
#  define rpmsg_trace_end() trace_end(NXTAG_RPMSG)
#  define rpmsg_trace_beginex(name) trace_beginex(NXTAG_RPMSG, name)
#  define rpmsg_trace_endex(name) trace_endex(NXTAG_RPMSG, name)
#  define rpmsg_trace_mark(s) trace_mark(NXTAG_RPMSG, s)
#  define rpmsg_trace_event(level, id, fmt, ...) \
    trace_event(NXTAG_RPMSG, level, id, fmt, ##__VA_ARGS__)
#else
#  define rpmsg_trace_begin()
#  define rpmsg_trace_end()
#  define rpmsg_trace_beginex(name)
#  define rpmsg_trace_endex(name)
#  define rpmsg_trace_mark(s)
#  define rpmsg_trace_event(...)
#endif

#ifdef CONFIG_TRACE_RPTUN
#  define rptun_trace_begin() trace_begin(NXTAG_RPTUN)
#  define rptun_trace_end() trace_end(NXTAG_RPTUN)
#  define rptun_trace_beginex(name) trace_beginex(NXTAG_RPTUN, name)
#  define rptun_trace_endex(name) trace_endex(NXTAG_RPTUN, name)
#  define rptun_trace_mark(s) trace_mark(NXTAG_RPTUN, s)
#  define rptun_trace_event(level, id, fmt, ...) \
    trace_event(NXTAG_RPTUN, level, id, fmt, ##__VA_ARGS__)
#else
#  define rptun_trace_begin()
#  define rptun_trace_end()
#  define rptun_trace_beginex(name)
#  define rptun_trace_endex(name)
#  define rptun_trace_mark(s)
#  define rptun_trace_event(...)
#endif

#ifdef CONFIG_TRACE_RTC
#  define rtc_trace_begin() trace_begin(NXTAG_RTC)
#  define rtc_trace_end() trace_end(NXTAG_RTC)
#  define rtc_trace_beginex(name) trace_beginex(NXTAG_RTC, name)
#  define rtc_trace_endex(name) trace_endex(NXTAG_RTC, name)
#  define rtc_trace_mark(s) trace_mark(NXTAG_RTC, s)
#  define rtc_trace_event(level, id, fmt, ...) \
    trace_event(NXTAG_RTC, level, id, fmt, ##__VA_ARGS__)
#else
#  define rtc_trace_begin()
#  define rtc_trace_end()
#  define rtc_trace_beginex(name)
#  define rtc_trace_endex(name)
#  define rtc_trace_mark(s)
#  define rtc_trace_event(...)
#endif

#ifdef CONFIG_TRACE_SAFETY
#  define safety_trace_begin() trace_begin(NXTAG_SAFETY)
#  define safety_trace_end() trace_end(NXTAG_SAFETY)
#  define safety_trace_beginex(name) trace_beginex(NXTAG_SAFETY, name)
#  define safety_trace_endex(name) trace_endex(NXTAG_SAFETY, name)
#  define safety_trace_mark(s) trace_mark(NXTAG_SAFETY, s)
#  define safety_trace_event(level, id, fmt, ...) \
    trace_event(NXTAG_SAFETY, level, id, fmt, ##__VA_ARGS__)
#else
#  define safety_trace_begin()
#  define safety_trace_end()
#  define safety_trace_beginex(name)
#  define safety_trace_endex(name)
#  define safety_trace_mark(s)
#  define safety_trace_event(...)
#endif

#ifdef CONFIG_TRACE_SCHED
#  define sched_trace_begin() trace_begin(NXTAG_SCHED)
#  define sched_trace_end() trace_end(NXTAG_SCHED)
#  define sched_trace_beginex(name) trace_beginex(NXTAG_SCHED, name)
#  define sched_trace_endex(name) trace_endex(NXTAG_SCHED, name)
#  define sched_trace_mark(s) trace_mark(NXTAG_SCHED, s)
#  define sched_trace_event(level, id, fmt, ...) \
    trace_event(NXTAG_SCHED, level, id, fmt, ##__VA_ARGS__)
#else
#  define sched_trace_begin()
#  define sched_trace_end()
#  define sched_trace_beginex(name)
#  define sched_trace_endex(name)
#  define sched_trace_mark(s)
#  define sched_trace_event(...)
#endif

#ifdef CONFIG_TRACE_SENSORS
#  define sensors_trace_begin() trace_begin(NXTAG_SENSORS)
#  define sensors_trace_end() trace_end(NXTAG_SENSORS)
#  define sensors_trace_beginex(name) trace_beginex(NXTAG_SENSORS, name)
#  define sensors_trace_endex(name) trace_endex(NXTAG_SENSORS, name)
#  define sensors_trace_mark(s) trace_mark(NXTAG_SENSORS, s)
#  define sensors_trace_event(level, id, fmt, ...) \
    trace_event(NXTAG_SENSORS, level, id, fmt, ##__VA_ARGS__)
#else
#  define sensors_trace_begin()
#  define sensors_trace_end()
#  define sensors_trace_beginex(name)
#  define sensors_trace_endex(name)
#  define sensors_trace_mark(s)
#  define sensors_trace_event(...)
#endif

#ifdef CONFIG_TRACE_SPI
#  define spi_trace_begin() trace_begin(NXTAG_SPI)
#  define spi_trace_end() trace_end(NXTAG_SPI)
#  define spi_trace_beginex(name) trace_beginex(NXTAG_SPI, name)
#  define spi_trace_endex(name) trace_endex(NXTAG_SPI, name)
#  define spi_trace_mark(s) trace_mark(NXTAG_SPI, s)
#  define spi_trace_event(level, id, fmt, ...) \
    trace_event(NXTAG_SPI, level, id, fmt, ##__VA_ARGS__)
#else
#  define spi_trace_begin()
#  define spi_trace_end()
#  define spi_trace_beginex(name)
#  define spi_trace_endex(name)
#  define spi_trace_mark(s)
#  define spi_trace_event(...)
#endif

#ifdef CONFIG_TRACE_STEPPER
#  define stepper_trace_begin() trace_begin(NXTAG_STEPPER)
#  define stepper_trace_end() trace_end(NXTAG_STEPPER)
#  define stepper_trace_beginex(name) trace_beginex(NXTAG_STEPPER, name)
#  define stepper_trace_endex(name) trace_endex(NXTAG_STEPPER, name)
#  define stepper_trace_mark(s) trace_mark(NXTAG_STEPPER, s)
#  define stepper_trace_event(level, id, fmt, ...) \
    trace_event(NXTAG_STEPPER, level, id, fmt, ##__VA_ARGS__)
#else
#  define stepper_trace_begin()
#  define stepper_trace_end()
#  define stepper_trace_beginex(name)
#  define stepper_trace_endex(name)
#  define stepper_trace_mark(s)
#  define stepper_trace_event(...)
#endif

#ifdef CONFIG_TRACE_SYSCALL
#  define syscall_trace_begin() trace_begin(NXTAG_SYSCALL)
#  define syscall_trace_end() trace_end(NXTAG_SYSCALL)
#  define syscall_trace_beginex(name) trace_beginex(NXTAG_SYSCALL, name)
#  define syscall_trace_endex(name) trace_endex(NXTAG_SYSCALL, name)
#  define syscall_trace_mark(s) trace_mark(NXTAG_SYSCALL, s)
#  define syscall_trace_event(level, id, fmt, ...) \
    trace_event(NXTAG_SYSCALL, level, id, fmt, ##__VA_ARGS__)
#else
#  define syscall_trace_begin()
#  define syscall_trace_end()
#  define syscall_trace_beginex(name)
#  define syscall_trace_endex(name)
#  define syscall_trace_mark(s)
#  define syscall_trace_event(...)
#endif

#ifdef CONFIG_TRACE_THERMAL
#  define thermal_trace_begin() trace_begin(NXTAG_THERMAL)
#  define thermal_trace_end() trace_end(NXTAG_THERMAL)
#  define thermal_trace_beginex(name) trace_beginex(NXTAG_THERMAL, name)
#  define thermal_trace_endex(name) trace_endex(NXTAG_THERMAL, name)
#  define thermal_trace_mark(s) trace_mark(NXTAG_THERMAL, s)
#  define thermal_trace_event(level, id, fmt, ...) \
    trace_event(NXTAG_THERMAL, level, id, fmt, ##__VA_ARGS__)
#else
#  define thermal_trace_begin()
#  define thermal_trace_end()
#  define thermal_trace_beginex(name)
#  define thermal_trace_endex(name)
#  define thermal_trace_mark(s)
#  define thermal_trace_event(...)
#endif

#ifdef CONFIG_TRACE_TIMER
#  define timer_trace_begin() trace_begin(NXTAG_TIMER)
#  define timer_trace_end() trace_end(NXTAG_TIMER)
#  define timer_trace_beginex(name) trace_beginex(NXTAG_TIMER, name)
#  define timer_trace_endex(name) trace_endex(NXTAG_TIMER, name)
#  define timer_trace_mark(s) trace_mark(NXTAG_TIMER, s)
#  define timer_trace_event(level, id, fmt, ...) \
    trace_event(NXTAG_TIMER, level, id, fmt, ##__VA_ARGS__)
#else
#  define timer_trace_begin()
#  define timer_trace_end()
#  define timer_trace_beginex(name)
#  define timer_trace_endex(name)
#  define timer_trace_mark(s)
#  define timer_trace_event(...)
#endif

#ifdef CONFIG_TRACE_USB
#  define usb_trace_begin() trace_begin(NXTAG_USB)
#  define usb_trace_end() trace_end(NXTAG_USB)
#  define usb_trace_beginex(name) trace_beginex(NXTAG_USB, name)
#  define usb_trace_endex(name) trace_endex(NXTAG_USB, name)
#  define usb_trace_mark(s) trace_mark(NXTAG_USB, s)
#  define usb_trace_event(level, id, fmt, ...) \
    trace_event(NXTAG_USB, level, id, fmt, ##__VA_ARGS__)
#else
#  define usb_trace_begin()
#  define usb_trace_end()
#  define usb_trace_beginex(name)
#  define usb_trace_endex(name)
#  define usb_trace_mark(s)
#  define usb_trace_event(...)
#endif

#ifdef CONFIG_TRACE_VHOST
#  define vhost_trace_begin() trace_begin(NXTAG_VHOST)
#  define vhost_trace_end() trace_end(NXTAG_VHOST)
#  define vhost_trace_beginex(name) trace_beginex(NXTAG_VHOST, name)
#  define vhost_trace_endex(name) trace_endex(NXTAG_VHOST, name)
#  define vhost_trace_mark(s) trace_mark(NXTAG_VHOST, s)
#  define vhost_trace_event(level, id, fmt, ...) \
    trace_event(NXTAG_VHOST, level, id, fmt, ##__VA_ARGS__)
#else
#  define vhost_trace_begin()
#  define vhost_trace_end()
#  define vhost_trace_beginex(name)
#  define vhost_trace_endex(name)
#  define vhost_trace_mark(s)
#  define vhost_trace_event(...)
#endif

#ifdef CONFIG_TRACE_VIDEO
#  define video_trace_begin() trace_begin(NXTAG_VIDEO)
#  define video_trace_end() trace_end(NXTAG_VIDEO)
#  define video_trace_beginex(name) trace_beginex(NXTAG_VIDEO, name)
#  define video_trace_endex(name) trace_endex(NXTAG_VIDEO, name)
#  define video_trace_mark(s) trace_mark(NXTAG_VIDEO, s)
#  define video_trace_event(level, id, fmt, ...) \
    trace_event(NXTAG_VIDEO, level, id, fmt, ##__VA_ARGS__)
#else
#  define video_trace_begin()
#  define video_trace_end()
#  define video_trace_beginex(name)
#  define video_trace_endex(name)
#  define video_trace_mark(s)
#  define video_trace_event(...)
#endif

#ifdef CONFIG_TRACE_VIRTIO
#  define virtio_trace_begin() trace_begin(NXTAG_VIRTIO)
#  define virtio_trace_end() trace_end(NXTAG_VIRTIO)
#  define virtio_trace_beginex(name) trace_beginex(NXTAG_VIRTIO, name)
#  define virtio_trace_endex(name) trace_endex(NXTAG_VIRTIO, name)
#  define virtio_trace_mark(s) trace_mark(NXTAG_VIRTIO, s)
#  define virtio_trace_event(level, id, fmt, ...) \
    trace_event(NXTAG_VIRTIO, level, id, fmt, ##__VA_ARGS__)
#else
#  define virtio_trace_begin()
#  define virtio_trace_end()
#  define virtio_trace_beginex(name)
#  define virtio_trace_endex(name)
#  define virtio_trace_mark(s)
#  define virtio_trace_event(...)
#endif

#ifdef CONFIG_TRACE_WATCHDOG
#  define watchdog_trace_begin() trace_begin(NXTAG_WATCHDOG)
#  define watchdog_trace_end() trace_end(NXTAG_WATCHDOG)
#  define watchdog_trace_beginex(name) trace_beginex(NXTAG_WATCHDOG, name)
#  define watchdog_trace_endex(name) trace_endex(NXTAG_WATCHDOG, name)
#  define watchdog_trace_mark(s) trace_mark(NXTAG_WATCHDOG, s)
#  define watchdog_trace_event(level, id, fmt, ...) \
    trace_event(NXTAG_WATCHDOG, level, id, fmt, ##__VA_ARGS__)
#else
#  define watchdog_trace_begin()
#  define watchdog_trace_end()
#  define watchdog_trace_beginex(name)
#  define watchdog_trace_endex(name)
#  define watchdog_trace_mark(s)
#  define watchdog_trace_event(...)
#endif

#ifdef CONFIG_TRACE_WIRELESS
#  define wireless_trace_begin() trace_begin(NXTAG_WIRELESS)
#  define wireless_trace_end() trace_end(NXTAG_WIRELESS)
#  define wireless_trace_beginex(name) trace_beginex(NXTAG_WIRELESS, name)
#  define wireless_trace_endex(name) trace_endex(NXTAG_WIRELESS, name)
#  define wireless_trace_mark(s) trace_mark(NXTAG_WIRELESS, s)
#  define wireless_trace_event(level, id, fmt, ...) \
    trace_event(NXTAG_WIRELESS, level, id, fmt, ##__VA_ARGS__)
#else
#  define wireless_trace_begin()
#  define wireless_trace_end()
#  define wireless_trace_beginex(name)
#  define wireless_trace_endex(name)
#  define wireless_trace_mark(s)
#  define wireless_trace_event(...)
#endif
/* Diagnostic IDs */

#define DIAG_MM_ALLOC_FAIL       0
#define DIAG_MM_FREE_FAIL        1

#endif /* __INCLUDE_NUTTX_TRACE_H */
