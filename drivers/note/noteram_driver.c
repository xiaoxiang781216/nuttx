/****************************************************************************
 * drivers/note/noteram_driver.c
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
#include <nuttx/compiler.h>

#include <stdint.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sched.h>
#include <fcntl.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <poll.h>

#include <nuttx/atomic.h>
#include <nuttx/spinlock.h>
#include <nuttx/sched.h>
#include <nuttx/sched_note.h>
#include <nuttx/kmalloc.h>
#include <nuttx/note/note_driver.h>
#include <nuttx/note/noteram_driver.h>
#include <nuttx/lib/math32.h>
#include <nuttx/nuttx.h>
#include <nuttx/ratelimit.h>
#include <nuttx/fs/fs.h>
#include <nuttx/streams.h>
#include <nuttx/queue.h>

#include "note_driver.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define NOTERAM_MAGIC   0x6e6f7465   /* "note" */

#define NOTERAM_BUFSIZE CONFIG_DRIVERS_NOTERAM_BUFSIZE

/****************************************************************************
 * Private Types
 ****************************************************************************/

aligned_data(sizeof(uintptr_t))
struct noteram_header_s
{
  volatile uint32_t magic;
  volatile uint32_t head;
  volatile uint32_t tail;
  volatile uint32_t reserved;
  uint8_t buffer[0];
};

struct noteram_driver_s
{
  struct note_driver_s driver;
  FAR struct noteram_header_s *header;
  size_t bufsize;
  unsigned int overwrite;

  /* 0 if power of 2 (use natural overflow),
   * else align_down(UINT32_MAX, bufsize)
   */

  uint32_t max_index;
  atomic_t count_ref;       /* Reference count, init 1 at creation */
  bool dynamic;             /* true if dynamically allocated */
  struct ratelimit_state_s ratelimit;
  dq_queue_t poll_list;     /* Registered poll users list */
  rspinlock_t lock;
  atomic_t poll_ref;        /* Poll notify in-flight reference */
#if CONFIG_DRIVERS_NOTERAM_POLLTIMEOUT_MS > 0
  struct wdog_s wdog;
#endif
};

/* The structure to hold the context data of trace dump */

struct noteram_dump_context_s
{
  struct note_dump_context_s ctx;  /* Shared per-CPU parse context */
  unsigned int mode;
  uint32_t read;
  unsigned int threshold;   /* Per-file poll threshold */
  FAR struct pollfd *pfd;   /* Per-file pollfd pointer */
  dq_entry_t poll_node;     /* Poll list node */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int noteram_open(FAR struct file *filep);
static int noteram_close(FAR struct file *filep);
static ssize_t noteram_read(FAR struct file *filep,
                            FAR char *buffer, size_t buflen);
static int noteram_ioctl(FAR struct file *filep, int cmd, unsigned long arg);
static int noteram_poll(FAR struct file *filep, FAR struct pollfd *fds,
                        bool setup);
static ssize_t noteram_add(FAR struct note_driver_s *drv,
                           FAR const void *note, size_t len,
                           bool noswitches);
static uint32_t noteram_unread_length(FAR struct noteram_driver_s *drv,
                                      uint32_t read);
static uint32_t noteram_length(FAR struct noteram_driver_s *drv);

#if CONFIG_DRIVERS_NOTERAM_POLLTIMEOUT_MS > 0
static void noteram_timeout_handler(wdparm_t arg);
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

struct noteram_buffer_s
{
  struct noteram_header_s header;
  uint8_t buffer[NOTERAM_BUFSIZE];
};

static
#ifdef DRIVERS_NOTERAM_SECTION
locate_data(CONFIG_DRIVERS_NOTERAM_SECTION)
#endif
DEFINE_PER_CPU_BSS_BMP(struct noteram_buffer_s, g_ramnote_buffer);
#define g_ramnote_buffer this_cpu_var_bmp(g_ramnote_buffer)

static const struct note_driver_ops_s g_noteram_ops =
{
  .add = noteram_add,
  .open = noteram_open,
  .close = noteram_close,
  .read = noteram_read,
  .ioctl = noteram_ioctl,
  .poll = noteram_poll,
};

/****************************************************************************
 * Public Data
 ****************************************************************************/

DEFINE_PER_CPU_BMP(struct noteram_driver_s, g_noteram_driver) =
{
  {
#ifdef CONFIG_SCHED_INSTRUMENTATION_FILTER
    "ram",
    {
      {
        CONFIG_DRIVERS_NOTERAM_FILTER_DEFAULT_MODE,
#  ifndef CONFIG_UP
        CONFIG_DRIVERS_NOTERAM_CPUSET
#  endif
      },

#  ifdef CONFIG_SCHED_INSTRUMENTATION_DUMP
      {
        CONFIG_DRIVERS_NOTE_DEFAULT_LEVEL,
      },
#  endif
    },
#endif
    &g_noteram_ops
  },
#ifndef CONFIG_BMP
  &g_ramnote_buffer.header,
#else
  NULL,
#endif
  NOTERAM_BUFSIZE,
#ifdef CONFIG_DRIVERS_NOTERAM_DEFAULT_NOOVERWRITE
  NOTE_MODE_OVERWRITE_DISABLE,
#else
  NOTE_MODE_OVERWRITE_ENABLE,
#endif
#if !IS_POWER_OF_2(NOTERAM_BUFSIZE)
  ALIGN_DOWN(UINT32_MAX, NOTERAM_BUFSIZE)
#endif
};
#define g_noteram_driver this_cpu_var_bmp(g_noteram_driver)

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: noteram_free
 ****************************************************************************/

static void noteram_free(FAR struct noteram_driver_s *drv)
{
  if (drv->dynamic)
    {
      kmm_free(drv->header);
    }

  if (drv != &g_noteram_driver)
    {
      kmm_free(drv);
    }
}

/****************************************************************************
 * Name: noteram_deinitialize_by_name
 ****************************************************************************/

static int noteram_deinitialize_by_name(FAR const char *name)
{
  FAR struct noteram_driver_s *drv;
  char devpath[PATH_MAX];
  struct file filep;
  int ret;

  snprintf(devpath, sizeof(devpath), "/dev/note/%s", name);
  ret = file_open(&filep, devpath, O_RDONLY);
  if (ret < 0)
    {
      return ret;
    }

  drv = (FAR struct noteram_driver_s *)filep.f_inode->i_private;
  file_close(&filep);
  noteram_deinitialize(&drv->driver);
  return OK;
}

#ifdef CONFIG_BMP
static void noteram_driver_initialize(void)
{
  if (g_noteram_driver.header == NULL)
    {
      g_noteram_driver.header = &g_ramnote_buffer.header;
    }
}
#else
#  define noteram_driver_initialize()
#endif

#if CONFIG_DRIVERS_NOTERAM_POLLTIMEOUT_MS > 0
static inline_function
void noteram_timeout_reset(FAR struct noteram_driver_s *drv)
{
  if (dq_empty(&drv->poll_list) || noteram_length(drv) == 0)
    {
      wd_cancel(&drv->wdog);
    }
  else
    {
      wd_start(&drv->wdog, MSEC2TICK(CONFIG_DRIVERS_NOTERAM_POLLTIMEOUT_MS),
               noteram_timeout_handler, (wdparm_t)drv);
    }
}

static void noteram_timeout_handler(wdparm_t arg)
{
  FAR struct noteram_driver_s *drv = (FAR struct noteram_driver_s *)arg;
  irqstate_t flags = rspin_lock_irqsave_notrace(&drv->lock);
  FAR struct noteram_dump_context_s *ctx;

  atomic_add(&drv->poll_ref, 1);
  dq_for_every_entry(&drv->poll_list, ctx,
                     struct noteram_dump_context_s, poll_node)
    {
      /* On timeout, notify as long as there is any unread data,
       * regardless of threshold. This ensures events are delivered
       * within bounded latency even when data volume is low.
       */

      if (ctx->pfd &&
          noteram_unread_length(drv, ctx->read) > 0)
        {
          rspin_unlock_irqrestore_notrace(&drv->lock, flags);
          poll_notify(&ctx->pfd, 1, POLLIN);
          flags = rspin_lock_irqsave_notrace(&drv->lock);
        }
    }

  atomic_sub(&drv->poll_ref, 1);

  rspin_unlock_irqrestore_notrace(&drv->lock, flags);

  noteram_timeout_reset(drv);
}
#else
#  define noteram_timeout_reset(drv)
#endif /* CONFIG_RAMLOG_POLLTIMEOUT_MS > 0 */

/****************************************************************************
 * Name: noteram_header_init
 *
 * Description:
 *   Initialize the header of circular buffer.
 *
 * Input Parameters:
 *   driver - The channel of note driver
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static inline void noteram_header_init(FAR struct noteram_driver_s *drv)
{
  if (drv->header->magic != NOTERAM_MAGIC)
    {
      memset(drv->header, 0, sizeof(struct noteram_header_s));
      drv->header->magic = NOTERAM_MAGIC;
    }
}

/****************************************************************************
 * Name: noteram_next
 *
 * Description:
 *   Return the logical index advanced by offset. When max_index is 0
 *   (power-of-2 bufsize), relies on natural uint overflow. Otherwise,
 *   wraps at max_index boundary.
 *
 * Input Parameters:
 *   drv    - Driver instance
 *   idx    - Current logical index
 *   offset - Number of bytes to advance
 *
 * Returned Value:
 *   New logical index
 *
 ****************************************************************************/

static inline uint32_t noteram_next(FAR struct noteram_driver_s *drv,
                                    uint32_t idx, uint32_t offset)
{
  idx += offset;
  if (drv->max_index != 0 && idx >= drv->max_index)
    {
      idx -= drv->max_index;
    }

  return idx;
}

/****************************************************************************
 * Name: noteram_dist
 *
 * Description:
 *   Calculate the unsigned distance between two logical indices in the
 *   circular index space. When max_index is 0 (power-of-2 bufsize), relies
 *   on natural uint overflow. Otherwise, calculates distance in
 *   [0, max_index) space.
 *
 * Input Parameters:
 *   drv - Driver instance
 *   a   - First logical index (typically head)
 *   b   - Second logical index (typically tail or read)
 *
 * Returned Value:
 *   Unsigned distance from b to a in the circular space
 *
 ****************************************************************************/

static inline uint32_t noteram_dist(FAR struct noteram_driver_s *drv,
                                    uint32_t a, uint32_t b)
{
  if (drv->max_index == 0)
    {
      return a - b;
    }

  return (a >= b) ? (a - b) : (a + drv->max_index - b);
}

/****************************************************************************
 * Name: noteram_offset
 *
 * Description:
 *   Convert a logical index to physical buffer offset. When max_index is 0
 *   (power-of-2 bufsize), uses fast bitwise AND. Otherwise, uses modulo.
 *
 * Input Parameters:
 *   drv - Driver instance
 *   idx - Logical index
 *
 * Returned Value:
 *   Physical offset in buffer [0, bufsize)
 *
 ****************************************************************************/

static inline uint32_t noteram_offset(FAR struct noteram_driver_s *drv,
                                      uint32_t idx)
{
  if (drv->max_index == 0)
    {
      return idx & (drv->bufsize - 1);
    }

  return idx % drv->bufsize;
}

/****************************************************************************
 * Name: noteram_is_overrun
 *
 * Description:
 *   Check if the read position has been overrun by the writer.
 *   This happens when read falls outside the valid window [tail, head).
 *
 * Input Parameters:
 *   drv  - Driver instance
 *   head - Current head (next write position)
 *   tail - Current tail (oldest valid data)
 *   read - Reader's current position
 *
 * Returned Value:
 *   true if read has been overrun (data at read is no longer valid)
 *
 ****************************************************************************/

static inline bool noteram_is_overrun(FAR struct noteram_driver_s *drv,
                                      uint32_t head, uint32_t tail,
                                      uint32_t read)
{
  return noteram_dist(drv, head, read) > noteram_dist(drv, head, tail);
}

/****************************************************************************
 * Name: noteram_is_complete
 *
 * Description:
 *   Check if head is far enough ahead of read to contain size bytes.
 *   This is a necessary (but not sufficient) condition for the entry
 *   to be readable — the caller must re-check for overrun after copy.
 *
 * Input Parameters:
 *   drv  - Driver instance
 *   head - Current head (next write position)
 *   read - Start of the range to check
 *   size - Size of the range in bytes
 *
 * Returned Value:
 *   true if head is at least size bytes ahead of read
 *
 ****************************************************************************/

static inline bool noteram_is_complete(FAR struct noteram_driver_s *drv,
                                       uint32_t head, uint32_t read,
                                       uint32_t size)
{
  return noteram_dist(drv, head, read) >= size;
}

/****************************************************************************
 * Name: noteram_length
 *
 * Description:
 *   Length of data currently in circular buffer.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   Length of data currently in circular buffer.
 *
 ****************************************************************************/

static uint32_t noteram_length(FAR struct noteram_driver_s *drv)
{
  return noteram_dist(drv, drv->header->head, drv->header->tail);
}

/****************************************************************************
 * Name: noteram_unread_length
 *
 * Description:
 *   Length of unread data currently in circular buffer.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   Length of unread data currently in circular buffer.
 *
 ****************************************************************************/

static uint32_t noteram_unread_length(FAR struct noteram_driver_s *drv,
                                      uint32_t read)
{
  uint32_t tail = drv->header->tail;
  uint32_t head = drv->header->head;

  if (noteram_is_overrun(drv, head, tail, read))
    {
      read = tail;
    }

  return noteram_dist(drv, head, read);
}

/****************************************************************************
 * Name: noteram_remove
 *
 * Description:
 *   Remove the variable length note from the tail of the circular buffer
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   The length in bytes of the note that was removed.
 *
 * Assumptions:
 *   We are within a critical section.
 *   The buffer is not empty (caller must check noteram_length > 0).
 *
 ****************************************************************************/

static uint8_t noteram_remove(FAR struct noteram_driver_s *drv)
{
  uint32_t tail;
  uint8_t length;

  /* Get the tail index of the circular buffer */

  tail = drv->header->tail;

  /* Get the length of the note at the tail index */

  length = drv->header->buffer[noteram_offset(drv, tail)];
  DEBUGASSERT(length <= noteram_length(drv));

  /* Increment the tail index to remove the entire note from the circular
   * buffer.
   */

  drv->header->tail = noteram_next(drv, tail, length);
  return length;
}

/****************************************************************************
 * Name: noteram_get
 *
 * Description:
 *   Get the next note from the read index of the circular buffer.
 *
 *   This function is lock-free on the reader side. Each file descriptor
 *   maintains its own read position (ctx->read), so multiple readers do
 *   not conflict with each other. For read-write concurrency, instead of
 *   holding the spinlock (which would block the writer in noteram_add),
 *   we use a seqlock-like retry mechanism:
 *
 *   1. Snapshot tail/head before reading
 *   2. Validate read position and copy data
 *   3. Re-check tail/head after copy to detect concurrent overwrites
 *   4. Retry if data was overwritten during the copy
 *
 *   This approach ensures data consistency without blocking the writer,
 *   which is critical for trace instrumentation where noteram_add may be
 *   called from interrupt context or with interrupts disabled.
 *
 * Input Parameters:
 *   drv    - Driver instance
 *   buffer - Location to return the next note
 *   buflen - The length of the user provided buffer
 *   pread  - Pointer to the reader's current position (updated on success)
 *
 * Returned Value:
 *   On success, the positive, non-zero length of the return note is
 *   provided.  Zero is returned only if the circular buffer is empty.  A
 *   negated errno value is returned in the event of any failure.
 *
 ****************************************************************************/

static ssize_t noteram_get(FAR struct noteram_driver_s *drv,
                           FAR uint8_t *buffer, size_t buflen,
                           FAR uint32_t *pread)
{
  uint32_t head;
  uint32_t tail;
  uint32_t read;
  uint32_t notelen;
  uint32_t first;
  uint32_t offset;

  DEBUGASSERT(buffer != NULL);
  DEBUGASSERT(pread != NULL);

  tail = drv->header->tail;
  head = drv->header->head;
  read  = *pread;

  if (noteram_is_overrun(drv, head, tail, read))
    {
      *pread = tail;
      read = tail;
    }

  if (noteram_dist(drv, head, read) == 0)
    {
      return 0;
    }

  notelen = drv->header->buffer[noteram_offset(drv, read)];

  /* Invalid entry length: either zero (corrupted) or exceeds buffer
   * size. This can happen if the entry was overwritten during read,
   * or if the buffer contains corrupted data.
   */

  if (notelen == 0 || notelen > drv->bufsize)
    {
      /* If overrun happen during this period, then try again */

      *pread = tail; /* Move up read position to tail for next try */
      if (noteram_is_overrun(drv, drv->header->head,
                             drv->header->tail, read))
        {
          return -EAGAIN;
        }
      else
        {
          return -EIO;
        }
    }

  if (notelen > buflen)
    {
      return -EFBIG;
    }

  if (!noteram_is_complete(drv, head, read, notelen))
    {
      return -EAGAIN;
    }

  offset = noteram_offset(drv, read);
  first = drv->bufsize - offset;

  if (first > notelen)
    {
      first = notelen;
    }

  memcpy(buffer, drv->header->buffer + offset, first);
  if (first < notelen)
    {
      memcpy(buffer + first, drv->header->buffer, notelen - first);
    }

  tail = drv->header->tail;
  head = drv->header->head;

  if (noteram_is_overrun(drv, head, tail, read))
    {
      *pread = tail;
      return -EAGAIN;
    }

  *pread = noteram_next(drv, read, notelen);
  return notelen;
}

/****************************************************************************
 * Name: noteram_open
 ****************************************************************************/

static int noteram_open(FAR struct file *filep)
{
  FAR struct noteram_driver_s *drv = (FAR struct noteram_driver_s *)
                                     filep->f_inode->i_private;
  FAR struct noteram_dump_context_s *ctx;

  noteram_driver_initialize();

  atomic_add(&drv->count_ref, 1);

  ctx = kmm_zalloc(sizeof(*ctx));
  if (ctx == NULL)
    {
      if (atomic_sub(&drv->count_ref, 1) == 1)
        {
          noteram_free(drv);
          return -ENOMEM;
        }

      return -ENOMEM;
    }

  ctx->read = drv->header->tail;
  ctx->threshold = sizeof(struct note_common_s);

  filep->f_priv = ctx;
#ifdef CONFIG_DRIVERS_NOTERAM_TEXT_DUMP
  note_dump_context_init(&ctx->ctx);
#endif
  return OK;
}

int noteram_close(FAR struct file *filep)
{
  FAR struct noteram_driver_s *drv = filep->f_inode->i_private;
  FAR struct noteram_dump_context_s *ctx = filep->f_priv;

  if (ctx->pfd != NULL)
    {
      irqstate_t flags;

      flags = rspin_lock_irqsave_notrace(&drv->lock);

      while (atomic_read(&drv->poll_ref) > 0)
        {
          rspin_unlock_irqrestore_notrace(&drv->lock, flags);
          sched_yield();
          flags = rspin_lock_irqsave_notrace(&drv->lock);
        }

      ctx->pfd = NULL;
      dq_rem(&ctx->poll_node, &drv->poll_list);
      rspin_unlock_irqrestore_notrace(&drv->lock, flags);
    }

  filep->f_priv = NULL;
  kmm_free(ctx);

  if (atomic_sub(&drv->count_ref, 1) == 1)
    {
      noteram_free(drv);
    }

  return OK;
}

/****************************************************************************
 * Name: noteram_read
 ****************************************************************************/

static ssize_t noteram_read(FAR struct file *filep, FAR char *buffer,
                            size_t buflen)
{
  FAR struct noteram_dump_context_s *ctx = filep->f_priv;
  FAR struct noteram_driver_s *drv = filep->f_inode->i_private;
  ssize_t ret = 0;

#ifdef CONFIG_DRIVERS_NOTERAM_TEXT_DUMP
  if (ctx->mode == NOTE_MODE_READ_BINARY)
#endif
    {
      size_t nread = 0;
      while (nread < buflen)
        {
          ret = noteram_get(drv, (FAR uint8_t *)buffer + nread,
                            buflen - nread, &ctx->read);
          if (ret <= 0)
            {
              break;
            }

          nread += ret;
        }

      return nread > 0 ? nread : ret;
    }
#ifdef CONFIG_DRIVERS_NOTERAM_TEXT_DUMP
  else
    {
      FAR struct lib_memoutstream_s stream;

      lib_memoutstream(&stream, buffer, buflen);

      do
        {
          struct note_max_s note;

          /* Get the next note (removing it from the buffer) */

          ret = noteram_get(drv, (FAR uint8_t *)&note, sizeof(note),
                            &ctx->read);
          if (ret <= 0)
            {
              break;
            }

          /* Parse notes into text format */

          ret = note_dump_one(&note, (FAR struct lib_outstream_s *)&stream,
                              &ctx->ctx);
        }
      while (ret == 0 && stream.common.nput < buflen);

      return stream.common.nput;
    }
#endif
}

/****************************************************************************
 * Name: noteram_ioctl
 ****************************************************************************/

static int noteram_ioctl(FAR struct file *filep, int cmd, unsigned long arg)
{
  FAR struct noteram_dump_context_s *ctx = filep->f_priv;
  FAR struct noteram_driver_s *drv = filep->f_inode->i_private;
  irqstate_t flags;
  int ret = -ENOTTY;

  /* Handle the ioctl commands */

  switch (cmd)
    {
      /* NOTE_CLEAR
       *      - Clear all contents of the circular buffer
       *        Argument: Ignored
       */

      case NOTE_CLEAR:
        flags = rspin_lock_irqsave_notrace(&drv->lock);
#ifdef CONFIG_DRIVERS_NOTE_STATS
        atomic_add_relaxed(&drv->driver.stats.drop_bytes,
                           noteram_length(drv));
#endif
        drv->header->tail = drv->header->head;
        if (drv->overwrite == NOTE_MODE_OVERWRITE_OVERFLOW)
          {
            drv->overwrite = NOTE_MODE_OVERWRITE_DISABLE;
          }

        rspin_unlock_irqrestore_notrace(&drv->lock, flags);
        ret = OK;
        break;

      /* NOTE_GETOVERWRITE
       *      - Get overwrite mode
       *        Argument: A writable pointer to unsigned int
       */

      case NOTE_GETOVERWRITE:
        if (arg == 0)
          {
            ret = -EINVAL;
          }
        else
          {
            flags = rspin_lock_irqsave_notrace(&drv->lock);
            *(FAR unsigned int *)arg = drv->overwrite;
            rspin_unlock_irqrestore_notrace(&drv->lock, flags);
            ret = OK;
          }
        break;

      /* NOTE_SETOVERWRITE
       *      - Set overwrite mode
       *        Argument: A read-only pointer to unsigned int
       */

      case NOTE_SETOVERWRITE:
        if (arg == 0)
          {
            ret = -EINVAL;
          }
        else
          {
            flags = rspin_lock_irqsave_notrace(&drv->lock);
            drv->overwrite = *(FAR unsigned int *)arg;
            rspin_unlock_irqrestore_notrace(&drv->lock, flags);
            ret = OK;
          }
        break;

      /* NOTE_GETREADMODE
       *      - Get read mode
       *        Argument: A writable pointer to unsigned int
       */

      case NOTE_GETREADMODE:
        if (arg == 0)
          {
            ret = -EINVAL;
          }
        else
          {
            *(FAR unsigned int *)arg = ctx->mode;
            ret = OK;
          }
        break;

      /* NOTE_SETREADMODE
       *      - Set read mode
       *        Argument: A read-only pointer to unsigned int
       */

      case NOTE_SETREADMODE:
        if (arg == 0)
          {
            ret = -EINVAL;
          }
        else
          {
            ctx->mode = arg;
            ret = OK;
          }
        break;

      case NOTE_GETRATELIMIT:
        if (arg == 0)
          {
            ret = -EINVAL;
          }
        else
          {
            FAR struct note_ratelimit_s *limit =
              (FAR struct note_ratelimit_s *)arg;

            flags = rspin_lock_irqsave_notrace(&drv->lock);
            limit->interval = drv->ratelimit.interval;
            limit->burst = drv->ratelimit.burst;
            rspin_unlock_irqrestore_notrace(&drv->lock, flags);
            ret = OK;
          }
        break;

      case NOTE_SETRATELIMIT:
        if (arg == 0)
          {
            ret = -EINVAL;
          }
        else
          {
            FAR struct note_ratelimit_s *limit =
              (FAR struct note_ratelimit_s *)arg;

            flags = rspin_lock_irqsave_notrace(&drv->lock);
            drv->ratelimit.interval = limit->interval;
            drv->ratelimit.burst = limit->burst;
            rspin_unlock_irqrestore_notrace(&drv->lock, flags);
            ret = OK;
          }
        break;

      case FIONREAD:
        if (arg == 0)
          {
            ret = -EINVAL;
          }
        else
          {
            flags = rspin_lock_irqsave_notrace(&drv->lock);
            *(FAR unsigned int *)arg = noteram_unread_length(drv, ctx->read);
            rspin_unlock_irqrestore_notrace(&drv->lock, flags);
            ret = OK;
          }
        break;

      case PIPEIOC_POLLINTHRD:
        ctx->threshold = (uint32_t)arg;
        ret = OK;
        break;

      case PIPEIOC_GETSIZE:
        if (arg == 0)
          {
            ret = -EINVAL;
          }
        else
          {
            *(FAR unsigned int *)arg = drv->bufsize;
            ret = OK;
          }
        break;

      case NOTE_CREATE:
        {
          FAR struct noteram_config_s *config =
            (FAR struct noteram_config_s *)arg;

          if (config == NULL || config->devname == NULL ||
              config->bufsize == 0)
            {
              ret = -EINVAL;
            }
          else
            {
              FAR struct note_driver_s *new_drv;
              new_drv = noteram_initialize(config->devname,
                                           config->bufsize,
                                           config->overwrite,
                                           true);
              ret = new_drv != NULL ? OK : -ENOMEM;
            }
        }
        break;

      case NOTE_DESTROY:
        {
          FAR const char *devname = (FAR const char *)arg;

          if (devname == NULL)
            {
              ret = -EINVAL;
            }
          else
            {
              ret = noteram_deinitialize_by_name(devname);
            }
        }
        break;

      default:
        break;
    }

  return ret;
}

/****************************************************************************
 * Name: noteram_poll
 ****************************************************************************/

static int noteram_poll(FAR struct file *filep, FAR struct pollfd *fds,
                        bool setup)
{
  int ret = 0;
  FAR struct inode *inode;
  FAR struct noteram_driver_s *drv;
  FAR struct noteram_dump_context_s *ctx;
  irqstate_t flags;

  DEBUGASSERT(filep != NULL && fds != NULL);
  inode = filep->f_inode;

  DEBUGASSERT(inode != NULL && inode->i_private != NULL);
  drv = inode->i_private;
  ctx = filep->f_priv;

  /* Ignore waits that do not include POLLIN */

  if ((fds->events & POLLIN) == 0)
    {
      return -EDEADLK;
    }

  flags = rspin_lock_irqsave_notrace(&drv->lock);

  while (atomic_read(&drv->poll_ref) > 0)
    {
      rspin_unlock_irqrestore_notrace(&drv->lock, flags);
      sched_yield();
      flags = rspin_lock_irqsave_notrace(&drv->lock);
    }

  if (setup)
    {
      if (ctx->pfd != NULL)
        {
          rspin_unlock_irqrestore_notrace(&drv->lock, flags);
          return -EBUSY;
        }

      ctx->pfd = fds;
      dq_addlast(&ctx->poll_node, &drv->poll_list);

      if (noteram_unread_length(drv, ctx->read) >= ctx->threshold)
        {
          rspin_unlock_irqrestore_notrace(&drv->lock, flags);
          poll_notify(&fds, 1, POLLIN);
        }
      else
        {
          rspin_unlock_irqrestore_notrace(&drv->lock, flags);
        }
    }
  else
    {
      if (ctx->pfd != NULL)
        {
          ctx->pfd = NULL;
          dq_rem(&ctx->poll_node, &drv->poll_list);
        }

      rspin_unlock_irqrestore_notrace(&drv->lock, flags);
    }

  noteram_timeout_reset(drv);

  return ret;
}

/****************************************************************************
 * Name: noteram_add
 *
 * Description:
 *   Add the variable length note to the transport layer
 *
 * Input Parameters:
 *   note    - The note buffer
 *   notelen - The buffer length
 *   noswitches - True: Can't do context switches now.
 *
 * Returned Value:
 *   None
 *
 * Assumptions:
 *   We are within a critical section.
 *
 ****************************************************************************/

static ssize_t noteram_add(FAR struct note_driver_s *driver,
                           FAR const void *note, size_t notelen,
                           bool noswitches)
{
  FAR const char *buf = note;
  FAR struct noteram_driver_s *drv = (FAR struct noteram_driver_s *)driver;
  uint32_t head;
  uint32_t remain;
  uint32_t space;
  irqstate_t flags;
#ifdef CONFIG_DRIVERS_NOTE_STATS
  size_t dropped = 0;
#endif

  noteram_driver_initialize();

  flags = rspin_lock_irqsave_notrace(&drv->lock);

  /* Initialize the noteram header if it has not been initialized yet. */

  noteram_header_init(drv);

  if (ratelimit_islimited(&drv->ratelimit))
    {
      goto out;
    }

  if (drv->overwrite == NOTE_MODE_OVERWRITE_OVERFLOW)
    {
      goto out;
    }

  DEBUGASSERT(note != NULL && notelen < drv->bufsize);
  remain = drv->bufsize - noteram_length(drv);

  if (remain <= notelen)
    {
      if (drv->overwrite == NOTE_MODE_OVERWRITE_DISABLE)
        {
          /* Stop recording if not in overwrite mode */

          drv->overwrite = NOTE_MODE_OVERWRITE_OVERFLOW;
          goto out;
        }

      /* Remove the note at the tail index, make sure there is enough space.
       * Each evicted note's bytes were previously counted as accepted by
       * note_do_add(); accumulate them and reclassify to drop_bytes after
       * the loop.  The current note still fully succeeds (return notelen)
       * so the dispatcher will count it as accepted.
       */

      do
        {
#ifdef CONFIG_DRIVERS_NOTE_STATS
          dropped += noteram_remove(drv);
#else
          noteram_remove(drv);
#endif
          remain = drv->bufsize - noteram_length(drv);
        }
      while (remain <= notelen);
    }

  head = drv->header->head;
  space = drv->bufsize - noteram_offset(drv, head);
  space = space < notelen ? space : notelen;
  memcpy(drv->header->buffer + noteram_offset(drv, head), note, space);
  memcpy(drv->header->buffer, buf + space, notelen - space);
  drv->header->head = noteram_next(drv, head, notelen);

  if (!noswitches && !rspin_lock_is_recursive(&drv->lock) &&
      !dq_empty(&drv->poll_list))
    {
      FAR struct noteram_dump_context_s *ctx;

      atomic_add(&drv->poll_ref, 1);

      dq_for_every_entry(&drv->poll_list, ctx,
                          struct noteram_dump_context_s, poll_node)
        {
          if (ctx->pfd &&
              noteram_unread_length(drv, ctx->read) >= ctx->threshold)
            {
              rspin_unlock_irqrestore_notrace(&drv->lock, flags);
              poll_notify(&ctx->pfd, 1, POLLIN);
              flags = rspin_lock_irqsave_notrace(&drv->lock);
            }
        }

      atomic_sub(&drv->poll_ref, 1);
    }

  rspin_unlock_irqrestore_notrace(&drv->lock, flags);

  noteram_timeout_reset(drv);

#ifdef CONFIG_DRIVERS_NOTE_STATS
  if (dropped > 0)
    {
      atomic_add_relaxed(&drv->driver.stats.drop_bytes, dropped);
    }
#endif

  return notelen;

out:
  rspin_unlock_irqrestore_notrace(&drv->lock, flags);
  noteram_timeout_reset(drv);
  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: noteram_initialize
 *
 * Description:
 *   Register a serial driver at /dev/note/ram that can be used by an
 *   application to read data from the circular note buffer.
 *
 * Input Parameters:
 *  devname: The name of the Noteram device
 *  bufsize: The size of the circular buffer
 *  overwrite: The overwrite mode
 *  regnote: If register the note driver
 *
 * Returned Value:
 *   Zero on succress. A negated errno value is returned on a failure.
 *
 ****************************************************************************/

FAR struct note_driver_s *
noteram_initialize(FAR const char *devname, size_t bufsize,
                   bool overwrite, bool regnote)
{
  FAR struct note_driver_s *drv;
  FAR void *buffer;

  bufsize += sizeof(struct noteram_header_s);
  buffer = kmm_zalloc(bufsize);
  if (buffer == NULL)
    {
      return NULL;
    }

  drv = noteram_initialize_with_buffer(devname, buffer, bufsize,
                                       overwrite, regnote);
  if (drv == NULL)
    {
      kmm_free(buffer);
    }
  else
    {
      FAR struct noteram_driver_s *noteram_drv;

      noteram_drv = container_of(drv, struct noteram_driver_s, driver);
      noteram_drv->dynamic = true;
    }

  return drv;
}

/****************************************************************************
 * Name: noteram_initialize_with_buffer
 *
 * Description:
 *   Register a serial driver at /dev/note/ram that can be used by an
 *   application to read data from the circular note buffer.
 *
 * Input Parameters:
 *  devname: The name of the Noteram device
 *  buffer: The buffer to use by the noteram driver
 *  bufsize: The size of the circular buffer
 *  overwrite: The overwrite mode
 *  regnote: If register the note driver
 *
 * Returned Value:
 *   Zero on succress. A negated errno value is returned on a failure.
 *
 ****************************************************************************/

FAR struct note_driver_s *
noteram_initialize_with_buffer(FAR const char *devname,
                               FAR void *buffer, size_t bufsize,
                               bool overwrite, bool regnote)
{
  FAR struct noteram_driver_s *drv;
  size_t len = 0;
  int ret;

  if (!buffer || bufsize <= sizeof(struct noteram_header_s))
    {
      return NULL;
    }

  if (devname)
    {
      len = strlen(devname) + 1;
    }

  drv = kmm_zalloc(sizeof(*drv) + len);
  if (drv == NULL)
    {
      return NULL;
    }

  atomic_set(&drv->count_ref, 1);

  if (devname)
    {
      memcpy(drv + 1, devname, len);
    }

#ifdef CONFIG_SCHED_INSTRUMENTATION_FILTER
  drv->driver.name = (FAR const char *)(drv + 1);
  drv->driver.filter.mode.type_mask =
    CONFIG_DRIVERS_NOTERAM_FILTER_DEFAULT_MODE;

#  ifndef CONFIG_UP
  drv->driver.filter.mode.cpuset =
    CONFIG_DRIVERS_NOTERAM_CPUSET;
#  endif

#  ifdef CONFIG_SCHED_INSTRUMENTATION_DUMP
  drv->driver.filter.level.level = CONFIG_DRIVERS_NOTE_DEFAULT_LEVEL;
#  endif
#endif

  drv->driver.ops = &g_noteram_ops;
  drv->header = (FAR struct noteram_header_s *)buffer;
  bufsize -= sizeof(struct noteram_header_s);

  /* Initialize the noteram header if it has not been initialized yet. */

  noteram_header_init(drv);

  drv->bufsize = bufsize;

  if (!IS_POWER_OF_2(bufsize))
    {
      drv->max_index = ALIGN_DOWN(UINT32_MAX, bufsize);
    }

  drv->overwrite = overwrite;
  dq_init(&drv->poll_list);

  if (regnote)
    {
      ret = note_driver_register(&drv->driver);
      if (ret < 0)
        {
          kmm_free(drv);
          return NULL;
        }
    }

  return &drv->driver;
}

/****************************************************************************
 * Name: noteram_deinitialize
 *
 * Description:
 *   Unregister noteram driver.
 *
 * Input Parameters:
 *  drv: the ptr of note_driver_s.
 *
 * Returned Value:
 *  None.
 *
 ****************************************************************************/

void noteram_deinitialize(struct note_driver_s *drv)
{
  FAR struct noteram_driver_s *noteram_drv;

  if (drv == NULL)
    {
      return;
    }

  noteram_drv = container_of(drv, struct noteram_driver_s, driver);

  noteram_drv->header->magic = ~NOTERAM_MAGIC;

  note_driver_unregister(drv);

  unregister_driver((char *)(noteram_drv + 1));

  if (atomic_sub(&noteram_drv->count_ref, 1) == 1)
    {
      noteram_free(noteram_drv);
    }
}
