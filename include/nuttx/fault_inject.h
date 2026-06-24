/****************************************************************************
 * include/nuttx/fault_inject.h
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

#ifndef __INCLUDE_NUTTX_FAULT_INJECT_H
#define __INCLUDE_NUTTX_FAULT_INJECT_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>
#include <stdbool.h>
#include <stdint.h>

#include <nuttx/compiler.h>
#include <nuttx/fs/ioctl.h>
#include <nuttx/iterable_sections.h>
#include <nuttx/list.h>
#include <nuttx/spinlock.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Fault-injection device path layout.  Each registered injection point is
 * exposed as /dev/fault/<group>/<name>.  A single global control
 * node at /dev/fault/control supports the LIST_ALL / CLEAR_ALL
 * ioctls and the audit dump entry point.
 */

#define FAULT_INJECT_DEV_DIR        "/dev/fault"
#define FAULT_INJECT_CTRL_PATH      "/dev/fault/control"
#define FAULT_INJECT_DEV_PATH_MAX   64

/* Trigger modes (config.trig_mode) */

#define FAULT_TRIG_DISABLED         0
#define FAULT_TRIG_ALWAYS           1
#define FAULT_TRIG_ONESHOT          2
#define FAULT_TRIG_COUNT            3
#define FAULT_TRIG_RANDOM           4

/* Probability units: parts-per-thousand (permille).  1000 == 100%. */

#define FAULT_PROB_MAX              1000u

/* Maximum length of a registered injection-point name (group + leaf). */

#define FAULT_INJECT_NAME_MAX       48
#define FAULT_INJECT_GROUP_MAX      16

/* Audit record flag bits */

#define FAULT_AUDIT_FLAG_ACTIVE     (1u << 0)  /* 0 = passive, 1 = active */
#define FAULT_AUDIT_FLAG_REACTION   (1u << 1)  /* event is a reaction link */

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* The 8 built-in fault actions. */

enum fault_action_e
{
  FAULT_ACTION_RETURN_ERR  = 0,  /* Return errno (negative int) */
  FAULT_ACTION_RETURN_NULL = 1,  /* Return NULL pointer */
  FAULT_ACTION_DELAY       = 2,  /* up_udelay(), then continue */
  FAULT_ACTION_SKIP        = 3,  /* Skip the wrapped call */
  FAULT_ACTION_MEMTAMPER   = 4,  /* Tamper a memory / MMIO location */
  FAULT_ACTION_EXCEPTION   = 5,  /* Raise hard fault / bus error */
  FAULT_ACTION_CUSTOM      = 6,  /* Invoke a user-supplied callback */
  FAULT_ACTION_NR
};

/* Per-action callback prototype (CUSTOM action). */

typedef int (*fault_custom_cb_t)(FAR void *fp, FAR void *arg);

/* Build-time metadata for one injection point.  Instances are emitted into
 * an iterable section (STRUCT_SECTION_ITERABLE) by the macro family below
 * and discovered at boot.
 *
 * `point` directly references the static fault_point_s instance declared
 * at the call site.  The hot path reads point->enabled with a single load
 * (no double indirection).
 */

struct fault_point_meta_s
{
  FAR const char *name;                       /* "group.leaf"             */
  FAR const char *group;                      /* "mm", "irq", "safety"    */
  FAR const char *file;                       /* __FILE__ at site         */
  int             line;                       /* __LINE__ at site         */
  uint32_t        supported;                  /* (1u << action) bitmask   */
  FAR struct fault_point_s *point;            /* static fault_point_s     */
};

/* Runtime configuration written via FAULTIOC_SET_CONFIG.  Designed to be
 * a fixed-size POD so it can be marshalled over ioctl().
 */

struct fault_config_s
{
  uint8_t  trig_mode;               /* FAULT_TRIG_* */
  uint8_t  action;                  /* enum fault_action_e */
  uint8_t  reserved[2];

  /* Cheap-first filter chain (Linux fault_attr style). */

  uint32_t probability;             /* 0..FAULT_PROB_MAX */
  uint32_t interval;                /* fire every N calls (0 == off)      */
  int32_t  times;                   /* -1 == unbounded                    */
  pid_t    filter_pid;              /* 0 == any                           */
  int32_t  filter_irq;              /* <= 0 == any, > 0 == IRQ context    */
  uintptr_t filter_obj;             /* 0 == any                           */

  /* Action parameters (union, fixed size). */

  union
  {
    int32_t  err_code;              /* RETURN_ERR    */
    uint32_t delay_us;              /* DELAY         */
    struct
    {
      uintptr_t addr;                 /* Target memory / MMIO address    */
      uint32_t  size;                 /* Byte count to tamper            */
      uint64_t  mask;                 /* Which bits to overwrite         */
      uint64_t  value;                /* Value for the masked bits       */

      /* Applied byte-wise: new = (old & ~mask) | (value & mask).
       * A register is just an MMIO address, so this action covers both
       * RAM and registers (use size = 4 for a 32-bit register).
       */
    } memtamper;                    /* MEMTAMPER     */
    struct
    {
      uint32_t  vector;
    } exception;                    /* EXCEPTION     */
    struct
    {
      uintptr_t cb;
      uintptr_t arg;
    } custom;                       /* CUSTOM        */
  } params;
};

/* Runtime counters returned by FAULTIOC_GET_STATS. */

struct fault_stats_s
{
  uint64_t call_count;              /* Times the check ran                */
  uint64_t inject_count;            /* Times an action actually fired     */
  uint64_t last_inject_us;          /* Wall-time of last injection (us)   */
  uint32_t last_seq;                /* Sequence number of last injection  */
};

/* Audit record emitted into sched_note on every injection. */

struct fault_audit_record_s
{
  uint64_t ts_us;                   /* Microsecond timestamp              */
  uint32_t point_id;                /* Hash of point->meta->name          */
  uint32_t seq;                     /* Monotonic sequence number          */
  pid_t    pid;                     /* Current task PID                   */
  uint8_t  action;                  /* enum fault_action_e                */
  uint8_t  trig_mode;               /* FAULT_TRIG_* that fired            */
  uint8_t  cpu;
  uint8_t  flags;                   /* FAULT_AUDIT_FLAG_*                 */
  union
  {
    int32_t   err_code;
    uint32_t  delay_us;
    uintptr_t addr;
    uint32_t  vector;
  } params_snapshot;
};

/* Runtime point structure (referenced by the injection macros).  Defined
 * here, after the fault_config_s / fault_stats_s types it embeds.
 */

#ifdef CONFIG_DRIVERS_FAULT_INJECT

struct fault_point_s
{
  uint16_t               magic;       /* FAULT_POINT_MAGIC (standby valid) */
  uint16_t               crc;         /* CRC16 of config (integrity check) */
  FAR const struct fault_point_meta_s *meta;
  volatile uint8_t       enabled;     /* Hot-path short circuit       */
  uint8_t                registered;
  uint8_t                reserved[2];
  struct fault_config_s  config;      /* Embedded config storage      */
  struct fault_stats_s   stats;       /* Embedded stats storage       */
  spinlock_t             lock;
  struct list_node       node;
  FAR void              *fds;         /* Internal: pollfd anchor      */
};

#  define FAULT_POINT_S_INITIALIZER  { 0 }

/* Magic value written alongside config to validate standby RAM content. */

#define FAULT_POINT_MAGIC  0x4650u  /* "FP" */

#endif /* CONFIG_DRIVERS_FAULT_INJECT */

/* Variable-size payload for FAULTIOC_LIST_ALL.  The caller supplies
 * a `count` element entries[] array; the kernel fills as many as it
 * can and returns the total via `total`.
 */

struct fault_entry_s
{
  char     name[FAULT_INJECT_NAME_MAX];
  char     group[FAULT_INJECT_GROUP_MAX];
  uint32_t supported;
  uint8_t  enabled;
  uint8_t  trig_mode;
  uint8_t  action;
  uint8_t  reserved;
};

struct fault_array_s
{
  uint32_t count;                   /* IN: entries[] capacity             */
  uint32_t total;                   /* OUT: total points in the system    */
  struct fault_entry_s entries[1];
};

/* Argument for FAULTIOC_GET_META. */

struct fault_meta_query_s
{
  struct fault_point_meta_s meta;
  char file_storage[64];
  char name_storage[FAULT_INJECT_NAME_MAX];
  char group_storage[FAULT_INJECT_GROUP_MAX];
};

/****************************************************************************
 * ioctl Command Codes
 ****************************************************************************/

#define _FAULTIOC(nr)               _IOC(_FAULTBASE, nr)

#define FAULTIOC_GET_CONFIG         _FAULTIOC(0x80)
#define FAULTIOC_SET_CONFIG         _FAULTIOC(0x81)
#define FAULTIOC_ENABLE             _FAULTIOC(0x82)
#define FAULTIOC_TRIGGER            _FAULTIOC(0x83)
#define FAULTIOC_GET_STATS          _FAULTIOC(0x84)
#define FAULTIOC_RESET              _FAULTIOC(0x85)
#define FAULTIOC_GET_META           _FAULTIOC(0x86)
#define FAULTIOC_CLEAR_STATS        _FAULTIOC(0x87)

/* The following commands are only honoured on the global control node. */

#define FAULTIOC_LIST_ALL           _FAULTIOC(0x90)
#define FAULTIOC_CLEAR_ALL          _FAULTIOC(0x91)
#define FAULTIOC_AUDIT_LINK         _FAULTIOC(0x92)

/****************************************************************************
 * Injection-point declaration macros
 ****************************************************************************/

#ifdef CONFIG_DRIVERS_FAULT_INJECT

/* Internal helper: declare a static fault_point_s + metadata record for
 * one injection site.  `_fp` is the fault_point_s instance in BSS;
 * `_meta` is the metadata record emitted into the linker section.
 * Both are block-scope static, distinct per site.
 */

#  define FAULT_INJECT_DECLARE_SITE(_fp, _meta, _name, _group, _supported)   \
  static struct fault_point_s _fp used_data;                                  \
  static const STRUCT_SECTION_ITERABLE(fault_point_meta_s, _meta) =           \
  {                                                                           \
    .name      = (_name),                                                     \
    .group     = (_group),                                                    \
    .file      = __FILE__,                                                    \
    .line      = __LINE__,                                                    \
    .supported = (_supported),                                                \
    .point     = &_fp,                                                        \
  }

/* Standby variant: fault_point_s is placed in the .fi_standby linker
 * section (NOLOAD, survives warm reset).  Falls back to normal BSS when
 * CONFIG_DRIVERS_FAULT_INJECT_STANDBY is not enabled.
 */

#  define FAULT_INJECT_STANDBY_SECTION ".fi_standby"

#  ifdef CONFIG_DRIVERS_FAULT_INJECT_STANDBY

#    define FAULT_INJECT_DECLARE_SITE_STANDBY(_fp, _meta, _name, _group,     \
                                              _supported)                     \
  static struct fault_point_s _fp                                             \
    locate_data(FAULT_INJECT_STANDBY_SECTION) used_data;                      \
  static const STRUCT_SECTION_ITERABLE(fault_point_meta_s, _meta) =           \
  {                                                                           \
    .name      = (_name),                                                     \
    .group     = (_group),                                                    \
    .file      = __FILE__,                                                    \
    .line      = __LINE__,                                                    \
    .supported = (_supported),                                                \
    .point     = &_fp,                                                        \
  }

#  else /* !CONFIG_DRIVERS_FAULT_INJECT_STANDBY */
#    define FAULT_INJECT_DECLARE_SITE_STANDBY FAULT_INJECT_DECLARE_SITE
#  endif

/* Embedded-site helper.  `_invoke` is the expression the macro runs when
 * the framework decides to fire.
 */

#  define FAULT_INJECT_SITE(_name, _group, _supported, _invoke)              \
  do                                                                         \
    {                                                                        \
      FAULT_INJECT_DECLARE_SITE(__fi_fp, __fi_meta,                          \
                                _name, _group, _supported);                  \
      if (predict_false(__fi_fp.enabled))                                    \
        {                                                                    \
          _invoke;                                                           \
        }                                                                    \
    }                                                                        \
  while (0)

#  define FAULT_INJECT_SITE_STANDBY(_name, _group, _supported, _invoke)      \
  do                                                                         \
    {                                                                        \
      FAULT_INJECT_DECLARE_SITE_STANDBY(__fi_fp, __fi_meta,                  \
                                        _name, _group, _supported);          \
      if (predict_false(__fi_fp.enabled))                                    \
        {                                                                    \
          _invoke;                                                           \
        }                                                                    \
    }                                                                        \
  while (0)

/* ===== Wrapper-path macros (called inside generated __wrap_*) ======== */

#  define FAULT_INJECT_WRAP_RETVAL(_group, _fn, _err_default, _realcall,     \
                                   _out)                                      \
  do                                                                         \
    {                                                                         \
      FAULT_INJECT_DECLARE_SITE(__fi_fp_##_fn, __fi_meta_##_fn,              \
                                #_group "." #_fn,                             \
                                #_group,                                      \
                                (1u << FAULT_ACTION_RETURN_ERR) |             \
                                (1u << FAULT_ACTION_DELAY)      |             \
                                (1u << FAULT_ACTION_SKIP));                   \
      if (predict_false(__fi_fp_##_fn.enabled) &&                            \
          fault_inject_wrap_int(&__fi_fp_##_fn, (_err_default), (_out)))      \
        {                                                                     \
          break;                                                              \
        }                                                                     \
      *(_out) = (_realcall);                                                 \
    }                                                                         \
  while (0)

#  define FAULT_INJECT_WRAP_RETVAL_NULL(_group, _fn, _realcall, _out_ptr)    \
  do                                                                         \
    {                                                                         \
      FAULT_INJECT_DECLARE_SITE(__fi_fp_##_fn, __fi_meta_##_fn,              \
                                #_group "." #_fn,                             \
                                #_group,                                      \
                                (1u << FAULT_ACTION_RETURN_NULL) |            \
                                (1u << FAULT_ACTION_DELAY));                  \
      if (predict_false(__fi_fp_##_fn.enabled) &&                            \
          fault_inject_wrap_ptr(&__fi_fp_##_fn, (_out_ptr)))                  \
        {                                                                     \
          break;                                                              \
        }                                                                     \
      *(_out_ptr) = (_realcall);                                             \
    }                                                                         \
  while (0)

#  define FAULT_INJECT_WRAP_RETVAL_VOID(_group, _fn, _realcall)              \
  do                                                                         \
    {                                                                         \
      FAULT_INJECT_DECLARE_SITE(__fi_fp_##_fn, __fi_meta_##_fn,              \
                                #_group "." #_fn,                             \
                                #_group,                                      \
                                (1u << FAULT_ACTION_SKIP) |                   \
                                (1u << FAULT_ACTION_DELAY));                  \
      if (!(predict_false(__fi_fp_##_fn.enabled) &&                          \
            fault_inject_wrap_skip(&__fi_fp_##_fn)))                          \
        {                                                                     \
          (_realcall);                                                        \
        }                                                                     \
    }                                                                         \
  while (0)

/* Standby variants of wrapper macros (for early-boot paths). */

#  define FAULT_INJECT_WRAP_RETVAL_STANDBY(_group, _fn, _err_default,        \
                                           _realcall, _out)                   \
  do                                                                         \
    {                                                                         \
      FAULT_INJECT_DECLARE_SITE_STANDBY(__fi_fp_##_fn, __fi_meta_##_fn,      \
                                        #_group "." #_fn,                     \
                                        #_group,                              \
                                        (1u << FAULT_ACTION_RETURN_ERR) |     \
                                        (1u << FAULT_ACTION_DELAY)      |     \
                                        (1u << FAULT_ACTION_SKIP));           \
      if (predict_false(__fi_fp_##_fn.enabled) &&                            \
          fault_inject_wrap_int(&__fi_fp_##_fn, (_err_default), (_out)))      \
        {                                                                     \
          break;                                                              \
        }                                                                     \
      *(_out) = (_realcall);                                                 \
    }                                                                         \
  while (0)

#  define FAULT_INJECT_WRAP_RETVAL_NULL_STANDBY(_group, _fn, _realcall,      \
                                                _out_ptr)                     \
  do                                                                         \
    {                                                                         \
      FAULT_INJECT_DECLARE_SITE_STANDBY(__fi_fp_##_fn, __fi_meta_##_fn,      \
                                        #_group "." #_fn,                     \
                                        #_group,                              \
                                        (1u << FAULT_ACTION_RETURN_NULL) |    \
                                        (1u << FAULT_ACTION_DELAY));          \
      if (predict_false(__fi_fp_##_fn.enabled) &&                            \
          fault_inject_wrap_ptr(&__fi_fp_##_fn, (_out_ptr)))                  \
        {                                                                     \
          break;                                                              \
        }                                                                     \
      *(_out_ptr) = (_realcall);                                             \
    }                                                                         \
  while (0)

#  define FAULT_INJECT_WRAP_RETVAL_VOID_STANDBY(_group, _fn, _realcall)      \
  do                                                                         \
    {                                                                         \
      FAULT_INJECT_DECLARE_SITE_STANDBY(__fi_fp_##_fn, __fi_meta_##_fn,      \
                                        #_group "." #_fn,                     \
                                        #_group,                              \
                                        (1u << FAULT_ACTION_SKIP) |           \
                                        (1u << FAULT_ACTION_DELAY));          \
      if (!(predict_false(__fi_fp_##_fn.enabled) &&                          \
            fault_inject_wrap_skip(&__fi_fp_##_fn)))                          \
        {                                                                     \
          (_realcall);                                                        \
        }                                                                     \
    }                                                                         \
  while (0)

#  define FAULT_INJECT_POINT_EXCEPTION(_name, _group)                        \
  FAULT_INJECT_SITE(_name, _group,                                           \
                    (1u << FAULT_ACTION_EXCEPTION),                          \
                    fault_inject_act_embedded(&__fi_fp, NULL))

/* A hardware register is just a memory-mapped address; the MEMTAMPER
 * action covers both RAM and MMIO (use size = 4 for a 32-bit register).
 */

#  define FAULT_INJECT_POINT_MEMTAMPER(_name, _group)                          \
  FAULT_INJECT_SITE(_name, _group,                                           \
                    (1u << FAULT_ACTION_MEMTAMPER),                            \
                    fault_inject_act_embedded(&__fi_fp, NULL))

#  define FAULT_INJECT_POINT_GENERIC(_name, _group, _arg)                    \
  FAULT_INJECT_SITE(_name, _group,                                           \
                    (1u << FAULT_ACTION_CUSTOM)    |                         \
                    (1u << FAULT_ACTION_RETURN_ERR),                         \
                    fault_inject_act_embedded(&__fi_fp, (FAR void *)(_arg)))

#else /* CONFIG_DRIVERS_FAULT_INJECT */

/* Disabled build: macros expand to plain real-function calls and emit no
 * metadata, no runtime objects, no symbols.  Verified post-build with
 * `nm out/nuttx | grep -i fault_inject` returning no matches (NFR1).
 */

#  define FAULT_INJECT_DECLARE_META(_var, _name, _group, _supported)
#  define FAULT_INJECT_SITE(_name, _group, _supported, _invoke)              \
  do {} while (0)
#  define FAULT_INJECT_SITE_STANDBY(_name, _group, _supported, _invoke)      \
  do {} while (0)

#  define FAULT_INJECT_WRAP_RETVAL(_group, _fn, _err_default, _realcall,     \
                                   _out)                                      \
  do { *(_out) = (_realcall); } while (0)
#  define FAULT_INJECT_WRAP_RETVAL_NULL(_group, _fn, _realcall, _out_ptr)    \
  do { *(_out_ptr) = (_realcall); } while (0)
#  define FAULT_INJECT_WRAP_RETVAL_VOID(_group, _fn, _realcall)              \
  do { (_realcall); } while (0)

#  define FAULT_INJECT_WRAP_RETVAL_STANDBY(_group, _fn, _err_default,        \
                                           _realcall, _out)                   \
  do { *(_out) = (_realcall); } while (0)
#  define FAULT_INJECT_WRAP_RETVAL_NULL_STANDBY(_group, _fn, _realcall,      \
                                                _out_ptr)                     \
  do { *(_out_ptr) = (_realcall); } while (0)
#  define FAULT_INJECT_WRAP_RETVAL_VOID_STANDBY(_group, _fn, _realcall)      \
  do { (_realcall); } while (0)

#  define FAULT_INJECT_POINT_EXCEPTION(_name, _group) \
  do {} while (0)
#  define FAULT_INJECT_POINT_MEMTAMPER(_name, _group)   \
  do {} while (0)
#  define FAULT_INJECT_POINT_GENERIC(_name, _group, _arg) \
  do {} while (0)

#endif /* CONFIG_DRIVERS_FAULT_INJECT */

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#  ifdef __cplusplus
extern "C"
{
#  endif

/****************************************************************************
 * Name: fault_inject_init
 *
 * Description:
 *   Boot-time entry called from drivers_initialize().  Walks the
 *   fault_inject_meta linker section, validates standby-RAM points,
 *   registers all points on the global list, and registers the singleton
 *   /dev/fault/control node.  Zero dynamic memory allocation.
 *
 ****************************************************************************/

void fault_inject_init(void);

/****************************************************************************
 * Name: fault_inject_register
 *
 * Description:
 *   Dynamically register a runtime point object at module initialisation
 *   time.  Intended for modules that load after the boot-time
 *   fault_inject_init() has already run (e.g. dynamically loaded drivers).
 *   The caller owns the fault_point_s instance and passes the matching
 *   metadata record.  Idempotent; safe to call from any context
 *   including IRQ.
 *
 ****************************************************************************/

void fault_inject_register(FAR struct fault_point_s *fp,
                           FAR const struct fault_point_meta_s *meta);

/****************************************************************************
 * Name: fault_inject_wrap_int / _wrap_ptr / _wrap_skip
 *
 * Description:
 *   Slow-path hooks invoked by the FAULT_INJECT_WRAP_* macros once the
 *   short-circuit predict_false check passes.  Each returns true iff the
 *   wrapper should bypass the real call and use the injected effect.
 *
 ****************************************************************************/

bool fault_inject_wrap_int(FAR struct fault_point_s *fp,
                           int err_default,
                           FAR int *err_out);

bool fault_inject_wrap_ptr(FAR struct fault_point_s *fp,
                           FAR void * FAR *ptr_out);

bool fault_inject_wrap_skip(FAR struct fault_point_s *fp);

/****************************************************************************
 * Name: fault_inject_act_embedded
 *
 * Description:
 *   Slow-path entry for embedded injection points.  Evaluates the filter
 *   chain and, if a rule fires, dispatches to the configured executor.
 *
 ****************************************************************************/

void fault_inject_act_embedded(FAR struct fault_point_s *fp,
                               FAR void *user_arg);

/****************************************************************************
 * Name: fault_inject_link_reaction
 *
 * Description:
 *   Associate a system reaction (panic/assert/safety mechanism) with the
 *   most recent injection identified by `seq`.  Used by the panic and
 *   safety-driver hooks to close FR9's symptom-to-source loop.
 *
 ****************************************************************************/

void fault_inject_link_reaction(uint32_t seq, uint32_t reaction_event);

/****************************************************************************
 * Name: fault_arch_trigger_exception
 *
 * Description:
 *   Trigger a hardware exception for the FAULT_ACTION_EXCEPTION action.
 *   The default (weak) implementation writes to an unmapped address
 *   (0xFFFFFFFF) which works on ARM Cortex-M/R.  Architecture or board
 *   code may override this to provide a platform-appropriate mechanism.
 *
 * Input Parameters:
 *   vector - Exception vector hint from user config (currently reserved,
 *            arch implementations may use it to select exception type).
 *
 ****************************************************************************/

void fault_arch_trigger_exception(uint32_t vector);

#  ifdef __cplusplus
}
#  endif

#endif /* __INCLUDE_NUTTX_FAULT_INJECT_H */
