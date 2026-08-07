/****************************************************************************
 * sched/clock/clock_getres.c
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

#include <stdint.h>
#include <time.h>
#include <errno.h>
#include <debug.h>

#include <nuttx/clock.h>
#include <nuttx/fs/fs.h>
#include <nuttx/timers/ptp_clock.h>

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: clock_getres
 *
 * Description:
 *   Return the resolution of the specified clock.  Based on POSIX APIs.
 *
 *   The following clock types are supported:
 *
 *     CLOCK_MONOTONIC, CLOCK_BOOTTIME, CLOCK_REALTIME,
 *     CLOCK_PROCESS_CPUTIME_ID, CLOCK_THREAD_CPUTIME_ID
 *       - Report 1 nanosecond resolution (hrtimer-backed).
 *       - Per-process/per-thread variants (clock_id with a non-zero
 *         pid/tid encoded in the upper bits) are rejected with EINVAL.
 *
 *     CLOCK_FD (requires CONFIG_PTP_CLOCK)
 *       - Resolution is queried from the underlying PTP device via
 *         file_ioctl(PTP_CLOCK_GETRES).
 *
 * Input Parameters:
 *   clock_id - Identifies the clock whose resolution is to be returned.
 *              The lower bits (CLOCK_MASK) select the clock type; the
 *              upper bits may encode an fd (CLOCK_FD) or pid/tid.
 *   res      - A pointer to a timespec in which to store the resolution.
 *
 * Returned Value:
 *   OK (0) on success; ERROR (-1) on failure with errno set.
 *
 ****************************************************************************/

int clock_getres(clockid_t clock_id, struct timespec *res)
{
  clockid_t clock_type = clock_id & CLOCK_MASK;
  int       ret        = -EINVAL;

  sinfo("clock_id=%d, clock_type=%d\n", clock_id, clock_type);

  switch (clock_type)
    {
      case CLOCK_MONOTONIC:
      case CLOCK_BOOTTIME:
      case CLOCK_REALTIME:
      case CLOCK_PROCESS_CPUTIME_ID:
      case CLOCK_THREAD_CPUTIME_ID:
        /* Reject clock_ids that encode a pid/tid in the upper bits;
         * only the bare clock type is supported here.
         */

        if (!(clock_id >> CLOCK_SHIFT))
          {
            /* Report 1 nanosecond resolution for hrtimer-backed clocks */

            ret = OK;

            res->tv_sec  = 0;
            res->tv_nsec = 1;

            sinfo("Returning res=(%lld,%ld)\n", (long long)res->tv_sec,
                                                res->tv_nsec);
          }

        break;

#ifdef CONFIG_PTP_CLOCK
      case CLOCK_FD:
        {
          FAR struct file *filep;

          /* Resolve the PTP clock fd and query its resolution */

          ret = ptp_clockid_to_filep(clock_id, &filep);
          if (ret >= 0)
            {
              ret = file_ioctl(filep, PTP_CLOCK_GETRES, res);
              file_put(filep);
            }

          break;
        }
#endif

      default:
        break;
    }

  /* Unified error handling: translate negative errno to ERROR with
   * errno set; otherwise return OK.
   */

  if (ret < 0)
    {
      set_errno(-ret);
      ret = ERROR;
    }

  return ret;
}
