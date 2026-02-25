/****************************************************************************
 * arch/xtensa/src/common/xtensa_oneshot.c
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

#include <assert.h>
#include <nuttx/spinlock.h>
#include <nuttx/kmalloc.h>
#include <nuttx/timers/oneshot.h>

#include "xtensa_counter.h"
#include "xtensa.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* This structure provides the private representation of the "lower-half"
 * driver state structure.  This structure must be cast-compatible with the
 * oneshot_lowerhalf_s structure.
 */

struct xoneshot_lowerhalf_s
{
  struct oneshot_lowerhalf_s lh;       /* Lower half operations */
  uint32_t                   freq;     /* Timer working clock frequency(Hz) */
  uint32_t                   irq;
  spinlock_t                 lock;     /* Lock to protect oneshot state */
  uint32_t                   overflow;
  uint32_t                   last_count;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

#ifdef CONFIG_ONESHOT_COUNT
static clkcnt_t xtensa_oneshot_current(struct oneshot_lowerhalf_s *lower);
static void     xtensa_oneshot_start(struct oneshot_lowerhalf_s *lower,
                                     clkcnt_t delay);
static void     xtensa_oneshot_start_absolute(
                    struct oneshot_lowerhalf_s *lower, clkcnt_t cnt);
static void     xtensa_oneshot_cancel(struct oneshot_lowerhalf_s *lower);
static clkcnt_t xtensa_oneshot_maxdelay(struct oneshot_lowerhalf_s *lower);
#else
static int xtensa_oneshot_maxdelay(struct oneshot_lowerhalf_s *lower,
                                   struct timespec *ts);
static int xtensa_oneshot_start(struct oneshot_lowerhalf_s *lower,
                                const struct timespec *ts);
static int xtensa_oneshot_cancel(struct oneshot_lowerhalf_s *lower,
                                 struct timespec *ts);
static int xtensa_oneshot_current(struct oneshot_lowerhalf_s *lower,
                                  struct timespec *ts);
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct oneshot_operations_s g_xtensa_oneshot_ops =
{
#ifdef CONFIG_ONESHOT_COUNT
  .current        = xtensa_oneshot_current,
  .start          = xtensa_oneshot_start,
  .start_absolute = xtensa_oneshot_start_absolute,
  .cancel         = xtensa_oneshot_cancel,
  .max_delay      = xtensa_oneshot_maxdelay,
#else
  .max_delay = xtensa_oneshot_maxdelay,
  .start     = xtensa_oneshot_start,
  .cancel    = xtensa_oneshot_cancel,
  .current   = xtensa_oneshot_current,
#endif
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static inline clkcnt_t
xtensa_oneshot_get_count(struct xoneshot_lowerhalf_s *lower)
{
  irqstate_t flags;
  uint32_t count;
  clkcnt_t result;

  flags = spin_lock_irqsave(&lower->lock);

  count = xtensa_getcount();
  if (count < lower->last_count)
    {
      lower->overflow++;
    }

  lower->last_count = count;
  result = (clkcnt_t)count + (clkcnt_t)lower->overflow * UINT32_MAX;

  spin_unlock_irqrestore(&lower->lock, flags);

  return result;
}

#ifdef CONFIG_ONESHOT_COUNT

static clkcnt_t xtensa_oneshot_current(struct oneshot_lowerhalf_s *lower_)
{
  struct xoneshot_lowerhalf_s *lower =
    (struct xoneshot_lowerhalf_s *)lower_;

  return xtensa_oneshot_get_count(lower);
}

static void xtensa_oneshot_start(struct oneshot_lowerhalf_s *lower_,
                                 clkcnt_t delay)
{
  struct xoneshot_lowerhalf_s *lower =
    (struct xoneshot_lowerhalf_s *)lower_;
  irqstate_t flags;

  flags = spin_lock_irqsave(&lower->lock);

  xtensa_setcompare(xtensa_getcount() + (uint32_t)delay);
  up_enable_irq(lower->irq);

  spin_unlock_irqrestore(&lower->lock, flags);
}

static void xtensa_oneshot_start_absolute(struct oneshot_lowerhalf_s *lower_,
                                          clkcnt_t cnt)
{
  struct xoneshot_lowerhalf_s *lower =
    (struct xoneshot_lowerhalf_s *)lower_;
  irqstate_t flags;

  flags = spin_lock_irqsave(&lower->lock);

  xtensa_setcompare((uint32_t)cnt);
  up_enable_irq(lower->irq);

  spin_unlock_irqrestore(&lower->lock, flags);
}

static void xtensa_oneshot_cancel(struct oneshot_lowerhalf_s *lower_)
{
  struct xoneshot_lowerhalf_s *lower =
    (struct xoneshot_lowerhalf_s *)lower_;
  irqstate_t flags;

  flags = spin_lock_irqsave(&lower->lock);

  up_disable_irq(lower->irq);

  spin_unlock_irqrestore(&lower->lock, flags);
}

static clkcnt_t xtensa_oneshot_maxdelay(struct oneshot_lowerhalf_s *lower_)
{
  UNUSED(lower_);
  return (clkcnt_t)UINT32_MAX;
}

#else /* !CONFIG_ONESHOT_COUNT */

static inline uint64_t nsec_from_count(uint32_t count, uint32_t freq)
{
  return (uint64_t)count * NSEC_PER_SEC / freq;
}

static inline uint64_t nsec_to_count(uint32_t nsec, uint32_t freq)
{
  return (uint64_t)nsec * freq / NSEC_PER_SEC;
}

static inline uint64_t sec_to_count(uint32_t sec, uint32_t freq)
{
  return (uint64_t)sec * freq;
}

static int xtensa_oneshot_start(struct oneshot_lowerhalf_s *lower_,
                                const struct timespec *ts)
{
  struct xoneshot_lowerhalf_s *lower =
    (struct xoneshot_lowerhalf_s *)lower_;
  uint32_t count;
  irqstate_t flags;

  flags = spin_lock_irqsave(&lower->lock);

  count = sec_to_count((uint64_t)ts->tv_sec, lower->freq) +
          nsec_to_count((uint64_t)ts->tv_nsec, lower->freq);

  count = xtensa_getcount() + count;
  xtensa_setcompare(count);

  up_enable_irq(lower->irq);

  spin_unlock_irqrestore(&lower->lock, flags);

  return 0;
}

static int xtensa_oneshot_cancel(struct oneshot_lowerhalf_s *lower_,
                                 struct timespec *ts)
{
  struct xoneshot_lowerhalf_s *lower =
    (struct xoneshot_lowerhalf_s *)lower_;
  irqstate_t flags;

  flags = spin_lock_irqsave(&lower->lock);

  up_disable_irq(lower->irq);

  spin_unlock_irqrestore(&lower->lock, flags);

  return 0;
}

static int xtensa_oneshot_current(struct oneshot_lowerhalf_s *lower_,
                                  struct timespec *ts)
{
  struct xoneshot_lowerhalf_s *lower =
    (struct xoneshot_lowerhalf_s *)lower_;
  clkcnt_t count;
  uint64_t left;

  count = xtensa_oneshot_get_count(lower);

  ts->tv_sec  = count / lower->freq;
  left        = count - (uint64_t)ts->tv_sec * lower->freq;
  ts->tv_nsec = NSEC_PER_SEC * left / lower->freq;

  return 0;
}

static int xtensa_oneshot_maxdelay(struct oneshot_lowerhalf_s *lower_,
                                   struct timespec *ts)
{
  struct xoneshot_lowerhalf_s *lower =
    (struct xoneshot_lowerhalf_s *)lower_;

  uint64_t maxnsec = nsec_from_count(UINT32_MAX, lower->freq);

  ts->tv_sec  = maxnsec / NSEC_PER_SEC;
  ts->tv_nsec = maxnsec % NSEC_PER_SEC;

  return 0;
}

#endif /* CONFIG_ONESHOT_COUNT */

static int xtensa_oneshot_interrupt(int irq, void *context, void *arg)
{
  struct xoneshot_lowerhalf_s *lower = arg;

  DEBUGASSERT(lower != NULL);

  oneshot_process_callback(&lower->lh);

  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

struct oneshot_lowerhalf_s *
xtensa_oneshot_initialize(uint32_t irq, uint32_t freq)
{
  struct xoneshot_lowerhalf_s *lower = kmm_zalloc(sizeof(*lower));

  if (lower == NULL)
    {
      return NULL;
    }

  lower->lh.ops = &g_xtensa_oneshot_ops;
  lower->freq   = freq;
  lower->irq    = irq;

#ifdef CONFIG_ONESHOT_COUNT
  oneshot_count_init(&lower->lh, freq);
#endif

  spin_lock_init(&lower->lock);
  irq_attach(irq, xtensa_oneshot_interrupt, lower);

  return (struct oneshot_lowerhalf_s *)lower;
}
