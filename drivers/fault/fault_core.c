/****************************************************************************
 * drivers/fault/fault_core.c
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

#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <debug.h>

#include <nuttx/arch.h>
#include <nuttx/atomic.h>
#include <nuttx/clock.h>
#include <nuttx/crc16.h>
#include <nuttx/irq.h>
#include <nuttx/lib/lib.h>
#include <nuttx/list.h>
#include <nuttx/note/note_driver.h>
#include <nuttx/percpu.h>
#include <nuttx/sched.h>
#include <nuttx/sched_note.h>
#include <nuttx/spinlock.h>
#include <nuttx/tag.h>

#include <nuttx/fault_inject.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* 32-bit FNV-1a hash of the registered name; used as the stable point_id
 * carried in audit records.
 */

#define FAULT_FNV_OFFSET            0x811c9dc5u
#define FAULT_FNV_PRIME             0x01000193u

/* Audit records are emitted under NXTAG_SAFETY so they share the safety
 * event timeline (injection point <-> subsequent safety reaction align on
 * one stream).  LOG_INFO keeps the records visible under default filter
 * masks without flooding higher priorities used by safety code.
 */

#define FAULT_AUDIT_LEVEL           LOG_INFO

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef void (*fault_inject_iter_t)(FAR struct fault_point_s *fp,
                                    FAR void *priv);

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* All registered runtime points live on this list.  Mutated under
 * g_fault_lock from any context (IRQ-safe spinlock).
 */

static struct list_node g_fault_list = LIST_INITIAL_VALUE(g_fault_list);
static spinlock_t       g_fault_lock = SP_UNLOCKED;
static atomic_t         g_fault_seq;
static atomic_t         g_fault_init_state;     /* 0=unstarted 1=started 2=done */

/* The build-time metadata records are collected into an iterable section
 * (see include/nuttx/iterable_sections.h).  The board linker script must
 * include <nuttx/linker/common-rom.ld> to define the boundary symbols.
 */

STRUCT_SECTION_DECLARE(fault_point_meta_s);

/* Standby RAM section boundaries (provided by board linker script). */

#ifdef CONFIG_DRIVERS_FAULT_INJECT_STANDBY
extern char __start_fi_standby[] weak_data;
extern char __stop_fi_standby[] weak_data;
#endif

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/* Audit */

static void fault_audit_emit_seq(FAR struct fault_point_s *fp,
                                 FAR const struct fault_config_s *cfg,
                                 uint8_t flags, uint32_t seq);

/* Action executors */

static void fault_action_dispatch(FAR struct fault_point_s *fp,
                                  FAR const struct fault_config_s *cfg,
                                  FAR void *user_arg);

/* External: fault_dev.c */

void fault_dev_register(FAR struct fault_point_s *fp);
int  fault_dev_init_control(void);

/* Forward declaration (defined later in this file) */

uint32_t fault_inject_point_id(FAR const struct fault_point_s *fp);

/****************************************************************************
 * Private Functions — Standby RAM
 ****************************************************************************/

#ifdef CONFIG_DRIVERS_FAULT_INJECT_STANDBY

/****************************************************************************
 * Name: fault_is_standby_point
 ****************************************************************************/

static bool fault_is_standby_point(FAR struct fault_point_s *fp)
{
  /* Use volatile pointers to prevent the compiler from optimizing away
   * the NULL check.  For weak array symbols, &array[0] is the symbol
   * value itself — but without volatile the compiler may assume "address
   * of an array is never NULL" and eliminate the branch.
   */

  FAR volatile const char *start = __start_fi_standby;
  FAR volatile const char *stop  = __stop_fi_standby;

  if (start == NULL || stop == NULL)
    {
      return false;
    }

  return (FAR const char *)fp >= (FAR const char *)start &&
         (FAR const char *)fp <  (FAR const char *)stop;
}

/****************************************************************************
 * Name: fault_standby_validate
 *
 * Description:
 *   Validate a standby-RAM fault point after a (warm) reset.
 *
 *   The persisted config is protected by magic + CRC16.  On a valid point
 *   the config AND stats are preserved: this is what lets an early-boot
 *   ONESHOT point remember "already injected once" across the reset it
 *   triggered, so the second boot does not fire again.  stats itself is
 *   not CRC-protected; magic acts as a weak validity guard for the whole
 *   standby block.
 *
 *   Runtime linkage fields (meta/node/lock/fds/registered) reference the
 *   previous boot's address space and are ALWAYS rebuilt, regardless of
 *   validity, so the point re-enters the fresh global list in
 *   fault_inject_populate().
 *
 ****************************************************************************/

static void fault_standby_validate(FAR struct fault_point_s *fp)
{
  if (fp->magic != FAULT_POINT_MAGIC ||
      fp->crc != crc16((FAR const uint8_t *)&fp->config,
                       sizeof(fp->config)))
    {
      /* Invalid data: cold boot / firmware change / corruption.  Drop the
       * persisted config, enabled state and stats entirely.
       */

      fp->magic   = 0;
      fp->crc     = 0;
      fp->enabled = 0;
      memset(&fp->config, 0, sizeof(fp->config));
      memset(&fp->stats, 0, sizeof(fp->stats));
    }

  /* Runtime linkage is never valid across a reset; rebuild unconditionally
   * so the (surviving) point is re-registered onto the fresh global list.
   * Note: registered lives in NOLOAD standby RAM and would otherwise retain
   * the previous boot's value, short-circuiting re-registration.
   */

  fp->registered = 0;
  fp->meta       = NULL;
  fp->fds        = NULL;
}

#endif /* CONFIG_DRIVERS_FAULT_INJECT_STANDBY */

/****************************************************************************
 * Private Functions — Audit
 ****************************************************************************/

/****************************************************************************
 * Name: fault_audit_fill
 ****************************************************************************/

static void fault_audit_fill(FAR struct fault_audit_record_s *rec,
                             FAR struct fault_point_s *fp,
                             FAR const struct fault_config_s *cfg,
                             uint8_t flags, uint32_t seq)
{
  memset(rec, 0, sizeof(*rec));

  rec->ts_us     = (uint64_t)TICK2USEC(clock_systime_ticks());
  rec->point_id  = fault_inject_point_id(fp);
  rec->seq       = seq;
  rec->pid       = _SCHED_GETPID();
  rec->action    = cfg->action;
  rec->trig_mode = cfg->trig_mode;
  rec->cpu       = (uint8_t)up_cpu_index();
  rec->flags     = flags;

  switch (cfg->action)
    {
      case FAULT_ACTION_RETURN_ERR:
        rec->params_snapshot.err_code = cfg->params.err_code;
        break;

      case FAULT_ACTION_DELAY:
        rec->params_snapshot.delay_us = cfg->params.delay_us;
        break;

      case FAULT_ACTION_MEMTAMPER:
        rec->params_snapshot.addr = cfg->params.memtamper.addr;
        break;

      case FAULT_ACTION_EXCEPTION:
        rec->params_snapshot.vector = cfg->params.exception.vector;
        break;

      default:
        break;
    }
}

/****************************************************************************
 * Name: fault_audit_emit_seq
 ****************************************************************************/

static void fault_audit_emit_seq(FAR struct fault_point_s *fp,
                                 FAR const struct fault_config_s *cfg,
                                 uint8_t flags, uint32_t seq)
{
#ifdef CONFIG_DRIVERS_FAULT_INJECT_AUDIT
  struct fault_audit_record_s rec;

  if (fp == NULL || cfg == NULL)
    {
      return;
    }

  fault_audit_fill(&rec, fp, cfg, flags, seq);

  sched_note_event(NXTAG_SAFETY, FAULT_AUDIT_LEVEL,
                   NOTE_DUMP_BINARY, &rec, sizeof(rec));
#else
  UNUSED(fp);
  UNUSED(cfg);
  UNUSED(flags);
  UNUSED(seq);
#endif
}

/****************************************************************************
 * Private Functions — Action Executors
 ****************************************************************************/

/****************************************************************************
 * Name: fault_act_memtamper
 ****************************************************************************/

static void fault_act_memtamper(FAR const struct fault_config_s *cfg)
{
  FAR volatile uint8_t *p =
    (FAR volatile uint8_t *)cfg->params.memtamper.addr;
  uint32_t  size          = cfg->params.memtamper.size;
  uint64_t  mask          = cfg->params.memtamper.mask;
  uint64_t  value         = cfg->params.memtamper.value;
  uint32_t  i;

  if (p == NULL || size == 0)
    {
      return;
    }

  /* Overwrite the masked bits with the supplied value, byte-wise:
   *   new = (old & ~mask) | (value & mask)
   * A hardware register is just an MMIO address, so this single action
   * covers both RAM and register tampering (use size = 4 for a 32-bit
   * register).
   */

  for (i = 0; i < size; i++)
    {
      uint8_t m = (uint8_t)(mask  >> ((i & 7) * 8));
      uint8_t v = (uint8_t)(value >> ((i & 7) * 8));

      p[i] = (uint8_t)((p[i] & ~m) | (v & m));
    }
}

/****************************************************************************
 * Name: fault_arch_trigger_exception (weak default)
 ****************************************************************************/

void weak_function fault_arch_trigger_exception(uint32_t vector)
{
  /* Default: write to unmapped address (ARM Cortex-M/R System space).
   * Architecture or board code can override this function to provide
   * a platform-appropriate mechanism (e.g. __builtin_trap(), EBREAK,
   * or triggering a specific exception vector).
   */

  FAR volatile uint32_t *addr;

  UNUSED(vector);

  addr = (FAR volatile uint32_t *)0xfffffffful;
  *addr = 0xdeadbeefu;
}

/****************************************************************************
 * Name: fault_act_exception
 ****************************************************************************/

static void fault_act_exception(FAR const struct fault_config_s *cfg)
{
  fault_arch_trigger_exception(cfg->params.exception.vector);
}

/****************************************************************************
 * Name: fault_act_custom
 ****************************************************************************/

static void fault_act_custom(FAR const struct fault_config_s *cfg,
                             FAR void *user_arg)
{
  fault_custom_cb_t cb = (fault_custom_cb_t)cfg->params.custom.cb;

  if (cb != NULL)
    {
      cb((FAR void *)cfg->params.custom.arg, user_arg);
    }
}

/****************************************************************************
 * Name: fault_action_dispatch
 ****************************************************************************/

static void fault_action_dispatch(FAR struct fault_point_s *fp,
                                  FAR const struct fault_config_s *cfg,
                                  FAR void *user_arg)
{
  UNUSED(fp);

  switch (cfg->action)
    {
      case FAULT_ACTION_DELAY:
        up_udelay(cfg->params.delay_us);
        break;

      case FAULT_ACTION_MEMTAMPER:
        fault_act_memtamper(cfg);
        break;

      case FAULT_ACTION_EXCEPTION:
        fault_act_exception(cfg);
        break;

      case FAULT_ACTION_CUSTOM:
        fault_act_custom(cfg, user_arg);
        break;

      case FAULT_ACTION_RETURN_ERR:
      case FAULT_ACTION_RETURN_NULL:
      case FAULT_ACTION_SKIP:
        ferr("fault_inject: action %d is wrapper-only\n", cfg->action);
        break;

      default:
        ferr("fault_inject: unknown action %d\n", cfg->action);
        break;
    }
}

/****************************************************************************
 * Private Functions — Policy Engine
 ****************************************************************************/

/****************************************************************************
 * Name: fault_hash_name
 ****************************************************************************/

static uint32_t fault_hash_name(FAR const char *s)
{
  uint32_t h = FAULT_FNV_OFFSET;

  while (*s)
    {
      h ^= (uint32_t)(uint8_t)*s++;
      h *= FAULT_FNV_PRIME;
    }

  return h;
}

/****************************************************************************
 * Name: fault_random
 ****************************************************************************/

static uint32_t fault_random(void)
{
  /* nrand() uses per-task state (task_get_info) making it safe for
   * concurrent callers without external locking.  It also works in
   * interrupt context because tls_get_info resolves to the interrupted
   * task's TLS.
   */

  return (uint32_t)nrand(FAULT_PROB_MAX);
}

/****************************************************************************
 * Name: fault_filter_passes
 ****************************************************************************/

static bool fault_filter_passes(FAR const struct fault_config_s *cfg,
                                FAR const void *obj)
{
  if (cfg->filter_pid != 0 && cfg->filter_pid != _SCHED_GETPID())
    {
      return false;
    }

  /* filter_irq: <= 0 means "any context".  > 0 means the injection may
   * only fire from interrupt context (a specific IRQ number is carried in
   * the field but the generic driver layer cannot cheaply resolve the
   * currently-serviced vector, so the check is limited to context).  Zero
   * is treated as "any" to stay consistent with the other filter fields
   * and keep a zero-initialised config firing in thread context.
   */

  if (cfg->filter_irq > 0 && !up_interrupt_context())
    {
      return false;
    }

  /* filter_obj: 0 means "any object".  Otherwise the caller-supplied
   * object pointer must match the configured value.  The wrapper paths
   * pass obj == NULL and therefore only pass when filter_obj is 0.
   */

  if (cfg->filter_obj != 0 &&
      cfg->filter_obj != (uintptr_t)obj)
    {
      return false;
    }

  return true;
}

/****************************************************************************
 * Name: fault_trig_evaluate
 ****************************************************************************/

static bool fault_trig_evaluate(FAR struct fault_point_s *fp,
                                FAR struct fault_config_s *cfg)
{
  uint64_t call = fp->stats.call_count;

  switch (cfg->trig_mode)
    {
      case FAULT_TRIG_DISABLED:
        return false;

      case FAULT_TRIG_ALWAYS:
        break;

      case FAULT_TRIG_ONESHOT:
        if (fp->stats.inject_count != 0)
          {
            return false;
          }
        break;

      case FAULT_TRIG_COUNT:
        if (cfg->interval == 0)
          {
            return false;
          }

        if ((call % cfg->interval) != 0)
          {
            return false;
          }
        break;

      case FAULT_TRIG_RANDOM:
        if (cfg->probability == 0)
          {
            return false;
          }

        if (fault_random() >= cfg->probability)
          {
            return false;
          }
        break;

      default:
        return false;
    }

  if (cfg->times >= 0)
    {
      if (cfg->times == 0)
        {
          return false;
        }

      cfg->times--;
    }

  return true;
}

/****************************************************************************
 * Name: fault_decide
 ****************************************************************************/

static bool fault_decide(FAR struct fault_point_s *fp,
                         FAR struct fault_config_s *out,
                         FAR const void *obj)
{
  irqstate_t flags;
  uint32_t   seq;
  bool       fire;

  flags = spin_lock_irqsave(&fp->lock);

  /* Sanity check: reject corrupted config (standby RAM cold boot). */

  if (fp->config.trig_mode > FAULT_TRIG_RANDOM ||
      fp->config.action >= FAULT_ACTION_NR)
    {
      spin_unlock_irqrestore(&fp->lock, flags);
      return false;
    }

  fp->stats.call_count++;

  if (!fault_filter_passes(&fp->config, obj))
    {
      spin_unlock_irqrestore(&fp->lock, flags);
      return false;
    }

  fire = fault_trig_evaluate(fp, &fp->config);
  if (!fire)
    {
      spin_unlock_irqrestore(&fp->lock, flags);
      return false;
    }

  *out = fp->config;
  fp->stats.inject_count++;

  seq = (uint32_t)atomic_add(&g_fault_seq, 1) + 1;
  fp->stats.last_seq = seq;
  fp->stats.last_inject_us = (uint64_t)TICK2USEC(clock_systime_ticks());

  spin_unlock_irqrestore(&fp->lock, flags);

  /* Wake any poll() waiter blocking on this point's device node. */

  poll_notify((FAR struct pollfd **)&fp->fds, 1, POLLIN);

  fault_audit_emit_seq(fp, out, 0, seq);
  return true;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: fault_inject_validate_action
 ****************************************************************************/

int fault_inject_validate_action(FAR const struct fault_point_s *fp,
                                 FAR const struct fault_config_s *cfg)
{
  if (cfg->action >= FAULT_ACTION_NR)
    {
      return -EINVAL;
    }

  if (fp->meta != NULL &&
      ((fp->meta->supported & (1u << cfg->action)) == 0))
    {
      return -ENOTSUP;
    }

  if (cfg->probability > FAULT_PROB_MAX)
    {
      return -EINVAL;
    }

  if (cfg->trig_mode > FAULT_TRIG_RANDOM)
    {
      return -EINVAL;
    }

  return 0;
}

/****************************************************************************
 * Name: fault_inject_register
 ****************************************************************************/

void fault_inject_register(FAR struct fault_point_s *fp,
                           FAR const struct fault_point_meta_s *meta)
{
  irqstate_t flags;
  bool       need_dev = false;

  flags = spin_lock_irqsave(&g_fault_lock);

  if (!fp->registered)
    {
      fp->meta = meta;
      spin_lock_init(&fp->lock);
      list_initialize(&fp->node);
      list_add_tail(&g_fault_list, &fp->node);
      fp->registered = 1;
      need_dev = !up_interrupt_context();
    }

  spin_unlock_irqrestore(&g_fault_lock, flags);

  if (need_dev)
    {
      fault_dev_register(fp);
    }
}

/****************************************************************************
 * Name: fault_inject_wrap_int
 ****************************************************************************/

bool fault_inject_wrap_int(FAR struct fault_point_s *fp,
                           int err_default,
                           FAR int *err_out)
{
  struct fault_config_s cfg;

  if (!fault_decide(fp, &cfg, NULL))
    {
      return false;
    }

  switch (cfg.action)
    {
      case FAULT_ACTION_RETURN_ERR:
        *err_out = cfg.params.err_code;
        return true;

      case FAULT_ACTION_DELAY:
        up_udelay(cfg.params.delay_us);
        return false;

      case FAULT_ACTION_SKIP:
        *err_out = 0;
        return true;

      default:
        *err_out = err_default;
        return true;
    }
}

/****************************************************************************
 * Name: fault_inject_wrap_ptr
 ****************************************************************************/

bool fault_inject_wrap_ptr(FAR struct fault_point_s *fp,
                           FAR void * FAR *ptr_out)
{
  struct fault_config_s cfg;

  if (!fault_decide(fp, &cfg, NULL))
    {
      return false;
    }

  switch (cfg.action)
    {
      case FAULT_ACTION_RETURN_NULL:
        *ptr_out = NULL;
        return true;

      case FAULT_ACTION_DELAY:
        up_udelay(cfg.params.delay_us);
        return false;

      default:
        *ptr_out = NULL;
        return true;
    }
}

/****************************************************************************
 * Name: fault_inject_wrap_skip
 ****************************************************************************/

bool fault_inject_wrap_skip(FAR struct fault_point_s *fp)
{
  struct fault_config_s cfg;

  if (!fault_decide(fp, &cfg, NULL))
    {
      return false;
    }

  if (cfg.action == FAULT_ACTION_DELAY)
    {
      up_udelay(cfg.params.delay_us);
      return false;
    }

  return cfg.action == FAULT_ACTION_SKIP;
}

/****************************************************************************
 * Name: fault_inject_act_embedded
 ****************************************************************************/

void fault_inject_act_embedded(FAR struct fault_point_s *fp,
                               FAR void *user_arg)
{
  struct fault_config_s cfg;

  if (!fault_decide(fp, &cfg, user_arg))
    {
      return;
    }

  fault_action_dispatch(fp, &cfg, user_arg);
}

/****************************************************************************
 * Name: fault_inject_active_trigger
 ****************************************************************************/

int fault_inject_active_trigger(FAR struct fault_point_s *fp)
{
  irqstate_t flags;
  struct fault_config_s cfg;
  uint32_t   seq;

  flags = spin_lock_irqsave(&fp->lock);

  if (fp->config.trig_mode == FAULT_TRIG_DISABLED)
    {
      spin_unlock_irqrestore(&fp->lock, flags);
      return -ENODEV;
    }

  cfg = fp->config;
  fp->stats.call_count++;
  fp->stats.inject_count++;
  seq = (uint32_t)atomic_add(&g_fault_seq, 1) + 1;
  fp->stats.last_seq = seq;
  fp->stats.last_inject_us = (uint64_t)TICK2USEC(clock_systime_ticks());

  spin_unlock_irqrestore(&fp->lock, flags);

  /* Wake any poll() waiter blocking on this point's device node. */

  poll_notify((FAR struct pollfd **)&fp->fds, 1, POLLIN);

  fault_audit_emit_seq(fp, &cfg, FAULT_AUDIT_FLAG_ACTIVE, seq);
  fault_action_dispatch(fp, &cfg, NULL);

  return OK;
}

/****************************************************************************
 * Name: fault_inject_lookup
 ****************************************************************************/

FAR struct fault_point_s *fault_inject_lookup(FAR const char *name)
{
  FAR struct fault_point_s *fp;
  irqstate_t flags;

  flags = spin_lock_irqsave(&g_fault_lock);

  list_for_every_entry(&g_fault_list, fp, struct fault_point_s, node)
    {
      if (fp->meta != NULL && strcmp(fp->meta->name, name) == 0)
        {
          spin_unlock_irqrestore(&g_fault_lock, flags);
          return fp;
        }
    }

  spin_unlock_irqrestore(&g_fault_lock, flags);
  return NULL;
}

/****************************************************************************
 * Name: fault_inject_for_each
 ****************************************************************************/

void fault_inject_for_each(fault_inject_iter_t cb, FAR void *priv)
{
  FAR struct fault_point_s *fp;

  /* The global list is append-only (points are never removed), so we can
   * iterate without holding g_fault_lock.  This avoids disabling interrupts
   * for the entire traversal, which could violate ISR-latency budgets when
   * the point count is large.  Callers that mutate per-point state (e.g.
   * CLEAR_ALL) must take fp->lock inside the callback.
   */

  list_for_every_entry(&g_fault_list, fp, struct fault_point_s, node)
    {
      cb(fp, priv);
    }
}

/****************************************************************************
 * Name: fault_inject_point_id
 ****************************************************************************/

uint32_t fault_inject_point_id(FAR const struct fault_point_s *fp)
{
  if (fp == NULL || fp->meta == NULL || fp->meta->name == NULL)
    {
      return 0;
    }

  return fault_hash_name(fp->meta->name);
}

/****************************************************************************
 * Name: fault_inject_link_reaction
 ****************************************************************************/

void fault_inject_link_reaction(uint32_t seq, uint32_t reaction_event)
{
#ifdef CONFIG_DRIVERS_FAULT_INJECT_AUDIT
  struct fault_audit_record_s rec;

  memset(&rec, 0, sizeof(rec));
  rec.ts_us = (uint64_t)TICK2USEC(clock_systime_ticks());
  rec.seq   = seq;
  rec.flags = FAULT_AUDIT_FLAG_REACTION;
  rec.pid   = _SCHED_GETPID();
  rec.cpu   = (uint8_t)up_cpu_index();
  rec.params_snapshot.vector = reaction_event;

  sched_note_event(NXTAG_SAFETY, FAULT_AUDIT_LEVEL,
                   NOTE_DUMP_BINARY, &rec, sizeof(rec));
#else
  UNUSED(seq);
  UNUSED(reaction_event);
#endif
}

/****************************************************************************
 * Name: fault_inject_populate
 *
 * Description:
 *   Walk every fault_point_meta_s entry emitted in the linker section,
 *   validate standby-RAM points, register all points on the global list,
 *   and register the matching /dev/fault/<group>/<leaf> character device.
 *
 *   Zero dynamic memory allocation — all fault_point_s instances are
 *   statically declared at their call sites.
 *
 ****************************************************************************/

static void fault_inject_populate(void)
{
  FAR struct fault_point_meta_s *p;
  FAR struct fault_point_s *fp;
  int32_t expected;
  bool    elected;

  /* BMP bring-up election.  One CPU registers; others wait. */

  expected = 0;
  elected  = atomic_try_cmpxchg(&g_fault_init_state, &expected, 1);

  if (elected)
    {
      STRUCT_SECTION_FOREACH(fault_point_meta_s, p)
        {
          fp = p->point;
          if (fp == NULL)
            {
              continue;
            }

#ifdef CONFIG_DRIVERS_FAULT_INJECT_STANDBY
          /* Validate + rebuild runtime linkage for standby points BEFORE
           * the registered check.  registered may be a stale value left in
           * NOLOAD standby RAM from the previous boot;
           * fault_standby_validate() clears it so the point below
           * re-registers correctly.
           */

          if (fault_is_standby_point(fp))
            {
              fault_standby_validate(fp);
            }
#endif

          if (fp->registered)
            {
              continue;
            }

          fp->meta = p;
          spin_lock_init(&fp->lock);
          list_initialize(&fp->node);
          list_add_tail(&g_fault_list, &fp->node);
          fp->registered = 1;
        }

      atomic_set_release(&g_fault_init_state, 2);
    }
  else
    {
      while (atomic_read_acquire(&g_fault_init_state) != 2)
        {
          /* Busy-wait; scheduling not yet active on secondary CPUs. */
        }
    }

  /* Per-CPU side-effect: register device nodes on this core's VFS. */

  STRUCT_SECTION_FOREACH(fault_point_meta_s, p)
    {
      if (p->point != NULL)
        {
          fault_dev_register(p->point);
        }
    }
}

/****************************************************************************
 * Name: fault_inject_init
 *
 * Description:
 *   Boot-time entry called from drivers_initialize().  Validates standby
 *   points, registers all points on the global list, and creates the
 *   singleton /dev/fault/control device.  Zero dynamic memory allocation
 *   for fault points themselves.
 *
 ****************************************************************************/

void fault_inject_init(void)
{
  int ret;

  fault_inject_populate();

  ret = fault_dev_init_control();
  if (ret < 0)
    {
      finfo("fault_inject: control node registration failed (%d)\n", ret);
    }
}
