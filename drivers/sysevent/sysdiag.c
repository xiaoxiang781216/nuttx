/****************************************************************************
 * drivers/sysevent/sysdiag.c
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

#include <debug.h>
#include <time.h>
#include <stdio.h>

#include <nuttx/sysevent/sysdiag.h>

#include "sysevent_dev.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: sysdiag_write
 *
 * Description:
 *   Write a diagnosis event to a general buffer for later analysis and
 *   further recovery or monitor.
 *
 ****************************************************************************/

int sysdiag_write(enum diagnosis_tag_e diag_id, FAR const char *fmt, ...)
{
  va_list ap;
  int buf_size;
  diag_event_t event;
  struct timespec timestamp;

  if (diag_id < DIAGNOSIS_TAG_ALWAYS || diag_id > DIAGNOSIS_TAG_MAX)
    {
      return -EINVAL;
    }

  memset(&event, 0, sizeof(event));
  va_start(ap, fmt);
  buf_size = vsnprintf(event.payload, DIAGNOSIS_EVENT_MAX_LEN, fmt, ap);
  va_end(ap);
  if (buf_size > DIAGNOSIS_EVENT_MAX_LEN)
    {
      return -EINVAL;
    }

  event.header.id = diag_id;
  event.header.type = EVENT_TYPE_KERNEL;
  memcpy(event.header.format, fmt, strlen(fmt));
  strcpy(event.header.core, "local");
  clock_gettime(CLOCK_REALTIME, &timestamp);
  event.header.time = timestamp.tv_sec;
  event.header.pid = getpid();
  event.header.tid = gettid();

  write_sysevent_kfifo((FAR char *)&event, sizeof(event));

  return 0;
}
