/****************************************************************************
 * arch/arm/src/arm_a_r/arm_pmuv2.c
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

#include <nuttx/bits.h>
#include <nuttx/compiler.h>
#include <nuttx/kmalloc.h>
#include <nuttx/nuttx.h>
#include <nuttx/perf.h>
#include <nuttx/sched.h>

#include <arch/barriers.h>
#include <arch/irq.h>

#include <debug.h>

#include "sctlr.h"
#include "arm_pmuv2.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define HW_OP_UNSUPPORTED          0xFFFF
#define CACHE_OP_UNSUPPORTED       0xFFFF
#define C(_x)                      CONCATENATE(PERF_COUNT_HW_CACHE_, _x)

#define PERF_MAP_ALL_UNSUPPORTED                              \
        [0 ... PERF_COUNT_HW_MAX - 1] = HW_OP_UNSUPPORTED

#define PERF_CACHE_MAP_ALL_UNSUPPORTED                        \
        [0 ... C(MAX) - 1] = {                                \
          [0 ... C(OP_MAX) - 1] = {                           \
            [0 ... C(RESULT_MAX) - 1] = CACHE_OP_UNSUPPORTED, \
          },                                                  \
        }

#define TO_ARMV7_PMU(p) (container_of(p, struct armv7_pmu_s, pmu))

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct pmu_hw_events_s
{
  struct perf_event_s *events[ARMV7PMU_MAX_HWEVENTS];
  unsigned long used_mask;
  int irq;
};

struct armv7_pmu_s
{
  struct pmu_s pmu;
  unsigned long supported_cpus;
  FAR const char *name;

  int (*handle_irq)(FAR struct armv7_pmu_s *pmu);
  void (*enable)(FAR struct perf_event_s *event);
  void (*disable)(FAR struct perf_event_s *event);
  int (*get_event_idx)(FAR struct pmu_hw_events_s *hw_events,
                       FAR struct perf_event_s *event);
  void (*clear_event_idx)(FAR struct pmu_hw_events_s *hw_events,
                          FAR struct perf_event_s *event);
  int (*set_event_filter)(FAR struct hw_perf_event_s *evt,
                          FAR struct perf_event_attr_s *attr);
  uint64_t (*read_counter)(FAR struct perf_event_s *event);
  void (*write_counter)(FAR struct perf_event_s *event, uint64_t val);
  void (*start)(FAR struct armv7_pmu_s *);
  void (*stop)(FAR struct armv7_pmu_s *);
  void (*reset)(FAR void *);
  int (*map_event)(FAR struct perf_event_s *event);

  int num_events;
  struct pmu_hw_events_s hw_events[CONFIG_SMP_NCPUS];
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static void armv7pmu_enable(FAR struct pmu_s *pmu);
static void armv7pmu_disable(FAR struct pmu_s *pmu);
static int armv7pmu_event_init(FAR struct perf_event_s *event);
static int armv7pmu_add(FAR struct perf_event_s *event, int flags);
static void armv7pmu_del(FAR struct perf_event_s *event, int flags);
static int armv7pmu_start(FAR struct perf_event_s *event, int flags);
static int armv7pmu_stop(FAR struct perf_event_s *event, int flags);
static int armv7pmu_read(FAR struct perf_event_s *event);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct pmu_ops_s g_armv7pmu_ops =
{
  .pmu_enable  = armv7pmu_enable,
  .pmu_disable = armv7pmu_disable,
  .event_init  = armv7pmu_event_init,
  .event_add   = armv7pmu_add,
  .event_del   = armv7pmu_del,
  .event_start = armv7pmu_start,
  .event_stop  = armv7pmu_stop,
  .event_read  = armv7pmu_read,
};

static const unsigned int g_armv7_perf_map[PERF_COUNT_HW_MAX] =
{
  PERF_MAP_ALL_UNSUPPORTED,
  [PERF_COUNT_HW_CPU_CYCLES]       = ARMV7_PERFCTR_CPU_CYCLES,
  [PERF_COUNT_HW_INSTRUCTIONS]     = ARMV7_PERFCTR_INSTR_EXECUTED,
  [PERF_COUNT_HW_CACHE_REFERENCES] = ARMV7_PERFCTR_L1_DCACHE_ACCESS,
  [PERF_COUNT_HW_CACHE_MISSES]     = ARMV7_PERFCTR_L1_DCACHE_REFILL,
  [PERF_COUNT_HW_BRANCH_INSTRUCTIONS]
                                   = ARMV7_PERFCTR_PC_WRITE,
  [PERF_COUNT_HW_BRANCH_MISSES]    = ARMV7_PERFCTR_PC_BRANCH_MIS_PRED,
  [PERF_COUNT_HW_BUS_CYCLES]       = ARMV7_PERFCTR_BUS_CYCLES,
};

static const unsigned int
g_armv7_perf_cache_map[PERF_COUNT_HW_CACHE_MAX]
                      [PERF_COUNT_HW_CACHE_OP_MAX]
                      [PERF_COUNT_HW_CACHE_RESULT_MAX] =
{
  PERF_CACHE_MAP_ALL_UNSUPPORTED,

  [C(L1D)][C(OP_READ)][C(RESULT_ACCESS)]
                      = ARMV7_PERFCTR_L1_DCACHE_ACCESS,
  [C(L1D)][C(OP_READ)][C(RESULT_MISS)]
                      = ARMV7_PERFCTR_L1_DCACHE_REFILL,
  [C(L1D)][C(OP_WRITE)][C(RESULT_ACCESS)]
                      = ARMV7_PERFCTR_L1_DCACHE_ACCESS,
  [C(L1D)][C(OP_WRITE)][C(RESULT_MISS)]
                      = ARMV7_PERFCTR_L1_DCACHE_REFILL,

  [C(L1I)][C(OP_READ)][C(RESULT_MISS)]
                      = ARMV7_PERFCTR_L1_ICACHE_REFILL,

  [C(DTLB)][C(OP_READ)][C(RESULT_MISS)]
                      = ARMV7_PERFCTR_DTLB_REFILL,
  [C(DTLB)][C(OP_WRITE)][C(RESULT_MISS)]
                      = ARMV7_PERFCTR_DTLB_REFILL,

  [C(ITLB)][C(OP_READ)][C(RESULT_MISS)]
                      = ARMV7_PERFCTR_ITLB_REFILL,
  [C(ITLB)][C(OP_WRITE)][C(RESULT_MISS)]
                      = ARMV7_PERFCTR_ITLB_REFILL,

  [C(BPU)][C(OP_READ)][C(RESULT_ACCESS)]
                      = ARMV7_PERFCTR_PC_BRANCH_PRED,
  [C(BPU)][C(OP_READ)][C(RESULT_MISS)]
                      = ARMV7_PERFCTR_PC_BRANCH_MIS_PRED,
  [C(BPU)][C(OP_WRITE)][C(RESULT_ACCESS)]
                      = ARMV7_PERFCTR_PC_BRANCH_PRED,
  [C(BPU)][C(OP_WRITE)][C(RESULT_MISS)]
                      = ARMV7_PERFCTR_PC_BRANCH_MIS_PRED,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static inline uint32_t armv7pmu_counter_bit(int idx)
{
  return idx == ARMV7_IDX_CYCLE_COUNTER ?
         BIT(31) : BIT(ARMV7_IDX_TO_COUNTER(idx));
}

static inline uint64_t
armv7pmu_event_max_period(FAR struct perf_event_s *event)
{
  return UINT32_MAX;
}

static int armv7pmu_map_cache_event(
                    FAR const unsigned int (*cache_map)
                    [PERF_COUNT_HW_CACHE_MAX]
                    [PERF_COUNT_HW_CACHE_OP_MAX]
                    [PERF_COUNT_HW_CACHE_RESULT_MAX],
                    uint64_t config)
{
  unsigned int cache_type;
  unsigned int cache_op;
  unsigned int cache_result;
  unsigned int mapping;

  cache_type = (config >> 0) & 0xff;
  if (cache_type >= PERF_COUNT_HW_CACHE_MAX)
    {
      return -EINVAL;
    }

  cache_op = (config >> 8) & 0xff;
  if (cache_op >= PERF_COUNT_HW_CACHE_OP_MAX)
    {
      return -EINVAL;
    }

  cache_result = (config >> 16) & 0xff;
  if (cache_result >= PERF_COUNT_HW_CACHE_RESULT_MAX)
    {
      return -EINVAL;
    }

  if (!cache_map)
    {
      return -ENOENT;
    }

  mapping = (*cache_map)[cache_type][cache_op][cache_result];
  if (mapping == CACHE_OP_UNSUPPORTED)
    {
      return -ENOENT;
    }

  return mapping;
}

static int armv7pmu_map_hw_event(
                    FAR const unsigned int (*event_map)[PERF_COUNT_HW_MAX],
                    uint64_t config)
{
  int mapping;

  if (config >= PERF_COUNT_HW_MAX)
    {
      return -EINVAL;
    }

  if (!event_map)
    {
      return -ENOENT;
    }

  mapping = (*event_map)[config];
  if (mapping == HW_OP_UNSUPPORTED)
    {
      return -ENOENT;
    }

  return mapping;
}

static int armv7pmu_map_raw_event(uint32_t raw_event_mask, uint64_t config)
{
  return (int)(config & raw_event_mask);
}

static int armv7pmu_map_event(FAR struct perf_event_s *event,
                              FAR const unsigned int (*event_map)
                              [PERF_COUNT_HW_MAX],
                              FAR const unsigned int (*cache_map)
                              [PERF_COUNT_HW_CACHE_MAX]
                              [PERF_COUNT_HW_CACHE_OP_MAX]
                              [PERF_COUNT_HW_CACHE_RESULT_MAX],
                              uint32_t raw_event_mask)
{
  uint64_t config = event->attr.config;
  int type = event->attr.type;

  switch (type)
    {
      case PERF_TYPE_HARDWARE:
        return armv7pmu_map_hw_event(event_map, config);
      case PERF_TYPE_HW_CACHE:
        return armv7pmu_map_cache_event(cache_map, config);
      case PERF_TYPE_RAW:
        return armv7pmu_map_raw_event(raw_event_mask, config);
      default:
        return -ENOENT;
    }
}

static void armv7pmu_event_update_period(FAR struct perf_event_s *event)
{
  FAR struct armv7_pmu_s *armpmu = TO_ARMV7_PMU(event->pmu);
  FAR struct hw_perf_event_s *hwc = &event->hw;
  int64_t left = atomic64_read(&hwc->left_period);
  int64_t period = event->attr.sample_period;
  uint64_t max_period = armv7pmu_event_max_period(event);

  if (predict_false(left <= -period))
    {
      left = period;
      atomic64_set(&hwc->left_period, left);
      atomic64_set(&hwc->last_period, period);
    }

  if (predict_false(left <= 0))
    {
      left += period;
      atomic64_set(&hwc->left_period, left);
      atomic64_set(&hwc->last_period, period);
    }

  if (left > (max_period >> 1))
    {
      left = (max_period >> 1);
    }

  atomic64_set(&hwc->prev_count, (uint64_t)-left);
  armpmu->write_counter(event, (uint64_t)(-left) & max_period);
}

static uint64_t armv7pmu_event_update(FAR struct perf_event_s *event)
{
  FAR struct armv7_pmu_s *armpmu = TO_ARMV7_PMU(event->pmu);
  FAR struct hw_perf_event_s *hwc = &event->hw;
  uint64_t now;
  uint64_t delta;

  now = armpmu->read_counter(event);
  delta = (now - atomic64_xchg(&hwc->prev_count, now)) &
          armv7pmu_event_max_period(event);

  atomic64_add(&event->count, delta);
  atomic64_sub(&hwc->left_period, delta);
  atomic64_set(&hwc->last_period, delta);

  return now;
}

static int armv7pmu_read(FAR struct perf_event_s *event)
{
  return (int)armv7pmu_event_update(event);
}

static int armv7pmu_stop(FAR struct perf_event_s *event, int flags)
{
  FAR struct armv7_pmu_s *armv7pmu = TO_ARMV7_PMU(event->pmu);
  FAR struct hw_perf_event_s *hwc = &event->hw;

  if (!(hwc->state & PERF_HES_STOPPED))
    {
      armv7pmu->disable(event);
      armv7pmu_event_update(event);
      hwc->state |= PERF_HES_STOPPED | PERF_HES_UPTODATE;
    }

  return OK;
}

static int armv7pmu_start(FAR struct perf_event_s *event, int flags)
{
  FAR struct armv7_pmu_s *armv7pmu = TO_ARMV7_PMU(event->pmu);
  FAR struct hw_perf_event_s *hwc = &event->hw;

  hwc->state = 0;
  armv7pmu_event_update_period(event);
  armv7pmu->enable(event);

  return OK;
}

static void armv7pmu_del(FAR struct perf_event_s *event, int flags)
{
  FAR struct armv7_pmu_s *armv7pmu = TO_ARMV7_PMU(event->pmu);
  FAR struct pmu_hw_events_s *hw_events = &armv7pmu->hw_events[this_cpu()];
  FAR struct hw_perf_event_s *hwc = &event->hw;

  armv7pmu_stop(event, PERF_EF_UPDATE);
  hw_events->events[hwc->idx] = NULL;
  armv7pmu->clear_event_idx(hw_events, event);
  hwc->idx = -1;
}

static int armv7pmu_add(FAR struct perf_event_s *event, int flags)
{
  FAR struct armv7_pmu_s *armv7pmu = TO_ARMV7_PMU(event->pmu);
  FAR struct pmu_hw_events_s *hw_events = &armv7pmu->hw_events[this_cpu()];
  FAR struct hw_perf_event_s *hwc = &event->hw;
  int idx;

  if (!test_bit(this_cpu(), &armv7pmu->supported_cpus))
    {
      return -ENOENT;
    }

  idx = armv7pmu->get_event_idx(hw_events, event);
  if (idx < 0)
    {
      return idx;
    }

  event->hw.idx = idx;
  armv7pmu->disable(event);
  hw_events->events[idx] = event;

  hwc->state = PERF_HES_STOPPED | PERF_HES_UPTODATE;
  if (flags & PERF_EF_START)
    {
      armv7pmu_start(event, PERF_EF_RELOAD);
    }

  return OK;
}

static int armv7pmu_event_init(FAR struct perf_event_s *event)
{
  FAR struct armv7_pmu_s *armv7pmu = TO_ARMV7_PMU(event->pmu);
  FAR struct hw_perf_event_s *hwc = &event->hw;
  int mapping;

  if (event->cpu != -1 &&
      !test_bit(event->cpu, &armv7pmu->supported_cpus))
    {
      return -ENOENT;
    }

  hwc->flags = 0;
  mapping = armv7pmu->map_event(event);
  if (mapping < 0)
    {
      return mapping;
    }

  hwc->idx = -1;
  hwc->config_base = 0;
  atomic64_set(&hwc->prev_count, 0);

  if (armv7pmu->set_event_filter &&
      armv7pmu->set_event_filter(hwc, &event->attr))
    {
      printf("ARMv7 PMU does not support requested event filters\n");
      return -EOPNOTSUPP;
    }

  hwc->config_base |= (unsigned long)mapping;
  return OK;
}

static void armv7pmu_enable(FAR struct pmu_s *pmu)
{
  FAR struct armv7_pmu_s *armv7pmu = TO_ARMV7_PMU(pmu);
  FAR struct pmu_hw_events_s *hw_events = &armv7pmu->hw_events[this_cpu()];

  if (!test_bit(this_cpu(), &armv7pmu->supported_cpus))
    {
      return;
    }

  if (hw_events->used_mask)
    {
      armv7pmu->start(armv7pmu);
    }
}

static void armv7pmu_disable(FAR struct pmu_s *pmu)
{
  FAR struct armv7_pmu_s *armv7pmu = TO_ARMV7_PMU(pmu);

  if (!test_bit(this_cpu(), &armv7pmu->supported_cpus))
    {
      return;
    }

  armv7pmu->stop(armv7pmu);
}

static int armv7pmu_dispatch_irq(int irq, FAR void *context, FAR void *arg)
{
  FAR struct armv7_pmu_s *armv7pmu = arg;
  return armv7pmu->handle_irq(armv7pmu);
}

static int armv7pmu_request_irq(int irq, FAR struct armv7_pmu_s *armv7pmu)
{
  int ret;

  ret = irq_attach(irq, armv7pmu_dispatch_irq, armv7pmu);
  if (ret < 0)
    {
      printf("ERROR: armv7 pmu irq attach failed: %d\n", ret);
      return ret;
    }

  return OK;
}

static int armv7pmu_request_irqs(FAR struct armv7_pmu_s *armv7pmu)
{
  FAR struct pmu_hw_events_s *hw_events = armv7pmu->hw_events;
  return armv7pmu_request_irq(hw_events->irq, armv7pmu);
}

static FAR struct armv7_pmu_s *armv7pmu_alloc(void)
{
  FAR struct armv7_pmu_s *armv7pmu;

  armv7pmu = kmm_zalloc(sizeof(struct armv7_pmu_s));
  if (!armv7pmu)
    {
      return NULL;
    }

  armv7pmu->pmu.ops = &g_armv7pmu_ops;
  return armv7pmu;
}

static void armv7pmu_free(FAR struct armv7_pmu_s *armv7pmu)
{
  kmm_free(armv7pmu);
}

static int armv7pmu_register(FAR struct armv7_pmu_s *armv7pmu)
{
  return perf_pmu_register(&armv7pmu->pmu, armv7pmu->name);
}

static inline void armv7pmu_pmcr_write(uint32_t val)
{
  val &= ARMV7_PMNC_MASK;
  UP_ISB();
  cp15_pmu_wrpmcr(val);
}

static inline uint32_t armv7pmu_pmcr_read(void)
{
  return cp15_pmu_rdpmcr();
}

static inline void armv7pmu_select_counter(int idx)
{
  cp15_pmu_wrecsr((uint32_t)idx);
  UP_ISB();
}

static inline uint32_t armv7pmu_read_evcntr(int idx)
{
  armv7pmu_select_counter(ARMV7_IDX_TO_COUNTER(idx));
  return cp15_pmu_rdecr();
}

static inline void armv7pmu_write_evcntr(int idx, uint32_t val)
{
  armv7pmu_select_counter(ARMV7_IDX_TO_COUNTER(idx));
  CP15_SET(PMXEVCNTR, val);
}

static inline uint32_t armv7pmu_getreset_flags(void)
{
  uint32_t flags = cp15_pmu_rdofsr();
  flags &= ARMV7_OVERFLOWED_MASK;
  cp15_pmu_wrofsr(flags);
  return flags;
}

static inline int armv7pmu_has_overflowed(uint32_t flags)
{
  return (flags & ARMV7_OVERFLOWED_MASK) != 0;
}

static inline int armv7pmu_counter_has_overflowed(uint32_t flags, int idx)
{
  return (flags & armv7pmu_counter_bit(idx)) != 0;
}

static uint64_t armv7pmu_read_counter(FAR struct perf_event_s *event)
{
  int idx = event->hw.idx;

  if (idx == ARMV7_IDX_CYCLE_COUNTER)
    {
      return cp15_pmu_rdccr();
    }

  return armv7pmu_read_evcntr(idx);
}

static void armv7pmu_write_counter(FAR struct perf_event_s *event,
                                   uint64_t value)
{
  int idx = event->hw.idx;

  if (idx == ARMV7_IDX_CYCLE_COUNTER)
    {
      CP15_SET(PMCCNTR, (uint32_t)value);
      return;
    }

  armv7pmu_write_evcntr(idx, (uint32_t)value);
}

static void armv7pmu_write_event_type(FAR struct perf_event_s *event)
{
  FAR struct hw_perf_event_s *hwc = &event->hw;
  uint32_t val = (uint32_t)hwc->config_base & ARMV7_EVTYPE_MASK;
  int idx = hwc->idx;

  if (idx == ARMV7_IDX_CYCLE_COUNTER)
    {
      /* On most ARMv7 PMUs cycle filtering is PMUv2 specific. */

      armv7pmu_select_counter(31);
      cp15_pmu_wretsr(val);
      return;
    }

  armv7pmu_select_counter(ARMV7_IDX_TO_COUNTER(idx));
  cp15_pmu_wretsr(val);
}

static void armv7pmu_enable_counter_idx(int idx)
{
  cp15_pmu_wrcesr(armv7pmu_counter_bit(idx));
}

static void armv7pmu_disable_counter_idx(int idx)
{
  cp15_pmu_wrcecr(armv7pmu_counter_bit(idx));
  UP_ISB();
}

static void armv7pmu_enable_event_irq(FAR struct perf_event_s *event)
{
  FAR struct pmu_hw_events_s *hw_events =
                              TO_ARMV7_PMU(event->pmu)->hw_events;

  cp15_pmu_wriesr(armv7pmu_counter_bit(event->hw.idx));
  up_enable_irq(hw_events->irq);
}

static void armv7pmu_disable_event_irq(FAR struct perf_event_s *event)
{
  FAR struct pmu_hw_events_s *hw_events =
                              TO_ARMV7_PMU(event->pmu)->hw_events;
  uint32_t mask = armv7pmu_counter_bit(event->hw.idx);

  cp15_pmu_wriecr(mask);
  UP_ISB();
  cp15_pmu_wrofsr(mask);
  UP_ISB();
  up_disable_irq(hw_events->irq);
}

static void armv7pmu_enable_event(FAR struct perf_event_s *event)
{
  armv7pmu_disable_counter_idx(event->hw.idx);
  armv7pmu_write_event_type(event);
  armv7pmu_enable_event_irq(event);
  armv7pmu_enable_counter_idx(event->hw.idx);
}

static void armv7pmu_disable_event(FAR struct perf_event_s *event)
{
  armv7pmu_disable_counter_idx(event->hw.idx);
  armv7pmu_disable_event_irq(event);
}

static void armv7pmu_start_hw(FAR struct armv7_pmu_s *cpu_pmu)
{
  armv7pmu_pmcr_write(armv7pmu_pmcr_read() | ARMV7_PMNC_E);
}

static void armv7pmu_stop_hw(FAR struct armv7_pmu_s *cpu_pmu)
{
  armv7pmu_pmcr_write(armv7pmu_pmcr_read() & ~ARMV7_PMNC_E);
}

static int armv7pmu_handle_irq(FAR struct armv7_pmu_s *cpu_pmu)
{
  uint32_t flags;
  FAR struct pmu_hw_events_s *cpuc = &cpu_pmu->hw_events[this_cpu()];
  int idx;

  flags = armv7pmu_getreset_flags();
  if (!armv7pmu_has_overflowed(flags))
    {
      return 0;
    }

  armv7pmu_stop_hw(cpu_pmu);

  for (idx = 0; idx < cpu_pmu->num_events; idx++)
    {
      FAR struct perf_event_s *event = cpuc->events[idx];

      if (!event)
        {
          continue;
        }

      if (!armv7pmu_counter_has_overflowed(flags, idx))
        {
          continue;
        }

      armv7pmu_event_update(event);

      if (perf_event_overflow(event))
        {
          cpu_pmu->disable(event);
          continue;
        }

      armv7pmu_event_update_period(event);
    }

  armv7pmu_start_hw(cpu_pmu);
  return OK;
}

static int armv7pmu_get_event_idx(FAR struct pmu_hw_events_s *cpuc,
                                  FAR struct perf_event_s *event)
{
  FAR struct armv7_pmu_s *cpu_pmu = TO_ARMV7_PMU(event->pmu);
  FAR struct hw_perf_event_s *hwc = &event->hw;
  unsigned long evtype = hwc->config_base & ARMV7_EVTYPE_EVENT;
  int idx;

  if (evtype == ARMV7_PERFCTR_CPU_CYCLES)
    {
      if (!test_bit(ARMV7_IDX_CYCLE_COUNTER, &cpuc->used_mask))
        {
          set_bit(ARMV7_IDX_CYCLE_COUNTER, &cpuc->used_mask);
          return ARMV7_IDX_CYCLE_COUNTER;
        }
    }

  for (idx = ARMV7_IDX_COUNTER0; idx < cpu_pmu->num_events; idx++)
    {
      if (!test_bit(idx, &cpuc->used_mask))
        {
          set_bit(idx, &cpuc->used_mask);
          return idx;
        }
    }

  return -EAGAIN;
}

static void armv7pmu_clear_event_idx(FAR struct pmu_hw_events_s *cpuc,
                                     FAR struct perf_event_s *event)
{
  clear_bit(event->hw.idx, &cpuc->used_mask);
}

static int armv7pmu_event_idx(FAR struct perf_event_s *event)
{
  if (event->hw.idx == ARMV7_IDX_CYCLE_COUNTER)
    {
      return ARMV7_IDX_CYCLE_COUNTER_USER;
    }

  return event->hw.idx;
}

static int armv7pmu_set_event_filter(FAR struct hw_perf_event_s *event,
                                     FAR struct perf_event_attr_s *attr)
{
  unsigned long config_base = 0;

  if (attr->exclude_idle)
    {
      return -EPERM;
    }

  if (attr->exclude_user)
    {
      config_base |= ARMV7_EXCLUDE_USER;
    }

  if (attr->exclude_kernel)
    {
      config_base |= ARMV7_EXCLUDE_PL1;
    }

  if (!attr->exclude_hv)
    {
      config_base |= ARMV7_INCLUDE_HYP;
    }

  event->config_base = config_base;
  return OK;
}

static void armv7pmu_reset(FAR void *info)
{
  FAR struct armv7_pmu_s *cpu_pmu = info;
  uint32_t pmcr;
  int idx;

  for (idx = 0; idx < cpu_pmu->num_events; idx++)
    {
      armv7pmu_disable_counter_idx(idx);
      cp15_pmu_wriecr(armv7pmu_counter_bit(idx));
    }

  cp15_pmu_wrofsr(ARMV7_OVERFLOWED_MASK);

  pmcr = ARMV7_PMNC_P | ARMV7_PMNC_C;
  armv7pmu_pmcr_write(pmcr);
}

static int armv7pmu_map_event_id(FAR struct armv7_pmu_s *armpmu,
                                 FAR struct perf_event_s *event)
{
  return armv7pmu_map_event(event,
                            &g_armv7_perf_map,
                            &g_armv7_perf_cache_map,
                            0xff);
}

static int armv7pmu_map_cpu_event(FAR struct perf_event_s *event)
{
  FAR struct armv7_pmu_s *armpmu = TO_ARMV7_PMU(event->pmu);
  int hw_event_id;

  hw_event_id = armv7pmu_map_event_id(armpmu, event);
  if (hw_event_id < 0)
    {
      return hw_event_id;
    }

  event->hw.flags |= PERF_EVENT_FLAG_USER_READ_CNT;
  return hw_event_id;
}

static int armv7pmu_common_init(FAR struct armv7_pmu_s *cpu_pmu,
                                FAR const char *name)
{
  uint32_t pmcr;
  int cpu;

  pmcr = armv7pmu_pmcr_read();
  cpu_pmu->num_events = ((pmcr >> ARMV7_PMNC_N_SHIFT) &
                         ARMV7_PMNC_N_MASK) + 1;

  binfo("ARMv7 PMU supports %d events\n", cpu_pmu->num_events);

  /* PMNC.N is 5-bit (max 31), so after '+1' num_events should be <= 32.
   * Keep this clamp as a defensive guard for unexpected values.
   */

  if (cpu_pmu->num_events > ARMV7PMU_MAX_HWEVENTS)
    {
      cpu_pmu->num_events = ARMV7PMU_MAX_HWEVENTS;
    }

  cpu_pmu->handle_irq       = armv7pmu_handle_irq;
  cpu_pmu->enable           = armv7pmu_enable_event;
  cpu_pmu->disable          = armv7pmu_disable_event;
  cpu_pmu->read_counter     = armv7pmu_read_counter;
  cpu_pmu->write_counter    = armv7pmu_write_counter;
  cpu_pmu->get_event_idx    = armv7pmu_get_event_idx;
  cpu_pmu->clear_event_idx  = armv7pmu_clear_event_idx;
  cpu_pmu->start            = armv7pmu_start_hw;
  cpu_pmu->stop             = armv7pmu_stop_hw;
  cpu_pmu->reset            = armv7pmu_reset;
  cpu_pmu->set_event_filter = armv7pmu_set_event_filter;
  cpu_pmu->pmu.ops->event_idx = armv7pmu_event_idx;
  cpu_pmu->name             = name;
  cpu_pmu->map_event        = armv7pmu_map_cpu_event;

  for (cpu = 0; cpu < CONFIG_SMP_NCPUS; cpu++)
    {
      FAR struct pmu_hw_events_s *events;

      cpu_pmu->supported_cpus |= BIT(cpu);
      events = &cpu_pmu->hw_events[cpu];
      events->irq = ARMV7PMU_IRQ;
    }

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int arm_pmu_initialize(void)
{
  FAR struct armv7_pmu_s *armv7pmu;
  int ret;

  armv7pmu = armv7pmu_alloc();
  if (!armv7pmu)
    {
      printf("ERROR: alloc armv7 pmu failed\n");
      return -ENOMEM;
    }

  ret = armv7pmu_common_init(armv7pmu, "arm,armv7-pmu");
  if (ret < 0)
    {
      printf("ERROR: armv7 pmu init failed: %d\n", ret);
      goto out_free;
    }

  ret = armv7pmu_request_irqs(armv7pmu);
  if (ret < 0)
    {
      goto out_free;
    }

  ret = armv7pmu_register(armv7pmu);
  if (ret < 0)
    {
      printf("ERROR: failed to register armv7 pmu: %d\n", ret);
      goto out_free;
    }

  return OK;

out_free:
  armv7pmu_free(armv7pmu);
  return ret;
}
