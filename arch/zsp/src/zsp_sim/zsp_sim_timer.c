/****************************************************************************
 * arch/zsp/src/zsp_sim/zsp_sim_timer.c
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
#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <nuttx/timers/oneshot.h>
#include <nuttx/timers/arch_alarm.h>
#include <arch/irq.h>

#include "zsp_internal.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Timer clock frequency. Both Timer0 and Timer1 run at the CPU clock
 * with prescale=0 (divide by 1). On the simulator this is effectively
 * 1 count per instruction cycle.
 */

#define ZSP_TIMER_FREQ  1000000

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct zsp_timer_lowerhalf_s
{
  struct oneshot_lowerhalf_s lower;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static clkcnt_t zsp_timer_max_delay(struct oneshot_lowerhalf_s *lower);
static clkcnt_t zsp_timer_current(struct oneshot_lowerhalf_s *lower);
static void zsp_timer_start(struct oneshot_lowerhalf_s *lower, clkcnt_t d);
static void zsp_timer_cancel(struct oneshot_lowerhalf_s *lower);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct oneshot_operations_s g_zsp_timer_ops =
{
  .current   = zsp_timer_current,
  .start     = zsp_timer_start,
  .cancel    = zsp_timer_cancel,
  .max_delay = zsp_timer_max_delay
};

static struct zsp_timer_lowerhalf_s g_zsp_timer =
{
  .lower.ops = &g_zsp_timer_ops
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: zsp_timer1_read
 *
 * Description:
 *   Read the Timer1 free-running counter. Timer1 counts down from
 *   0xFFFFFFFF with auto-reload, providing a monotonic time source.
 *   We invert to get a monotonically increasing value.
 *
 ****************************************************************************/

static inline uint32_t zsp_timer1_read(void)
{
  uint32_t val;
  __asm__ __volatile__("mov32 %0, %%timer1" : "=r"(val));
  return val;
}

/****************************************************************************
 * Name: zsp_timer_max_delay
 *
 * Description:
 *   Timer0 is 32-bit, so maximum delay is UINT32_MAX counts.
 *
 ****************************************************************************/

static clkcnt_t zsp_timer_max_delay(struct oneshot_lowerhalf_s *lower)
{
  UNUSED(lower);
  return UINT32_MAX;
}

/****************************************************************************
 * Name: zsp_timer_current
 *
 * Description:
 *   Return current time by reading Timer1 free-running counter.
 *   Timer1 counts down, so we invert to get monotonically increasing
 *   time. This is drift-free since it reads hardware directly.
 *   (Reference: RISC-V mtime in riscv_mtimer.c)
 *
 ****************************************************************************/

static clkcnt_t zsp_timer_current(struct oneshot_lowerhalf_s *lower)
{
  UNUSED(lower);
  return UINT32_MAX - zsp_timer1_read();
}

/****************************************************************************
 * Name: zsp_timer_start
 *
 * Description:
 *   Start Timer0 as a one-shot alarm. Timer0 counts down from the
 *   loaded value and generates an interrupt when reaching zero.
 *   (Reference: RISC-V mtimecmp in riscv_mtimer.c)
 *
 ****************************************************************************/

__attribute__((optimize("O0")))
static void zsp_timer_start(struct oneshot_lowerhalf_s *lower, clkcnt_t d)
{
  UNUSED(lower);

  /* Stop Timer0, load count, set single-shot mode, start */

  __asm__ __volatile__("bitc %%tc, 7" : : : "memory");
  __asm__ __volatile__("mov32 %%timer0, %0" : : "r"((uint32_t)d));
  __asm__ __volatile__("bitc %%tc, 6" : : : "memory");
  __asm__ __volatile__("bits %%tc, 7" : : : "memory");
}

/****************************************************************************
 * Name: zsp_timer_cancel
 *
 * Description:
 *   Cancel pending Timer0 alarm by stopping the timer.
 *   (Reference: RISC-V sets mtimecmp=UINT64_MAX to cancel)
 *
 ****************************************************************************/

__attribute__((optimize("O0")))
static void zsp_timer_cancel(struct oneshot_lowerhalf_s *lower)
{
  UNUSED(lower);
  __asm__ __volatile__("bitc %%tc, 7" : : : "memory");
}

/****************************************************************************
 * Name: zsp_timer_interrupt
 *
 * Description:
 *   Timer0 interrupt handler. Notifies the oneshot framework.
 *
 ****************************************************************************/

static int zsp_timer_interrupt(int irq, void *context, void *arg)
{
  UNUSED(irq);
  UNUSED(context);

  struct zsp_timer_lowerhalf_s *priv =
    (struct zsp_timer_lowerhalf_s *)arg;

  oneshot_process_callback(&priv->lower);
  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_timer_initialize
 *
 * Description:
 *   Initialize timer subsystem:
 *   - Timer1: free-running counter (auto-reload from 0xFFFFFFFF) for
 *     current() time source — drift-free hardware readback.
 *   - Timer0: one-shot alarm for scheduling interrupts.
 *
 *   This two-timer design follows the same pattern as RISC-V mtime/mtimecmp
 *   (see arch/risc-v/src/common/riscv_mtimer.c).
 *
 ****************************************************************************/

__attribute__((optimize("O0")))
void up_timer_initialize(void)
{
  struct zsp_timer_lowerhalf_s *priv = &g_zsp_timer;
  uint32_t maxval = UINT32_MAX;  /* Cannot mov32 immediate to control reg */

  /* Initialize Timer1 as free-running counter:
   * prescale=0, auto-reload mode, count from 0xFFFFFFFF
   */

  __asm__ __volatile__("bitc %%tc, 15" : : : "memory");
  __asm__ __volatile__("mov32 %%timer1, %0" : : "r"(maxval));
  __asm__ __volatile__("bits %%tc, 14" : : : "memory");
  __asm__ __volatile__("bits %%tc, 15" : : : "memory");

  /* Register oneshot driver with Timer0 alarm interrupt */

  oneshot_count_init(&priv->lower, ZSP_TIMER_FREQ);
  irq_attach(ZSP_IRQ_TIMER0, zsp_timer_interrupt, priv);
  up_enable_irq(ZSP_IRQ_TIMER0);
  up_alarm_set_lowerhalf(&priv->lower);
}
