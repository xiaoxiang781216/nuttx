/****************************************************************************
 * include/debug.h
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

#ifndef __INCLUDE_DEBUG_H
#define __INCLUDE_DEBUG_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/compiler.h>
#include <nuttx/streams.h>
#include <nuttx/tag.h>

#ifdef CONFIG_ARCH_DEBUG_H
#  include <arch/debug.h>
#endif
#ifdef CONFIG_ARCH_CHIP_DEBUG_H
#  include <arch/chip/debug.h>
#endif
#ifdef CONFIG_ARCH_BOARD_DEBUG_H
#  include <arch/board/debug.h>
#endif

#include <syslog.h>
#include <sys/uio.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Debug macros to runtime filter the debug messages sent to the console.  In
 * general, there are four forms of the debug macros:
 *
 * [a-z]info() -- Outputs messages to the console similar to printf() except
 *    that the output is not buffered.  Output is only generated if
 *    CONFIG_DEBUG_INFO is defined.  The info macros are intended for
 *    verbose "informational" debug output.  If you enable CONFIG_DEBUG_INFO,
 *    then very chatty (and often annoying) output will be generated.
 *
 *    The first character of the macro name indicates the system
 *    (e.g., n=network, f=filesystm, etc.).  If the first character is
 *    missing (i.e., _info()), then it is common.  The common _info() macro
 *    is enabled simply with CONFIG_DEBUG_INFO.  Subsystem debug requires an
 *    additional configuration setting to enable it (e.g., CONFIG_DEBUG_NET
 *    for the network, CONFIG_DEBUG_FS for the file system, etc).
 *
 *    In general, error messages and output of importance use [a-z]err().
 *    [a-z]err() is implementation dependent but usually uses file
 *    descriptors. (that is a problem only because the interrupt task may
 *    have redirected stdout).  Therefore [a-z]err() should not be used in
 *    interrupt handlers.
 *
 * [a-z]warn() -- Identical to [a-z]info() except that it also requires that
 *    CONFIG_DEBUG_WARN be defined.  This is intended for important exception
 *    conditions that are potential errors (or perhaps real errors with non-
 *    fatal consequences).
 *
 * [a-z]err() -- Identical to [a-z]info() except that it also requires that
 *    CONFIG_DEBUG_ERROR be defined.  This is intended for important
 *    error-related information that you probably don't want to suppress
 *    during general debugging.
 *
 * _alert() - is a special, high-priority, unconditional version that is
 *    really intended only for crash error reporting.
 */

#if !defined(EXTRA_FMT) && !defined(EXTRA_ARG) && \
    defined(CONFIG_HAVE_FUNCTIONNAME) && !defined(CONFIG_DEFAULT_SMALL)
#  define EXTRA_FMT "%s: "
#  define EXTRA_ARG ,__FUNCTION__
#endif

#ifndef EXTRA_FMT
#  define EXTRA_FMT
#endif

#ifndef EXTRA_ARG
#  define EXTRA_ARG
#endif

/* Debug macros will differ depending upon if the toolchain supports
 * macros with a variable number of arguments or not.
 */

#ifdef CONFIG_CPP_HAVE_VARARGS
/* don't call syslog while performing the compiler's format check. */

#  define _none(format, ...) \
    do { if (0) syslog(LOG_ERR, format, ##__VA_ARGS__); } while (0)
#else
#  define _none       (void)
#endif

/* The actual logger function may be overridden in arch/debug.h if needed.
 * (Currently only if the pre-processor supports variadic macros)
 */

#ifndef __arch_syslog
#  ifdef CONFIG_CPP_HAVE_VARARGS
#    define __arch_syslog(priority, fmt, ...) \
            syslog(priority, fmt, ##__VA_ARGS__)
#  else
#    define __arch_syslog syslog
#  endif
#endif

#if !defined(CONFIG_DEBUG_ALERT)
#  define _alert      _none
#elif defined(CONFIG_CPP_HAVE_VARARGS)
#  define _alert(format, ...) \
   __arch_syslog(LOG_ALERT, EXTRA_FMT format EXTRA_ARG, ##__VA_ARGS__)
#endif

#if !defined(CONFIG_DEBUG_ERROR)
#  define _err        _none
#elif defined(CONFIG_CPP_HAVE_VARARGS)
#  define _err(format, ...) \
   __arch_syslog(LOG_ERR, EXTRA_FMT format EXTRA_ARG, ##__VA_ARGS__)
#endif

#if !defined(CONFIG_DEBUG_WARN)
#  define _warn       _none
#elif defined(CONFIG_CPP_HAVE_VARARGS)
#  define _warn(format, ...) \
   __arch_syslog(LOG_WARNING, EXTRA_FMT format EXTRA_ARG, ##__VA_ARGS__)
#endif

#if !defined(CONFIG_DEBUG_INFO)
#  define _info       _none
#elif defined(CONFIG_CPP_HAVE_VARARGS)
#  define _info(format, ...) \
   __arch_syslog(LOG_INFO, EXTRA_FMT format EXTRA_ARG, ##__VA_ARGS__)
#endif

/* Tagged variants — carry tag in priority upper bits */

#if !defined(CONFIG_DEBUG_ERROR)
#  define _terr(tag, format, ...) _none(format, ##__VA_ARGS__)
#elif defined(CONFIG_CPP_HAVE_VARARGS)
#  define _terr(tag, format, ...) \
   __arch_syslog(LOG_TAGPRI(tag, LOG_ERR), \
                 EXTRA_FMT format EXTRA_ARG, ##__VA_ARGS__)
#endif

#if !defined(CONFIG_DEBUG_WARN)
#  define _twarn(tag, format, ...) _none(format, ##__VA_ARGS__)
#elif defined(CONFIG_CPP_HAVE_VARARGS)
#  define _twarn(tag, format, ...) \
   __arch_syslog(LOG_TAGPRI(tag, LOG_WARNING), \
                 EXTRA_FMT format EXTRA_ARG, ##__VA_ARGS__)
#endif

#if !defined(CONFIG_DEBUG_INFO)
#  define _tinfo(tag, format, ...) _none(format, ##__VA_ARGS__)
#elif defined(CONFIG_CPP_HAVE_VARARGS)
#  define _tinfo(tag, format, ...) \
   __arch_syslog(LOG_TAGPRI(tag, LOG_INFO), \
                 EXTRA_FMT format EXTRA_ARG, ##__VA_ARGS__)
#endif

/* Subsystem specific debug */

#ifdef CONFIG_DEBUG_MM_ERROR
#  define merr(format, ...) _terr(NXTAG_MM, format, ##__VA_ARGS__)
#else
#  define merr        _none
#endif

#ifdef CONFIG_DEBUG_MM_WARN
#  define mwarn(format, ...) _twarn(NXTAG_MM, format, ##__VA_ARGS__)
#else
#  define mwarn       _none
#endif

#ifdef CONFIG_DEBUG_MM_INFO
#  define minfo(format, ...) _tinfo(NXTAG_MM, format, ##__VA_ARGS__)
#else
#  define minfo       _none
#endif

#ifdef CONFIG_DEBUG_SCHED_ERROR
#  define serr(format, ...) _terr(NXTAG_SCHED, format, ##__VA_ARGS__)
#else
#  define serr        _none
#endif

#ifdef CONFIG_DEBUG_SCHED_WARN
#  define swarn(format, ...) _twarn(NXTAG_SCHED, format, ##__VA_ARGS__)
#else
#  define swarn       _none
#endif

#ifdef CONFIG_DEBUG_SCHED_INFO
#  define sinfo(format, ...) _tinfo(NXTAG_SCHED, format, ##__VA_ARGS__)
#else
#  define sinfo       _none
#endif

#ifdef CONFIG_DEBUG_SYSCALL_ERROR
#  define svcerr(format, ...) _terr(NXTAG_SYSCALL, format, ##__VA_ARGS__)
#else
#  define svcerr      _none
#endif

#ifdef CONFIG_DEBUG_SYSCALL_WARN
#  define svcwarn(format, ...) _twarn(NXTAG_SYSCALL, format, ##__VA_ARGS__)
#else
#  define svcwarn     _none
#endif

#ifdef CONFIG_DEBUG_SYSCALL_INFO
#  define svcinfo(format, ...) _tinfo(NXTAG_SYSCALL, format, ##__VA_ARGS__)
#else
#  define svcinfo     _none
#endif

#ifdef CONFIG_DEBUG_PAGING_ERROR
#  define pgerr(format, ...) _terr(NXTAG_PAGING, format, ##__VA_ARGS__)
#else
#  define pgerr       _none
#endif

#ifdef CONFIG_DEBUG_PAGING_WARN
#  define pgwarn(format, ...) _twarn(NXTAG_PAGING, format, ##__VA_ARGS__)
#else
#  define pgwarn      _none
#endif

#ifdef CONFIG_DEBUG_PAGING_INFO
#  define pginfo(format, ...) _tinfo(NXTAG_PAGING, format, ##__VA_ARGS__)
#else
#  define pginfo      _none
#endif

#ifdef CONFIG_DEBUG_NET_ERROR
#  define nerr(format, ...) _terr(NXTAG_NET, format, ##__VA_ARGS__)
#else
#  define nerr        _none
#endif

#ifdef CONFIG_DEBUG_NET_WARN
#  define nwarn(format, ...) _twarn(NXTAG_NET, format, ##__VA_ARGS__)
#else
#  define nwarn       _none
#endif

#ifdef CONFIG_DEBUG_NET_INFO
#  define ninfo(format, ...) _tinfo(NXTAG_NET, format, ##__VA_ARGS__)
#else
#  define ninfo       _none
#endif

#ifdef CONFIG_DEBUG_POWER_ERROR
#  define pwrerr(format, ...) _terr(NXTAG_POWER, format, ##__VA_ARGS__)
#else
#  define pwrerr       _none
#endif

#ifdef CONFIG_DEBUG_POWER_WARN
#  define pwrwarn(format, ...) _twarn(NXTAG_POWER, format, ##__VA_ARGS__)
#else
#  define pwrwarn      _none
#endif

#ifdef CONFIG_DEBUG_POWER_INFO
#  define pwrinfo(format, ...) _tinfo(NXTAG_POWER, format, ##__VA_ARGS__)
#else
#  define pwrinfo      _none
#endif

#ifdef CONFIG_DEBUG_BATTERY_ERROR
#  define baterr(format, ...) _terr(NXTAG_BATTERY, format, ##__VA_ARGS__)
#else
#  define baterr       _none
#endif

#ifdef CONFIG_DEBUG_BATTERY_WARN
#  define batwarn(format, ...) _twarn(NXTAG_BATTERY, format, ##__VA_ARGS__)
#else
#  define batwarn      _none
#endif

#ifdef CONFIG_DEBUG_BATTERY_INFO
#  define batinfo(format, ...) _tinfo(NXTAG_BATTERY, format, ##__VA_ARGS__)
#else
#  define batinfo      _none
#endif

#ifdef CONFIG_DEBUG_WIRELESS_ERROR
#  define wlerr(format, ...) _terr(NXTAG_WIRELESS, format, ##__VA_ARGS__)
#else
#  define wlerr       _none
#endif

#ifdef CONFIG_DEBUG_WIRELESS_WARN
#  define wlwarn(format, ...) _twarn(NXTAG_WIRELESS, format, ##__VA_ARGS__)
#else
#  define wlwarn      _none
#endif

#ifdef CONFIG_DEBUG_WIRELESS_INFO
#  define wlinfo(format, ...) _tinfo(NXTAG_WIRELESS, format, ##__VA_ARGS__)
#else
#  define wlinfo      _none
#endif

#ifdef CONFIG_DEBUG_FS_ERROR
#  define ferr(format, ...) _terr(NXTAG_FS, format, ##__VA_ARGS__)
#else
#  define ferr        _none
#endif

#ifdef CONFIG_DEBUG_FS_WARN
#  define fwarn(format, ...) _twarn(NXTAG_FS, format, ##__VA_ARGS__)
#else
#  define fwarn       _none
#endif

#ifdef CONFIG_DEBUG_FS_INFO
#  define finfo(format, ...) _tinfo(NXTAG_FS, format, ##__VA_ARGS__)
#else
#  define finfo       _none
#endif

#ifdef CONFIG_DEBUG_CONTACTLESS_ERROR
#  define ctlserr(format, ...) _terr(NXTAG_CONTACTLESS, format, ##__VA_ARGS__)
#else
#  define ctlserr     _none
#endif

#ifdef CONFIG_DEBUG_CONTACTLESS_WARN
#  define ctlswarn(format, ...) _twarn(NXTAG_CONTACTLESS, format, ##__VA_ARGS__)
#else
#  define ctlswarn    _none
#endif

#ifdef CONFIG_DEBUG_CONTACTLESS_INFO
#  define ctlsinfo(format, ...) _tinfo(NXTAG_CONTACTLESS, format, ##__VA_ARGS__)
#else
#  define ctlsinfo    _none
#endif

#ifdef CONFIG_DEBUG_CRYPTO_ERROR
#  define crypterr(format, ...) _terr(NXTAG_CRYPTO, format, ##__VA_ARGS__)
#else
#  define crypterr    _none
#endif

#ifdef CONFIG_DEBUG_CRYPTO_WARN
#  define cryptwarn(format, ...) _twarn(NXTAG_CRYPTO, format, ##__VA_ARGS__)
#else
#  define cryptwarn   _none
#endif

#ifdef CONFIG_DEBUG_CRYPTO_INFO
#  define cryptinfo(format, ...) _tinfo(NXTAG_CRYPTO, format, ##__VA_ARGS__)
#else
#  define cryptinfo   _none
#endif

#ifdef CONFIG_DEBUG_INPUT_ERROR
#  define ierr(format, ...) _terr(NXTAG_INPUT, format, ##__VA_ARGS__)
#else
#  define ierr        _none
#endif

#ifdef CONFIG_DEBUG_INPUT_WARN
#  define iwarn(format, ...) _twarn(NXTAG_INPUT, format, ##__VA_ARGS__)
#else
#  define iwarn       _none
#endif

#ifdef CONFIG_DEBUG_INPUT_INFO
#  define iinfo(format, ...) _tinfo(NXTAG_INPUT, format, ##__VA_ARGS__)
#else
#  define iinfo       _none
#endif

#ifdef CONFIG_DEBUG_ANALOG_ERROR
#  define aerr(format, ...) _terr(NXTAG_ANALOG, format, ##__VA_ARGS__)
#else
#  define aerr        _none
#endif

#ifdef CONFIG_DEBUG_ANALOG_WARN
#  define awarn(format, ...) _twarn(NXTAG_ANALOG, format, ##__VA_ARGS__)
#else
#  define awarn       _none
#endif

#ifdef CONFIG_DEBUG_ANALOG_INFO
#  define ainfo(format, ...) _tinfo(NXTAG_ANALOG, format, ##__VA_ARGS__)
#else
#  define ainfo       _none
#endif

#ifdef CONFIG_DEBUG_CAN_ERROR
#  define canerr(format, ...) _terr(NXTAG_CAN, format, ##__VA_ARGS__)
#else
#  define canerr      _none
#endif

#ifdef CONFIG_DEBUG_CAN_WARN
#  define canwarn(format, ...) _twarn(NXTAG_CAN, format, ##__VA_ARGS__)
#else
#  define canwarn     _none
#endif

#ifdef CONFIG_DEBUG_CAN_INFO
#  define caninfo(format, ...) _tinfo(NXTAG_CAN, format, ##__VA_ARGS__)
#else
#  define caninfo     _none
#endif

#ifdef CONFIG_DEBUG_GRAPHICS_ERROR
#  define gerr(format, ...) _terr(NXTAG_GRAPHICS, format, ##__VA_ARGS__)
#else
#  define gerr        _none
#endif

#ifdef CONFIG_DEBUG_GRAPHICS_WARN
#  define gwarn(format, ...) _twarn(NXTAG_GRAPHICS, format, ##__VA_ARGS__)
#else
#  define gwarn       _none
#endif

#ifdef CONFIG_DEBUG_GRAPHICS_INFO
#  define ginfo(format, ...) _tinfo(NXTAG_GRAPHICS, format, ##__VA_ARGS__)
#else
#  define ginfo       _none
#endif

#ifdef CONFIG_DEBUG_BINFMT_ERROR
#  define berr(format, ...) _terr(NXTAG_BINFMT, format, ##__VA_ARGS__)
#else
#  define berr        _none
#endif

#ifdef CONFIG_DEBUG_BINFMT_WARN
#  define bwarn(format, ...) _twarn(NXTAG_BINFMT, format, ##__VA_ARGS__)
#else
#  define bwarn       _none
#endif

#ifdef CONFIG_DEBUG_BINFMT_INFO
#  define binfo(format, ...) _tinfo(NXTAG_BINFMT, format, ##__VA_ARGS__)
#else
#  define binfo       _none
#endif

#ifdef CONFIG_DEBUG_LIB_ERROR
#  define lerr(format, ...) _terr(NXTAG_LIB, format, ##__VA_ARGS__)
#else
#  define lerr        _none
#endif

#ifdef CONFIG_DEBUG_LIB_WARN
#  define lwarn(format, ...) _twarn(NXTAG_LIB, format, ##__VA_ARGS__)
#else
#  define lwarn       _none
#endif

#ifdef CONFIG_DEBUG_LIB_INFO
#  define linfo(format, ...) _tinfo(NXTAG_LIB, format, ##__VA_ARGS__)
#else
#  define linfo       _none
#endif

#ifdef CONFIG_DEBUG_AUDIO_ERROR
#  define auderr(format, ...) _terr(NXTAG_AUDIO, format, ##__VA_ARGS__)
#else
#  define auderr      _none
#endif

#ifdef CONFIG_DEBUG_AUDIO_WARN
#  define audwarn(format, ...) _twarn(NXTAG_AUDIO, format, ##__VA_ARGS__)
#else
#  define audwarn     _none
#endif

#ifdef CONFIG_DEBUG_AUDIO_INFO
#  define audinfo(format, ...) _tinfo(NXTAG_AUDIO, format, ##__VA_ARGS__)
#else
#  define audinfo     _none
#endif

#ifdef CONFIG_DEBUG_DMA_ERROR
#  define dmaerr(format, ...) _terr(NXTAG_DMA, format, ##__VA_ARGS__)
#else
#  define dmaerr      _none
#endif

#ifdef CONFIG_DEBUG_DMA_WARN
#  define dmawarn(format, ...) _twarn(NXTAG_DMA, format, ##__VA_ARGS__)
#else
#  define dmawarn     _none
#endif

#ifdef CONFIG_DEBUG_DMA_INFO
#  define dmainfo(format, ...) _tinfo(NXTAG_DMA, format, ##__VA_ARGS__)
#else
#  define dmainfo     _none
#endif

#ifdef CONFIG_DEBUG_IRQ_ERROR
#  define irqerr(format, ...) _terr(NXTAG_IRQ, format, ##__VA_ARGS__)
#else
#  define irqerr      _none
#endif

#ifdef CONFIG_DEBUG_IRQ_WARN
#  define irqwarn(format, ...) _twarn(NXTAG_IRQ, format, ##__VA_ARGS__)
#else
#  define irqwarn     _none
#endif

#ifdef CONFIG_DEBUG_IRQ_INFO
#  define irqinfo(format, ...) _tinfo(NXTAG_IRQ, format, ##__VA_ARGS__)
#else
#  define irqinfo     _none
#endif

#ifdef CONFIG_DEBUG_LCD_ERROR
#  define lcderr(format, ...) _terr(NXTAG_LCD, format, ##__VA_ARGS__)
#else
#  define lcderr      _none
#endif

#ifdef CONFIG_DEBUG_LCD_WARN
#  define lcdwarn(format, ...) _twarn(NXTAG_LCD, format, ##__VA_ARGS__)
#else
#  define lcdwarn     _none
#endif

#ifdef CONFIG_DEBUG_LCD_INFO
#  define lcdinfo(format, ...) _tinfo(NXTAG_LCD, format, ##__VA_ARGS__)
#else
#  define lcdinfo     _none
#endif

#ifdef CONFIG_DEBUG_LEDS_ERROR
#  define lederr(format, ...) _terr(NXTAG_LEDS, format, ##__VA_ARGS__)
#else
#  define lederr      _none
#endif

#ifdef CONFIG_DEBUG_LEDS_WARN
#  define ledwarn(format, ...) _twarn(NXTAG_LEDS, format, ##__VA_ARGS__)
#else
#  define ledwarn     _none
#endif

#ifdef CONFIG_DEBUG_LEDS_INFO
#  define ledinfo(format, ...) _tinfo(NXTAG_LEDS, format, ##__VA_ARGS__)
#else
#  define ledinfo     _none
#endif

#ifdef CONFIG_DEBUG_GPIO_ERROR
#  define gpioerr(format, ...) _terr(NXTAG_GPIO, format, ##__VA_ARGS__)
#else
#  define gpioerr     _none
#endif

#ifdef CONFIG_DEBUG_GPIO_WARN
#  define gpiowarn(format, ...) _twarn(NXTAG_GPIO, format, ##__VA_ARGS__)
#else
#  define gpiowarn    _none
#endif

#ifdef CONFIG_DEBUG_GPIO_INFO
#  define gpioinfo(format, ...) _tinfo(NXTAG_GPIO, format, ##__VA_ARGS__)
#else
#  define gpioinfo    _none
#endif

#ifdef CONFIG_DEBUG_I2C_ERROR
#  define i2cerr(format, ...) _terr(NXTAG_I2C, format, ##__VA_ARGS__)
#else
#  define i2cerr      _none
#endif

#ifdef CONFIG_DEBUG_I2C_WARN
#  define i2cwarn(format, ...) _twarn(NXTAG_I2C, format, ##__VA_ARGS__)
#else
#  define i2cwarn     _none
#endif

#ifdef CONFIG_DEBUG_I2C_INFO
#  define i2cinfo(format, ...) _tinfo(NXTAG_I2C, format, ##__VA_ARGS__)
#else
#  define i2cinfo     _none
#endif

#ifdef CONFIG_DEBUG_I2S_ERROR
#  define i2serr(format, ...) _terr(NXTAG_I2S, format, ##__VA_ARGS__)
#else
#  define i2serr      _none
#endif

#ifdef CONFIG_DEBUG_I2S_WARN
#  define i2swarn(format, ...) _twarn(NXTAG_I2S, format, ##__VA_ARGS__)
#else
#  define i2swarn     _none
#endif

#ifdef CONFIG_DEBUG_I2S_INFO
#  define i2sinfo(format, ...) _tinfo(NXTAG_I2S, format, ##__VA_ARGS__)
#else
#  define i2sinfo     _none
#endif

#ifdef CONFIG_DEBUG_I3C_ERROR
#  define i3cerr(format, ...) _terr(NXTAG_I3C, format, ##__VA_ARGS__)
#else
#  define i3cerr      _none
#endif

#ifdef CONFIG_DEBUG_I3C_WARN
#  define i3cwarn(format, ...) _twarn(NXTAG_I3C, format, ##__VA_ARGS__)
#else
#  define i3cwarn     _none
#endif

#ifdef CONFIG_DEBUG_I3C_INFO
#  define i3cinfo(format, ...) _tinfo(NXTAG_I3C, format, ##__VA_ARGS__)
#else
#  define i3cinfo     _none
#endif

#ifdef CONFIG_DEBUG_PWM_ERROR
#  define pwmerr(format, ...) _terr(NXTAG_PWM, format, ##__VA_ARGS__)
#else
#  define pwmerr      _none
#endif

#ifdef CONFIG_DEBUG_PWM_WARN
#  define pwmwarn(format, ...) _twarn(NXTAG_PWM, format, ##__VA_ARGS__)
#else
#  define pwmwarn     _none
#endif

#ifdef CONFIG_DEBUG_PWM_INFO
#  define pwminfo(format, ...) _tinfo(NXTAG_PWM, format, ##__VA_ARGS__)
#else
#  define pwminfo     _none
#endif

#ifdef CONFIG_DEBUG_CAPTURE_ERROR
#  define cperr(format, ...) _terr(NXTAG_CAPTURE, format, ##__VA_ARGS__)
#else
#  define cperr      _none
#endif

#ifdef CONFIG_DEBUG_CAPTURE_WARN
#  define cpwarn(format, ...) _twarn(NXTAG_CAPTURE, format, ##__VA_ARGS__)
#else
#  define cpwarn     _none
#endif

#ifdef CONFIG_DEBUG_CAPTURE_INFO
#  define cpinfo(format, ...) _tinfo(NXTAG_CAPTURE, format, ##__VA_ARGS__)
#else
#  define cpinfo     _none
#endif

#ifdef CONFIG_DEBUG_RC_ERROR
#  define rcerr(format, ...) _terr(NXTAG_RC, format, ##__VA_ARGS__)
#else
#  define rcerr       _none
#endif

#ifdef CONFIG_DEBUG_RC_WARN
#  define rcwarn(format, ...) _twarn(NXTAG_RC, format, ##__VA_ARGS__)
#else
#  define rcwarn      _none
#endif

#ifdef CONFIG_DEBUG_RC_INFO
#  define rcinfo(format, ...) _tinfo(NXTAG_RC, format, ##__VA_ARGS__)
#else
#  define rcinfo      _none
#endif

#ifdef CONFIG_DEBUG_REGMAP_ERROR
#  define regmaperr(format, ...) _terr(NXTAG_REGMAP, format, ##__VA_ARGS__)
#else
#  define regmaperr      _none
#endif

#ifdef CONFIG_DEBUG_REGMAP_WARN
#  define regmapwarn(format, ...) _twarn(NXTAG_REGMAP, format, ##__VA_ARGS__)
#else
#  define regmapwarn     _none
#endif

#ifdef CONFIG_DEBUG_REGMAP_INFO
#  define regmapinfo(format, ...) _tinfo(NXTAG_REGMAP, format, ##__VA_ARGS__)
#else
#  define regmapinfo     _none
#endif

#ifdef CONFIG_DEBUG_RMT_ERROR
#  define rmterr(format, ...) _terr(NXTAG_RMT, format, ##__VA_ARGS__)
#else
#  define rmterr       _none
#endif

#ifdef CONFIG_DEBUG_RMT_WARN
#  define rmtwarn(format, ...) _twarn(NXTAG_RMT, format, ##__VA_ARGS__)
#else
#  define rmtwarn      _none
#endif

#ifdef CONFIG_DEBUG_RMT_INFO
#  define rmtinfo(format, ...) _tinfo(NXTAG_RMT, format, ##__VA_ARGS__)
#else
#  define rmtinfo      _none
#endif

#ifdef CONFIG_DEBUG_RTC_ERROR
#  define rtcerr(format, ...) _terr(NXTAG_RTC, format, ##__VA_ARGS__)
#else
#  define rtcerr      _none
#endif

#ifdef CONFIG_DEBUG_RTC_WARN
#  define rtcwarn(format, ...) _twarn(NXTAG_RTC, format, ##__VA_ARGS__)
#else
#  define rtcwarn     _none
#endif

#ifdef CONFIG_DEBUG_RTC_INFO
#  define rtcinfo(format, ...) _tinfo(NXTAG_RTC, format, ##__VA_ARGS__)
#else
#  define rtcinfo     _none
#endif

#ifdef CONFIG_DEBUG_MEMCARD_ERROR
#  define mcerr(format, ...) _terr(NXTAG_MEMCARD, format, ##__VA_ARGS__)
#else
#  define mcerr       _none
#endif

#ifdef CONFIG_DEBUG_MEMCARD_WARN
#  define mcwarn(format, ...) _twarn(NXTAG_MEMCARD, format, ##__VA_ARGS__)
#else
#  define mcwarn      _none
#endif

#ifdef CONFIG_DEBUG_MEMCARD_INFO
#  define mcinfo(format, ...) _tinfo(NXTAG_MEMCARD, format, ##__VA_ARGS__)
#else
#  define mcinfo      _none
#endif

#ifdef CONFIG_DEBUG_SENSORS_ERROR
#  define snerr(format, ...) _terr(NXTAG_SENSORS, format, ##__VA_ARGS__)
#else
#  define snerr       _none
#endif

#ifdef CONFIG_DEBUG_SENSORS_WARN
#  define snwarn(format, ...) _twarn(NXTAG_SENSORS, format, ##__VA_ARGS__)
#else
#  define snwarn      _none
#endif

#ifdef CONFIG_DEBUG_SENSORS_INFO
#  define sninfo(format, ...) _tinfo(NXTAG_SENSORS, format, ##__VA_ARGS__)
#else
#  define sninfo      _none
#endif

#ifdef CONFIG_DEBUG_SAFETY_ERROR
#  define saerr(format, ...) _terr(NXTAG_SAFETY, format, ##__VA_ARGS__)
#else
#  define saerr       _none
#endif

#ifdef CONFIG_DEBUG_SAFETY_WARN
#  define sawarn(format, ...) _twarn(NXTAG_SAFETY, format, ##__VA_ARGS__)
#else
#  define sawarn      _none
#endif

#ifdef CONFIG_DEBUG_SAFETY_INFO
#  define sainfo(format, ...) _tinfo(NXTAG_SAFETY, format, ##__VA_ARGS__)
#else
#  define sainfo      _none
#endif

#ifdef CONFIG_DEBUG_SPI_ERROR
#  define spierr(format, ...) _terr(NXTAG_SPI, format, ##__VA_ARGS__)
#else
#  define spierr      _none
#endif

#ifdef CONFIG_DEBUG_SPI_WARN
#  define spiwarn(format, ...) _twarn(NXTAG_SPI, format, ##__VA_ARGS__)
#else
#  define spiwarn     _none
#endif

#ifdef CONFIG_DEBUG_SPI_INFO
#  define spiinfo(format, ...) _tinfo(NXTAG_SPI, format, ##__VA_ARGS__)
#else
#  define spiinfo     _none
#endif

#ifdef CONFIG_DEBUG_THERMAL_ERROR
#  define therr(format, ...) _terr(NXTAG_THERMAL, format, ##__VA_ARGS__)
#else
#  define therr        _none
#endif

#ifdef CONFIG_DEBUG_THERMAL_WARN
#  define thwarn(format, ...) _twarn(NXTAG_THERMAL, format, ##__VA_ARGS__)
#else
#  define thwarn       _none
#endif

#ifdef CONFIG_DEBUG_THERMAL_INFO
#  define thinfo(format, ...) _tinfo(NXTAG_THERMAL, format, ##__VA_ARGS__)
#else
#  define thinfo       _none
#endif

#ifdef CONFIG_DEBUG_TIMER_ERROR
#  define tmrerr(format, ...) _terr(NXTAG_TIMER, format, ##__VA_ARGS__)
#else
#  define tmrerr      _none
#endif

#ifdef CONFIG_DEBUG_TIMER_WARN
#  define tmrwarn(format, ...) _twarn(NXTAG_TIMER, format, ##__VA_ARGS__)
#else
#  define tmrwarn     _none
#endif

#ifdef CONFIG_DEBUG_TIMER_INFO
#  define tmrinfo(format, ...) _tinfo(NXTAG_TIMER, format, ##__VA_ARGS__)
#else
#  define tmrinfo     _none
#endif

#ifdef CONFIG_DEBUG_USB_ERROR
#  define uerr(format, ...) _terr(NXTAG_USB, format, ##__VA_ARGS__)
#else
#  define uerr        _none
#endif

#ifdef CONFIG_DEBUG_USB_WARN
#  define uwarn(format, ...) _twarn(NXTAG_USB, format, ##__VA_ARGS__)
#else
#  define uwarn       _none
#endif

#ifdef CONFIG_DEBUG_USB_INFO
#  define uinfo(format, ...) _tinfo(NXTAG_USB, format, ##__VA_ARGS__)
#else
#  define uinfo       _none
#endif

#ifdef CONFIG_DEBUG_WATCHDOG_ERROR
#  define wderr(format, ...) _terr(NXTAG_WATCHDOG, format, ##__VA_ARGS__)
#else
#  define wderr       _none
#endif

#ifdef CONFIG_DEBUG_WATCHDOG_WARN
#  define wdwarn(format, ...) _twarn(NXTAG_WATCHDOG, format, ##__VA_ARGS__)
#else
#  define wdwarn      _none
#endif

#ifdef CONFIG_DEBUG_WATCHDOG_INFO
#  define wdinfo(format, ...) _tinfo(NXTAG_WATCHDOG, format, ##__VA_ARGS__)
#else
#  define wdinfo      _none
#endif

#ifdef CONFIG_DEBUG_MOTOR_ERROR
#  define mtrerr(format, ...) _terr(NXTAG_MOTOR, format, ##__VA_ARGS__)
#else
#  define mtrerr      _none
#endif

#ifdef CONFIG_DEBUG_MOTOR_WARN
#  define mtrwarn(format, ...) _twarn(NXTAG_MOTOR, format, ##__VA_ARGS__)
#else
#  define mtrwarn     _none
#endif

#ifdef CONFIG_DEBUG_MOTOR_INFO
#  define mtrinfo(format, ...) _tinfo(NXTAG_MOTOR, format, ##__VA_ARGS__)
#else
#  define mtrinfo     _none
#endif

#ifdef CONFIG_DEBUG_STEPPER_ERROR
#  define stperr(format, ...) _terr(NXTAG_STEPPER, format, ##__VA_ARGS__)
#else
#  define stperr      _none
#endif

#ifdef CONFIG_DEBUG_STEPPER_WARN
#  define stpwarn(format, ...) _twarn(NXTAG_STEPPER, format, ##__VA_ARGS__)
#else
#  define stpwarn     _none
#endif

#ifdef CONFIG_DEBUG_STEPPER_INFO
#  define stpinfo(format, ...) _tinfo(NXTAG_STEPPER, format, ##__VA_ARGS__)
#else
#  define stpinfo     _none
#endif

#ifdef CONFIG_DEBUG_VIDEO_ERROR
#  define verr(format, ...) _terr(NXTAG_VIDEO, format, ##__VA_ARGS__)
#else
#  define verr        _none
#endif

#ifdef CONFIG_DEBUG_VIDEO_WARN
#  define vwarn(format, ...) _twarn(NXTAG_VIDEO, format, ##__VA_ARGS__)
#else
#  define vwarn       _none
#endif

#ifdef CONFIG_DEBUG_VIDEO_INFO
#  define vinfo(format, ...) _tinfo(NXTAG_VIDEO, format, ##__VA_ARGS__)
#else
#  define vinfo       _none
#endif

#ifdef CONFIG_DEBUG_VIRTIO_ERROR
#  define vrterr(format, ...) _terr(NXTAG_VIRTIO, format, ##__VA_ARGS__)
#else
#  define vrterr      _none
#endif

#ifdef CONFIG_DEBUG_VIRTIO_WARN
#  define vrtwarn(format, ...) _twarn(NXTAG_VIRTIO, format, ##__VA_ARGS__)
#else
#  define vrtwarn     _none
#endif

#ifdef CONFIG_DEBUG_VIRTIO_INFO
#  define vrtinfo(format, ...) _tinfo(NXTAG_VIRTIO, format, ##__VA_ARGS__)
#else
#  define vrtinfo     _none
#endif

#ifdef CONFIG_DEBUG_VHOST_ERROR
#  define vhosterr(format, ...) _terr(NXTAG_VHOST, format, ##__VA_ARGS__)
#else
#  define vhosterr     _none
#endif

#ifdef CONFIG_DEBUG_VHOST_WARN
#  define vhostwarn(format, ...) _twarn(NXTAG_VHOST, format, ##__VA_ARGS__)
#else
#  define vhostwarn    _none
#endif

#ifdef CONFIG_DEBUG_VHOST_INFO
#  define vhostinfo(format, ...) _tinfo(NXTAG_VHOST, format, ##__VA_ARGS__)
#else
#  define vhostinfo    _none
#endif

#ifdef CONFIG_DEBUG_RESET_ERROR
#  define rsterr(format, ...) _terr(NXTAG_RESET, format, ##__VA_ARGS__)
#else
#  define rsterr      _none
#endif

#ifdef CONFIG_DEBUG_RESET_WARN
#  define rstwarn(format, ...) _twarn(NXTAG_RESET, format, ##__VA_ARGS__)
#else
#  define rstwarn     _none
#endif

#ifdef CONFIG_DEBUG_RESET_INFO
#  define rstinfo(format, ...) _tinfo(NXTAG_RESET, format, ##__VA_ARGS__)
#else
#  define rstinfo     _none
#endif

#ifdef CONFIG_DEBUG_IPC_ERROR
#  define ipcerr(format, ...) _terr(NXTAG_IPC, format, ##__VA_ARGS__)
#else
#  define ipcerr      _none
#endif

#ifdef CONFIG_DEBUG_IPC_WARN
#  define ipcwarn(format, ...) _twarn(NXTAG_IPC, format, ##__VA_ARGS__)
#else
#  define ipcwarn     _none
#endif

#ifdef CONFIG_DEBUG_IPC_INFO
#  define ipcinfo(format, ...) _tinfo(NXTAG_IPC, format, ##__VA_ARGS__)
#else
#  define ipcinfo     _none
#endif

#ifdef CONFIG_DEBUG_PCI_ERROR
#  define pcierr(format, ...) _terr(NXTAG_PCI, format, ##__VA_ARGS__)
#else
#  define pcierr      _none
#endif

#ifdef CONFIG_DEBUG_PCI_WARN
#  define pciwarn(format, ...) _twarn(NXTAG_PCI, format, ##__VA_ARGS__)
#else
#  define pciwarn     _none
#endif

#ifdef CONFIG_DEBUG_PCI_INFO
#  define pciinfo(format, ...) _tinfo(NXTAG_PCI, format, ##__VA_ARGS__)
#else
#  define pciinfo     _none
#endif

#ifdef CONFIG_DEBUG_RPMSG_ERROR
#  define rpmsgerr(format, ...) _terr(NXTAG_RPMSG, format, ##__VA_ARGS__)
#else
#  define rpmsgerr      _none
#endif

#ifdef CONFIG_DEBUG_RPMSG_WARN
#  define rpmsgwarn(format, ...) _twarn(NXTAG_RPMSG, format, ##__VA_ARGS__)
#else
#  define rpmsgwarn     _none
#endif

#ifdef CONFIG_DEBUG_RPMSG_INFO
#  define rpmsginfo(format, ...) _tinfo(NXTAG_RPMSG, format, ##__VA_ARGS__)
#else
#  define rpmsginfo     _none
#endif

#ifdef CONFIG_DEBUG_RPTUN_ERROR
#  define rptunerr(format, ...) _terr(NXTAG_RPTUN, format, ##__VA_ARGS__)
#else
#  define rptunerr     _none
#endif

#ifdef CONFIG_DEBUG_RPTUN_WARN
#  define rptunwarn(format, ...) _twarn(NXTAG_RPTUN, format, ##__VA_ARGS__)
#else
#  define rptunwarn    _none
#endif

#ifdef CONFIG_DEBUG_RPTUN_INFO
#  define rptuninfo(format, ...) _tinfo(NXTAG_RPTUN, format, ##__VA_ARGS__)
#else
#  define rptuninfo    _none
#endif

#ifdef CONFIG_DEBUG_HWTRACING_ERROR
#  define hterr(format, ...) _terr(NXTAG_HWTRACING, format, ##__VA_ARGS__)
#else
#  define hterr      _none
#endif

#ifdef CONFIG_DEBUG_HWTRACING_WARN
#  define htwarn(format, ...) _twarn(NXTAG_HWTRACING, format, ##__VA_ARGS__)
#else
#  define htwarn     _none
#endif

#ifdef CONFIG_DEBUG_HWTRACING_INFO
#  define htinfo(format, ...) _tinfo(NXTAG_HWTRACING, format, ##__VA_ARGS__)
#else
#  define htinfo     _none
#endif

#ifdef CONFIG_DEBUG_PTP_ERROR
#  define ptperr(format, ...) _terr(NXTAG_PTP, format, ##__VA_ARGS__)
#else
#  define ptperr     _none
#endif

#ifdef CONFIG_DEBUG_PTP_WARN
#  define ptpwarn(format, ...) _twarn(NXTAG_PTP, format, ##__VA_ARGS__)
#else
#  define ptpwarn    _none
#endif

#ifdef CONFIG_DEBUG_PTP_INFO
#  define ptpinfo(format, ...) _tinfo(NXTAG_PTP, format, ##__VA_ARGS__)
#else
#  define ptpinfo    _none
#endif

#ifdef CONFIG_DEBUG_IOB_ERR
#  ifdef CONFIG_SYSLOG_NETDEV
#    define ioberr   lowsyslog
#  else
#    define ioberr   _err
#  endif
#else
#    define ioberr   _none
#endif

#ifdef CONFIG_DEBUG_IOB_WARN
#  ifdef CONFIG_SYSLOG_NETDEV
#    define iobwarn  lowsyslog
#  else
#    define iobwarn  _warn
#  endif
#else
#    define iobwarn  _none
#endif

#ifdef CONFIG_DEBUG_IOB_INFO
#  ifdef CONFIG_SYSLOG_NETDEV
#    define iobinfo  lowsyslog
#  else
#    define iobinfo  _info
#  endif
#else
#    define iobinfo  _none
#endif

/* Buffer dumping macros do not depend on varargs */

#ifdef CONFIG_DEBUG_ERROR
#  define errdumpbuffer(m,b,n) lib_dumpbuffer(m,b,n)
#  ifdef CONFIG_DEBUG_INFO
#    define infodumpbuffer(m,b,n) lib_dumpbuffer(m,b,n)
#  else
#    define infodumpbuffer(m,b,n)
#  endif
#else
#    define errdumpbuffer(m,b,n)
#    define infodumpbuffer(m,b,n)
#  endif

/* Subsystem specific debug */

#ifdef CONFIG_DEBUG_MM
#  define merrdumpbuffer(m,b,n)  errdumpbuffer(m,b,n)
#  define minfodumpbuffer(m,b,n) infodumpbuffer(m,b,n)
#else
#  define merrdumpbuffer(m,b,n)
#  define minfodumpbuffer(m,b,n)
#endif

#ifdef CONFIG_DEBUG_SCHED
#  define serrdumpbuffer(m,b,n)  errdumpbuffer(m,b,n)
#  define sinfodumpbuffer(m,b,n) infodumpbuffer(m,b,n)
#else
#  define serrdumpbuffer(m,b,n)
#  define sinfodumpbuffer(m,b,n)
#endif

#ifdef CONFIG_DEBUG_SYSCALL
#  define svcerrdumpbuffer(m,b,n)  errdumpbuffer(m,b,n)
#  define svcinfodumpbuffer(m,b,n) infodumpbuffer(m,b,n)
#else
#  define svcerrdumpbuffer(m,b,n)
#  define svcinfodumpbuffer(m,b,n)
#endif

#ifdef CONFIG_DEBUG_PAGING
#  define pgerrdumpbuffer(m,b,n)  errdumpbuffer(m,b,n)
#  define pginfodumpbuffer(m,b,n) infodumpbuffer(m,b,n)
#else
#  define pgerrdumpbuffer(m,b,n)
#  define pginfodumpbuffer(m,b,n)
#endif

#ifdef CONFIG_DEBUG_NET
#  define nerrdumpbuffer(m,b,n)  errdumpbuffer(m,b,n)
#  define ninfodumpbuffer(m,b,n) infodumpbuffer(m,b,n)
#else
#  define nerrdumpbuffer(m,b,n)
#  define ninfodumpbuffer(m,b,n)
#endif

#ifdef CONFIG_DEBUG_POWER
#  define pwrerrdumpbuffer(m,b,n)  errdumpbuffer(m,b,n)
#  define pwrinfodumpbuffer(m,b,n) infodumpbuffer(m,b,n)
#else
#  define pwrerrdumpbuffer(m,b,n)
#  define pwrinfodumpbuffer(m,b,n)
#endif

#ifdef CONFIG_DEBUG_WIRELESS
#  define wlerrdumpbuffer(m,b,n)  errdumpbuffer(m,b,n)
#  define wlinfodumpbuffer(m,b,n) infodumpbuffer(m,b,n)
#else
#  define wlerrdumpbuffer(m,b,n)
#  define wlinfodumpbuffer(m,b,n)
#endif

#ifdef CONFIG_DEBUG_FS
#  define ferrdumpbuffer(m,b,n)  errdumpbuffer(m,b,n)
#  define finfodumpbuffer(m,b,n) infodumpbuffer(m,b,n)
#else
#  define ferrdumpbuffer(m,b,n)
#  define finfodumpbuffer(m,b,n)
#endif

#ifdef CONFIG_DEBUG_CONTACTLESS
#  define ctlserrdumpbuffer(m,b,n) errdumpbuffer(m,b,n)
#  define ctlinfodumpbuffer(m,b,n) infodumpbuffer(m,b,n)
#else
#  define ctlserrferrdumpbuffer(m,b,n)
#  define ctlinfodumpbuffer(m,b,n)
#endif

#ifdef CONFIG_DEBUG_INPUT
#  define ierrdumpbuffer(m,b,n)  errdumpbuffer(m,b,n)
#  define iinfodumpbuffer(m,b,n) infodumpbuffer(m,b,n)
#else
#  define ierrdumpbuffer(m,b,n)
#  define iinfodumpbuffer(m,b,n)
#endif

#ifdef CONFIG_DEBUG_ANALOG
#  define aerrdumpbuffer(m,b,n)  errdumpbuffer(m,b,n)
#  define ainfodumpbuffer(m,b,n) infodumpbuffer(m,b,n)
#else
#  define aerrdumpbuffer(m,b,n)
#  define ainfodumpbuffer(m,b,n)
#endif

#ifdef CONFIG_DEBUG_CAN
#  define canerrdumpbuffer(m,b,n)  errdumpbuffer(m,b,n)
#  define caninfodumpbuffer(m,b,n) infodumpbuffer(m,b,n)
#else
#  define canerrdumpbuffer(m,b,n)
#  define caninfodumpbuffer(m,b,n)
#endif

#ifdef CONFIG_DEBUG_GRAPHICS
#  define gerrdumpbuffer(m,b,n)  errdumpbuffer(m,b,n)
#  define ginfodumpbuffer(m,b,n) infodumpbuffer(m,b,n)
#else
#  define gerrdumpbuffer(m,b,n)
#  define ginfodumpbuffer(m,b,n)
#endif

#ifdef CONFIG_DEBUG_BINFMT
#  define berrdumpbuffer(m,b,n)  errdumpbuffer(m,b,n)
#  define binfodumpbuffer(m,b,n) infodumpbuffer(m,b,n)
#else
#  define berrdumpbuffer(m,b,n)
#  define binfodumpbuffer(m,b,n)
#endif

#ifdef CONFIG_DEBUG_LIB
#  define lerrdumpbuffer(m,b,n)  errdumpbuffer(m,b,n)
#  define linfodumpbuffer(m,b,n) infodumpbuffer(m,b,n)
#else
#  define lerrdumpbuffer(m,b,n)
#  define linfodumpbuffer(m,b,n)
#endif

#ifdef CONFIG_DEBUG_AUDIO
#  define auderrdumpbuffer(m,b,n)  errdumpbuffer(m,b,n)
#  define audinfodumpbuffer(m,b,n) infodumpbuffer(m,b,n)
#else
#  define auderrdumpbuffer(m,b,n)
#  define audinfodumpbuffer(m,b,n)
#endif

#ifdef CONFIG_DEBUG_DMA
#  define dmaerrdumpbuffer(m,b,n)  errdumpbuffer(m,b,n)
#  define dmainfodumpbuffer(m,b,n) infodumpbuffer(m,b,n)
#else
#  define dmaerrdumpbuffer(m,b,n)
#  define dmainfodumpbuffer(m,b,n)
#endif

#ifdef CONFIG_DEBUG_IRQ
#  define irqerrdumpbuffer(m,b,n)  errdumpbuffer(m,b,n)
#  define irqinfodumpbuffer(m,b,n) infodumpbuffer(m,b,n)
#else
#  define irqerrdumpbuffer(m,b,n)
#  define irqinfodumpbuffer(m,b,n)
#endif

#ifdef CONFIG_DEBUG_LCD
#  define lcderrdumpbuffer(m,b,n)  errdumpbuffer(m,b,n)
#  define lcdinfodumpbuffer(m,b,n) infodumpbuffer(m,b,n)
#else
#  define lcderrdumpbuffer(m,b,n)
#  define lcdinfodumpbuffer(m,b,n)
#endif

#ifdef CONFIG_DEBUG_LEDS
#  define lederrdumpbuffer(m,b,n)  errdumpbuffer(m,b,n)
#  define ledinfodumpbuffer(m,b,n) infodumpbuffer(m,b,n)
#else
#  define lederrdumpbuffer(m,b,n)
#  define ledinfodumpbuffer(m,b,n)
#endif

#ifdef CONFIG_DEBUG_GPIO
#  define gpioerrdumpbuffer(m,b,n)  errdumpbuffer(m,b,n)
#  define gpioinfodumpbuffer(m,b,n) infodumpbuffer(m,b,n)
#else
#  define gpioerrdumpbuffer(m,b,n)
#  define gpioinfodumpbuffer(m,b,n)
#endif

#ifdef CONFIG_DEBUG_I2C
#  define i2cerrdumpbuffer(m,b,n)  errdumpbuffer(m,b,n)
#  define i2cinfodumpbuffer(m,b,n) infodumpbuffer(m,b,n)
#else
#  define i2cerrdumpbuffer(m,b,n)
#  define i2cinfodumpbuffer(m,b,n)
#endif

#ifdef CONFIG_DEBUG_I2S
#  define i2serrdumpbuffer(m,b,n)  errdumpbuffer(m,b,n)
#  define i2sinfodumpbuffer(m,b,n) infodumpbuffer(m,b,n)
#else
#  define i2serrdumpbuffer(m,b,n)
#  define i2sinfodumpbuffer(m,b,n)
#endif

#ifdef CONFIG_DEBUG_I3C
#  define i3cerrdumpbuffer(m,b,n)  errdumpbuffer(m,b,n)
#  define i3cinfodumpbuffer(m,b,n) infodumpbuffer(m,b,n)
#else
#  define i3cerrdumpbuffer(m,b,n)
#  define i3cinfodumpbuffer(m,b,n)
#endif

#ifdef CONFIG_DEBUG_PWM
#  define pwmerrdumpbuffer(m,b,n)  errdumpbuffer(m,b,n)
#  define pwminfodumpbuffer(m,b,n) infodumpbuffer(m,b,n)
#else
#  define pwmerrdumpbuffer(m,b,n)
#  define pwminfodumpbuffer(m,b,n)
#endif

#ifdef CONFIG_DEBUG_RTC
#  define rtcerrdumpbuffer(m,b,n)  errdumpbuffer(m,b,n)
#  define rtcinfodumpbuffer(m,b,n) infodumpbuffer(m,b,n)
#else
#  define rtcerrdumpbuffer(m,b,n)
#  define rtcinfodumpbuffer(m,b,n)
#endif

#ifdef CONFIG_DEBUG_MEMCARD
#  define mcerrdumpbuffer(m,b,n)  errdumpbuffer(m,b,n)
#  define mcinfodumpbuffer(m,b,n) infodumpbuffer(m,b,n)
#else
#  define mcerrdumpbuffer(m,b,n)
#  define mcinfodumpbuffer(m,b,n)
#endif

#ifdef CONFIG_DEBUG_SENSORS
#  define snerrdumpbuffer(m,b,n)  errdumpbuffer(m,b,n)
#  define sninfodumpbuffer(m,b,n) infodumpbuffer(m,b,n)
#else
#  define snerrdumpbuffer(m,b,n)
#  define sninfodumpbuffer(m,b,n)
#endif

#ifdef CONFIG_DEBUG_SPI
#  define spierrdumpbuffer(m,b,n)  errdumpbuffer(m,b,n)
#  define spiinfodumpbuffer(m,b,n) infodumpbuffer(m,b,n)
#else
#  define spierrdumpbuffer(m,b,n)
#  define spiinfodumpbuffer(m,b,n)
#endif

#ifdef CONFIG_DEBUG_TIMER
#  define tmrerrdumpbuffer(m,b,n)  errdumpbuffer(m,b,n)
#  define tmrinfodumpbuffer(m,b,n) infodumpbuffer(m,b,n)
#else
#  define tmrerrdumpbuffer(m,b,n)
#  define tmrinfodumpbuffer(m,b,n)
#endif

#ifdef CONFIG_DEBUG_USB
#  define uerrdumpbuffer(m,b,n)  errdumpbuffer(m,b,n)
#  define uinfodumpbuffer(m,b,n) infodumpbuffer(m,b,n)
#else
#  define uerrdumpbuffer(m,b,n)
#  define uinfodumpbuffer(m,b,n)
#endif

#ifdef CONFIG_DEBUG_WATCHDOG
#  define wderrdumpbuffer(m,b,n)  errdumpbuffer(m,b,n)
#  define wdinfodumpbuffer(m,b,n) infodumpbuffer(m,b,n)
#else
#  define wderrdumpbuffer(m,b,n)
#  define wdinfodumpbuffer(m,b,n)
#endif

#ifdef CONFIG_DEBUG_MOTOR
#  define mtrerrdumpbuffer(m,b,n)  errdumpbuffer(m,b,n)
#  define mtrinfodumpbuffer(m,b,n) infodumpbuffer(m,b,n)
#else
#  define mtrerrdumpbuffer(m,b,n)
#  define mtrinfodumpbuffer(m,b,n)
#endif

#ifdef CONFIG_DEBUG_RESET
#  define reseterrdumpbuffer(m,b,n)  errdumpbuffer(m,b,n)
#  define resetinfodumpbuffer(m,b,n) infodumpbuffer(m,b,n)
#else
#  define reseterrdumpbuffer(m,b,n)
#  define resetinfodumpbuffer(m,b,n)
#endif

/****************************************************************************
 * Name: lowsyslog
 *
 * Description:
 *   lowsyslog() is used for output debug information at early boot-stage.
 *
 *   The NuttX implementation does not support any special formatting
 *   characters beyond those supported by printf.
 *
 ****************************************************************************/

#define lowsyslog(...) \
  do \
    { \
       struct lib_outstream_s stream; \
       lib_lowoutstream(&stream); \
       lib_sprintf(&stream, __VA_ARGS__); \
    } \
  while (0)

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#if defined(__cplusplus)
extern "C"
{
#endif

/* Type of the call out function pointer provided to
 * lib_dumphandler() or lib_dumpvhandler()
 */

typedef CODE void (*lib_dump_handler_t)(FAR void *arg,
                                        FAR const char *fmt, ...)
                  printf_like(2, 3);

/* Dump a buffer of data with handler */

void lib_dumphandler(FAR const char *msg, FAR const uint8_t *buffer,
                     unsigned int buflen, lib_dump_handler_t handler,
                     FAR void *arg);

/* Do a pretty buffer dump from multiple buffers with handler. */

void lib_dumpvhandler(FAR const char *msg, FAR const struct iovec *iov,
                      int iovcnt, lib_dump_handler_t handler,
                      FAR void *arg);

/* Dump a buffer of data */

void lib_dumpbuffer(FAR const char *msg, FAR const uint8_t *buffer,
                    unsigned int buflen);

/* Do a pretty buffer dump from multiple buffers. */

void lib_dumpvbuffer(FAR const char *msg, FAR const struct iovec *iov,
                     int iovcnt);

/* Dump a buffer of data with fd */

void lib_dumpfile(int fd, FAR const char *msg, FAR const uint8_t *buffer,
                  unsigned int buflen);

/* Do a pretty buffer dump from multiple buffers with fd. */

void lib_dumpvfile(int fd, FAR const char *msg, FAR const struct iovec *iov,
                   int iovcnt);

/* The system logging interfaces are normally accessed via the macros
 * provided above.  If the cross-compiler's C pre-processor supports a
 * variable number of macro arguments, then those macros below will map all
 * debug statements to the logging interfaces declared in syslog.h.
 *
 * If the cross-compiler's pre-processor does not support variable length
 * arguments, then these additional APIs will be built.
 */

#ifndef CONFIG_CPP_HAVE_VARARGS
#ifdef CONFIG_DEBUG_ALERT
void _alert(FAR const char *format, ...) syslog_like(1, 2);
#endif

#ifdef CONFIG_DEBUG_ERROR
void _err(FAR const char *format, ...) syslog_like(1, 2);
#endif

#ifdef CONFIG_DEBUG_WARN
void _warn(FAR const char *format, ...) syslog_like(1, 2);
#endif

#ifdef CONFIG_DEBUG_INFO
void _info(FAR const char *format, ...) syslog_like(1, 2);
#endif
#endif /* CONFIG_CPP_HAVE_VARARGS */

#if defined(__cplusplus)
}
#endif

#endif /* __INCLUDE_DEBUG_H */
