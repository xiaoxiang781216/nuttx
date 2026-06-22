/****************************************************************************
 * libs/libc/misc/lib_pminfo.c
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

#include <nuttx/power/pm.h>

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const char *g_pm_state[PM_COUNT] =
{
  "normal", "idle", "standby", "sleep"
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/

ssize_t pm_wakelock_info(FAR const char *core, int domain,
                         FAR struct pm_wakelock_info_s *buf,
                         size_t maxcount)
{
  size_t count = 0;
  char path[64];
  char line[80];
  FILE *fp;
  int i;

  if (core == NULL)
    {
      snprintf(path, sizeof(path), "/proc/pm/wakelock%d", domain);
    }
  else
    {
      snprintf(path, sizeof(path), "/mnt/%s/pm/wakelock%d", core, domain);
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
      char state[16];
      int ret;

      buf[count].state = PM_NORMAL;
      ret = sscanf(line, "%24s %14s %" SCNu32 " %" SCNuTM "s",
                   buf[count].name, state, &buf[count].count,
                   &buf[count].time);
      if (ret < 4)
        {
          continue;
        }

      for (i = 0; i < PM_COUNT; i++)
        {
          if (strcmp(state, g_pm_state[i]) == 0)
            {
              buf[count].state = i;
              break;
            }
        }

      count++;
    }

  fclose(fp);
  return count;
}

ssize_t pm_state_info(FAR const char *core, int domain,
                      FAR struct pm_state_info_s *buf, size_t maxcount)
{
  size_t count = 0;
  char path[64];
  char line[80];
  FILE *fp;
  int i;

  if (core == NULL)
    {
      snprintf(path, sizeof(path), "/proc/pm/state%d", domain);
    }
  else
    {
      snprintf(path, sizeof(path), "/mnt/%s/pm/state%d", core, domain);
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
      char state[16];
      int ret;

      buf[count].state = PM_NORMAL;
      ret = sscanf(line, "%15s %" SCNuTM "s %" SCNu32 "%% %" SCNuTM
                   "s %" SCNu32 "%% %" SCNuTM "s %" SCNu32 "%%",
                   state, &buf[count].wake, &buf[count].wake_pct,
                   &buf[count].sleep, &buf[count].sleep_pct,
                   &buf[count].total, &buf[count].total_pct);
      if (ret < 7)
        {
          continue;
        }

      for (i = 0; i < PM_COUNT; i++)
        {
          if (strcmp(state, g_pm_state[i]) == 0)
            {
              buf[count].state = i;
              break;
            }
        }

      count++;
    }

  fclose(fp);
  return count;
}
