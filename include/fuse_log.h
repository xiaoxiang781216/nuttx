/****************************************************************************
 * include/fuse_log.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to you under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
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

#ifndef __INCLUDE_FUSE_LOG_H
#define __INCLUDE_FUSE_LOG_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <debug.h>
#include <stdarg.h>
#include <syslog.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define fuse_log(level, fmt, ...)           \
  do                                        \
    {                                       \
      if ((level) <= FUSE_LOG_ERR)          \
        {                                   \
          ferr(fmt, ##__VA_ARGS__);         \
        }                                   \
      else if ((level) == FUSE_LOG_WARNING) \
        {                                   \
          fwarn(fmt, ##__VA_ARGS__);        \
        }                                   \
      else                                  \
        {                                   \
          finfo(fmt, ##__VA_ARGS__);        \
        }                                   \
    }                                       \
  while (0)

/****************************************************************************
 * Public Type Definitions
 ****************************************************************************/

enum fuse_log_level
{
  FUSE_LOG_EMERG   = LOG_EMERG  ,
  FUSE_LOG_ALERT   = LOG_ALERT  ,
  FUSE_LOG_CRIT    = LOG_CRIT   ,
  FUSE_LOG_ERR     = LOG_ERR    ,
  FUSE_LOG_WARNING = LOG_WARNING,
  FUSE_LOG_NOTICE  = LOG_NOTICE ,
  FUSE_LOG_INFO    = LOG_INFO   ,
  FUSE_LOG_DEBUG   = LOG_DEBUG  ,
};

typedef CODE void (*fuse_log_func_t)(enum fuse_log_level level,
                                     FAR const char *fmt, va_list ap);

/****************************************************************************
 * Inline Functions
 ****************************************************************************/

static inline void fuse_set_log_func(fuse_log_func_t func)
{
}

static inline void fuse_log_enable_syslog(FAR const char *ident,
                                          int option, int facility)
{
  openlog(ident, option, facility);
}

static inline void fuse_log_close_syslog(void)
{
  closelog();
}

#endif /* __INCLUDE_FUSE_LOG_H */
