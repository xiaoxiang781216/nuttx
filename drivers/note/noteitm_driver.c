/****************************************************************************
 * drivers/note/noteitm_driver.c
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

#include <nuttx/note/note_driver.h>
#include <nuttx/coresight/coresight_itm.h>

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct noteitm_s
{
  struct note_driver_s driver;
  FAR struct itm_port_s *stimulus;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static void noteitm_add(FAR struct note_driver_s *drv,
                        FAR const void *buf, size_t notelen,
                        bool noswitches);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct note_driver_ops_s g_noteitm_ops =
{
  noteitm_add,
};

struct noteitm_s g_noteitm =
{
  {
#ifdef CONFIG_SCHED_INSTRUMENTATION_FILTER
    "itm",
    {
      {
        CONFIG_DRIVERS_NOTERTT_FILTER_DEFAULT_MODE,
#  ifdef CONFIG_SMP
        CONFIG_DRIVERS_NOTERTT_CPUSET
#  endif
      },

#  ifdef CONFIG_SCHED_INSTRUMENTATION_DUMP
      {
        CONFIG_DRIVERS_NOTE_DEFAULT_LEVEL,
      },
#  endif
    },
#endif
    &g_noteitm_ops
  }
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: noteitm_add
 *
 * Description:
 *   Put the variable length note to ITM
 *
 * Input Parameters:
 *   buf    - The note buffer
 *   notelen - The buffer length
 *   noswitches - True: Can't do context switches now.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void noteitm_add(FAR struct note_driver_s *drv,
                        FAR const void *buf, size_t notelen,
                        bool noswitches)
{
  FAR struct noteitm_s *note = (FAR struct noteitm_s *)drv;

  itm_send(note->stimulus, buf, notelen);
}

/****************************************************************************
 * Name: noteitm_register
 *
 * Description:
 *   Register a ITM driver using note_driver_register
 *
 * Input Parameters:
 *   None.
 *
 * Returned Value:
 *   Zero on succress. A negated errno value is returned on a failure.
 *
 ****************************************************************************/

int noteitm_register(void)
{
  FAR struct itm_port_s *stimulus = itm_alloc_port();
  if (stimulus == NULL)
    {
      return -ENOSPC;
    }

  g_noteitm.stimulus = stimulus;
  return note_driver_register(&g_noteitm.driver);
}
