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

#include <nuttx/atomic.h>
#include <nuttx/arch.h>
#include <nuttx/circbuf.h>
#include <nuttx/kmalloc.h>
#include <nuttx/perf.h>
#include <nuttx/sched.h>

#include "perf.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define PERF_RECORD_MISC_USER (2 << 0)
#define FLAME_GRAPH_SKIP      11
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

  /* Wake up the reader (perf record) when the buffer is filling up so it
   * can drain the data before we start dropping samples.
   */

  if (space < circbuf_size(&(event->buf->rb)) / 5 && event->pfd)
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
  while (sizeof(uintptr_t) != sizeof(uint64_t) && nr-- >= 0)
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
  if (event->parent_event != NULL)
    {
      return event->parent_event->id;
    }
  else
    {
      return event->id;
    }
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

  header.size = perf_prepare_sample(data, event, ip);
  header.misc = PERF_RECORD_MISC_USER;
  header.type = PERF_RECORD_SAMPLE;

  flags = spin_lock_irqsave(&event->buf->lock);
  perf_output_sample(&header, data, event);
  spin_unlock_irqrestore(&event->buf->lock, flags);
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
