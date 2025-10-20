/****************************************************************************
 * drivers/hwtracing/hwtracing_note.c
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

#include <nuttx/hwtracing/hwtracing_note.h>
#include <nuttx/note/note_driver.h>
#include <nuttx/spinlock.h>
#include <nuttx/kmalloc.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct hwtracing_note_s
{
  struct note_driver_s driver;
  spinlock_t lock;
  FAR uintptr_t *note_addr;
  hwtracing_note_enabled enabled;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static void hwtracing_note_add(FAR struct note_driver_s *drv,
                               FAR const void *note, size_t len);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct note_driver_ops_s g_hwtracing_note_ops =
{
  hwtracing_note_add,
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: trigger_data
 ****************************************************************************/

static inline void hwtracing_note_trigger(FAR uintptr_t *note_addr,
                                          FAR const void *data, size_t len)
{
  size_t words  = len / sizeof(uintptr_t);
  size_t remain = len % sizeof(uintptr_t);
  FAR const uintptr_t *buf = data;

  while (words--)
    {
      *note_addr = *buf++;
    }

  if (remain)
    {
      FAR const uint8_t *src8 = (const uint8_t *)buf;
      FAR uint8_t *dst8 = (uint8_t *)note_addr;

      switch (remain)
        {
          case 1:
            dst8[0] = src8[0];
            break;
          case 2:
            dst8[0] = src8[0];
            dst8[1] = src8[1];
            break;
          case 3:
            dst8[0] = src8[0];
            dst8[1] = src8[1];
            dst8[2] = src8[2];
            break;
          default:
            break;
        }
    }
}

/****************************************************************************
 * Name: hwtracing_note_add
 *
 * Description:
 *   Put the variable length note to dtu
 *
 * Input Parameters:
 *   buf    - The note buffer
 *   notelen - The buffer length
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void hwtracing_note_add(FAR struct note_driver_s *drv,
                               FAR const void *buf, size_t notelen)
{
  FAR struct hwtracing_note_s *note = (FAR struct hwtracing_note_s *)drv;
  irqstate_t flags = spin_lock_irqsave(&note->lock);

  if (!note->enabled || note->enabled())
    {
      hwtracing_note_trigger(note->note_addr, buf, notelen);
    }

  spin_unlock_irqrestore(&note->lock, flags);
}

/****************************************************************************
 * Name: hwtracing_note_register
 *
 * Description:
 *   Register a trace32 fdx driver using note_driver_register
 *
 * Input Parameters:
 *   None.
 *
 * Returned Value:
 *   Zero on succress. A negated errno value is returned on a failure.
 *
 ****************************************************************************/

int hwtracing_note_register(FAR uintptr_t *note_addr,
                            hwtracing_note_enabled enabled)
{
  FAR struct hwtracing_note_s *drv;
  int ret;

  drv = kmm_zalloc(sizeof(*drv));
  if (drv == NULL)
    {
      return -1;
    }

#ifdef CONFIG_SCHED_INSTRUMENTATION_FILTER
  drv->driver.name = "hwtracing";
  drv->driver.filter.mode.flag =
                      CONFIG_HWTRACING_NOTE_FILTER_DEFAULT_MODE;

#  ifdef CONFIG_SMP
  drv->driver.filter.mode.cpuset =
                      CONFIG_HWTRACING_NOTE_CPUSET;
#  endif
#endif
  drv->driver.ops = &g_hwtracing_note_ops;

  ret = note_driver_register(&drv->driver);
  if (ret < 0)
    {
      kmm_free(drv);
      return ret;
    }

  spin_lock_init(&drv->lock);
  drv->note_addr = note_addr;
  drv->enabled   = enabled;

  return 0;
}
