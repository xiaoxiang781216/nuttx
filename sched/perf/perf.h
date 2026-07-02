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

#include <nuttx/atomic.h>
#include <nuttx/circbuf.h>
#include <nuttx/perf.h>
#include <nuttx/sched.h>
#include <nuttx/spinlock.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Cumulative count of an event including its detached children.  Shared
 * across the sched/perf layers that read counters back (perf_event.c,
 * perf_fops.c, perf_swevent.c, perf_resident.c).
 */

#define PERF_GET_COUNT(event) \
        (atomic64_read(&(event)->count) + atomic64_read(&(event)->child_count))

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

/* Per-task (or per-CPU) event container.  Only the perf core and its split
 * layers (perf.c, perf_context.c, perf_fops.c, perf_resident.c) touch its
 * fields; perf_event_s and pmu_event_context_s hold it by pointer and the
 * tcb holds it by pointer, so it is private to sched/perf.
 */

struct perf_event_context_s
{
  struct list_node event_list;   /* Used for list perf event */
  struct list_node group_list;   /* Used for list group leader event */
  struct list_node pmu_ctx_list; /* Used for list PMU context */
  spinlock_t lock;               /* Lock for pref event context */
  FAR struct tcb_s *tcb;         /* The tcb context belongs to */
  uint32_t active_num;           /* Active event in this context */
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/* Sample preparation and output helpers implemented in perf_sample.c
 * and shared with the core framework in perf.c.  perf_get_event_count()
 * reads the current counter(s) from the lower-half PMU and is called from
 * the file-operation layer and the software-event timer path.
 */

int perf_get_event_count(FAR void *arg);
void perf_sample_data_init(FAR struct perf_sample_data_s *data,
                           uint64_t period);
int perf_event_data_overflow(FAR struct perf_event_s *event,
                             FAR struct perf_sample_data_s *data,
                             uintptr_t ip);
uint64_t perf_get_eventid(FAR struct perf_event_s *event);
int perf_buffer_set_output(FAR struct perf_event_s *event,
                           FAR struct perf_event_s *output);
void perf_buffer_release(FAR struct perf_event_s *event);

/* Built-in PMU registration implemented in perf_swevent.c and called once
 * from perf_event_init() in the core framework.
 */

void perf_pmu_builtin_register(void);

/* Software event entry implemented in perf_swevent.c and called from the
 * core scheduler hooks (e.g. context switch) to record software samples.
 */

void perf_swevent(uint32_t event_id, uintptr_t ip);

/* Core framework helpers implemented in perf.c and shared with the
 * file-operation layer in perf_fops.c.
 */

int perf_function_call(FAR struct perf_event_s *event,
                       nxsched_cpu_call_t func);
int perf_event_enable(FAR void *arg);
int perf_event_disable(FAR void *arg);
int perf_event_reset(FAR void *arg);
void perf_context_detach(FAR struct perf_event_s *event,
                         FAR struct perf_event_context_s *ctx);
void perf_free_event(FAR struct perf_event_s *event);

/* Task and scheduler integration entry points implemented in perf.c and
 * called from the sched/ core: perf_event_init() once at startup,
 * perf_event_task_init()/perf_event_task_exit() at task creation/teardown,
 * and perf_event_task_sched_in()/perf_event_task_sched_out() on every
 * context switch.
 */

int perf_event_init(void);
int perf_event_task_init(FAR struct tcb_s *tcb);
void perf_event_task_exit(FAR struct tcb_s *tcb);
void perf_event_task_sched_in(FAR struct tcb_s *tcb);
void perf_event_task_sched_out(FAR struct tcb_s *tcb);

/* File-operation layer implemented in perf_fops.c.  perf_install_fd()
 * allocates a file descriptor bound to the perf event and is called from
 * perf_event_open() in the core framework.
 */

int perf_install_fd(FAR struct perf_event_s *event, unsigned long flags);

/* PMU registry implemented in perf_pmu.c.  perf_get_pmu() selects and
 * initializes the PMU for an event and is called from the core framework.
 */

FAR struct pmu_s *perf_get_pmu(FAR struct perf_event_s *event);

/* Event/PMU context lifecycle implemented in perf_context.c and shared with
 * the core framework and the PMU registry.  perf_init_cpu_contexts() sets up
 * the per-CPU system-wide contexts once from perf_event_init().
 */

void perf_init_cpu_contexts(void);
void perf_init_context(FAR struct perf_event_context_s *ctx);
FAR struct perf_event_context_s *
perf_get_context(FAR struct perf_event_s *event, FAR struct tcb_s *tcb);
void perf_free_context(FAR struct perf_event_context_s *ctx);
void perf_init_pmu_context(FAR struct pmu_event_context_s *pmu_ctx,
                           FAR struct pmu_s *pmu);
FAR struct pmu_event_context_s *
perf_get_pmu_context(FAR struct pmu_s *pmu, FAR struct perf_event_s *event,
                     FAR struct perf_event_context_s *ctx);
void perf_free_pmu_context(FAR struct pmu_event_context_s *pmu_ctx);

/* Event object lifecycle implemented in perf_event.c: allocation, creation,
 * fork-time inheritance and group/child teardown.  Shared with the core
 * framework (perf.c) which orchestrates open/enable/disable/sched.
 */

FAR struct perf_event_s *
perf_event_create(FAR struct perf_event_attr_s *attr, int cpu,
                  FAR struct tcb_s *tcb,
                  FAR struct perf_event_s *group_leader, FAR int *err);
void perf_add_event_to_count(FAR struct perf_event_s *event,
                             FAR struct perf_event_context_s *ctx);
int perf_inherit_group_events(FAR struct perf_event_s *group_leader,
                              FAR struct tcb_s *parent,
                              FAR struct tcb_s *child);
void perf_group_detach(FAR struct perf_event_s *event);
void perf_child_detach(FAR struct perf_event_s *event);

#endif /* __SCHED_PERF_PERF_H */
