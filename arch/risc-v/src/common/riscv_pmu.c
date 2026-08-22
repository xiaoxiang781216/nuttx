/****************************************************************************
 * arch/risc-v/src/common/riscv_pmu.c
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

#include "riscv_internal.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define HW_OP_UNSUPPORTED          0xFFFF

#define RISCV_PMU_MAX_HWEVENTS     32

/* RISC-V HPM event codes.
 * These are implementation-defined. For QEMU virt machine,
 * only cycle and instret are guaranteed. We define generic
 * event codes that map to the fixed counters.
 */

#define RISCV_PMU_EVT_CPU_CYCLES   0x0001
#define RISCV_PMU_EVT_INST_RETIRED 0x0002

/* Counter index mapping:
 * - Index 0 is the dedicated cycle counter (mcycle).
 * - Index 1 is the dedicated instret counter (minstret).
 * - Index 2..N are programmable HPM counters (mhpmcounter3..31).
 */

#define RISCV_IDX_CYCLE_COUNTER    0
#define RISCV_IDX_INSTRET_COUNTER  1
#define RISCV_IDX_COUNTER0         2
#define RISCV_IDX_COUNTER_MAX      RISCV_PMU_MAX_HWEVENTS
#define RISCV_IDX_CYCLE_COUNTER_USER 32

/* Map driver index to mcountinhibit bit position:
 * idx 0 -> bit 0 (CY)
 * idx 1 -> bit 2 (IR)
 * idx 2 -> bit 3 (HPM3)
 * idx N -> bit (N+1)
 */

#define RISCV_IDX_TO_INHIBIT_BIT(idx) \
  ((idx) == RISCV_IDX_CYCLE_COUNTER ? 0 : \
   (idx) == RISCV_IDX_INSTRET_COUNTER ? 2 : (idx) + 1)

#define TO_RISCV_PMU(p) (container_of(p, struct riscv_pmu_s, pmu))

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct pmu_hw_events_s
{
  FAR struct perf_event_s *events[RISCV_PMU_MAX_HWEVENTS];
  unsigned long used_mask;
};

struct riscv_pmu_s
{
  struct pmu_s pmu;
  unsigned long supported_cpus;
  FAR const char *name;
  int num_events;
  struct pmu_hw_events_s hw_events[CPU_SETSIZE];
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static void riscvpmu_pmu_enable(FAR struct pmu_s *pmu);
static void riscvpmu_pmu_disable(FAR struct pmu_s *pmu);
static int riscvpmu_event_init(FAR struct perf_event_s *event);
static int riscvpmu_event_add(FAR struct perf_event_s *event, int flags);
static void riscvpmu_event_del(FAR struct perf_event_s *event, int flags);
static int riscvpmu_event_start(FAR struct perf_event_s *event, int flags);
static int riscvpmu_event_stop(FAR struct perf_event_s *event, int flags);
static int riscvpmu_event_read(FAR struct perf_event_s *event);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct pmu_ops_s g_riscvpmu_ops =
{
  .pmu_enable  = riscvpmu_pmu_enable,
  .pmu_disable = riscvpmu_pmu_disable,
  .event_init  = riscvpmu_event_init,
  .event_add   = riscvpmu_event_add,
  .event_del   = riscvpmu_event_del,
  .event_start = riscvpmu_event_start,
  .event_stop  = riscvpmu_event_stop,
  .event_read  = riscvpmu_event_read,
};

/* The single, statically allocated PMU instance.  There is exactly one PMU
 * device for the system, so it needs no dynamic allocation.
 */

static struct riscv_pmu_s g_riscv_pmu_dev =
{
  .pmu.ops = &g_riscvpmu_ops,
};

/* Map generic perf HW events to RISC-V PMU event codes */

static const unsigned int g_riscv_perf_map[PERF_COUNT_HW_MAX] =
{
  [PERF_COUNT_HW_CPU_CYCLES]              = RISCV_PMU_EVT_CPU_CYCLES,
  [PERF_COUNT_HW_INSTRUCTIONS]            = RISCV_PMU_EVT_INST_RETIRED,
  [PERF_COUNT_HW_CACHE_REFERENCES]        = HW_OP_UNSUPPORTED,
  [PERF_COUNT_HW_CACHE_MISSES]            = HW_OP_UNSUPPORTED,
  [PERF_COUNT_HW_BRANCH_INSTRUCTIONS]     = HW_OP_UNSUPPORTED,
  [PERF_COUNT_HW_BRANCH_MISSES]           = HW_OP_UNSUPPORTED,
  [PERF_COUNT_HW_BUS_CYCLES]              = HW_OP_UNSUPPORTED,
  [PERF_COUNT_HW_STALLED_CYCLES_FRONTEND] = HW_OP_UNSUPPORTED,
  [PERF_COUNT_HW_STALLED_CYCLES_BACKEND]  = HW_OP_UNSUPPORTED,
  [PERF_COUNT_HW_REF_CPU_CYCLES]          = HW_OP_UNSUPPORTED,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static inline uint64_t riscvpmu_event_max_period(void)
{
  /* On RV32, mcycle/minstret are read as full 64-bit values
   * by combining the high and low CSRs, so the effective
   * counter width is 64 bits on all platforms.
   */

  return UINT64_MAX;
}

static int riscvpmu_map_hw_event(uint64_t config)
{
  int mapping;

  if (config >= PERF_COUNT_HW_MAX)
    {
      return -EINVAL;
    }

  mapping = g_riscv_perf_map[config];
  if (mapping == HW_OP_UNSUPPORTED)
    {
      return -ENOENT;
    }

  return mapping;
}

static int riscvpmu_map_event(FAR struct perf_event_s *event)
{
  uint64_t config = event->attr.config;
  int type = event->attr.type;

  switch (type)
    {
      case PERF_TYPE_HARDWARE:
        return riscvpmu_map_hw_event(config);
      case PERF_TYPE_RAW:
        return (int)(config & 0xffff);
      default:
        return -ENOENT;
    }
}

static uint64_t riscvpmu_read_cycle(void)
{
#ifdef CONFIG_ARCH_RV64
  return READ_CSR(CSR_MCYCLE);
#else
  uint32_t hi;
  uint32_t lo;
  uint32_t hi2;

  do
    {
      hi  = READ_CSR(CSR_MCYCLEH);
      lo  = READ_CSR(CSR_MCYCLE);
      hi2 = READ_CSR(CSR_MCYCLEH);
    }
  while (hi != hi2);

  return ((uint64_t)hi << 32) | lo;
#endif
}

static uint64_t riscvpmu_read_instret(void)
{
#ifdef CONFIG_ARCH_RV64
  return READ_CSR(CSR_MINSTRET);
#else
  uint32_t hi;
  uint32_t lo;
  uint32_t hi2;

  do
    {
      hi  = READ_CSR(CSR_MINSTRETH);
      lo  = READ_CSR(CSR_MINSTRET);
      hi2 = READ_CSR(CSR_MINSTRETH);
    }
  while (hi != hi2);

  return ((uint64_t)hi << 32) | lo;
#endif
}

static uint64_t riscvpmu_read_counter(FAR struct perf_event_s *event)
{
  int idx = event->hw.idx;

  if (idx == RISCV_IDX_CYCLE_COUNTER)
    {
      return riscvpmu_read_cycle();
    }

  if (idx == RISCV_IDX_INSTRET_COUNTER)
    {
      return riscvpmu_read_instret();
    }

  /* HPM counters not supported on QEMU */

  return 0;
}

static void riscvpmu_enable_counter(int idx)
{
  unsigned long bit = BIT(RISCV_IDX_TO_INHIBIT_BIT(idx));

  CLEAR_CSR(CSR_MCOUNTINHIBIT, bit);
}

static void riscvpmu_disable_counter(int idx)
{
  unsigned long bit = BIT(RISCV_IDX_TO_INHIBIT_BIT(idx));

  SET_CSR(CSR_MCOUNTINHIBIT, bit);
}

static void riscvpmu_enable_event(FAR struct perf_event_s *event)
{
  int idx = event->hw.idx;

  riscvpmu_enable_counter(idx);
}

static void riscvpmu_disable_event(FAR struct perf_event_s *event)
{
  int idx = event->hw.idx;

  riscvpmu_disable_counter(idx);
}

static void riscvpmu_event_update_period(FAR struct perf_event_s *event)
{
  FAR struct hw_perf_event_s *hwc = &event->hw;
  uint64_t max_period = riscvpmu_event_max_period();

  /* No overflow interrupt on basic RISC-V, just set prev_count
   * to current value for delta computation.
   */

  atomic64_set(&hwc->prev_count, riscvpmu_read_counter(event));
  UNUSED(max_period);
}

static uint64_t riscvpmu_event_update(FAR struct perf_event_s *event)
{
  FAR struct hw_perf_event_s *hwc = &event->hw;
  uint64_t now;
  uint64_t delta;

  now = riscvpmu_read_counter(event);
  delta = (now - atomic64_xchg(&hwc->prev_count, now)) &
          riscvpmu_event_max_period();

  atomic64_add(&event->count, delta);
  atomic64_sub(&hwc->left_period, delta);
  atomic64_set(&hwc->last_period, delta);

  return now;
}

static int riscvpmu_get_event_idx(FAR struct pmu_hw_events_s *cpuc,
                                  FAR struct perf_event_s *event)
{
  FAR struct hw_perf_event_s *hwc = &event->hw;
  unsigned long evtype = hwc->config_base;

  /* CPU cycles must use the dedicated cycle counter */

  if (evtype == RISCV_PMU_EVT_CPU_CYCLES)
    {
      if (!test_bit(RISCV_IDX_CYCLE_COUNTER, &cpuc->used_mask))
        {
          set_bit(RISCV_IDX_CYCLE_COUNTER, &cpuc->used_mask);
          return RISCV_IDX_CYCLE_COUNTER;
        }

      return -EAGAIN;
    }

  /* Instructions retired must use the dedicated instret counter */

  if (evtype == RISCV_PMU_EVT_INST_RETIRED)
    {
      if (!test_bit(RISCV_IDX_INSTRET_COUNTER, &cpuc->used_mask))
        {
          set_bit(RISCV_IDX_INSTRET_COUNTER, &cpuc->used_mask);
          return RISCV_IDX_INSTRET_COUNTER;
        }

      return -EAGAIN;
    }

  /* For other events, find a free HPM counter */

  return -ENOENT;
}

static void riscvpmu_clear_event_idx(FAR struct pmu_hw_events_s *cpuc,
                                     FAR struct perf_event_s *event)
{
  clear_bit(event->hw.idx, &cpuc->used_mask);
}

static int riscvpmu_event_idx(FAR struct perf_event_s *event)
{
  if (event->hw.idx == RISCV_IDX_CYCLE_COUNTER)
    {
      return RISCV_IDX_CYCLE_COUNTER_USER;
    }

  return event->hw.idx;
}

static int riscvpmu_event_init(FAR struct perf_event_s *event)
{
  FAR struct riscv_pmu_s *riscvpmu = TO_RISCV_PMU(event->pmu);
  FAR struct hw_perf_event_s *hwc = &event->hw;
  int mapping;

  if (event->cpu != -1 &&
      !test_bit(event->cpu, &riscvpmu->supported_cpus))
    {
      return -ENOENT;
    }

  hwc->flags = 0;
  mapping = riscvpmu_map_event(event);
  if (mapping < 0)
    {
      return mapping;
    }

  hwc->idx = -1;
  hwc->config_base = (unsigned long)mapping;
  atomic64_set(&hwc->prev_count, 0);

  hwc->flags |= PERF_EVENT_FLAG_USER_READ_CNT;
  return OK;
}

static int riscvpmu_event_add(FAR struct perf_event_s *event, int flags)
{
  FAR struct riscv_pmu_s *riscvpmu = TO_RISCV_PMU(event->pmu);
  FAR struct pmu_hw_events_s *hw_events =
                              &riscvpmu->hw_events[this_cpu()];
  FAR struct hw_perf_event_s *hwc = &event->hw;
  int idx;

  if (!test_bit(this_cpu(), &riscvpmu->supported_cpus))
    {
      return -ENOENT;
    }

  idx = riscvpmu_get_event_idx(hw_events, event);
  if (idx < 0)
    {
      return idx;
    }

  event->hw.idx = idx;
  riscvpmu_disable_event(event);
  hw_events->events[idx] = event;

  hwc->state = PERF_HES_STOPPED | PERF_HES_UPTODATE;
  if (flags & PERF_EF_START)
    {
      riscvpmu_event_start(event, PERF_EF_RELOAD);
    }

  return OK;
}

static void riscvpmu_event_del(FAR struct perf_event_s *event, int flags)
{
  FAR struct riscv_pmu_s *riscvpmu = TO_RISCV_PMU(event->pmu);
  FAR struct pmu_hw_events_s *hw_events =
                              &riscvpmu->hw_events[this_cpu()];
  FAR struct hw_perf_event_s *hwc = &event->hw;

  riscvpmu_event_stop(event, PERF_EF_UPDATE);
  hw_events->events[hwc->idx] = NULL;
  riscvpmu_clear_event_idx(hw_events, event);
  hwc->idx = -1;
}

static int riscvpmu_event_start(FAR struct perf_event_s *event, int flags)
{
  FAR struct hw_perf_event_s *hwc = &event->hw;

  hwc->state = 0;
  riscvpmu_event_update_period(event);
  riscvpmu_enable_event(event);

  return OK;
}

static int riscvpmu_event_stop(FAR struct perf_event_s *event, int flags)
{
  FAR struct hw_perf_event_s *hwc = &event->hw;

  if (!(hwc->state & PERF_HES_STOPPED))
    {
      riscvpmu_disable_event(event);
      riscvpmu_event_update(event);
      hwc->state |= PERF_HES_STOPPED | PERF_HES_UPTODATE;
    }

  return OK;
}

static int riscvpmu_event_read(FAR struct perf_event_s *event)
{
  return (int)riscvpmu_event_update(event);
}

static void riscvpmu_pmu_enable(FAR struct pmu_s *pmu)
{
  /* RISC-V uses per-counter inhibit bits, no global enable needed */
}

static void riscvpmu_pmu_disable(FAR struct pmu_s *pmu)
{
  /* RISC-V uses per-counter inhibit bits, no global disable needed */
}

static void riscvpmu_reset(FAR struct riscv_pmu_s *cpu_pmu)
{
  /* Inhibit all counters */

  WRITE_CSR(CSR_MCOUNTINHIBIT, UINT32_MAX);

  /* Reset cycle and instret counters */

  WRITE_CSR(CSR_MCYCLE, 0);
  WRITE_CSR(CSR_MINSTRET, 0);
#ifndef CONFIG_ARCH_RV64
  WRITE_CSR(CSR_MCYCLEH, 0);
  WRITE_CSR(CSR_MINSTRETH, 0);
#endif
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

#if defined(CONFIG_ARCH_PERF_EVENTS)

/****************************************************************************
 * Name: riscvpmu_cycle_reserve
 *
 * Description:
 *   Reserve the dedicated cycle counter index in EVERY CPU's perf
 *   allocation bitmap (used_mask), so riscvpmu_get_event_idx() never hands
 *   the cycle counter to a cpu-cycles event.  This prevents perf from
 *   "stealing" the mcycle counter out from under the resident cycle time
 *   source (up_perf_gettime via riscv_perf_cycle.c, which reads mcycle).
 *
 *   The reservation is a pure per-CPU memory update done once at init, so no
 *   locking against concurrent access is required.
 *
 ****************************************************************************/

static void riscvpmu_cycle_reserve(FAR struct riscv_pmu_s *riscvpmu)
{
  int cpu;

  for (cpu = 0; cpu < CPU_SETSIZE; cpu++)
    {
      set_bit(RISCV_IDX_CYCLE_COUNTER,
              &riscvpmu->hw_events[cpu].used_mask);
    }
}

#endif

int riscv_pmu_initialize(void)
{
  FAR struct riscv_pmu_s *riscvpmu = &g_riscv_pmu_dev;
  int cpu;
  int ret;

  riscvpmu->name = "riscv,pmu";

  /* RISC-V base ISA guarantees mcycle and minstret.
   * num_events = 2 (cycle + instret).
   * HPM counters are implementation-defined and not probed here.
   */

  riscvpmu->num_events = RISCV_IDX_COUNTER0;

  _info("RISC-V PMU: cycle counter, instret counter\n");

  g_riscvpmu_ops.event_idx = riscvpmu_event_idx;

  for (cpu = 0; cpu < CPU_SETSIZE; cpu++)
    {
      riscvpmu->supported_cpus |= BIT(cpu);
    }

  /* Reset PMU to a known state */

  riscvpmu_reset(riscvpmu);

  /* Register with perf framework */

  ret = perf_pmu_register(&riscvpmu->pmu, riscvpmu->name);
  if (ret < 0)
    {
      _err("ERROR: failed to register riscv pmu: %d\n", ret);
      return ret;
    }

#if defined(CONFIG_ARCH_PERF_EVENTS)
  /* Reserve the cycle counter in every CPU's allocation bitmap so the perf
   * framework never hands it to a cpu-cycles event.  up_perf_gettime reads
   * the same mcycle counter, so it must not be stolen out from under the
   * resident cycle time source.
   */

  riscvpmu_cycle_reserve(riscvpmu);
#endif

  return OK;
}
