/****************************************************************************
 * include/nuttx/sched_note.h
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

#ifndef __INCLUDE_NUTTX_SCHED_NOTE_H
#define __INCLUDE_NUTTX_SCHED_NOTE_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <syslog.h>
#include <limits.h>

#ifdef CONFIG_SCHED_INSTRUMENTATION
  #include <nuttx/arch.h>
  #include <nuttx/macro.h>
  #include <nuttx/ratelimit.h>
#endif

#include <nuttx/note/note_driver.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Printf argument type */

#define NOTE_PRINTF_UINT32 0
#define NOTE_PRINTF_UINT64 1
#define NOTE_PRINTF_DOUBLE 2
#define NOTE_PRINTF_STRING 3

/* Get/set printf tag. each parameter occupies 2 bits. The highest
 * bits are used to represent the number of parameters.
 * On 32-bit platforms (uintptr_t = 4 bytes): 4-bit count, up to 14 args.
 * On 64-bit platforms (uintptr_t = 8 bytes): 5-bit count, up to 29 args.
 */

#define NOTE_PRINTF_COUNT_BITS \
    (sizeof(uintptr_t) >= 8 ? 5 : 4)
#define NOTE_PRINTF_COUNT_SHIFT \
    (sizeof(uintptr_t) * 8 - NOTE_PRINTF_COUNT_BITS)

#define NOTE_PRINTF_GET_TYPE(tag, index) (((tag) >> (index) * 2) & 0x03)
#define NOTE_PRINTF_GET_COUNT(tag)       ((tag) >> NOTE_PRINTF_COUNT_SHIFT)

#define sched_note_event(tag, level, event, buf, len) \
        sched_note_event_ip(tag, level, up_getpc(), event, buf, len)
#define sched_note_vprintf(tag, level, fmt, va) \
        sched_note_vprintf_ip(tag, level, up_getpc(), fmt, va)

#define sched_note_printf(tag, level, fmt, ...) \
        sched_note_printf_ip(tag, level, up_getpc(), fmt, ##__VA_ARGS__)

#define sched_note_begin(tag) \
        sched_note_event(tag, LOG_DEBUG, NOTE_DUMP_BEGIN, NULL, 0)
#define sched_note_end(tag) \
        sched_note_event(tag, LOG_DEBUG, NOTE_DUMP_END, NULL, 0)
#define sched_note_beginex(tag, str) \
        sched_note_event(tag, LOG_DEBUG, NOTE_DUMP_BEGIN, str, strlen(str))
#define sched_note_endex(tag, str) \
        sched_note_event(tag, LOG_DEBUG, NOTE_DUMP_END, str, strlen(str))
#define sched_note_mark(tag, str) \
        sched_note_event(tag, LOG_DEBUG, NOTE_DUMP_MARK, str, strlen(str))

#define sched_note_counter(tag, name_, value_) \
        do \
          { \
            struct note_counter_s counter; \
            counter.value = value_; \
            strlcpy(counter.name, name_, NAME_MAX); \
            sched_note_event(tag, LOG_DEBUG, NOTE_DUMP_COUNTER, \
                             &counter, sizeof(counter)); \
          } \
        while (0)

#define sched_note_threadtime(_elapsed) \
        do \
          { \
            struct note_threadtime_s threadtime; \
            threadtime.elapsed = (_elapsed); \
            sched_note_event(NXTAG_SCHED, LOG_DEBUG, NOTE_DUMP_THREADTIME, \
                             &threadtime, sizeof(threadtime)); \
          } \
        while (0)

#define sched_note_async_begin(tag, name, cookie) \
        sched_note_async(tag, NOTE_DUMP_ASYNC_BEGIN, NULL, name, cookie)
#define sched_note_async_end(tag, name, cookie) \
        sched_note_async(tag, NOTE_DUMP_ASYNC_END, NULL, name, cookie)
#define sched_note_async_begin_track(tag, track, name, cookie) \
        sched_note_async(tag, NOTE_DUMP_ASYNC_BEGIN, track, name, cookie)
#define sched_note_async_end_track(tag, track, name, cookie) \
        sched_note_async(tag, NOTE_DUMP_ASYNC_END, track, name, cookie)

#if defined(CONFIG_SCHED_INSTRUMENTATION_SWITCH) && defined(CONFIG_DRIVERS_NOTE)
#  define sched_note_start(t) \
       do \
         { \
           if (!note_filter_type(NOTE_START)) \
             { \
               note_start(t); \
             } \
         } \
       while (0)
#  define sched_note_stop(t) \
       do \
         { \
           if (!note_filter_type(NOTE_STOP)) \
             { \
               note_stop(t); \
             } \
         } \
       while (0)
#  define sched_note_suspend(t) \
       do \
         { \
           if (!note_filter_type(NOTE_SUSPEND)) \
             { \
               note_suspend(t); \
             } \
         } \
       while (0)
#  define sched_note_resume(t) \
       do \
         { \
           if (!note_filter_type(NOTE_RESUME)) \
             { \
               note_resume(t); \
             } \
         } \
       while (0)
#  define sched_note_ready(t,s) \
       do \
         { \
           if (!note_filter_type(NOTE_READY)) \
             { \
               note_ready(t,s); \
             } \
         } \
       while (0)
#else
#  define sched_note_stop(t)
#  define sched_note_start(t)
#  define sched_note_suspend(t)
#  define sched_note_resume(t)
#  define sched_note_ready(t,s)
#endif

#if defined(CONFIG_SMP) && defined(CONFIG_SCHED_INSTRUMENTATION_SWITCH) && defined(CONFIG_DRIVERS_NOTE)
#  define sched_note_cpu_start(t,c) \
       do \
         { \
           if (!note_filter_type(NOTE_CPU_START)) \
             { \
               note_cpu_start(t,c); \
             } \
         } \
       while (0)
#  define sched_note_cpu_started(t) \
       do \
         { \
           if (!note_filter_type(NOTE_CPU_STARTED)) \
             { \
               note_cpu_started(t); \
             } \
         } \
       while (0)
#else
#  define sched_note_cpu_start(t,c)
#  define sched_note_cpu_started(t)
#endif

#if defined(CONFIG_SCHED_INSTRUMENTATION_PREEMPTION) && defined(CONFIG_DRIVERS_NOTE)
#  define sched_note_preemption(t,l) \
       do \
         { \
           if (!note_filter_type(NOTE_PREEMPT_LOCK + !(l))) \
             { \
               note_preemption(t,l); \
             } \
         } \
       while (0)
#else
#  define sched_note_preemption(t,l)
#endif

#if defined(CONFIG_SCHED_INSTRUMENTATION_CSECTION) && defined(CONFIG_DRIVERS_NOTE)
#  define sched_note_csection(t,e) \
       do \
         { \
           if (!note_filter_type(NOTE_CSECTION_ENTER + !(e))) \
             { \
               note_csection(t,e); \
             } \
         } \
       while (0)
#else
#  define sched_note_csection(t,e)
#endif

#if defined(CONFIG_SCHED_INSTRUMENTATION_SPINLOCKS) && defined(CONFIG_DRIVERS_NOTE)
#  define sched_note_spinlock(s,t) \
       do \
         { \
           if (!note_filter_type(t)) \
             { \
               note_spinlock(s,t); \
             } \
         } \
       while (0)
#else
#  define sched_note_spinlock(s,t)
#endif

#if defined(CONFIG_SCHED_INSTRUMENTATION_SYSCALL) && defined(CONFIG_DRIVERS_NOTE)
#  define sched_note_syscall_enter(n,a,...) \
       do \
         { \
           if (!note_filter_syscall(n)) \
             { \
               note_syscall_enter(n,a,##__VA_ARGS__); \
             } \
         } \
       while (0)
#  define sched_note_syscall_leave(n,r) \
       do \
         { \
           if (!note_filter_syscall(n)) \
             { \
               note_syscall_leave(n,r); \
             } \
         } \
       while (0)
#else
#  define sched_note_syscall_enter(n,a,...)
#  define sched_note_syscall_leave(n,r)
#endif

#if defined(CONFIG_SCHED_INSTRUMENTATION_IRQHANDLER) && defined(CONFIG_DRIVERS_NOTE)
#  define sched_note_irqhandler(i,h,e) \
       do \
         { \
           if (!note_filter_irq(i)) \
             { \
               note_irqhandler(i,h,e); \
             } \
         } \
       while (0)
#else
#  define sched_note_irqhandler(i,h,e)
#endif

#if defined(CONFIG_SCHED_INSTRUMENTATION_WDOG) && defined(CONFIG_DRIVERS_NOTE)
#  define sched_note_wdog(e,h,a) \
       do \
         { \
           if (!note_filter_type(e)) \
             { \
               note_wdog(e,h,a); \
             } \
         } \
       while (0)
#else
#  define sched_note_wdog(e,h,a)
#endif

#if defined(CONFIG_SCHED_INSTRUMENTATION_HEAP) && defined(CONFIG_DRIVERS_NOTE)
#  define sched_note_heap(e,h,m,s,c) \
       do \
         { \
           if (!note_filter_type(e)) \
             { \
               note_heap(e,h,m,s,c); \
             } \
         } \
       while (0)
#else
#  define sched_note_heap(e,h,m,s,c)
#endif

#if defined(CONFIG_SCHED_INSTRUMENTATION_WQUEUE) && defined(CONFIG_DRIVERS_NOTE)
#  define sched_note_wqueue(e,w,a) \
       do \
         { \
           if (!note_filter_type(e)) \
             { \
               note_wqueue(e,w,a); \
             } \
         } \
       while (0)
#else
#  define sched_note_wqueue(e,w,a)
#endif

#if defined(CONFIG_SCHED_INSTRUMENTATION_DUMP) && defined(CONFIG_DRIVERS_NOTE)
#  define sched_note_event_ip(t,l,i,e,b,n) note_event_ip(t,l,i,e,b,n)

#  ifdef CONFIG_DRIVERS_NOTE_STRIP_FORMAT
#    define sched_note_vprintf_ip(t,l,i,f,v) \
          do \
            { \
              locate_data("note_format") used_data \
              static const struct { uint8_t sz; char str[sizeof(f)]; } \
              fmt = { sizeof(f) - 1, f }; \
              locate_data("note_type") used_data \
              static const union { FAR const char *fmt; uintptr_t type; } \
              ft = { .fmt = fmt.str }; \
              note_vprintf_ip(t,l,i,fmt.str,ft.type,&(v)); \
            } \
          while (0)
#    define sched_note_printf_ip(t,l,i,f,...) \
          do \
            { \
              locate_data("note_format") used_data \
              static const struct { uint8_t sz; char str[sizeof(f)]; } \
              fmt = { sizeof(f) - 1, f }; \
              locate_data("note_type") used_data \
              static const union { FAR const char *fmt; uintptr_t type; } \
              ft = { .fmt = fmt.str }; \
              note_printf_ip(t,l,i,fmt.str,ft.type,##__VA_ARGS__); \
            } \
          while (0)
#  else
#    define sched_note_vprintf_ip(t,l,i,f,v) \
         do \
           { \
             locate_data("note_format") used_data \
             static const struct { uint8_t sz; char str[sizeof(f)]; } \
             fmt = { sizeof(f) - 1, f }; \
             note_vprintf_ip(t,l,i,fmt.str,0,&(v)); \
           } \
         while (0)
#    define sched_note_printf_ip(t,l,i,f,...) \
         do \
           { \
             locate_data("note_format") used_data \
             static const struct { uint8_t sz; char str[sizeof(f)]; } \
             fmt = { sizeof(f) - 1, f }; \
             note_printf_ip(t,l,i,fmt.str,0,##__VA_ARGS__); \
           } \
         while (0)
#  endif

extern struct ratelimit_state_s g_note_ratelimit;

#  define sched_note_event_ip_ratelimit(t,l,i,e,b,n) \
       do \
         { \
           if (!ratelimit_islimited(&g_note_ratelimit)) \
             { \
               sched_note_event_ip(t,l,i,e,b,n); \
             } \
         } \
       while (0)

#  define sched_note_vprintf_ip_ratelimit(t,l,i,f,v) \
       do \
         { \
           if (!ratelimit_islimited(&g_note_ratelimit)) \
             { \
               sched_note_vprintf_ip(t,l,i,f,v); \
             } \
         } \
       while (0)

#  define sched_note_printf_ip_ratelimit(t,l,i,f,...) \
       do \
         { \
           if (!ratelimit_islimited(&g_note_ratelimit)) \
             { \
               sched_note_printf_ip(t,l,i,f, ##__VA_ARGS__); \
             } \
         } \
       while (0)

#define sched_note_event_ip_ratelimited(tag, level, ip, event, buf, len) \
  do                                                                     \
    {                                                                    \
      static DEFINE_RATELIMIT_STATE(_rs,                                 \
        CONFIG_DRIVERS_NOTE_RATELIMIT_INTERVAL,                          \
        CONFIG_DRIVERS_NOTE_RATELIMIT_BURST);                            \
      if (!ratelimit_islimited(&_rs))                                    \
        {                                                                \
          sched_note_event_ip(tag, level, ip, event, buf, len);          \
        }                                                                \
    }                                                                    \
  while (0)

#define sched_note_vprintf_ip_ratelimited(tag, level, ip, fmt, va) \
  do                                                                     \
    {                                                                    \
      static DEFINE_RATELIMIT_STATE(_rs,                                 \
        CONFIG_DRIVERS_NOTE_RATELIMIT_INTERVAL,                          \
        CONFIG_DRIVERS_NOTE_RATELIMIT_BURST);                            \
      if (!ratelimit_islimited(&_rs))                                    \
        {                                                                \
          sched_note_vprintf_ip(tag, level, ip, fmt, va);                \
        }                                                                \
    }                                                                    \
  while (0)

#define sched_note_printf_ip_ratelimited(tag, level, ip, fmt, ...) \
  do                                                                     \
    {                                                                    \
      static DEFINE_RATELIMIT_STATE(_rs,                                 \
        CONFIG_DRIVERS_NOTE_RATELIMIT_INTERVAL,                          \
        CONFIG_DRIVERS_NOTE_RATELIMIT_BURST);                            \
      if (!ratelimit_islimited(&_rs))                                    \
        {                                                                \
          sched_note_printf_ip(tag, level, ip, fmt,                      \
                               ##__VA_ARGS__);                           \
        }                                                                \
    }                                                                    \
  while (0)

#define sched_note_async(tag, event, track, name, ck) \
        do \
          { \
            struct note_max_s _tmp; \
            FAR struct note_async_s *_d = \
              (FAR struct note_async_s *)_tmp.data; \
            size_t _max = sizeof(_tmp.data) - \
                          sizeof(struct note_async_s); \
            size_t _nlen; \
            size_t _tlen = 0; \
            _d->cookie = (ck); \
            _nlen = strlcpy(_d->names, (name), _max) + 1; \
            if (_nlen > _max) \
              { \
                _nlen = _max; \
              } \
            if ((track) != NULL && _nlen < _max) \
              { \
                _tlen = strlcpy(_d->names + _nlen, (track), \
                                _max - _nlen) + 1; \
                if (_tlen > _max - _nlen) \
                  { \
                    _tlen = _max - _nlen; \
                  } \
              } \
            sched_note_event((tag), LOG_INFO, (event), \
                             _d, sizeof(struct note_async_s) + \
                             _nlen + _tlen); \
          } \
        while (0)

#else
#  define sched_note_event_ip(t,l,i,e,b,n)
#  define sched_note_vprintf_ip(t,l,i,f,v)
#  define sched_note_printf_ip(t,l,i,f,...)
#  define sched_note_event_ip_ratelimit(t,l,i,e,b,n)
#  define sched_note_vprintf_ip_ratelimit(t,l,i,f,v)
#  define sched_note_printf_ip_ratelimit(t,l,i,f,...)
#  define sched_note_event_ip_ratelimited(t,l,i,e,b,n)
#  define sched_note_vprintf_ip_ratelimited(t,l,i,f,va)
#  define sched_note_printf_ip_ratelimited(t,l,i,f,...)
#  define sched_note_async(t,e,r,n,c)
#endif /* CONFIG_SCHED_INSTRUMENTATION_DUMP && CONFIG_DRIVERS_NOTE */

#endif /* __INCLUDE_NUTTX_SCHED_NOTE_H */
