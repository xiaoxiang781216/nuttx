/****************************************************************************
 * libs/libc/misc/lib_note.c
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
#include <errno.h>
#include <string.h>

#include <nuttx/clock.h>
#include <nuttx/ratelimit.h>
#include <nuttx/sched.h>
#include <nuttx/sched_note.h>
#include <nuttx/streams.h>
#include <nuttx/ring.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Renumber idle task PIDs
 *  In NuttX, PID number less than CPU_SETSIZE are idle tasks.
 *  In Linux, there is only one idle task of PID 0.
 */

#define get_pid(pid) ((pid) < CPU_SETSIZE ? 0 : (pid))

#define get_task_state(s)                                                    \
  ((s) == 0 ? 'X' : ((s) <= LAST_READY_TO_RUN_STATE ? 'R' : 'S'))

#if CONFIG_TASK_NAME_SIZE > 0
#  define TASK_NAME_SIZE (CONFIG_TASK_NAME_SIZE + 1)
#else
#  define TASK_NAME_SIZE 16
#endif

#ifdef CONFIG_SCHED_INSTRUMENTATION_DUMP

/****************************************************************************
 * Public Data
 ****************************************************************************/

DEFINE_RATELIMIT_STATE(g_note_ratelimit,
  CONFIG_DRIVERS_NOTE_RATELIMIT_INTERVAL,
  CONFIG_DRIVERS_NOTE_RATELIMIT_BURST);

#endif /* CONFIG_SCHED_INSTRUMENTATION_DUMP */

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#ifdef CONFIG_LIBC_NOTE_TEXT_DUMP

/****************************************************************************
 * Name: note_dump_header
 ****************************************************************************/

static int note_dump_header(FAR struct lib_outstream_s *s,
                            FAR const struct note_common_s *note)
{
  struct timespec ts;
  char buf[TASK_NAME_SIZE];
  pid_t pid;
  int ret;

  perf_convert(note->nc_systime, &ts);
  pid = note->nc_pid;
#ifdef CONFIG_SMP
  int cpu = note->nc_cpu;
#else
  int cpu = 0;
#endif

  nxtask_namepool_get(pid, buf, TASK_NAME_SIZE);

  ret = lib_sprintf(s, "%8s-%-3u [%d] %3" PRIuTM ".%09lu: ",
                    buf, get_pid(pid), cpu,
                    ts.tv_sec, ts.tv_nsec);

  return ret;
}

#if defined(CONFIG_SCHED_INSTRUMENTATION_SWITCH) || \
    defined(CONFIG_SCHED_INSTRUMENTATION_IRQHANDLER)
/****************************************************************************
 * Name: note_dump_sched_switch
 ****************************************************************************/

static int note_dump_sched_switch(FAR struct lib_outstream_s *s,
                                  FAR struct note_dump_context_s *ctx)
{
  char current_buf[TASK_NAME_SIZE];
  char next_buf[TASK_NAME_SIZE];
  uint8_t current_priority;
  uint8_t next_priority;
  pid_t current_pid;
  pid_t next_pid;
  int ret;

  current_pid = ctx->current_pid;
  next_pid = ctx->next_pid;

  current_priority = ctx->current_priority;
  next_priority = ctx->next_priority;

  nxtask_namepool_get(current_pid, current_buf, TASK_NAME_SIZE);
  nxtask_namepool_get(next_pid, next_buf, TASK_NAME_SIZE);

  ret = lib_sprintf(s, "sched_switch: prev_comm=%s prev_pid=%u "
                    "prev_prio=%u prev_state=%c ==> "
                    "next_comm=%s next_pid=%u next_prio=%u\n",
                    current_buf, get_pid(current_pid),
                    current_priority, get_task_state(ctx->current_state),
                    next_buf, get_pid(next_pid),
                    next_priority);

  ctx->current_pid = ctx->next_pid;
  ctx->current_priority = ctx->next_priority;
  ctx->pendingswitch = false;
  return ret;
}
#endif

/****************************************************************************
 * Name: note_dump_printf
 ****************************************************************************/

#ifdef CONFIG_SCHED_INSTRUMENTATION_DUMP
static int note_dump_printf(FAR struct lib_outstream_s *s,
                            FAR struct note_printf_s *note)
{
  return lib_osprintf(s, NOTE_META_TYPE(note->npt_meta),
                      (FAR const char *)note->npt_fmt, note->npt_data);
}
#endif

/****************************************************************************
 * Name: note_dump_one
 ****************************************************************************/

static int note_dump_one(FAR struct note_dump_stream_s *stream,
                         FAR const struct note_max_s *p,
                         FAR struct lib_outstream_s *s)
{
  FAR const struct note_common_s *note = &p->cmn;
  FAR struct note_dump_context_s *ctx;
#ifdef CONFIG_SCHED_INSTRUMENTATION_SWITCH
  char buf[TASK_NAME_SIZE];
#endif
#ifdef CONFIG_SMP
  int cpu = note->nc_cpu;
#else
  int cpu = 0;
#endif
  int ret = 0;
  pid_t pid;

  ctx = &stream->ctx[cpu];
  pid = note->nc_pid;

  if (ctx->current_pid < 0)
    {
      ctx->current_pid = pid;
    }

  /* Output one note */

  switch (note->nc_type)
    {
#ifdef CONFIG_SCHED_INSTRUMENTATION_SWITCH
    case NOTE_START:
      {
        nxtask_namepool_get(pid, buf, TASK_NAME_SIZE);
        ret += note_dump_header(s, note);
        ret += lib_sprintf(s, "sched_wakeup_new: comm=%s pid=%d "
                           "target_cpu=%d\n",
                           buf, get_pid(pid), cpu);
      }
      break;

    case NOTE_STOP:
      {
        /* This note informs the task to be stopped.
         * Change current task state for the succeeding NOTE_RESUME.
         */

        ctx->current_state = 0;
      }
      break;

    case NOTE_TASKNAME:
      {
        FAR const char *name =
          (FAR const char *)p + sizeof(struct note_common_s);

        ret += note_dump_header(s, note);
        ret += lib_sprintf(s, "task_rename: pid=%d comm=%s\n",
                           get_pid(pid), name);
      }
      break;

    case NOTE_SUSPEND:
      {
        FAR struct note_suspend_s *nsu = (FAR struct note_suspend_s *)p;
        int i;

        UNUSED(i);

        /* This note informs the task to be suspended.
         * Preserve the information for the succeeding NOTE_RESUME.
         */

        ctx->current_state = nsu->nsu_state;

#if CONFIG_SCHED_INSTRUMENTATION_SWITCH_BACKTRACE > 0
        ret += lib_sprintf(s, "   <-");
        for (i = 0; i < CONFIG_SCHED_INSTRUMENTATION_SWITCH_BACKTRACE; i++)
          {
            if (nsu->nsu_backtrace[i] == 0)
              {
                break;
              }

            ret += lib_sprintf(s, " 0x%" PRIxPTR, nsu->nsu_backtrace[i]);
          }

        ret += lib_sprintf(s, "\n");
#endif
      }
      break;

    case NOTE_RESUME:
      {
        /* This note informs the task to be resumed.
         * The task switch timing depends on the running context.
         */

        ctx->next_pid = pid;
        ctx->next_priority = note->nc_priority;

        if (ctx->intr_nest == 0)
          {
            /* If not in the interrupt context, the task switch is
             * executed immediately.
             */

            ret += note_dump_header(s, note);
            ret += note_dump_sched_switch(s, ctx);
          }
        else
          {
            /* If in the interrupt context, the task switch is postponed
             * until leaving the interrupt handler.
             */

            nxtask_namepool_get(ctx->next_pid, buf, TASK_NAME_SIZE);
            ret += note_dump_header(s, note);
            ret += lib_sprintf(s, "sched_waking: comm=%s "
                               "pid=%d target_cpu=%d\n",
                               buf, get_pid(ctx->next_pid), cpu);
            ctx->pendingswitch = true;
          }
      }
      break;

    case NOTE_READY:
      {
        /* This note informs the task to be ready to run.
         * Output sched_waking using the note's own pid/priority,
         * which identifies the task that just became runnable.
         */

        nxtask_namepool_get(pid, buf, TASK_NAME_SIZE);
        ret += note_dump_header(s, note);
        ret += lib_sprintf(s, "sched_waking: comm=%s "
                           "pid=%d prio=%d target_cpu=%d\n",
                           buf, get_pid(pid), note->nc_priority, cpu);
      }
      break;
#endif

#ifdef CONFIG_SCHED_INSTRUMENTATION_SYSCALL
    case NOTE_SYSCALL_ENTER:
      {
        FAR struct note_syscall_enter_s *nsc;
        int i;

        UNUSED(i);
        nsc = (FAR struct note_syscall_enter_s *)p;
        if (nsc->nsc_nr < CONFIG_SYS_RESERVED ||
            nsc->nsc_nr >= SYS_maxsyscall)
          {
            break;
          }

        ret += note_dump_header(s, note);
        ret += lib_sprintf(s, "sys_%s(",
                           g_funcnames[nsc->nsc_nr - CONFIG_SYS_RESERVED]);

#ifdef CONFIG_SCHED_INSTRUMENTATION_FILTER_SYSCALL_ARGS
        for (i = 0; i < nsc->nsc_argc; i++)
          {
            uintptr_t arg;
            arg = nsc->nsc_args[i];
            if (i == 0)
              {
                ret += lib_sprintf(s, "arg%d: 0x%" PRIxPTR, i, arg);
              }
            else
              {
                ret += lib_sprintf(s, ", arg%d: 0x%" PRIxPTR, i, arg);
              }
          }
#endif

        ret += lib_sprintf(s, ")\n");
      }
      break;

    case NOTE_SYSCALL_LEAVE:
      {
        FAR struct note_syscall_leave_s *nsc;
        uintptr_t result;

        nsc = (FAR struct note_syscall_leave_s *)p;
        if (nsc->nsc_nr < CONFIG_SYS_RESERVED ||
            nsc->nsc_nr >= SYS_maxsyscall)
          {
            break;
          }

        ret += note_dump_header(s, note);
        result = nsc->nsc_result;
        ret += lib_sprintf(s, "sys_%s -> 0x%" PRIxPTR "\n",
                           g_funcnames[nsc->nsc_nr - CONFIG_SYS_RESERVED],
                           result);
      }
      break;
#endif

#ifdef CONFIG_SCHED_INSTRUMENTATION_IRQHANDLER
    case NOTE_IRQ_ENTER:
      {
        FAR struct note_irqhandler_s *nih;

        nih = (FAR struct note_irqhandler_s *)p;
        ret += note_dump_header(s, note);
        ret += lib_sprintf(s, "irq_handler_entry: irq=%u name=%pS\n",
                           nih->nih_irq, (FAR void *)nih->nih_handler);
        ctx->intr_nest++;
      }
      break;

    case NOTE_IRQ_LEAVE:
      {
        FAR struct note_irqhandler_s *nih;

        nih = (FAR struct note_irqhandler_s *)p;
        ret += note_dump_header(s, note);
        ret += lib_sprintf(s, "irq_handler_exit: irq=%u ret=handled\n",
                           nih->nih_irq);
        ctx->intr_nest--;

        if (ctx->intr_nest <= 0)
          {
            ctx->intr_nest = 0;
            if (ctx->pendingswitch)
              {
                /* If the pending task switch exists, it is executed here */

                ret += note_dump_header(s, note);
                ret += note_dump_sched_switch(s, ctx);
              }
          }
      }
      break;
#endif

#ifdef CONFIG_SCHED_INSTRUMENTATION_WDOG
    case NOTE_WDOG_START:
    case NOTE_WDOG_CANCEL:
      {
        FAR struct note_wdog_s *nw;

        nw = (FAR struct note_wdog_s *)p;
        ret += note_dump_header(s, note);
        ret += lib_sprintf(s, "tracing_mark_write: I|%d|"
                           "wdog_%s: handler=%pS arg=0x%" PRIxPTR "\n",
                           pid,
                           note->nc_type == NOTE_WDOG_START
                           ? "start" : "cancel",
                           (FAR void *)nw->handler, nw->arg);
      }
      break;
    case NOTE_WDOG_ENTER:
    case NOTE_WDOG_LEAVE:
      {
        FAR struct note_wdog_s *nw;

        nw = (FAR struct note_wdog_s *)p;
        ret += note_dump_header(s, note);
        ret += lib_sprintf(s, "tracing_mark_write: %c|%d|"
                           "wdog: handler=%pS arg=0x%" PRIxPTR "\n",
                           note->nc_type == NOTE_WDOG_ENTER ? 'B' : 'E',
                           pid, (FAR void *)nw->handler, nw->arg);
      }
      break;
#endif

#ifdef CONFIG_SCHED_INSTRUMENTATION_CSECTION
    case NOTE_CSECTION_ENTER:
    case NOTE_CSECTION_LEAVE:
      {
        struct note_csection_s *ncs;
        ncs = (FAR struct note_csection_s *)p;
        ret += note_dump_header(s, &ncs->ncs_cmn);
        ret += lib_sprintf(s, "tracing_mark_write: %c|%d|critical_section\n",
                           note->nc_type == NOTE_CSECTION_ENTER ?
                           'B' : 'E', pid);
      }
      break;
#endif

#ifdef CONFIG_SCHED_INSTRUMENTATION_PREEMPTION
    case NOTE_PREEMPT_LOCK:
    case NOTE_PREEMPT_UNLOCK:
      {
        struct note_preempt_s *npr;
        int16_t count;
        npr = (FAR struct note_preempt_s *)p;
        count = npr->npr_count;
        ret += note_dump_header(s, &npr->npr_cmn);
        ret += lib_sprintf(s,  "tracing_mark_write: %c|%d|sched_lock:%d\n",
                          note->nc_type == NOTE_PREEMPT_LOCK ?
                          'B' : 'E', pid, count);
      }
      break;
#endif

#ifdef CONFIG_SCHED_INSTRUMENTATION_DUMP
    case NOTE_DUMP_PRINTF:
      {
        FAR struct note_printf_s *npt;

        npt = (FAR struct note_printf_s *)p;
        ret += note_dump_header(s, &npt->npt_cmn);
        ret += lib_sprintf(s, "tracing_mark_write: I|%d|", pid);
        ret += note_dump_printf(s, npt);
        lib_stream_putc(s, '\n');
        ret++;
      }
      break;
    case NOTE_DUMP_BEGIN:
    case NOTE_DUMP_END:
      {
        FAR struct note_event_s *nbi = (FAR struct note_event_s *)p;
        char c = note->nc_type == NOTE_DUMP_BEGIN ? 'B' : 'E';
        int len = note->nc_length - SIZEOF_NOTE_EVENT(0);
        uintptr_t ip;

        ip = nbi->nev_ip;
        ret += note_dump_header(s, &nbi->nev_cmn);
        if (len > 0)
          {
            ret += lib_sprintf(s, "tracing_mark_write: %c|%d|%.*s\n",
                               c, pid, len, (FAR const char *)nbi->nev_data);
          }
        else
          {
            ret += lib_sprintf(s, "tracing_mark_write: %c|%d|%pS\n",
                               c, pid, (FAR void *)ip);
          }
      }
      break;
    case NOTE_DUMP_MARK:
      {
        FAR struct note_event_s *nbi = (FAR struct note_event_s *)p;
        int len = note->nc_length - SIZEOF_NOTE_EVENT(0);

        ret += note_dump_header(s, &nbi->nev_cmn);
        ret += lib_sprintf(s, "tracing_mark_write: I|%d|%.*s\n",
                           pid, len, (FAR const char *)nbi->nev_data);
      }
      break;
      case NOTE_DUMP_BINARY:
      {
        FAR struct note_event_s *nbi = (FAR struct note_event_s *)p;
        int len = note->nc_length - SIZEOF_NOTE_EVENT(0);
        int i;

        ret += note_dump_header(s, &nbi->nev_cmn);
        ret += lib_sprintf(s, "tracing_mark_write: I|%d|", pid);

        for (i = 0; i < len; i++)
          {
            ret += lib_sprintf(s, "%02x ", nbi->nev_data[i]);
          }

        lib_stream_putc(s, '\n');
        ret++;
      }
      break;
    case NOTE_DUMP_COUNTER:
      {
        FAR struct note_event_s *nbi = (FAR struct note_event_s *)p;
        FAR struct note_counter_s *counter;
        int64_t value;
        counter = (FAR struct note_counter_s *)nbi->nev_data;

        /* nev_data is not word aligned, copy the wide field out first. */

        memcpy(&value, &counter->value, sizeof(value));
        ret += note_dump_header(s, &nbi->nev_cmn);
        ret += lib_sprintf(s, "tracing_mark_write: C|%d|%s|%" PRId64 "\n",
                           pid, counter->name, value);
      }
      break;
    case NOTE_DUMP_THREADTIME:
      {
        FAR struct note_event_s *nev = (FAR struct note_event_s *)p;
        FAR struct note_threadtime_s *nts = (FAR struct note_threadtime_s *)
                                            nev->nev_data;
        clock_t elapsed;

        /* nev_data is not word aligned, copy the wide field out first. */

        memcpy(&elapsed, &nts->elapsed, sizeof(elapsed));
        ret += note_dump_header(s, &nev->nev_cmn);
        ret += lib_sprintf(s, "tracing_mark_write: I|%d|elapsed:%llu\n",
                           pid, (unsigned long long)elapsed);
      }
      break;
    case NOTE_DUMP_ASYNC_BEGIN:
    case NOTE_DUMP_ASYNC_END:
      {
        FAR struct note_event_s *nev = (FAR struct note_event_s *)p;
        FAR struct note_async_s *nas = (FAR struct note_async_s *)
                                       nev->nev_data;
        size_t total = note->nc_length - SIZEOF_NOTE_EVENT(0)
                         - sizeof(struct note_async_s);
        int32_t cookie;
        size_t nlen;

        if (note->nc_length <= SIZEOF_NOTE_EVENT(0)
              + sizeof(struct note_async_s))
          {
            break;
          }

        /* nev_data is not word aligned, copy the wide field out first. */

        memcpy(&cookie, &nas->cookie, sizeof(cookie));
        nlen = strnlen(nas->names, total) + 1;
        if (nlen > total)
          {
            nlen = total;
          }

        ret += note_dump_header(s, &nev->nev_cmn);
        if (nlen < total)
          {
            ret += lib_sprintf(s, "tracing_mark_write: "
                               "%c|%d|%s|%s|%" PRId32 "\n",
                               note->nc_type == NOTE_DUMP_ASYNC_BEGIN
                               ? 'G' : 'H',
                               pid, nas->names,
                               nas->names + nlen, cookie);
          }
        else
          {
            ret += lib_sprintf(s, "tracing_mark_write: "
                               "%c|%d|%s|%" PRId32 "\n",
                               note->nc_type == NOTE_DUMP_ASYNC_BEGIN
                               ? 'S' : 'F',
                               pid, nas->names, cookie);
          }
      }
      break;
#endif
#ifdef CONFIG_SCHED_INSTRUMENTATION_WQUEUE
    case NOTE_WQUEUE_START:
    case NOTE_WQUEUE_CANCEL:
      {
        FAR struct note_wqueue_s *nwq;

        nwq = (FAR struct note_wqueue_s *)p;
        ret += note_dump_header(s, note);
        ret += lib_sprintf(s, "tracing_mark_write: I|%d|"
                           "wqueue_%s: worker=%pS arg=0x%" PRIxPTR "\n",
                           pid, note->nc_type == NOTE_WQUEUE_START
                           ? "enqueue" : "cancel",
                           (FAR void *)nwq->worker, nwq->arg);
      }
      break;
    case NOTE_WQUEUE_ENTER:
    case NOTE_WQUEUE_LEAVE:
      {
        FAR struct note_wqueue_s *nwq;

        nwq = (FAR struct note_wqueue_s *)p;
        ret += note_dump_header(s, note);
        ret += lib_sprintf(s, "tracing_mark_write: %c|%d|"
                           "wqueue: worker=%pS arg=0x%" PRIxPTR "\n",
                           note->nc_type == NOTE_WQUEUE_ENTER ? 'B' : 'E',
                           pid, (FAR void *)nwq->worker, nwq->arg);
      }
      break;
#endif
#ifdef CONFIG_SCHED_INSTRUMENTATION_HEAP
    case NOTE_HEAP_ADD:
    case NOTE_HEAP_REMOVE:
    case NOTE_HEAP_ALLOC:
    case NOTE_HEAP_FREE:
      {
        FAR struct note_heap_s *nmm = (FAR struct note_heap_s *)p;
        FAR const char *name[] =
          {
            "add", "remove", "malloc", "free"
          };

        ret += note_dump_header(s, &nmm->nhp_cmn);
        ret += lib_sprintf(s, "tracing_mark_write: C|%d|Heap Usage|%zu|%s"
                           ": heap: 0x%" PRIxPTR
                           " size: %zu, address: 0x%" PRIxPTR "\n",
                           pid, nmm->used,
                           name[note->nc_type - NOTE_HEAP_ADD],
                           nmm->heap, nmm->size, nmm->mem);
      }
      break;
#endif
    default:
      break;
    }

  /* Return the length of the processed note */

  return ret;
}

#endif /* CONFIG_LIBC_NOTE_TEXT_DUMP */

/****************************************************************************
 * Name: note_type_size
 *
 * Description:
 *   Return the exact byte size of a fixed-length note type, or zero for a
 *   variable-length type (where only nc_length >= the common header can be
 *   checked).  The sizes mirror the write side so a header whose type is
 *   fixed but whose length disagrees can be rejected during resync.
 *
 ****************************************************************************/

static inline_function size_t note_type_size(uint8_t type)
{
  switch (type)
    {
      case NOTE_STOP:
      case NOTE_RESUME:
      case NOTE_READY:
        return sizeof(struct note_common_s);
      case NOTE_SUSPEND:
        return sizeof(struct note_suspend_s);
      case NOTE_CPU_START:
        return sizeof(struct note_cpu_start_s);
      case NOTE_CPU_STARTED:
        return sizeof(struct note_cpu_started_s);
      case NOTE_PREEMPT_LOCK:
      case NOTE_PREEMPT_UNLOCK:
        return sizeof(struct note_preempt_s);
      case NOTE_CSECTION_ENTER:
      case NOTE_CSECTION_LEAVE:
        return sizeof(struct note_csection_s);
      case NOTE_SPINLOCK_LOCK:
      case NOTE_SPINLOCK_LOCKED:
      case NOTE_SPINLOCK_UNLOCK:
      case NOTE_SPINLOCK_ABORT:
        return sizeof(struct note_spinlock_s);
      case NOTE_SYSCALL_LEAVE:
        return sizeof(struct note_syscall_leave_s);
      case NOTE_IRQ_ENTER:
      case NOTE_IRQ_LEAVE:
        return sizeof(struct note_irqhandler_s);
      case NOTE_WDOG_START:
      case NOTE_WDOG_CANCEL:
      case NOTE_WDOG_ENTER:
      case NOTE_WDOG_LEAVE:
        return sizeof(struct note_wdog_s);
      case NOTE_WQUEUE_START:
      case NOTE_WQUEUE_CANCEL:
      case NOTE_WQUEUE_ENTER:
      case NOTE_WQUEUE_LEAVE:
        return sizeof(struct note_wqueue_s);
      case NOTE_HEAP_ADD:
      case NOTE_HEAP_REMOVE:
      case NOTE_HEAP_ALLOC:
      case NOTE_HEAP_FREE:
        return sizeof(struct note_heap_s);
      default:
        return 0;
    }
}

/****************************************************************************
 * Name: note_is_valid
 *
 * Description:
 *   Check whether a common note header read at some ring offset is
 *   plausible: a real type, a CPU within range, and a length that fits the
 *   remaining bytes.  For a fixed-length type the length must match exactly.
 *
 ****************************************************************************/

static inline_function bool
note_is_valid(FAR const struct note_common_s *hdr)
{
  size_t size;

  if (hdr->nc_type <= NOTE_ALL || hdr->nc_type >= NOTE_TYPE_LAST ||
      hdr->nc_cpu >= CONFIG_NCPUS ||
      hdr->nc_length < sizeof(struct note_common_s))
    {
      return false;
    }

  size = note_type_size(hdr->nc_type);
  return size == 0 || hdr->nc_length == size;
}

/****************************************************************************
 * Name: note_read
 ****************************************************************************/

static inline_function ssize_t
note_read(FAR struct note_dump_stream_s *stream)
{
  FAR struct note_common_s *cmn = &stream->note.cmn;
  uint32_t start;
  uint32_t base;
  uint32_t len;
  int scan;

  do
    {
      /* Scan forward for the first position whose header validates.  A
       * fragment left by an overwrite cannot exceed one note, so bound the
       * scan by the maximum note length.
       */

      for (scan = 0; scan < NOTE_BUFFER_SIZE; scan++)
        {
          base = stream->pos;
          len = ring_mpmc_peek(stream->ring, (FAR uint8_t *)cmn,
                               sizeof(*cmn), &stream->pos);
          if (len < sizeof(*cmn))
            {
              /* Less than a whole header remains; nothing left to dump. */

              return 0;
            }

          if (note_is_valid(cmn))
            {
              break;
            }

          /* No valid header here.  The peek reads from the live cons_tail
           * when base fell behind it, so the real read start is
           * stream->pos - len.
           */

          start = ring_dist(stream->ring, stream->pos, len);
          if (base != RING_IDX_OLDEST && start != base)
            {
              /* An overwrite evicted the scanned bytes, re-anchor the scan
               * to the new cons_tail and restart the fragment-length budget.
               */

              stream->pos = start;
              scan = -1;
            }
          else
            {
              stream->pos = ring_advance(stream->ring, start, 1);
            }
        }

      if (scan == NOTE_BUFFER_SIZE)
        {
          return -EAGAIN;
        }

      /* stream->pos now sits at the payload; read it from there.  A short
       * read means the whole note is not present, so nothing is left.
       */

      start = stream->pos;
      len = cmn->nc_length - sizeof(*cmn);
      if (ring_mpmc_peek(stream->ring, stream->note.data, len,
                         &stream->pos) != len)
        {
          return 0;
        }

      /* If an overwrite advanced cons_tail between the header and payload
       * peeks, need rescan.
       */
    }
  while (stream->pos != ring_advance(stream->ring, start, len));

  return cmn->nc_length;
}

/****************************************************************************
 * Name: note_dump_gets
 ****************************************************************************/

static ssize_t note_dump_gets(FAR struct lib_instream_s *self,
                              FAR void *buffer, size_t len)
{
  FAR struct note_dump_stream_s *stream =
                              (FAR struct note_dump_stream_s *)self;
  FAR char *dest = buffer;
  FAR char *end = dest + len;
  ssize_t ret = 0;

  /* Emit as many whole notes as fit into the caller's buffer.  Binary mode
   * hands back the raw notes unchanged; text mode formats each note into
   * ftrace text.  A note that would not fit is left for the next call so it
   * is never split; the caller must provide a buffer large enough for at
   * least one note.
   */

  for (; ; )
    {
      uint32_t saved = stream->pos;
      ssize_t notelen;

      ret = note_read(stream);
      if (ret <= 0)
        {
          break;
        }

#ifdef CONFIG_LIBC_NOTE_TEXT_DUMP
      if (!stream->binary)
        {
          struct lib_memoutstream_s out;

          /* note_dump_one() returns the full formatted length even when the
           * stream clamps it, so a length past the remaining space means
           * the whole note did not fit.
           */

          lib_memoutstream(&out, dest, end - dest);
          notelen = note_dump_one(stream, &stream->note, &out.common);
          if (notelen > out.buflen)
            {
              /* Leave this note for the next call so it is never split. */

              stream->pos = saved;
              break;
            }
        }
      else
#endif
        {
          notelen = stream->note.cmn.nc_length;
          if (dest + notelen > end)
            {
              /* Leave this note for the next call so it is never split. */

              stream->pos = saved;
              break;
            }

          memcpy(dest, &stream->note, notelen);
        }

      dest += notelen;
    }

  /* If no note was emitted, propagate the last read error (for example
   * -EAGAIN at the end of the ring) so the caller can tell end-of-data
   * apart from a short fill.
   */

  return dest != buffer ? dest - (FAR char *)buffer : ret;
}

/****************************************************************************
 * Name: note_dump_getc
 ****************************************************************************/

static int note_dump_getc(FAR struct lib_instream_s *self)
{
  /* This stream only yields formatted text through gets(); a single-byte
   * read has no meaning here and must not consume a note.
   */

  UNUSED(self);
  return -EINVAL;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: note_dump_stream
 ****************************************************************************/

void note_dump_stream(FAR struct note_dump_stream_s *stream,
                      FAR struct ring_s *ring, bool binary)
{
  int cpu;

#ifndef CONFIG_LIBC_NOTE_TEXT_DUMP
  /* Without the text formatter built in, only binary dumps are possible. */

  DEBUGASSERT(binary);
#endif

  /* Wire the embedded non-consuming ring input stream. */

  stream->ring        = ring;
  stream->binary      = binary;
  stream->pos         = RING_IDX_OLDEST;
  stream->common.getc = note_dump_getc;
  stream->common.gets = note_dump_gets;
  stream->common.nget = 0;

  /* Initialize the trace dump context */

  for (cpu = 0; cpu < CPU_SETSIZE; cpu++)
    {
      stream->ctx[cpu].intr_nest = 0;
      stream->ctx[cpu].pendingswitch = false;
      stream->ctx[cpu].current_state = TSTATE_TASK_RUNNING;
      stream->ctx[cpu].current_pid = -1;
      stream->ctx[cpu].next_pid = -1;
      stream->ctx[cpu].current_priority = -1;
      stream->ctx[cpu].next_priority = -1;
    }
}
