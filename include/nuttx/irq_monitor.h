/****************************************************************************
 * include/nuttx/irq_monitor.h
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

#ifndef __INCLUDE_NUTTX_IRQ_MONITOR_H
#define __INCLUDE_NUTTX_IRQ_MONITOR_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>
#include <stdbool.h>
#include <stdint.h>

#include <nuttx/irq.h>
#ifdef CONFIG_PM
#  include <nuttx/power/pm.h>
#endif

/****************************************************************************
 * Public Types
 ****************************************************************************/

#ifndef __ASSEMBLY__
#ifdef CONFIG_SCHED_IRQMONITOR_PROCFS
/* User-space view of per-IRQ statistics, parsed from /proc/irqs by the
 * irq_monitor_info() helper in libc.
 */

struct irq_monitor_info_s
{
  int           irq;
  unsigned long handler;
  unsigned long arg;
  uint32_t      count;
  uint32_t      rate_int;
  uint32_t      rate_frac;
  uint32_t      time;
#ifdef CONFIG_PM
  uint32_t      nwakeup[PM_COUNT]; /* Wakeups indexed by PM state */
#endif
};
#endif
#endif /* __ASSEMBLY__ */

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifndef __ASSEMBLY__
#ifdef __cplusplus
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

/****************************************************************************
 * Name: irq_monitor_enable / irq_monitor_disable
 *
 * Description:
 *   Enable or disable interrupt frequency monitoring for the specified IRQ.
 *   When enabled, the system will track the interrupt count and timing
 *   statistics which can be queried via /proc/irqs or
 *   irq_monitor_get_freq().
 *
 * Input Parameters:
 *   irq - The IRQ number to enable/disable monitoring for.
 *
 ****************************************************************************/

#ifdef CONFIG_SCHED_IRQMONITOR
void irq_monitor_enable(int irq);
void irq_monitor_disable(int irq);

/****************************************************************************
 * Name: irq_monitor_enable_all / irq_monitor_disable_all
 *
 * Description:
 *   Global control of IRQ statistics collection for all IRQs.
 *   irq_monitor_enable_all() enables statistics collection for all IRQs.
 *   irq_monitor_disable_all() disables statistics collection for all IRQs.
 *
 ****************************************************************************/

void irq_monitor_enable_all(void);
void irq_monitor_disable_all(void);

/****************************************************************************
 * Name: irq_monitor_get_freq
 *
 * Description:
 *   Get the interrupt frequency (interrupts per second) for the specified
 *   IRQ. Optionally reset the counter after reading.
 *
 * Input Parameters:
 *   irq      - The IRQ number to query.
 *   reset    - If true, reset the interrupt counter after reading.
 *   freq     - Pointer to store the integer part of frequency.
 *   fracfreq - Pointer to store the fractional part (*1000) of frequency.
 *              Can be NULL if not needed.
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

int irq_monitor_get_freq(int irq, bool reset, FAR unsigned long *freq,
                         FAR unsigned long *fracfreq);

#ifdef CONFIG_PM
/****************************************************************************
 * Name: irq_monitor_get_wakeup_latency
 *
 * Description:
 *   Get the wakeup latency attributed to a specific IRQ (WFI return to that
 *   IRQ's dispatch), for the wakeup where this IRQ woke the system.  Reports
 *   the most recent value ('lat') and the per-PM-state running maximum
 *   ('lat_max').  This is a pure read.
 *
 * Input Parameters:
 *   irq     - The IRQ number to query.
 *   lat     - Location to store the most-recent wakeup latency, or NULL.
 *   lat_max - Caller array of PM_COUNT entries for the maximum wakeup
 *             latency of each PM state, or NULL.
 *
 * Returned Value:
 *   Zero on success; a negated errno value on failure.
 *
 ****************************************************************************/

int irq_monitor_get_wakeup_latency(int irq,
                                   FAR clock_t *lat,
                                   FAR clock_t *lat_max);
#endif /* CONFIG_PM */

/****************************************************************************
 * Name: irq_monitor_info
 *
 * Description:
 *   Read IRQ statistics from /proc/irqs into a caller-provided array.
 *
 * Input Parameters:
 *   core     - Remote core name to query via rpmsgfs, or NULL for local.
 *   buf      - Caller-provided output array.
 *   maxcount - Maximum number of entries buf can hold.
 *
 * Returned Value:
 *   The number of entries filled, or a negated errno on failure.
 *
 ****************************************************************************/

ssize_t irq_monitor_info(FAR const char *core,
                         FAR struct irq_monitor_info_s *buf,
                         size_t maxcount);
#endif

#undef EXTERN
#ifdef __cplusplus
}
#endif

#endif /* __ASSEMBLY__ */
#endif /* __INCLUDE_NUTTX_IRQ_MONITOR_H */
