/****************************************************************************
 * sched/perf/perf_sample.c
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
#include <string.h>
#include <unistd.h>

#include <nuttx/arch.h>
#include <nuttx/circbuf.h>
#include <nuttx/kmalloc.h>
#include <nuttx/perf.h>
#include <nuttx/sched.h>
#include <nuttx/sched_note.h>

#include "perf.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define PERF_RECORD_MISC_USER (2 << 0)
/* Skip perf-internal and interrupt entry frames before recording the
 * callchain.  Must not exceed the real chain depth (9 frames measured on
 * qemu-arm64-v8a-ap_test) or up_backtrace() consumes both stack-walk
 * passes and yields an empty chain.
 */

#define FLAME_GRAPH_SKIP      8
#define PERF_MAX_BACKTRACE    20

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static uint16_t perf_prepare_sample(FAR struct perf_sample_data_s *data,
                                    FAR struct perf_event_s *event,
                                    uintptr_t ip);
static bool perf_buffer_has_space(FAR struct perf_event_s *event,
                                  size_t size);
static void perf_output_sample(FAR struct perf_event_header_s *header,
                               FAR struct perf_sample_data_s *data,
                               FAR struct perf_event_s *event);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static uint16_t perf_prepare_sample(FAR struct perf_sample_data_s *data,
                                    FAR struct perf_event_s *event,
                                    uintptr_t ip)
{
  uint64_t sample_type = event->attr.sample_type;
  uint16_t size = sizeof(struct perf_event_header_s);

  if (sample_type & PERF_SAMPLE_IP)
    {
      data->ip = ip;
      data->sample_flags |= PERF_SAMPLE_IP;
      size += sizeof(data->ip);
    }

  if (sample_type & PERF_SAMPLE_ID)
    {
      data->id = perf_get_eventid(event);
      data->sample_flags |= PERF_SAMPLE_ID;
      size += sizeof(event->id);
    }

  if (sample_type & PERF_SAMPLE_IDENTIFIER)
    {
      data->id = perf_get_eventid(event);
      data->sample_flags |= PERF_SAMPLE_IDENTIFIER;
      size += sizeof(event->id);
    }

  if (sample_type & PERF_SAMPLE_CALLCHAIN)
    {
      data->sample_flags |= PERF_SAMPLE_CALLCHAIN;
      size += sizeof(uint64_t) * (PERF_MAX_BACKTRACE + 1);
    }

  if (sample_type & PERF_SAMPLE_CPU)
    {
      data->cpu_entry.cpu = this_cpu();
      data->cpu_entry.reserved = 0;
      data->sample_flags |= PERF_SAMPLE_CPU;
      size += sizeof(data->cpu_entry);
    }

  if (sample_type & PERF_SAMPLE_TIME)
    {
      data->time = up_perf_gettime();
      data->sample_flags |= PERF_SAMPLE_TIME;
      size += sizeof(data->time);
    }

  if (sample_type & PERF_SAMPLE_STREAM_ID)
    {
      data->stream_id = event->id;
      data->sample_flags |= PERF_SAMPLE_STREAM_ID;
      size += sizeof(data->stream_id);
    }

  if (sample_type & PERF_SAMPLE_PERIOD)
    {
      size += sizeof(data->period);
    }

  if (sample_type & PERF_SAMPLE_TID)
    {
      pid_t pid, tid;

      if (event->ctx && event->ctx->tcb)
        {
          tid = event->ctx->tcb->pid;
          if (event->ctx->tcb->group)
            {
              pid = event->ctx->tcb->group->tg_pid;
            }
          else
            {
              pid = tid;
            }
        }
      else
        {
          pid = nxsched_getpid();
          tid = gettid();
        }

      data->tid_entry.pid = pid;
      data->tid_entry.tid = tid;
      data->sample_flags |= PERF_SAMPLE_TID;
      size += sizeof(data->tid_entry);
    }

  return size;
}

static bool perf_buffer_has_space(FAR struct perf_event_s *event,
                                  size_t size)
{
  size_t space = circbuf_space(&(event->buf->rb));

  /* Wake the reader at half full instead of the last fifth, so it has
   * more time to drain before the producer runs out of room.
   */

  if (space < circbuf_size(&(event->buf->rb)) / 2 && event->pfd)
    {
      poll_notify(&event->pfd, 1, POLLIN);
    }

  /* Non-overwrite semantics: the producer only ever advances head, never
   * tail.  The consumer (perf record) maps the ring buffer and is the sole
   * owner of tail via circbuf_read().  If we evicted old records here we
   * would advance tail from the producer side too, racing the mmap'd
   * consumer (which cannot take event->buf->lock across the mmap boundary)
   * and corrupting the record stream.  Instead, when there is not enough
   * room for the whole record, drop the new sample and keep the stream
   * byte-aligned.  This matches the default behaviour of perf record.
   */

  return space >= size;
}

static inline void perf_backtrace_convert(FAR uint64_t *ptr, int nr)
{
  while (sizeof(uintptr_t) != sizeof(uint64_t) && nr-- > 0)
    {
      *(ptr + nr) = *((FAR uintptr_t *)ptr + nr);
    }
}

static void perf_output_sample(FAR struct perf_event_header_s *header,
                               FAR struct perf_sample_data_s *data,
                               FAR struct perf_event_s *event)
{
  uint64_t sample_type = data->sample_flags;
  uint8_t record[sizeof(struct perf_event_header_s) +
                 sizeof(uint64_t) * 8 +
                 sizeof(uint64_t) * (PERF_MAX_BACKTRACE + 1)];
  FAR struct perf_event_header_s *rec_header;
  size_t len;

  /* header->size already accounts for the worst case (including the full
   * PERF_MAX_BACKTRACE callchain).  If the whole record cannot fit, drop it
   * rather than evicting old records, so that the producer never advances
   * tail and the byte-aligned record stream stays intact for the mmap'd
   * reader.
   */

  if (!perf_buffer_has_space(event, header->size))
    {
      return;
    }

  /* Assemble the whole record in a local buffer first, then publish it to
   * the ring buffer with a single circbuf_write().  circbuf_write() advances
   * head exactly once, after all bytes are in place, so the mmap'd reader
   * never observes a partially written record.  Writing field-by-field
   * (advancing head per field) let the lockless reader copy out a truncated
   * record at buffer-wrap boundaries, corrupting the stream.
   */

  rec_header = (FAR struct perf_event_header_s *)record;
  memcpy(record, header, sizeof(*header));
  len = sizeof(*header);

  if (sample_type & PERF_SAMPLE_IDENTIFIER)
    {
      memcpy(record + len, &data->id, sizeof(data->id));
      len += sizeof(data->id);
    }

  if (sample_type & PERF_SAMPLE_IP)
    {
      memcpy(record + len, &data->ip, sizeof(data->ip));
      len += sizeof(data->ip);
    }

  if (sample_type & PERF_SAMPLE_TID)
    {
      memcpy(record + len, &data->tid_entry, sizeof(data->tid_entry));
      len += sizeof(data->tid_entry);
    }

  if (sample_type & PERF_SAMPLE_TIME)
    {
      memcpy(record + len, &data->time, sizeof(data->time));
      len += sizeof(data->time);
    }

  if (sample_type & PERF_SAMPLE_ID)
    {
      memcpy(record + len, &data->id, sizeof(data->id));
      len += sizeof(data->id);
    }

  if (sample_type & PERF_SAMPLE_STREAM_ID)
    {
      memcpy(record + len, &data->stream_id, sizeof(data->stream_id));
      len += sizeof(data->stream_id);
    }

  if (sample_type & PERF_SAMPLE_CPU)
    {
      memcpy(record + len, &data->cpu_entry, sizeof(data->cpu_entry));
      len += sizeof(data->cpu_entry);
    }

  if (sample_type & PERF_SAMPLE_PERIOD)
    {
      memcpy(record + len, &data->period, sizeof(data->period));
      len += sizeof(data->period);
    }

  if (sample_type & PERF_SAMPLE_CALLCHAIN)
    {
      FAR uint64_t *nr_ptr = (FAR uint64_t *)(record + len);
      FAR uint64_t *frames = nr_ptr + 1;
      int nr;

      nr = sched_backtrace(gettid(), (FAR void **)frames,
                           PERF_MAX_BACKTRACE, FLAME_GRAPH_SKIP);
      perf_backtrace_convert(frames, nr);
      *nr_ptr = nr;
      len += sizeof(uint64_t) * (nr + 1);
    }

  /* Publish the actual record length (the callchain may be shorter than the
   * reserved worst case) and commit the record atomically.
   */

  rec_header->size = len;
  circbuf_write(&(event->buf->rb), record, len);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: perf_get_event_count
 *
 * Description:
 *   Perf event read count from lowerhalf
 *
 * Input Parameters:
 *   arg - Perf event
 *
 * Returned Value:
 *   Count result
 *
 ****************************************************************************/

int perf_get_event_count(FAR void *arg)
{
  FAR struct perf_event_s *event = arg;
  FAR struct perf_event_s *sub_event;
  int ret;

#ifdef CONFIG_SMP
  if (event->oncpu != this_cpu())
    {
      return -EAGAIN;
    }
#endif

  if (event->state != PERF_EVENT_STATE_ACTIVE)
    {
      return OK;
    }

  ret = event->pmu->ops->event_read(event);
  if (ret < 0)
    {
      return ret;
    }

  list_for_every_entry(&event->sibling_list, sub_event,
                       struct perf_event_s, group_node)
    {
      ret = sub_event->pmu->ops->event_read(sub_event);
      if (ret < 0)
        {
          return ret;
        }
    }

  return OK;
}

/****************************************************************************
 * Name: perf_get_eventid
 *
 * Description:
 *   Get the perf event id
 *
 * Input Parameters:
 *   event - Perf event
 *
 * Returned Value:
 *   Event id
 *
 ****************************************************************************/

uint64_t perf_get_eventid(FAR struct perf_event_s *event)
{
  /* Always return the event's own id.  For inherited child events this is
   * seeded from the parent's id at inherit time (perf_inherit_event), so it
   * matches what the parent would report without dereferencing parent_event.
   * The parent (an fd-owned event) can be freed by perf_close() while an
   * inherited child is still sampling in a live task; dereferencing
   * parent_event from the child's hrtimer sampling callback after that free
   * is a use-after-free.  Reading the cached id here keeps the sampling hot
   * path free of any cross-event pointer chase.
   */

  return event->id;
}

void perf_sample_data_init(FAR struct perf_sample_data_s *data,
                           uint64_t period)
{
  data->sample_flags = PERF_SAMPLE_PERIOD;
  data->period = period;
}

int perf_event_data_overflow(FAR struct perf_event_s *event,
                             FAR struct perf_sample_data_s *data,
                             uintptr_t ip)
{
  struct perf_event_header_s header;
  irqstate_t flags;

  if (!event->buf)
    {
      return -ENOMEM;
    }

  /* For a system-wide event, drop samples taken while the perf tool that
   * opened the event was the running task, so the tool is not profiled into
   * its own system-wide capture.
   */

  if (event->exclude_pid != INVALID_PROCESS_ID &&
      nxsched_getpid() == event->exclude_pid)
    {
      return 0;
    }

  header.size = perf_prepare_sample(data, event, ip);
  header.misc = PERF_RECORD_MISC_USER;
  header.type = PERF_RECORD_SAMPLE;

  flags = spin_lock_irqsave(&event->buf->lock);
  perf_output_sample(&header, data, event);
  spin_unlock_irqrestore(&event->buf->lock, flags);

  /* Mirror the sample into the note stream so a perf capture can be
   * rendered on the same Perfetto timeline as note events.  Best-effort
   * and gated by CONFIG_SCHED_INSTRUMENTATION_PERF; a no-op when
   * disabled.  Both paths use the perf_gettime() time base, so the
   * timestamps align directly.
   */

  sched_note_perf_sample(ip, perf_get_eventid(event), event->attr.type,
                         event->attr.config, data->period);
  return 0;
}

int perf_buffer_set_output(FAR struct perf_event_s *event,
                           FAR struct perf_event_s *output)
{
  if (output->buf)
    {
      event->buf = output->buf;
      event->buf->ref_count++;
      return 0;
    }

  return -1;
}

void perf_buffer_release(FAR struct perf_event_s *event)
{
  if (event->buf)
    {
      event->buf->ref_count--;
      if (event->buf->ref_count == 0)
        {
          kumm_free(event->buf);
          event->buf = NULL;
        }
    }
}
