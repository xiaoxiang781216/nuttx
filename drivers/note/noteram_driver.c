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
#include <inttypes.h>
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
#include <nuttx/panic_notifier.h>
#include <nuttx/fs/fs.h>
#include <nuttx/streams.h>
#include <nuttx/queue.h>

#include "note_driver.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define NCPUS CONFIG_SMP_NCPUS

/* Renumber idle task PIDs
 *  In NuttX, PID number less than NCPUS are idle tasks.
 *  In Linux, there is only one idle task of PID 0.
 */

#define get_pid(pid) ((pid) < NCPUS ? 0 : (pid))

#define get_task_state(s)                                                    \
  ((s) == 0 ? 'X' : ((s) <= LAST_READY_TO_RUN_STATE ? 'R' : 'S'))

#if CONFIG_TASK_NAME_SIZE > 0
#  define TASK_NAME_SIZE (CONFIG_TASK_NAME_SIZE + 1)
#else
#  define TASK_NAME_SIZE 16
#endif

#define NOTERAM_MAGIC   0x6e6f7465   /* "note" */

#define NOTERAM_BUFSIZE CONFIG_DRIVERS_NOTERAM_BUFSIZE

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct noteram_ratelimit_s
{
  unsigned int interval; /* The interval in seconds */
  unsigned int burst;    /* The max allowed note number during interval */
  unsigned int printed;  /* The number of printed note during interval */
  unsigned long begin;   /* The timestamp in seconds */
};

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
  struct notifier_block nb;
  struct noteram_ratelimit_s ratelimit;
  dq_queue_t poll_list;     /* Registered poll users list */
  rspinlock_t lock;
  atomic_t poll_ref;        /* Poll notify in-flight reference */
#if CONFIG_DRIVERS_NOTERAM_POLLTIMEOUT_MS > 0
  struct wdog_s wdog;
#endif
};

/* The structure to hold the context data of trace dump */

struct noteram_dump_cpu_context_s
{
  int intr_nest;            /* Interrupt nest level */
  bool pendingswitch;       /* sched_switch pending flag */
  int current_state;        /* Task state of the current line */
  pid_t current_pid;        /* Task PID of the current line */
  pid_t next_pid;           /* Task PID of the next line */
  uint8_t current_priority; /* Task Priority of the current line */
  uint8_t next_priority;    /* Task Priority of the next line */
};

struct noteram_dump_context_s
{
  struct noteram_dump_cpu_context_s cpu[NCPUS];
  unsigned int mode;
  uint32_t read;
  spinlock_t lock;          /* Serialize readers on same fd */
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
static void noteram_add(FAR struct note_driver_s *drv,
                        FAR const void *note, size_t len,
                        bool noswitches);
static void
noteram_dump_init_context(FAR struct noteram_dump_context_s *ctx);
static int noteram_dump_one(FAR const struct note_max_s *p,
                            FAR struct lib_outstream_s *s,
                            FAR struct noteram_dump_context_s *ctx);
static uint32_t noteram_unread_length(FAR struct noteram_driver_s *drv,
                                      uint32_t read);

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
#  ifdef CONFIG_SMP
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
  if (dq_empty(&drv->poll_list))
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
      /* Relaxed read of ctx->read and ctx->threshold without
       * ctx->lock: we already hold drv lock which serializes with
       * poll_list add/remove, so ctx stays valid.  A stale value
       * only causes a harmless extra or missed notification.
       */

      if (ctx->pfd &&
          noteram_unread_length(drv, ctx->read) >= ctx->threshold)
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
 * Name: noteram_ratelimit
 *
 * Description:
 *   Check whether the instrumentation is limited.
 *
 * Input Parameters:
 *   driver - The channel of note driver
 *
 * Returned Value:
 *   True is returned if the instrumentation is limited.
 *
 ****************************************************************************/

static bool noteram_ratelimit(FAR struct noteram_driver_s *drv)
{
  bool ret;
  clock_t ticks;
  uint32_t seconds;
  FAR struct noteram_ratelimit_s *limit;

  limit = &drv->ratelimit;

  if (limit->interval == 0)
    {
      return false;
    }

  ticks = clock_systime_ticks();
  seconds = ticks * CONFIG_USEC_PER_TICK / 1000000;

  if (limit->begin == 0)
    {
      limit->begin = seconds;
    }

  /* Reset statistical information */

  if ((seconds - limit->begin) >= limit->interval)
    {
      limit->begin = seconds;
      limit->printed = 0;
    }

  /* Check if the note is limited */

  if (limit->burst && limit->burst > limit->printed)
    {
      limit->printed++;
      ret = false;
    }
  else
    {
      ret = true;
    }

  return ret;
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
 *   None
 *
 * Assumptions:
 *   We are within a critical section.
 *   The buffer is not empty (caller must check noteram_length > 0).
 *
 ****************************************************************************/

static void noteram_remove(FAR struct noteram_driver_s *drv)
{
  uint32_t tail;
  uint32_t length;

  /* Get the tail index of the circular buffer */

  tail = drv->header->tail;

  /* Get the length of the note at the tail index */

  length = drv->header->buffer[noteram_offset(drv, tail)];
  DEBUGASSERT(length <= noteram_length(drv));

  /* Increment the tail index to remove the entire note from the circular
   * buffer.
   */

  drv->header->tail = noteram_next(drv, tail, length);
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
  spin_lock_init(&ctx->lock);

  filep->f_priv = ctx;
  noteram_dump_init_context(ctx);
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
  FAR struct lib_memoutstream_s stream;
  irqstate_t flags;
  ssize_t ret = 0;

  flags = spin_lock_irqsave_notrace(&ctx->lock);

  if (ctx->mode == NOTE_MODE_READ_BINARY)
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

      spin_unlock_irqrestore_notrace(&ctx->lock, flags);
      return nread > 0 ? nread : ret;
    }
  else
    {
      lib_memoutstream(&stream, buffer, buflen);

      do
        {
          struct note_max_s note;

          /* Get the next note (removing it from the buffer) */

          ret = noteram_get(drv, (FAR uint8_t *)&note, sizeof(note),
                            &ctx->read);
          if (ret <= 0)
            {
              spin_unlock_irqrestore_notrace(&ctx->lock, flags);
              return ret;
            }

          /* Parse notes into text format */

          ret = noteram_dump_one(&note,
                                 (FAR struct lib_outstream_s *)&stream, ctx);
        }
      while (ret == 0);

      spin_unlock_irqrestore_notrace(&ctx->lock, flags);
      return stream.common.nput;
    }
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
        drv->header->tail = drv->header->head;
        if (drv->overwrite == NOTE_MODE_OVERWRITE_OVERFLOW)
          {
            drv->overwrite = NOTE_MODE_OVERWRITE_DISABLE;
          }

        rspin_unlock_irqrestore_notrace(&drv->lock, flags);
        ret = OK;
        break;

      /* NOTE_GETMODE
       *      - Get overwrite mode
       *        Argument: A writable pointer to unsigned int
       */

      case NOTE_GETMODE:
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

      /* NOTE_SETMODE
       *      - Set overwrite mode
       *        Argument: A read-only pointer to unsigned int
       */

      case NOTE_SETMODE:
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
            flags = spin_lock_irqsave_notrace(&ctx->lock);
            *(FAR unsigned int *)arg = ctx->mode;
            spin_unlock_irqrestore_notrace(&ctx->lock, flags);
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
            flags = spin_lock_irqsave_notrace(&ctx->lock);
            ctx->mode = arg;
            spin_unlock_irqrestore_notrace(&ctx->lock, flags);
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

      case NOTE_GETFREQ:
        *(FAR unsigned long *)arg = perf_getfreq();
        ret = OK;
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
        flags = spin_lock_irqsave_notrace(&ctx->lock);
        ctx->threshold = (uint32_t)arg;
        spin_unlock_irqrestore_notrace(&ctx->lock, flags);
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
                                           false, true);
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

static void noteram_add(FAR struct note_driver_s *driver,
                        FAR const void *note, size_t notelen,
                        bool noswitches)
{
  FAR const char *buf = note;
  FAR struct noteram_driver_s *drv = (FAR struct noteram_driver_s *)driver;
  uint32_t head;
  uint32_t remain;
  uint32_t space;
  irqstate_t flags;

  noteram_driver_initialize();

  flags = rspin_lock_irqsave_notrace(&drv->lock);

  /* Initialize the noteram header if it has not been initialized yet. */

  noteram_header_init(drv);

  if (noteram_ratelimit(drv))
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

      /* Remove the note at the tail index, make sure there is enough space
       */

      do
        {
          noteram_remove(drv);
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

out:
  rspin_unlock_irqrestore_notrace(&drv->lock, flags);
  noteram_timeout_reset(drv);
}

/****************************************************************************
 * Name: noteram_dump_init_context
 ****************************************************************************/

static void noteram_dump_init_context(FAR struct noteram_dump_context_s *ctx)
{
  int cpu;

  /* Initialize the trace dump context */

  for (cpu = 0; cpu < NCPUS; cpu++)
    {
      ctx->cpu[cpu].intr_nest = 0;
      ctx->cpu[cpu].pendingswitch = false;
      ctx->cpu[cpu].current_state = TSTATE_TASK_RUNNING;
      ctx->cpu[cpu].current_pid = -1;
      ctx->cpu[cpu].next_pid = -1;
      ctx->cpu[cpu].current_priority = -1;
      ctx->cpu[cpu].next_priority = -1;
    }
}

/****************************************************************************
 * Name: noteram_dump_header
 ****************************************************************************/

static int noteram_dump_header(FAR struct lib_outstream_s *s,
                               FAR const struct note_common_s *note,
                               FAR struct noteram_dump_context_s *ctx)
{
  struct timespec ts;
  char buf[TASK_NAME_SIZE];
  pid_t pid;
  int ret;

  NOTE_PERF_CONVERT(note->nc_systime, &ts);
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

#if (defined CONFIG_SCHED_INSTRUMENTATION_SWITCH)                            \
    || (defined CONFIG_SCHED_INSTRUMENTATION_IRQHANDLER)
/****************************************************************************
 * Name: noteram_dump_sched_switch
 ****************************************************************************/

static int noteram_dump_sched_switch(FAR struct lib_outstream_s *s,
                                     FAR const struct note_common_s *note,
                                     FAR struct noteram_dump_context_s *ctx)
{
  FAR struct noteram_dump_cpu_context_s *cctx;
  char current_buf[TASK_NAME_SIZE];
  char next_buf[TASK_NAME_SIZE];
  uint8_t current_priority;
  uint8_t next_priority;
  pid_t current_pid;
  pid_t next_pid;
  int ret;
#ifdef CONFIG_SMP
  int cpu = note->nc_cpu;
#else
  int cpu = 0;
#endif

  cctx = &ctx->cpu[cpu];
  current_pid = cctx->current_pid;
  next_pid = cctx->next_pid;

  current_priority = cctx->current_priority;
  next_priority = cctx->next_priority;

  nxtask_namepool_get(current_pid, current_buf, TASK_NAME_SIZE);
  nxtask_namepool_get(next_pid, next_buf, TASK_NAME_SIZE);

  ret = lib_sprintf(s, "sched_switch: prev_comm=%s prev_pid=%u "
                    "prev_prio=%u prev_state=%c ==> "
                    "next_comm=%s next_pid=%u next_prio=%u\n",
                    current_buf, get_pid(current_pid),
                    current_priority, get_task_state(cctx->current_state),
                    next_buf, get_pid(next_pid),
                    next_priority);

  cctx->current_pid = cctx->next_pid;
  cctx->current_priority = cctx->next_priority;
  cctx->pendingswitch = false;
  return ret;
}
#endif

/****************************************************************************
 * Name: noteram_dump_printf
 ****************************************************************************/

#ifdef CONFIG_SCHED_INSTRUMENTATION_DUMP
static int noteram_dump_printf(FAR struct lib_outstream_s *s,
                               FAR struct note_printf_s *note)
{
  size_t ret = 0;

  if (note->npt_type == 0)
    {
      ret = lib_osprintf(s, (FAR const char *)note->npt_fmt, note->npt_data);
    }
  else
    {
      size_t count = NOTE_PRINTF_GET_COUNT(note->npt_type);
      char fmt[128];
      size_t i;

      fmt[0] = '\0';
      ret += lib_sprintf(s, "%p", (FAR void *)note->npt_fmt);
      for (i = 0; i < count; i++)
        {
          int type = NOTE_PRINTF_GET_TYPE(note->npt_type, i);

          switch (type)
            {
              case NOTE_PRINTF_UINT32:
                {
                  strcat(fmt, " %u");
                }
                break;
              case NOTE_PRINTF_UINT64:
                {
                  strcat(fmt, " %llu");
                }
                break;
              case NOTE_PRINTF_STRING:
                {
                  strcat(fmt, " %s");
                }
                break;
              case NOTE_PRINTF_DOUBLE:
                {
                  strcat(fmt, " %f");
                }
            }
        }

        ret += lib_osprintf(s, fmt, note->npt_data);
    }

  return ret;
}
#endif

/****************************************************************************
 * Name: noteram_dump_one
 ****************************************************************************/

static int noteram_dump_one(FAR const struct note_max_s *p,
                            FAR struct lib_outstream_s *s,
                            FAR struct noteram_dump_context_s *ctx)
{
  FAR const struct note_common_s *note = &p->cmn;
  FAR struct noteram_dump_cpu_context_s *cctx;
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

  cctx = &ctx->cpu[cpu];
  pid = note->nc_pid;

  if (cctx->current_pid < 0)
    {
      cctx->current_pid = pid;
    }

  /* Output one note */

  switch (note->nc_type)
    {
#ifdef CONFIG_SCHED_INSTRUMENTATION_SWITCH
    case NOTE_START:
      {
        nxtask_namepool_get(pid, buf, TASK_NAME_SIZE);
        ret += noteram_dump_header(s, note, ctx);
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

        cctx->current_state = 0;
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

        cctx->current_state = nsu->nsu_state;

#if CONFIG_SCHED_INSTRUMENTATION_SWITCH_BACKTRACE > 0
        ret += lib_sprintf(s, "   <-");
        for (i = 0; i < CONFIG_SCHED_INSTRUMENTATION_SWITCH_BACKTRACE;
             i++)
          {
            if (nsu->nsu_backtrace[i] == 0)
              {
                break;
              }

            ret += lib_sprintf(s, " 0x%" PRIxPTR,
                               nsu->nsu_backtrace[i]);
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

        cctx->next_pid = pid;
        cctx->next_priority = note->nc_priority;

        if (cctx->intr_nest == 0)
          {
            /* If not in the interrupt context, the task switch is
             * executed immediately.
             */

            ret += noteram_dump_header(s, note, ctx);
            ret += noteram_dump_sched_switch(s, note, ctx);
          }
        else
          {
            /* If in the interrupt context, the task switch is postponed
             * until leaving the interrupt handler.
             */

            nxtask_namepool_get(cctx->next_pid, buf, TASK_NAME_SIZE);
            ret += noteram_dump_header(s, note, ctx);
            ret += lib_sprintf(s, "sched_waking: comm=%s "
                               "pid=%d target_cpu=%d\n",
                               buf, get_pid(cctx->next_pid), cpu);
            cctx->pendingswitch = true;
          }
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

        ret += noteram_dump_header(s, note, ctx);
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

        ret += noteram_dump_header(s, note, ctx);
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
        ret += noteram_dump_header(s, note, ctx);
        ret += lib_sprintf(s, "irq_handler_entry: irq=%u name=%pS\n",
                           nih->nih_irq, (FAR void *)nih->nih_handler);
        cctx->intr_nest++;
      }
      break;

    case NOTE_IRQ_LEAVE:
      {
        FAR struct note_irqhandler_s *nih;

        nih = (FAR struct note_irqhandler_s *)p;
        ret += noteram_dump_header(s, note, ctx);
        ret += lib_sprintf(s, "irq_handler_exit: irq=%u ret=handled\n",
                           nih->nih_irq);
        cctx->intr_nest--;

        if (cctx->intr_nest <= 0)
          {
            cctx->intr_nest = 0;
            if (cctx->pendingswitch)
              {
                /* If the pending task switch exists, it is executed here */

                ret += noteram_dump_header(s, note, ctx);
                ret += noteram_dump_sched_switch(s, note, ctx);
              }
          }
      }
      break;
#endif

#ifdef CONFIG_SCHED_INSTRUMENTATION_WDOG
    case NOTE_WDOG_START:
    case NOTE_WDOG_CANCEL:
    case NOTE_WDOG_ENTER:
    case NOTE_WDOG_LEAVE:
      {
        FAR struct note_wdog_s *nw;
        FAR const char *name[] =
          {
            "start", "cancel", "enter", "leave",
          };

        nw = (FAR struct note_wdog_s *)p;
        ret += noteram_dump_header(s, note, ctx);
        ret += lib_sprintf(s, "tracing_mark_write: I|%d|wdog: %s-%pS %p\n",
                           pid, name[note->nc_type - NOTE_WDOG_START],
                           (FAR void *)nw->handler, (FAR void *)nw->arg);
      }
      break;
#endif

#ifdef CONFIG_SCHED_INSTRUMENTATION_CSECTION
    case NOTE_CSECTION_ENTER:
    case NOTE_CSECTION_LEAVE:
      {
        struct note_csection_s *ncs;
        ncs = (FAR struct note_csection_s *)p;
        ret += noteram_dump_header(s, &ncs->ncs_cmn, ctx);
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
        ret += noteram_dump_header(s, &npr->npr_cmn, ctx);
        ret += lib_sprintf(s,  "tracing_mark_write: "
                          "%c|%d|sched_lock:%d\n",
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
        ret += noteram_dump_header(s, &npt->npt_cmn, ctx);
        ret += lib_sprintf(s, "tracing_mark_write: I|%d|", pid);
        ret += noteram_dump_printf(s, npt);
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
        ret += noteram_dump_header(s, &nbi->nev_cmn, ctx);
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

        ret += noteram_dump_header(s, &nbi->nev_cmn, ctx);
        ret += lib_sprintf(s, "tracing_mark_write: I|%d|%.*s\n",
                           pid, len, (FAR const char *)nbi->nev_data);
      }
      break;
      case NOTE_DUMP_BINARY:
      {
        FAR struct note_event_s *nbi = (FAR struct note_event_s *)p;
        int len = note->nc_length - SIZEOF_NOTE_EVENT(0);
        int i;

        ret += noteram_dump_header(s, &nbi->nev_cmn, ctx);
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
        counter = (FAR struct note_counter_s *)nbi->nev_data;
        ret += noteram_dump_header(s, &nbi->nev_cmn, ctx);
        ret += lib_sprintf(s, "tracing_mark_write: C|%d|%s|%" PRId64 "\n",
                           pid, counter->name, counter->value);
      }
      break;
    case NOTE_DUMP_THREADTIME:
      {
        FAR struct note_event_s *nev = (FAR struct note_event_s *)p;
        FAR struct note_threadtime_s *nts = (FAR struct note_threadtime_s *)
                                            nev->nev_data;
        ret += noteram_dump_header(s, &nev->nev_cmn, ctx);
        ret += lib_sprintf(s, "tracing_mark_write: I|%d|elapsed:%llu\n",
                          pid, (unsigned long long)nts->elapsed);
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

        cookie = nas->cookie;
        nlen = strnlen(nas->names, total) + 1;
        if (nlen > total)
          {
            nlen = total;
          }

        ret += noteram_dump_header(s, &nev->nev_cmn, ctx);
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

        ret += noteram_dump_header(s, &nmm->nhp_cmn, ctx);
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

#ifdef CONFIG_DRIVERS_NOTERAM_CRASH_DUMP

/****************************************************************************
 * Name: noteram_dump
 ****************************************************************************/

static void noteram_dump(FAR struct noteram_driver_s *drv)
{
  struct noteram_dump_context_s ctx;
  struct lib_syslograwstream_s stream;
  struct note_max_s note;
  uint32_t read;

  memset(&ctx, 0, sizeof(ctx));
  read = drv->header->tail;
  lib_syslograwstream_open(&stream);
  lib_sprintf(&stream.common, "# tracer:nop\n#\n");

  while (1)
    {
      ssize_t ret;

      ret = noteram_get(drv, (FAR uint8_t *)&note, sizeof(note), &read);
      if (ret <= 0)
        {
          break;
        }

      noteram_dump_one(&note, &stream.common, &ctx);
    }

  lib_syslograwstream_close(&stream);
}

/****************************************************************************
 * Name: noteram_crash_dump
 ****************************************************************************/

static int noteram_crash_dump(FAR struct notifier_block *nb,
                              unsigned long action, FAR void *data)
{
  FAR struct noteram_driver_s *drv =
      container_of(nb, struct noteram_driver_s, nb);
  if (action == PANIC_KERNEL)
    {
      noteram_dump(drv);
    }

  return 0;
}

static void noteram_crash_dump_register(FAR struct noteram_driver_s *drv)
{
  drv->nb.notifier_call = noteram_crash_dump;
  panic_notifier_chain_register(&drv->nb);
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: noteram_register
 *
 * Description:
 *   Register a serial driver at /dev/note/ram that can be used by an
 *   application to read data from the circular note buffer.
 *
 * Input Parameters:
 *   None.
 *
 * Returned Value:
 *   Zero on succress. A negated errno value is returned on a failure.
 *
 ****************************************************************************/

int noteram_register(void)
{
#ifdef CONFIG_DRIVERS_NOTERAM_CRASH_DUMP
  noteram_crash_dump_register(&g_noteram_driver);
#endif
  return 0;
}

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
 *  crashdump: If used by the crash dump
 *  regnote: If register the note driver
 *
 * Returned Value:
 *   Zero on succress. A negated errno value is returned on a failure.
 *
 ****************************************************************************/

FAR struct note_driver_s *
noteram_initialize(FAR const char *devname, size_t bufsize,
                   bool overwrite, bool crashdump, bool regnote)
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
                                       overwrite, crashdump, regnote);
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
 *  crashdump: If used by the crash dump
 *  regnote: If register the note driver
 *
 * Returned Value:
 *   Zero on succress. A negated errno value is returned on a failure.
 *
 ****************************************************************************/

FAR struct note_driver_s *
noteram_initialize_with_buffer(FAR const char *devname,
                               FAR void *buffer, size_t bufsize,
                               bool overwrite, bool crashdump, bool regnote)
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
    CONFIG_SCHED_INSTRUMENTATION_FILTER_DEFAULT_MODE;

#  ifdef CONFIG_SMP
  drv->driver.filter.mode.cpuset =
    CONFIG_SCHED_INSTRUMENTATION_CPUSET;
#  endif
#endif

#ifdef CONFIG_DRIVERS_NOTERAM_CRASH_DUMP
  if (crashdump)
    {
      noteram_crash_dump_register(drv);
    }
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

#ifdef CONFIG_DRIVERS_NOTERAM_CRASH_DUMP
  panic_notifier_chain_unregister(&noteram_drv->nb);
#endif

  note_driver_unregister(drv);

  unregister_driver((char *)(noteram_drv + 1));

  if (atomic_sub(&noteram_drv->count_ref, 1) == 1)
    {
      noteram_free(noteram_drv);
    }
}
