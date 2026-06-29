/****************************************************************************
 * sched/perf/perf.h
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

#ifndef __SCHED_PERF_PERF_H
#define __SCHED_PERF_PERF_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <sys/types.h>

#include <nuttx/circbuf.h>
#include <nuttx/perf.h>
#include <nuttx/spinlock.h>

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Per-event ring buffer backing the mmap'd sample stream.  Only the perf
 * core (perf.c) and the sample output path (perf_sample.c) need the layout;
 * perf_event_s holds it by pointer, so it is private to sched/perf.
 */

struct perf_buffer_s
{
  struct circbuf_s rb;
  uint32_t ref_count;
  spinlock_t lock;
};

/* Sample data scratch record assembled before output.  Element layout must
 * match Linux perf, aligned with uint64_t.  Used only within sched/perf.
 */

struct perf_sample_data_s
{
  uint64_t sample_flags;
  uint64_t period;
  uint64_t dyn_size;

  struct
  {
    pid_t pid;
    pid_t tid;
  } tid_entry;

  uint64_t time;
  uint64_t id;

  struct
  {
    uint32_t cpu;
    uint32_t reserved;
  } cpu_entry;

  uint64_t ip;
  uint64_t stream_id;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/* Sampling helpers implemented in perf.c (the core framework) and shared
 * with the PMU implementations in perf_pmu.c.
 */

int perf_get_event_count(FAR void *arg);

/* Sample preparation and output helpers implemented in perf_sample.c
 * and shared with the core framework in perf.c.
 */

void perf_sample_data_init(FAR struct perf_sample_data_s *data,
                           uint64_t period);
int perf_event_data_overflow(FAR struct perf_event_s *event,
                             FAR struct perf_sample_data_s *data,
                             uintptr_t ip);
uint64_t perf_get_eventid(FAR struct perf_event_s *event);
int perf_buffer_set_output(FAR struct perf_event_s *event,
                           FAR struct perf_event_s *output);
void perf_buffer_release(FAR struct perf_event_s *event);

/* Built-in PMU registration implemented in perf_pmu.c and called once from
 * perf_event_init() in the core framework.
 */

void perf_pmu_builtin_register(void);

/* Software event entry implemented in perf_pmu.c and called from the core
 * scheduler hooks (e.g. context switch) to record software samples.
 */

void perf_swevent(uint32_t event_id, uintptr_t ip);

#endif /* __SCHED_PERF_PERF_H */
