/****************************************************************************
 * include/nuttx/mm/iob.h
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

#ifndef __INCLUDE_NUTTX_MM_IOB_H
#define __INCLUDE_NUTTX_MM_IOB_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/compiler.h>

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <sys/param.h>
#include <nuttx/mm/mm.h>
#include <nuttx/mm/mempool.h>

#ifdef CONFIG_IOB_NOTIFIER
#  include <nuttx/wqueue.h>
#endif

#ifdef CONFIG_NET_TIMESTAMP
#  include <sys/time.h>
#endif

#ifdef CONFIG_MM_IOB

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Configuration ************************************************************/

/* I/O buffer allocation logic supports a throttle value for read-ahead
 * buffering to prevent the read-ahead logic from consuming all available I/O
 * buffers and blocking the write buffering logic.  This throttle logic
 * is only needed if both write buffering and read-ahead buffering are used.
 *
 * The correct way to disable throttling is to set the throttle value to
 * zero.
 */

#if !defined(CONFIG_IOB_THROTTLE)
#  define CONFIG_IOB_THROTTLE 0
#endif

/* Some I/O buffers should be allocated */

#if !defined(CONFIG_IOB_NBUFFERS)
#  warning CONFIG_IOB_NBUFFERS not defined
#  define CONFIG_IOB_NBUFFERS 0
#endif

#if CONFIG_IOB_NBUFFERS < 1
#  error CONFIG_IOB_NBUFFERS is zero
#endif

#if CONFIG_IOB_NBUFFERS <= CONFIG_IOB_THROTTLE
#  error CONFIG_IOB_NBUFFERS <= CONFIG_IOB_THROTTLE
#endif

/* Default config of alignment and head padding size */

#define IOB_ALIGNMENT           MAX(CONFIG_IOB_ALIGNMENT, MM_ALIGN)
#define IOB_ALIGN_SIZE(blksize) ALIGN_UP(offsetof(struct iob_s, io_data) + \
                                         blksize, IOB_ALIGNMENT)

/* IOB helpers */

#define IOB_DATA(p)      (&(p)->io_data[(p)->io_offset])
#define IOB_FREESPACE(p) (IOB_BUFSIZE(p) - (p)->io_len - (p)->io_offset)

/* Queue helpers */

#define IOB_QINIT(q)   do { (q)->qh_head = 0; (q)->qh_tail = 0; } while (0)
#define IOB_QEMPTY(q)  ((q)->qh_head == NULL)

/* IOB queue flags: Stored in the low bits of io_flink pointer.
 * Since IOB structures are aligned to at least sizeof(uintptr_t),
 * the low 2 bits (32-bit) or 3 bits (64-bit) are always zero and
 * can be used to store flags.
 *
 * IOB_CHAIN_END: Marks the last IOB in a chain within a queue.
 * When this flag is set, io_flink (after masking) points to the next
 * chain head in the queue.
 */

#define IOB_CHAIN_END     (1 << 0)
#define IOB_FLAGS_MASK    ((uintptr_t)0x3)  /* Mask for flag bits */
#define IOB_POINTER_MASK  (~IOB_FLAGS_MASK) /* Mask for pointer */

/* Helper macros to manipulate io_flink with embedded flags */

#define IOB_LINK_GET(iob)       ((FAR struct iob_s *)((uintptr_t)(iob)->io_flink & IOB_POINTER_MASK))
#define IOB_FLAG_GET(iob)       ((uintptr_t)(iob)->io_flink & IOB_FLAGS_MASK)
#define IOB_FLAG_SET(iob, ptr, flags) \
   do { \
     (iob)->io_flink = (FAR struct iob_s *)((uintptr_t)(ptr) | ((flags) & IOB_FLAGS_MASK)); \
   } while (0)
#define IOB_FLAG_TEST(iob, flag) (IOB_FLAG_GET(iob) & (flag))

#if CONFIG_IOB_NPOOLS > 1
#  define IOB_BUFSIZE(p) ((size_t)(uintptr_t)(p)->io_pool->priv)
#else
#  define IOB_BUFSIZE(p) CONFIG_IOB_BUFSIZE
#endif

/****************************************************************************
 * Public Types
 ****************************************************************************/

typedef CODE void (*iob_free_cb_t)(FAR void *data);

/* Represents one I/O buffer.  A packet is contained by one or more I/O
 * buffers in a chain.  The io_pktlen is only valid for the I/O buffer at
 * the head of the chain.
 */

struct iob_s
{
  /* Singly-link list support */

  FAR struct iob_s *io_flink;

  /* Payload */

#if CONFIG_IOB_BUFSIZE < 256
  uint8_t  io_len;      /* Length of the data in the entry */
  uint8_t  io_offset;   /* Data begins at this offset */
#else
  uint16_t io_len;      /* Length of the data in the entry */
  uint16_t io_offset;   /* Data begins at this offset */
#endif
  unsigned int io_pktlen; /* Total length of the packet */

  FAR struct socket_conn_s *io_conn;

#ifdef CONFIG_NET_TIMESTAMP
  /* timestamp of the packet.
   * d_features is the member of net_driver_s struct, if the NETDEV_RX_STAMP
   * bit of d_features is set, the timestamp is provided by hardware driver.
   * Otherwise it is filled in by kernel when the packet is passed into
   * respective protocol layer. The timestamp is in CLOCK_REALTIME.
   */

  struct timespec io_time;
#endif

#if CONFIG_IOB_NPOOLS > 1
  FAR struct mempool_s *io_pool; /* Reference to the mempool from which
                                  * this IOB was allocated
                                  */
#endif

  uint8_t io_data[1] aligned_data(IOB_ALIGNMENT);
};

/* The I/O buffer queue head structure.
 * Now directly links IOB chains without separate qentry allocation.
 */

struct iob_queue_s
{
  /* Head of the I/O buffer chain list */

  FAR struct iob_s *qh_head;
  FAR struct iob_s *qh_tail;
};

struct iob_stats_s
{
  int ntotal;
  int nfree;
  int nwait;
  int nthrottle;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: iob_timedalloc_with_size
 *
 * Description:
 *  Allocate an I/O buffer with specified size. This wait will be terminated
 *  when the specified timeout expires. Useful for multi-level mempool
 *  configurations.
 *
 * Input Parameters:
 *   size       - The size of buffer to allocate
 *   throttled  - An indication of the IOB allocation is "throttled"
 *   timeout    - Timeout value in milliseconds.
 *
 ****************************************************************************/

FAR struct iob_s *iob_timedalloc_with_size(uint16_t size, bool throttled,
                                           unsigned int timeout);

/****************************************************************************
 * Name: iob_timedalloc
 *
 * Description:
 *  Allocate an I/O buffer by taking the buffer at the head of the free list.
 *  This wait will be terminated when the specified timeout expires.
 *
 * Input Parameters:
 *   throttled  - An indication of the IOB allocation is "throttled"
 *   timeout    - Timeout value in milliseconds.
 *
 ****************************************************************************/

static inline_function
FAR struct iob_s *iob_timedalloc(bool throttled, unsigned int timeout)
{
  return iob_timedalloc_with_size(CONFIG_IOB_BUFSIZE, throttled, timeout);
}

/****************************************************************************
 * Name: iob_alloc
 *
 * Description:
 *   Allocate an I/O buffer by taking the buffer at the head of the free
 *   list.
 *
 ****************************************************************************/

static inline_function FAR struct iob_s *iob_alloc(bool throttled)
{
  return iob_timedalloc(throttled, UINT_MAX);
}

/****************************************************************************
 * Name: iob_tryalloc_with_size
 *
 * Description:
 *   Try to allocate an I/O buffer with specified size without waiting
 *   for a buffer to become free. Useful for multi-level mempool
 *   configurations.
 *
 * Input Parameters:
 *   size       - The size of buffer to allocate
 *   throttled  - An indication of the IOB allocation is "throttled"
 *
 ****************************************************************************/

static inline_function FAR struct iob_s *
iob_tryalloc_with_size(uint16_t size, bool throttled)
{
  return iob_timedalloc_with_size(size, throttled, 0);
}

/****************************************************************************
 * Name: iob_tryalloc
 *
 * Description:
 *   Try to allocate an I/O buffer by taking the buffer at the head of the
 *   free list without waiting for a buffer to become free.
 *
 ****************************************************************************/

static inline_function FAR struct iob_s *iob_tryalloc(bool throttled)
{
  return iob_tryalloc_with_size(CONFIG_IOB_BUFSIZE, throttled);
}

/****************************************************************************
 * Name: iob_alloc_with_size
 *
 * Description:
 *  Allocate an I/O buffer with specified size by taking the buffer from
 *  the appropriate size pool. Useful for multi-level mempool configurations.
 *
 * Input Parameters:
 *   size       - The size of buffer to allocate
 *   throttled  - An indication of the IOB allocation is "throttled"
 *
 ****************************************************************************/

static inline_function
FAR struct iob_s *iob_alloc_with_size(uint16_t size, bool throttled)
{
  return iob_timedalloc_with_size(size, throttled, UINT_MAX);
}

/****************************************************************************
 * Name: iob_navail_with_size
 *
 * Description:
 *   Return the number of available IOBs.
 *
 ****************************************************************************/

int iob_navail_with_size(size_t size, bool throttled);

/****************************************************************************
 * Name: iob_navail
 *
 * Description:
 *   Return the number of available IOBs.
 *
 ****************************************************************************/

static inline_function
int iob_navail(bool throttled)
{
  return iob_navail_with_size(CONFIG_IOB_BUFSIZE, throttled);
}

/****************************************************************************
 * Name: iob_free
 *
 * Description:
 *   Free the I/O buffer at the head of a buffer chain returning it to the
 *   free list.  The link to  the next I/O buffer in the chain is return.
 *
 ****************************************************************************/

FAR struct iob_s *iob_free(FAR struct iob_s *iob);

/****************************************************************************
 * Name: iob_notifier_setup
 *
 * Description:
 *   Set up to perform a callback to the worker function when an IOB is
 *   available.  The worker function will execute on the selected priority
 *   worker thread.
 *
 * Input Parameters:
 *   qid    - Selects work queue.  Must be HPWORK or LPWORK.
 *   worker - The worker function to execute on the high priority work queue
 *            when the event occurs.
 *   arg    - A user-defined argument that will be available to the worker
 *            function when it runs.
 *
 * Returned Value:
 *   > 0   - The signal notification is in place.  The returned value is a
 *           key that may be used later in a call to
 *           iob_notifier_teardown().
 *   == 0  - There are already free IOBs.  No signal notification will be
 *           provided.
 *   < 0   - An unexpected error occurred and no signal will be sent.  The
 *           returned value is a negated errno value that indicates the
 *           nature of the failure.
 *
 ****************************************************************************/

#ifdef CONFIG_IOB_NOTIFIER
int iob_notifier_setup(int qid, worker_t worker, FAR void *arg);
#endif

/****************************************************************************
 * Name: iob_notifier_teardown
 *
 * Description:
 *   Eliminate an IOB notification previously setup by iob_notifier_setup().
 *   This function should only be called if the notification should be
 *   aborted prior to the notification.  The notification will automatically
 *   be torn down after the signal is sent.
 *
 * Input Parameters:
 *   key - The key value returned from a previous call to
 *         iob_notifier_setup().
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

#ifdef CONFIG_IOB_NOTIFIER
void iob_notifier_teardown(int key);
#endif

/****************************************************************************
 * Name: iob_free_chain
 *
 * Description:
 *   Free an entire buffer chain, starting at the beginning of the I/O
 *   buffer chain
 *
 ****************************************************************************/

void iob_free_chain(FAR struct iob_s *iob);

/****************************************************************************
 * Name: iob_add_queue
 *
 * Description:
 *   Add one I/O buffer chain to the end of a queue.  May fail due to lack
 *   of resources.
 *
 ****************************************************************************/

void iob_add_queue(FAR struct iob_s *iob, FAR struct iob_queue_s *iobq);

/****************************************************************************
 * Name: iob_concat_queue
 *
 * Description:
 *   Concatenate iob_s queue src to dest
 *
 ****************************************************************************/

void iob_concat_queue(FAR struct iob_queue_s *dest,
                      FAR struct iob_queue_s *src);

/****************************************************************************
 * Name: iob_remove_queue
 *
 * Description:
 *   Remove and return one I/O buffer chain from the head of a queue.
 *
 * Returned Value:
 *   Returns a reference to the I/O buffer chain at the head of the queue.
 *
 ****************************************************************************/

FAR struct iob_s *iob_remove_queue(FAR struct iob_queue_s *iobq);

/****************************************************************************
 * Name: iob_peek_queue
 *
 * Description:
 *   Return a reference to the I/O buffer chain at the head of a queue. This
 *   is similar to iob_remove_queue except that the I/O buffer chain is in
 *   place at the head of the queue.  The I/O buffer chain may safely be
 *   modified by the caller but must be removed from the queue before it can
 *   be freed.
 *
 * Returned Value:
 *   Returns a reference to the I/O buffer chain at the head of the queue.
 *
 ****************************************************************************/

FAR struct iob_s *iob_peek_queue(FAR struct iob_queue_s *iobq);

/****************************************************************************
 * Name: iob_free_queue
 *
 * Description:
 *   Free an entire queue of I/O buffer chains.
 *
 ****************************************************************************/

void iob_free_queue(FAR struct iob_queue_s *qhead);

/****************************************************************************
 * Name: iob_free_queue_qentry
 *
 * Description:
 *   Free an iob entire queue of I/O buffer chains.
 *
 ****************************************************************************/

void iob_free_queue_qentry(FAR struct iob_s *iob,
                           FAR struct iob_queue_s *iobq);

/****************************************************************************
 * Name: iob_get_queue_size
 *
 * Description:
 *   Queue helper for get the iob queue buffer size.
 *
 ****************************************************************************/

unsigned int iob_get_queue_size(FAR struct iob_queue_s *queue);
unsigned int iob_get_queue_entry_count(FAR struct iob_queue_s *queue);

/****************************************************************************
 * Name: iob_copyin
 *
 * Description:
 *  Copy data 'len' bytes from a user buffer into the I/O buffer chain,
 *  starting at 'offset', extending the chain as necessary.
 *
 ****************************************************************************/

int iob_copyin(FAR struct iob_s *iob, FAR const uint8_t *src,
               unsigned int len, int offset, bool throttled);

/****************************************************************************
 * Name: iob_trycopyin
 *
 * Description:
 *  Copy data 'len' bytes from a user buffer into the I/O buffer chain,
 *  starting at 'offset', extending the chain as necessary BUT without
 *  waiting if buffers are not available.
 *
 ****************************************************************************/

int iob_trycopyin(FAR struct iob_s *iob, FAR const uint8_t *src,
                  unsigned int len, int offset, bool throttled);

/****************************************************************************
 * Name: iob_copyout
 *
 * Description:
 *  Copy data 'len' bytes of data into the user buffer starting at 'offset'
 *  in the I/O buffer, returning that actual number of bytes copied out.
 *
 ****************************************************************************/

int iob_copyout(FAR uint8_t *dest, FAR const struct iob_s *iob,
                unsigned int len, int offset);

/****************************************************************************
 * Name: iob_tailroom
 *
 * Description:
 *  Return the number of bytes at the tail of the I/O buffer chain which
 *  can be used to append data without additional allocations.
 *
 ****************************************************************************/

unsigned int iob_tailroom(FAR struct iob_s *iob);

/****************************************************************************
 * Name: iob_clone
 *
 * Description:
 *   Duplicate (and pack) the data in iob1 in iob2.  iob2 must be empty.
 *
 ****************************************************************************/

int iob_clone(FAR struct iob_s *iob1, FAR struct iob_s *iob2,
              bool throttled, bool block);

/****************************************************************************
 * Name: iob_clone_partial
 *
 * Description:
 *   Duplicate the data from partial bytes of iob1 to iob2
 *
 * Input Parameters:
 *   iob1      - Pointer to source iob_s
 *   len       - Number of bytes to copy
 *   offset1   - Offset of source iobs_s
 *   iob2      - Pointer to destination iob_s
 *   offset2   - Offset of destination iobs_s
 *   throttled - An indication of the IOB allocation is "throttled"
 *   block     - Flag of Enable/Disable nonblocking operation
 *
 * Returned Value:
 *   == 0  - Partial clone successfully.
 *   < 0   - No available to clone to destination iob.
 *
 ****************************************************************************/

int iob_clone_partial(FAR struct iob_s *iob1, unsigned int len,
                      int offset1, FAR struct iob_s *iob2,
                      int offset2, bool throttled, bool block);

/****************************************************************************
 * Name: iob_concat
 *
 * Description:
 *   Concatenate iob_s chain iob2 to iob1.
 *
 ****************************************************************************/

void iob_concat(FAR struct iob_s *iob1, FAR struct iob_s *iob2);

/****************************************************************************
 * Name: iob_trimhead
 *
 * Description:
 *   Remove bytes from the beginning of an I/O chain.  Emptied I/O buffers
 *   are freed and, hence, the beginning of the chain may change.
 *
 ****************************************************************************/

FAR struct iob_s *iob_trimhead(FAR struct iob_s *iob, unsigned int trimlen);

/****************************************************************************
 * Name: iob_trimhead_queue
 *
 * Description:
 *   Remove bytes from the beginning of an I/O chain at the head of the
 *   queue.  Emptied I/O buffers are freed and, hence, the head of the
 *   queue may change.
 *
 *   This function is just a wrapper around iob_trimhead() that assures that
 *   the iob at the head of queue is modified with the trimming operations.
 *
 * Returned Value:
 *   The new iob at the head of the queue is returned.
 *
 ****************************************************************************/

FAR struct iob_s *iob_trimhead_queue(FAR struct iob_queue_s *qhead,
                                     unsigned int trimlen);

/****************************************************************************
 * Name: iob_trimtail
 *
 * Description:
 *   Remove bytes from the end of an I/O chain.  Emptied I/O buffers are
 *   freed NULL will be returned in the special case where the entry I/O
 *   buffer chain is freed.
 *
 ****************************************************************************/

FAR struct iob_s *iob_trimtail(FAR struct iob_s *iob, unsigned int trimlen);

/****************************************************************************
 * Name: iob_pack
 *
 * Description:
 *   Pack all data in the I/O buffer chain so that the data offset is zero
 *   and all but the final buffer in the chain are filled.  Any emptied
 *   buffers at the end of the chain are freed.
 *
 ****************************************************************************/

FAR struct iob_s *iob_pack(FAR struct iob_s *iob);

/****************************************************************************
 * Name: iob_contig
 *
 * Description:
 *   Ensure that there is 'len' bytes of contiguous space at the beginning
 *   of the I/O buffer chain starting at 'iob'.
 *
 ****************************************************************************/

int iob_contig(FAR struct iob_s *iob, unsigned int len);

/****************************************************************************
 * Name: iob_reserve
 *
 * Description:
 *   Adjust headroom offset of iobs by reducing the tail room.
 *
 ****************************************************************************/

void iob_reserve(FAR struct iob_s *iob, unsigned int reserved);

/****************************************************************************
 * Name: iob_update_pktlen
 *
 * Description:
 *   This function will update packet length of the iob, it will be
 *   trimmed if the current length of the iob chain is greater than the
 *   new length, and will be grown if less than new length.
 *
 * Returned Value:
 *   The new effective iob packet length, or a negated errno value on error.
 *
 ****************************************************************************/

int iob_update_pktlen(FAR struct iob_s *iob, unsigned int pktlen,
                      bool throttled);

/****************************************************************************
 * Name: iob_count
 *
 * Description:
 *   Get iob entries count in chain.
 *
 ****************************************************************************/

int iob_count(FAR struct iob_s *iob);

/****************************************************************************
 * Name: iob_dump
 *
 * Description:
 *   Dump the contents of a I/O buffer chain
 *
 ****************************************************************************/

#ifdef CONFIG_DEBUG_FEATURES
void iob_dump(FAR const char *msg, FAR struct iob_s *iob, unsigned int len,
              unsigned int offset);
#else
#  define iob_dump(wrb)
#endif

#endif /* CONFIG_MM_IOB */
#endif /* __INCLUDE_NUTTX_MM_IOB_H */
