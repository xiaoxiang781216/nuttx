/****************************************************************************
 * libs/libc/misc/lib_irqinfo.c
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

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>

#include <nuttx/irq_monitor.h>
#include <nuttx/power/pm.h>

/****************************************************************************
 * Public Functions
 ****************************************************************************/

ssize_t irq_monitor_info(FAR const char *core,
                         FAR struct irq_monitor_info_s *buf,
                         size_t maxcount)
{
  size_t count = 0;
  char path[64];
  char line[80];
  FILE *fp;

  if (core == NULL)
    {
      strlcpy(path, "/proc/irqs", sizeof(path));
    }
  else
    {
      snprintf(path, sizeof(path), "/mnt/%s/irqs", core);
    }

  fp = fopen(path, "r");
  if (fp == NULL)
    {
      return -get_errno();
    }

  if (fgets(line, sizeof(line), fp) == NULL)
    {
      fclose(fp);
      return 0;
    }

  while (fgets(line, sizeof(line), fp) != NULL && count < maxcount)
    {
      int ret;

      ret = sscanf(line,
                   "%d %lx %lx %" SCNu32
                   " %" SCNu32 ".%" SCNu32 " %" SCNu32
#ifdef CONFIG_PM
                   " %" SCNu32 " %" SCNu32 " %" SCNu32
#endif
                   ,
                   &buf[count].irq, &buf[count].handler, &buf[count].arg,
                   &buf[count].count, &buf[count].rate_int,
                   &buf[count].rate_frac, &buf[count].time
#ifdef CONFIG_PM
                   , &buf[count].nwakeup[PM_IDLE],
                   &buf[count].nwakeup[PM_STANDBY],
                   &buf[count].nwakeup[PM_SLEEP]
#endif
                   );
      if (ret >= 7)
        {
          count++;
        }
    }

  fclose(fp);
  return count;
}
