/****************************************************************************
 * libs/libc/syslog/lib_syslog.c
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

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/compiler.h>

#include <stdarg.h>
#include <syslog.h>

#include <nuttx/tls_task.h>
#include <nuttx/ratelimit.h>
#include <nuttx/syslog/syslog.h>

/****************************************************************************
 * Private Data
 ****************************************************************************/

static DEFINE_RATELIMIT_STATE(g_ratelimit,
  CONFIG_SYSLOG_RATELIMIT_INTERVAL,
  CONFIG_SYSLOG_RATELIMIT_BURST);

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: nx_syslog
 *
 * Description:
 *   nx_syslog() handles the system logging system calls. It is functionally
 *   equivalent to syslog() but is used within the kernel space.
 *
 ****************************************************************************/

void nx_syslog(uint64_t meta, FAR const IPTR char *fmt, ...)
{
  if (syslog_is_enabled(meta))
    {
      va_list ap;

      va_start(ap, fmt);
      nx_vsyslog(meta, fmt, &ap);
      va_end(ap);
    }
}

/****************************************************************************
 * Name: vsyslog
 *
 * Description:
 *   The function vsyslog() performs the same task as syslog() with the
 *   difference that it takes a set of arguments which have been obtained
 *   using the stdarg variable argument list macros.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

void vsyslog(int priority, FAR const IPTR char *fmt, va_list ap)
{
  if (syslog_is_enabled(priority))
    {
#ifdef va_copy
      va_list copy;

      va_copy(copy, ap);
      nx_vsyslog(priority, fmt, &copy);
      va_end(copy);
#else
      nx_vsyslog(priority, fmt, &ap);
#endif
    }
}

/****************************************************************************
 * Name: syslog
 *
 * Description:
 *   syslog() generates a log message. The priority argument is formed by
 *   ORing the facility and the level values (see include/syslog.h). The
 *   remaining arguments are a format, as in printf and any arguments to the
 *   format.
 *
 *   The NuttX implementation does not support any special formatting
 *   characters beyond those supported by printf.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

#undef syslog
void syslog(int priority, FAR const IPTR char *fmt, ...)
{
  va_list ap;

  /* Let vsyslog do the work */

  va_start(ap, fmt);
  vsyslog(priority, fmt, ap);
  va_end(ap);
}

/****************************************************************************
 * Name: syslog_ratelimit
 *
 * Description:
 *   The rate-limiting version of syslog. Rate limiting will
 *   be performed using the global rate limiting state.
 *
 ****************************************************************************/

void syslog_ratelimit(int priority,
                      FAR const IPTR char *fmt, ...)
{
  if (!ratelimit_islimited(&g_ratelimit))
    {
      va_list ap;

      va_start(ap, fmt);
      vsyslog(priority, fmt, ap);
      va_end(ap);
    }
}
