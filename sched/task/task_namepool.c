/****************************************************************************
 * sched/task/task_namepool.c
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

#include <string.h>
#include <assert.h>

#include <nuttx/nuttx.h>
#include <nuttx/sched.h>

#include "task/task.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef CONFIG_TASK_NAMEPOOL_SIZE
#  define CONFIG_TASK_NAMEPOOL_SIZE 0
#endif

#if CONFIG_TASK_NAMEPOOL_SIZE > 0

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* Internal structure to store task name info in the circular buffer */

struct taskname_info_s
{
  pid_t pid;
  uint8_t size;
  char name[1];
};

static_assert(
  CONFIG_TASK_NAMEPOOL_SIZE >=
    ALIGN_UP(sizeof(struct taskname_info_s) + CONFIG_TASK_NAME_SIZE - 1,
             sizeof(uintptr_t)) * 3,
  "CONFIG_TASK_NAMEPOOL_SIZE too small."
);

/* Circular buffer structure for task name caching */

struct taskname_buffer_s
{
  size_t head;
  size_t tail;
  char buffer[CONFIG_TASK_NAMEPOOL_SIZE];
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct taskname_buffer_s g_taskname_buffer;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: taskname_find
 *
 * Description:
 *   Find task name info corresponding to the specified PID in the cache.
 *
 * Input Parameters:
 *   pid - Process ID
 *
 * Returned Value:
 *   Pointer to the task name info, or NULL if not found.
 *
 ****************************************************************************/

static FAR struct taskname_info_s *taskname_find(pid_t pid)
{
  FAR struct taskname_info_s *ti;
  int n = g_taskname_buffer.tail;

  while (n != g_taskname_buffer.head)
    {
      ti = (FAR struct taskname_info_s *)&g_taskname_buffer.buffer[n];
      if (ti->pid == pid)
        {
          return ti;
        }

      n += ti->size;
      if (n >= CONFIG_TASK_NAMEPOOL_SIZE)
        {
          n -= CONFIG_TASK_NAMEPOOL_SIZE;
        }
    }

  return NULL;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: nxtask_namepool_record
 *
 * Description:
 *   Record a task name into the cache buffer.
 *
 * Input Parameters:
 *   pid  - Process ID
 *   name - Task name string
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void nxtask_namepool_record(pid_t pid, FAR const char *name)
{
  FAR struct taskname_info_s *ti;
  size_t tilen;
  size_t namelen;
  size_t skiplen;
  size_t remain;

  namelen = strlen(name);
  DEBUGASSERT(namelen <= CONFIG_TASK_NAME_SIZE);
  tilen = sizeof(struct taskname_info_s) + namelen;
  tilen = (tilen + sizeof(uintptr_t) - 1) & ~(sizeof(uintptr_t) - 1);
  DEBUGASSERT(tilen <= UCHAR_MAX);

  skiplen = CONFIG_TASK_NAMEPOOL_SIZE - g_taskname_buffer.head;
  if (skiplen >= tilen + sizeof(struct taskname_info_s))
    {
      skiplen = 0; /* Have enough space at the tail - needn't skip */
    }

  if (g_taskname_buffer.head >= g_taskname_buffer.tail)
    {
      remain = CONFIG_TASK_NAMEPOOL_SIZE -
               (g_taskname_buffer.head - g_taskname_buffer.tail);
    }
  else
    {
      remain = g_taskname_buffer.tail - g_taskname_buffer.head;
    }

  while (skiplen + tilen >= remain)
    {
      /* No enough space, drop the old info */

      ti = (FAR struct taskname_info_s *)
            &g_taskname_buffer.buffer[g_taskname_buffer.tail];
      g_taskname_buffer.tail = (g_taskname_buffer.tail + ti->size) %
                               CONFIG_TASK_NAMEPOOL_SIZE;
      remain += ti->size;
    }

  if (skiplen)
    {
      /* Fill the skipped region with an invalid info */

      ti = (FAR struct taskname_info_s *)
            &g_taskname_buffer.buffer[g_taskname_buffer.head];
      ti->size = skiplen;
      ti->pid = INVALID_PROCESS_ID;
      ti->name[0] = '\0';

      /* Move to the begin of circle buffer */

      g_taskname_buffer.head = 0;
    }

  ti = (FAR struct taskname_info_s *)
        &g_taskname_buffer.buffer[g_taskname_buffer.head];
  ti->size = tilen;
  ti->pid = pid;
  strlcpy(ti->name, name, namelen + 1);
  g_taskname_buffer.head += ti->size;
}

/****************************************************************************
 * Name: nxtask_namepool_get
 *
 * Description:
 *   Get the task name for a given PID.
 *
 * Input Parameters:
 *   pid - Process ID
 *   buf - Buffer to hold the task name
 *   len - Length of the buffer
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void nxtask_namepool_get(pid_t pid, FAR char *buf, size_t len)
{
  FAR struct tcb_s *tcb = nxsched_get_tcb(pid);

  if (tcb != NULL)
    {
      strlcpy(buf, tcb->name, len);
      nxsched_put_tcb(tcb);
    }
  else
    {
      FAR struct taskname_info_s *ti = taskname_find(pid);
      strlcpy(buf, ti != NULL ? ti->name : "<noname>", len);
    }
}

/****************************************************************************
 * Name: nxtask_namepool_foreach
 *
 * Description:
 *   Iterate over all cached task names.
 *
 * Input Parameters:
 *   handler - Callback function to invoke for each task name
 *   arg     - User argument passed to the handler
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void nxtask_namepool_foreach(nxtask_namepool_handler_t handler,
                              FAR void *arg)
{
  FAR struct taskname_info_s *ti;
  int n = g_taskname_buffer.tail;

  while (n != g_taskname_buffer.head)
    {
      ti = (FAR struct taskname_info_s *)&g_taskname_buffer.buffer[n];

      if (ti->pid != INVALID_PROCESS_ID)
        {
          handler(ti->pid, ti->name, arg);
        }

      n += ti->size;
      if (n >= CONFIG_TASK_NAMEPOOL_SIZE)
        {
          n -= CONFIG_TASK_NAMEPOOL_SIZE;
        }
    }
}

#else

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct nxsched_foreach_callback_s
{
  nxtask_namepool_handler_t handler;
  FAR void *arg;
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void nxsched_foreach_callback(FAR struct tcb_s *tcb, FAR void *arg)
{
  struct nxsched_foreach_callback_s *ctx = arg;
  nxtask_namepool_handler_t handler = ctx->handler;
  handler(tcb->pid, get_task_name(tcb), ctx->arg);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void nxtask_namepool_record(pid_t pid, FAR const char *name)
{
  /* No operation */
}

void nxtask_namepool_get(pid_t pid, FAR char *buf, size_t len)
{
  struct tcb_s *tcb = nxsched_get_tcb(pid);
  strlcpy(buf, tcb ? get_task_name(tcb) : "<noname>", len);
  nxsched_put_tcb(tcb);
}

void nxtask_namepool_foreach(nxtask_namepool_handler_t handler,
                             FAR void *arg)
{
  struct nxsched_foreach_callback_s cb =
    {
      .handler = handler,
      .arg = arg,
    };

  nxsched_foreach(nxsched_foreach_callback, &cb);
}

#endif
