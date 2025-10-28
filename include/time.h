/****************************************************************************
 * include/time.h
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

#ifndef __INCLUDE_TIME_H
#define __INCLUDE_TIME_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/compiler.h>

#include <sys/types.h>
#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Clock tick of the system (frequency Hz).
 *
 * NOTE: This symbolic name CLK_TCK has been removed from the standard.
 *       It is replaced with CLOCKS_PER_SEC.  Both are defined here.
 *
 * The default value is 100Hz, but this default setting can be overridden by
 * defining the clock interval in microseconds as CONFIG_USEC_PER_TICK in the
 * board configuration file.
 */

#ifdef CONFIG_USEC_PER_TICK
#  define CLK_TCK                 (1000000/CONFIG_USEC_PER_TICK)
#  define CLOCKS_PER_SEC          (1000000/CONFIG_USEC_PER_TICK)
#else
#  define CLK_TCK                 (100)
#  define CLOCKS_PER_SEC          (100)
#endif

/* CLOCK_REALTIME refers to the standard time source.  For most
 * implementations, the standard time source is the system timer interrupt.
 * However, if the platform supports an RTC, then the standard time source
 * will be the RTC for the clock_gettime() and clock_settime() interfaces
 * (the system timer is still the time source for all of the interfaces).
 *
 * CLOCK_REALTIME represents the machine's best-guess as to the current
 * wall-clock, time-of-day time. This means that CLOCK_REALTIME can jump
 * forward and backward as the system time-of-day clock is changed.
 */

#define CLOCK_REALTIME            0

/* Clock that cannot be set and represents monotonic time since some
 * unspecified starting point. It is not affected by changes in the
 * system time-of-day clock.
 */

#define CLOCK_MONOTONIC           1

/* Clock that measures CPU time consumed by this process (i.e., CPU
 * time consumed by all threads in the process).
 */

#define CLOCK_PROCESS_CPUTIME_ID  2

/* Clock that measures CPU time consumed by this thread */

#define CLOCK_THREAD_CPUTIME_ID   3

/* Monotonic system-wide clock that includes time spent in suspension. */

#define CLOCK_BOOTTIME            4

/* This is a flag that may be passed to the timer_settime() and
 * clock_nanosleep() functions.
 */

#define TIMER_ABSTIME             1

/* Time base values for timespec_get.  */

#define TIME_UTC                  1

/* Redirect the timelocal and strftime_l */

#define timelocal                 mktime
#define strftime_l(s, m, f, t, l) strftime(s, m, f, t)

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* struct timespec is the standard representation of time as seconds and
 * nanoseconds.
 */

struct timespec
{
  time_t tv_sec;                   /* Seconds */
  long   tv_nsec;                  /* Nanoseconds */
};

/* struct tm is the standard representation for "broken out" time.
 *
 * REVISIT: This structure could be packed better using uint8_t's and
 * uint16_t's.  The standard definition does, however, call out type int for
 * all of the members.  NOTE: Any changes to this structure must be also be
 * reflected in struct rtc_time defined in include/nuttx/timers/rtc.h; these
 * two structures must be cast compatible.
 */

struct tm
{
  int  tm_sec;         /* Seconds (0-61, allows for leap seconds) */
  int  tm_min;         /* Minutes (0-59) */
  int  tm_hour;        /* Hours (0-23) */
  int  tm_mday;        /* Day of the month (1-31) */
  int  tm_mon;         /* Month (0-11) */
  int  tm_year;        /* Years since 1900 */
  int  tm_wday;        /* Day of the week (0-6) */
  int  tm_yday;        /* Day of the year (0-365) */
  int  tm_isdst;       /* Non-0 if daylight savings time is in effect */
  long tm_gmtoff;      /* Offset from UTC in seconds */
  const char *tm_zone; /* Timezone abbreviation. */
};

/* Struct itimerspec is used to define settings for an interval timer */

struct itimerspec
{
  struct timespec it_interval; /* Thereafter */
  struct timespec it_value;    /* First time */
};

/* forward reference (defined in signal.h) */

struct sigevent;

/****************************************************************************
 * Public Data
 ****************************************************************************/

#undef EXTERN
#if defined(__cplusplus)
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

#ifdef CONFIG_LIBC_LOCALTIME

/* daylight - Daylight savings time flag */

/* EXTERN int daylight; not supported */

/* timezone - Difference from UTC and local standard time */

/* EXTERN long int timezone; not supported */

/* tzname[] - Timezone strings
 * Setup by tzset()
 */

EXTERN FAR char *tzname[2];
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

clock_t clock(void);

int clock_settime(clockid_t, FAR const struct timespec *);
int clock_gettime(clockid_t, FAR struct timespec *);
int clock_getres(clockid_t, FAR struct timespec *);
int clock_getcpuclockid(pid_t pid, FAR clockid_t *);
int timespec_get(FAR struct timespec *, int);

time_t timegm(FAR struct tm *);
time_t mktime(FAR struct tm *);

FAR struct tm *gmtime(FAR const time_t *);
FAR struct tm *gmtime_r(FAR const time_t *, FAR struct tm *);

FAR struct tm *localtime(FAR const time_t *);
FAR struct tm *localtime_r(FAR const time_t *, FAR struct tm *);

size_t strftime(FAR char *, size_t, FAR const char *,
                FAR const struct tm *) strftime_like(3);

#ifdef CONFIG_ALLOW_MIT_COMPONENTS
FAR char *strptime(FAR const char *, FAR const char *,
                   FAR struct tm *);
#endif

FAR char *asctime(FAR const struct tm *);
FAR char *asctime_r(FAR const struct tm *, FAR char *);
FAR char *ctime(FAR const time_t *);
FAR char *ctime_r(FAR const time_t *, FAR char *);

time_t time(FAR time_t *);

#ifdef CONFIG_HAVE_DOUBLE
double difftime(time_t, time_t);
#elif defined(CONFIG_HAVE_FLOAT)
float difftime(time_t, time_t);
#endif

int timer_create(clockid_t, FAR struct sigevent *,
                 FAR timer_t *);
int timer_delete(timer_t);
int timer_settime(timer_t, int,
                  FAR const struct itimerspec *,
                  FAR struct itimerspec *);
int timer_gettime(timer_t, FAR struct itimerspec *);
int timer_getoverrun(timer_t);

int clock_nanosleep(clockid_t, int,
                    FAR const struct timespec *,
                    FAR struct timespec *);
int nanosleep(FAR const struct timespec *, FAR struct timespec *);

#ifdef CONFIG_LIBC_LOCALTIME
void tzset(void);
#endif

#undef EXTERN
#if defined(__cplusplus)
}
#endif

#endif /* __INCLUDE_TIME_H */
