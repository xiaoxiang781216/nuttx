/****************************************************************************
 * arch/tricore/src/common/tricore_perf.c
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

#include <nuttx/arch.h>
#include <nuttx/clock.h>
#include <nuttx/lib/math32.h>

/****************************************************************************
 * Pre-processor Prototypes
 ****************************************************************************/

/* CPU_CCTRL Enable bit. */

#define CCTRL_COUNT_ENABLE (1 << 1)

/* CCNT is a 31-bit counter (bits 0-30); bit 31 is the sticky overflow flag
 * (SOVF).  perf_gettime() combines `now` with the overflow count via
 * `now | (overflow << CONFIG_ARCH_PERF_COUNT_BITWIDTH)`, so `now` must fit
 * in the low CONFIG_ARCH_PERF_COUNT_BITWIDTH bits.  The mask is derived
 * from the same width to always match the overflow extension bit.
 */

#define CCNT_VALUE_MASK ((1UL << CONFIG_ARCH_PERF_COUNT_BITWIDTH) - \
                         1UL)

#ifdef CONFIG_ARCH_PERF_EVENTS

#if defined(CONFIG_BUILD_FLAT) || defined(__KERNEL__)
/****************************************************************************
 * Private Data
 ****************************************************************************/

static unsigned long g_cpu_freq = ULONG_MAX;
static invdiv_param64_t g_invdiv_param;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void tricore_reset_ccnt(void)
{
  uint32_t cctrl = tricore_mfcr(CPU_CCTRL);

  /* Disable and clear ccnt */

  cctrl &= ~CCTRL_COUNT_ENABLE;
  tricore_mtcr(CPU_CCTRL, cctrl);
  tricore_mtcr(CPU_CCNT, 0);
  UP_ISB();

  /* Re-enable ccnt */

  cctrl |= CCTRL_COUNT_ENABLE;
  tricore_mtcr(CPU_CCTRL, cctrl);
  UP_ISB();
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void up_perf_init(void *arg)
{
  g_cpu_freq = (unsigned long)(uintptr_t)arg;

  invdiv_init_param64(g_cpu_freq, &g_invdiv_param);

  tricore_reset_ccnt();
}

unsigned long up_perf_getfreq(void)
{
  return g_cpu_freq;
}

void up_perf_convert(clock_t elapsed, struct timespec *ts)
{
  clock_t left;

  ts->tv_sec  = invdiv_u64(elapsed, &g_invdiv_param);
  left        = elapsed - ts->tv_sec * g_cpu_freq;
  ts->tv_nsec = invdiv_u64(NSEC_PER_SEC * (uint64_t)left, &g_invdiv_param);
}
#endif

clock_t noinstrument_function up_perf_gettime(void)
{
  /* CCNT is a 31-bit counter (bits 0-30).  Bit 31 is the sticky overflow
   * flag (SOVF): it is set when the counter wraps and stays set until the
   * counter is cleared, so the raw 32-bit value is NOT a pure 31-bit
   * counter.  perf_gettime() uses CONFIG_ARCH_PERF_COUNT_BITWIDTH to
   * combine `now` with the overflow count via
   * `now | (overflow << CONFIG_ARCH_PERF_COUNT_BITWIDTH)`; if SOVF
   * (bit 31) leaked into `now`, it would collide with the overflow
   * extension bit and the combined timestamp would go backwards at every
   * wrap, producing negative elapsed times in critmon.  Mask SOVF off so
   * `now` is the bare 31-bit count.
   */

  return tricore_mfcr(CPU_CCNT) & CCNT_VALUE_MASK;
}

#endif
