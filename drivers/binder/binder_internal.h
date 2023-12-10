/****************************************************************************
 * drivers/binder/binder_internal.h
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

#ifndef __DRIVERS_BINDER_BINDER_INTERNAL_H__
#define __DRIVERS_BINDER_BINDER_INTERNAL_H__

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <poll.h>
#include <nuttx/android/binder.h>
#include <nuttx/list.h>
#include <nuttx/mutex.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define PAGE_SHIFT          12U
#define PAGE_SIZE           (1U << PAGE_SHIFT)
#define PAGE_MASK           (~((1 << PAGE_SHIFT) - 1))

#define __ALIGN_MASK(x, mask)   (((x) + (mask)) & ~(mask))
#define ALIGN(x, a)             __ALIGN_MASK((x), ((typeof(x))(a) - 1))
#define IS_ALIGNED(x, a)        (((x) & ((a) - 1)) == 0)

#define SZ_4M               0x00400000

#define max(a, b)               ((a) > (b) ? (a) : (b))
#define min(a, b)               (((a) < (b)) ? (a) : (b))

#define put_value(val, ptr)           \
  ({                                  \
    __typeof__(*(ptr)) * __p = (ptr); \
    *(__p) = val;                     \
  })

#define get_value(val, ptr)           \
  ({                                  \
    __typeof__(*(ptr)) * __p = (ptr); \
    val = *(__p);                     \
  })

#ifdef CONFIG_DRIVERS_BINDER_DEBUG
enum
{
  BINDER_DEBUG_ERROR                = 1U << 0,
  BINDER_DEBUG_WARNING              = 1U << 1,
  BINDER_DEBUG_OPEN_CLOSE           = 1U << 2,
  BINDER_DEBUG_PRIORITY             = 1U << 3,
  BINDER_DEBUG_THREADS              = 1U << 4,
  BINDER_DEBUG_INTERNAL_REFS        = 1U << 5,
  BINDER_DEBUG_ALLOC_BUFFER         = 1U << 6,
  BINDER_DEBUG_FREE_BUFFER          = 1U << 7,
  BINDER_DEBUG_READ_WRITE           = 1U << 8,
  BINDER_DEBUG_USER_REFS            = 1U << 9,
  BINDER_DEBUG_FAILED_TRANSACTION   = 1U << 10,
  BINDER_DEBUG_DEAD_BINDER          = 1U << 11,
  BINDER_DEBUG_DEATH_NOTIFICATION   = 1U << 12,
  BINDER_DEBUG_DEAD_TRANSACTION     = 1U << 13,
  BINDER_DEBUG_TRANSACTION          = 1U << 14,
  BINDER_DEBUG_TRANSACTION_COMPLETE = 1U << 15,
  BINDER_DEBUG_SCHED                = 1U << 16,
};

#define BINDER_LOG_BUFSIZE  256
extern char binder_debug_log[BINDER_LOG_BUFSIZE];

void binder_syslog(FAR char * buff, int pos,
      FAR const char *fmt, ...) syslog_like(3, 4);

#define binder_debug(mask, x ...)                  \
  do                                               \
    {                                              \
      if (binder_debug_mask & mask)                \
        {                                          \
          int pos;                                 \
          pos = snprintf(binder_debug_log, 256,    \
                         "[%s (%d)][%d:%d][%s]:",  \
                         LOG_TAG, __LINE__,        \
                         getpid(), gettid(),       \
                         __func__);                \
          binder_syslog(binder_debug_log, pos, x); \
        }                                          \
    } while ( 0 )

#else
#define binder_debug(mask, x ...)
#endif

#define WARN_ON(condition)                   \
  do                                         \
    {                                        \
      if (condition)                         \
        {                                    \
          binder_debug(BINDER_DEBUG_WARNING, \
                 "[%s][%d:%d]:"              \
                 "warning at %s(%d)\n",      \
                 LOG_TAG, getpid(),          \
                 gettid(),                   \
                 __FILE__, __LINE__);        \
        }                                    \
    } while ( 0 )

#define BUG_ON(condition)  DEBUGASSERT(!(condition))

#define list_first_entry_or_null(list, type, member) \
  ({                                                 \
    list_is_empty(list) ?                            \
    NULL: (list_first_entry(list, type, member));    \
  })

/* debug info output mask */

extern uint32_t     binder_debug_mask;

extern unsigned int binder_last_debug_id;

/****************************************************************************
 * Public Types
 ****************************************************************************/

enum binder_deferred_state
{
  BINDER_DEFERRED_FLUSH = 0x01, BINDER_DEFERRED_RELEASE      = 0x02,
};

enum
{
  BINDER_LOOPER_STATE_REGISTERED    = 0x01,
  BINDER_LOOPER_STATE_ENTERED       = 0x02,
  BINDER_LOOPER_STATE_EXITED        = 0x04,
  BINDER_LOOPER_STATE_INVALID       = 0x08,
  BINDER_LOOPER_STATE_WAITING       = 0x10,
  BINDER_LOOPER_STATE_POLL          = 0x20,
};

typedef int (*wait_queue_func_t)(FAR void *arg, unsigned mode);

struct wait_queue_entry
{
  FAR void       *private;
  wait_queue_func_t func;
  struct list_node entry;
};

/**
 * struct binder_buffer - buffer used for binder transactions
 * entry:              entry alloc->buffers
 * rb_node:            node for allocated_buffers/free_buffers rb trees
 * free:               true if buffer is free
 * clear_on_free:      true if buffer must be zeroed after use
 * allow_user_free:    true if user is allowed to free buffer
 * async_transaction:  true if buffer is in use for an async txn
 * oneway_spam_suspect: true if total async allocate size just exceed
 * spamming detect threshold
 * debug_id:           unique ID for debugging
 * transaction:        pointer to associated struct binder_transaction
 * target_node:        struct binder_node associated with this buffer
 * data_size:          size of transaction data
 * offsets_size:       size of array of offsets
 * extra_buffers_size: size of space for other objects (like sg lists)
 * user_data:          user pointer to base of buffer space
 * pid:                pid to attribute the buffer to (caller)
 *
 * Bookkeeping structure for binder transaction buffers
 */

struct binder_buffer
{
  struct list_node entry;   /* free and allocated entries by address */
  struct list_node rb_node; /* free entry by size or allocated entry */
  unsigned free : 1;
  unsigned clear_on_free : 1;
  unsigned allow_user_free : 1;
  unsigned async_transaction : 1;
  unsigned oneway_spam_suspect : 1;
  unsigned debug_id : 27;

  FAR struct binder_transaction *transaction;

  FAR struct binder_node *target_node;
  int data_size;
  int offsets_size;
  FAR void *user_data;
  int pid;
};

/**
 * struct binder_page - page data object used for binder
 * page_ptr: pointer to page address in mmap'd area
 */

struct binder_page
{
  FAR void * page_ptr;
};

/**
 * struct binder_alloc - per-binder proc state for binder allocator
 * pid:                   pid for associated binder_proc
 *                         (invariant after init)
 * alloc_lock:            Protected lock for associated binder_proc
 * buffer_data:           base of per-proc address space mapped via mmap
 * buffer_data_size:      size of address space specified via mmap
 * buffers_list:          list of all buffers for this proc
 * free_buffers_list:     list of buffers available for allocation
 *                         sorted by size
 * allocated_buffers_list:rb tree of allocated buffers sorted by address
 * pages_array:           array of binder_lru_page
 *
 * Bookkeeping structure for per-proc address space management for binder
 * buffers. It is normally initialized during binder_init() and binder_mmap()
 * calls.
 */

struct binder_alloc
{
  pid_t pid;
  mutex_t alloc_lock;
  FAR void *buffer_data;
  size_t buffer_data_size;

  struct list_node buffers_list;
  struct list_node free_buffers_list;
  struct list_node allocated_buffers_list;

  FAR struct binder_page *pages_array;
};

/**
 * struct binder_priority - scheduler policy and priority
 */

struct binder_priority
{
  unsigned int sched_policy;
  int sched_prio;
};

/**
 * struct binder_context - information about a binder context node
 */

struct binder_context
{
  FAR struct binder_node * mgr_node;
  mutex_t context_lock;
};

/**
 * struct binder_device - information about a binder device node
 */

struct binder_device
{
  struct binder_context context;
  unsigned int ref_count;
  struct list_node binder_procs_list;
  mutex_t binder_procs_lock;
};

/**
 * struct binder_work - work enqueued on a worklist
 * There are separate work lists for proc, thread, and node (async).
 */

struct binder_work
{
  struct list_node entry_node;

  enum binder_work_type
  {
    BINDER_WORK_TRANSACTION                     = 1,
    BINDER_WORK_TRANSACTION_COMPLETE            = 2,
    BINDER_WORK_TRANSACTION_ONEWAY_SPAM_SUSPECT = 3,
    BINDER_WORK_RETURN_ERROR                    = 4,
    BINDER_WORK_NODE                            = 5,
    BINDER_WORK_DEAD_BINDER                     = 6,
    BINDER_WORK_DEAD_BINDER_AND_CLEAR           = 7,
    BINDER_WORK_CLEAR_DEATH_NOTIFICATION        = 8,
  } type;
};

struct binder_error
{
  struct binder_work work;
  uint32_t cmd;
};

/**
 * struct binder_thread - Bookkeeping structure for binder threads.
 */

struct binder_thread
{
  pid_t tid;
  FAR struct binder_proc *proc;
  struct list_node thread_node;
  struct list_node waiting_thread_node;
  struct list_node wait;
  struct list_node todo;
  int looper;
  FAR struct binder_transaction *transaction_stack;

  struct wait_queue_entry wq_entry;
  unsigned int tmp_ref;
  bool process_todo;
  bool is_dead;
  bool looper_need_return;
  struct binder_error return_error;
  struct binder_error reply_error;
};

/**
 * struct binder_proc - binder process bookkeeping
 */

struct binder_proc
{
  /* Fields used to support task identify and lock */

  pid_t pid;
  struct list_node proc_node;
  mutex_t proc_lock;

  /* Fields used to support link into different list */

  struct list_node threads;
  struct list_node nodes;
  struct list_node freeze_wait;
  struct list_node todo_list;
  struct list_node delivered_death;
  struct list_node waiting_threads;
  struct list_node refs_by_desc;
  struct list_node refs_by_node;

  /* Fields used to support binder priority set and get */

  struct binder_priority default_priority;

  /* Fields used to support binder allocator bookkeeping */

  struct binder_alloc alloc;

  /* binder_context for this proc */

  FAR struct binder_context *context;

  /* Maximum threads can be created by binder process */

  int max_threads;

  bool is_frozen;
  bool sync_recv;
  bool async_recv;
  bool is_dead;
  int outstanding_txns;
  int requested_threads;
  int requested_threads_started;
  int tmp_ref;
};

/**
 * struct binder_node - Bookkeeping structure for binder nodes.
 *
 * debug_id:             unique ID for debugging
 *                        (invariant after initialized)
 * node_lock:            lock for node fields
 * work:                 worklist element for node work
 * rb_node:              element for proc->nodes list
 * dead_node:            element for binder_dead_nodes list
 *                        (protected by binder_dead_nodes_lock)
 * proc:                 binder_proc that owns this node
 *                        (invariant after initialized)
 * refs:                 list of references on this node
 *                        (protected by lock)
 * internal_strong_refs: used to take strong references when
 *                        initiating a transaction
 * local_weak_refs:      weak user refs from local process
 * local_strong_refs:    strong user refs from local process
 *                        (protected by proc->inner_lock if proc
 *                        and by lock)
 * tmp_refs:             temporary kernel refs
 *                        (protected by proc->inner_lock while proc
 *                        is valid, and by binder_dead_nodes_lock
 *                        if proc is NULL. During inc/dec and node release
 *                        it is also protected by lock to provide safety
 *                        as the node dies and proc becomes NULL)
 * ptr:                  pointer for node
 *                        (invariant, no lock needed)
 * cookie:               cookie for node
 *                        (invariant, no lock needed)
 * has_strong_ref:       userspace notified of strong ref
 * pending_strong_ref:   userspace has acked notification of strong ref
 * has_weak_ref:         userspace notified of weak ref
 * pending_weak_ref:     userspace has acked notification of weak ref
 * has_async_transaction: async transaction to node in progress
 *                        (protected by node_lock)
 * sched_policy:         minimum scheduling policy for node
 *                        (invariant after initialized)
 * accept_fds:           file descriptor operations supported for node
 *                        (invariant after initialized)
 * min_priority:         minimum scheduling priority
 *                        (invariant after initialized)
 * inherit_rt:           inherit RT scheduling policy from caller
 * txn_security_ctx:     require sender's security context
 *                        (invariant after initialized)
 * async_todo:           list of async work items
 *                        (protected by proc->inner_lock)
 */

struct binder_node
{
  int debug_id;
  mutex_t node_lock;
  struct binder_work work;
  union
  {
    struct list_node rb_node;
    struct list_node dead_node;
  };

  FAR struct binder_proc *proc;
  struct list_node refs;
  int internal_strong_refs;
  int local_weak_refs;
  int local_strong_refs;
  int tmp_refs;
  binder_uintptr_t ptr;
  binder_uintptr_t cookie;
  struct
  {
    /* bitfield elements protected by proc lock */

    _uint8_t has_strong_ref : 1;
    _uint8_t pending_strong_ref : 1;
    _uint8_t has_weak_ref : 1;
    _uint8_t pending_weak_ref : 1;
  };

  struct
  {
    /* invariant after initialization */

    _uint8_t sched_policy : 2;
    _uint8_t inherit_rt : 1;
    _uint8_t accept_fds : 1;
    _uint8_t txn_security_ctx : 1;
    _uint8_t min_priority;
  };

  bool has_async_transaction;
  struct list_node async_todo;
};

struct binder_ref_death
{
  /**
   * work: worklist element for death notifications
   *        (protected by inner_lock of the proc that
   *        this ref belongs to)
   */

  struct binder_work work;
  binder_uintptr_t cookie;
};

/**
 * struct binder_ref_data - binder_ref counts and id
 *
 * Structure to hold ref count and ref id information. Since
 * the actual ref can only be accessed with a lock, this structure
 * is used to return information about the ref to callers of
 * ref inc/dec functions.
 */

struct binder_ref_data
{
  int debug_id;
  uint32_t desc;
  int strong;
  int weak;
};

/**
 * struct binder_ref - struct to track references on nodes
 *
 * Structure to track references from procA to target node (on procB). This
 * structure is unsafe to access without holding proc->outer_lock.
 */

struct binder_ref
{
  /* Lookups needed:
   *   node + proc => ref (transaction)
   *   desc + proc => ref (transaction, inc/dec ref)
   *   node => refs + procs (proc exit)
   */

  struct binder_ref_data data;
  struct list_node rb_node_desc;
  struct list_node rb_node_node;
  struct list_node node_entry;
  FAR struct binder_proc *proc;
  FAR struct binder_node *node;
  FAR struct binder_ref_death *death;
};

struct binder_transaction
{
  int debug_id;
  struct binder_work work;
  FAR struct binder_thread *from;
  FAR struct binder_transaction *from_parent;
  FAR struct binder_proc *to_proc;
  FAR struct binder_thread *to_thread;
  FAR struct binder_transaction *to_parent;
  unsigned need_reply : 1;

  /* unsigned is_dead:1; */       /* not used at the moment */

  FAR struct binder_buffer *buffer;
  unsigned int code;
  unsigned int flags;
  struct binder_priority priority;
  struct binder_priority saved_priority;
  bool set_priority_called;
  uid_t sender_euid;
  struct list_node fd_fixups;
  binder_uintptr_t security_ctx;

  /**
   * lock:  protects from, to_proc, and to_thread
   *
   * from, to_proc, and to_thread can be set to NULL
   * during thread teardown
   */

  mutex_t lock;
};

/**
 * struct binder_object - union of flat binder object types
 * hdr:   generic object header
 * fbo:   binder object (nodes and refs)
 * fdo:   file descriptor object
 * bbo:   binder buffer pointer (TODO: support this object)
 * fdao:  file descriptor array (TODO: support this object)
 *
 * Used for type-independent object copies
 */

struct binder_object
{
  union
  {
    struct binder_object_header hdr;
    struct flat_binder_object fbo;
    struct binder_fd_object fdo;
    struct binder_buffer_object bbo;
    struct binder_fd_array_object fdao;
  };
};

/**
 * struct binder_txn_fd_fixup - transaction fd fixup list element
 * fixup_entry:          list entry
 * file:                 struct file to be associated with new fd
 * offset:               offset in buffer data to this fixup
 *
 * List element for fd fixups in a transaction. Since file
 * descriptors need to be allocated in the context of the
 * target process, we pass each fd to be processed in this
 * struct.
 */

struct binder_txn_fd_fixup
{
  struct list_node fixup_entry;
  struct file file;
  size_t offset;
};

/* binder allocator */

struct binder_mmap_area
{
  FAR void     *area_start;
  size_t        area_size;
  uint32_t      map_flag;
};

/****************************************************************************
 * Inline Functions
 ****************************************************************************/

static inline void binder_inc_node_tmpref_ilocked(
  FAR struct binder_node *node)
{
  /* No call to binder_inc_node() is needed since we
   * don't need to inform userspace of any changes to
   * tmp_refs
   */

  node->tmp_refs++;
}

static inline void binder_free_node(FAR struct binder_node *node)
{
  kmm_free(node);
}

static inline void binder_dequeue_work_ilocked(FAR struct binder_work *work)
{
  list_delete_init(&work->entry_node);
}

static inline bool binder_worklist_empty_ilocked(FAR struct list_node *list)
{
  return list_is_empty(list);
}

/**
 * binder_enqueue_work_ilocked() - Add an item to the work list
 * work:         struct binder_work to add to list
 * target_list:  list to add work to
 *
 * Adds the work to the specified list. Asserts that work
 * is not already on a list.
 */

static inline void binder_enqueue_work_ilocked(
  FAR struct binder_work *work, FAR struct list_node *target_list)
{
  BUG_ON(target_list == NULL);
  BUG_ON(work->entry_node.next && !list_is_empty(&work->entry_node));
  list_add_tail(target_list, &work->entry_node);
}

/**
 * binder_enqueue_thread_work_ilocked() - Add an item to the thread work list
 * thread:       thread to queue work to
 * work:         struct binder_work to add to list
 *
 * Adds the work to the todo list of the thread, and enables processing
 * of the todo queue.
 */

static inline void binder_enqueue_thread_work_ilocked(
  FAR struct binder_thread *thread, FAR struct binder_work *work)
{
  WARN_ON(!list_is_empty(&thread->waiting_thread_node));
  binder_enqueue_work_ilocked(work, &thread->todo);
  thread->process_todo = true;
}

/**
 * binder_enqueue_deferred_thread_work_ilocked() - Add deferred thread work
 * thread:       thread to queue work to
 * work:         struct binder_work to add to list
 *
 * Adds the work to the todo list of the thread. Doesn't set the process_todo
 * flag, which means that (if it wasn't already set) the thread will go to
 * sleep without handling this work when it calls read.
 *
 * Requires the proc->inner_lock to be held.
 */

static inline void binder_enqueue_deferred_thread_work_ilocked(
  FAR struct binder_thread *thread, FAR struct binder_work *work)
{
  WARN_ON(!list_is_empty(&thread->waiting_thread_node));
  binder_enqueue_work_ilocked(work, &thread->todo);
}

static inline bool binder_available_for_proc_work_ilocked(
  FAR struct binder_thread *thread)
{
  return !thread->transaction_stack && binder_worklist_empty_ilocked(
    &thread->todo) &&
         (thread->looper &
          (BINDER_LOOPER_STATE_ENTERED | BINDER_LOOPER_STATE_REGISTERED));
}

/* function prototype define for binder_alloc.c */

void binder_alloc_init(FAR struct binder_alloc *alloc, pid_t pid);
int binder_alloc_mmap(FAR struct binder_alloc *alloc,
                      FAR struct binder_mmap_area *vma);
int binder_alloc_unmmap(FAR struct binder_alloc *alloc,
                        FAR struct binder_mmap_area *vma);
FAR struct binder_buffer *binder_alloc_prepare_to_free(
  FAR struct binder_alloc *alloc, uintptr_t user_ptr);
int binder_alloc_copy_from_buffer(FAR struct binder_alloc *alloc,
                                  FAR void *dest,
                                  FAR struct binder_buffer *buffer,
                                  binder_size_t buffer_offset,
                                  size_t bytes);
int binder_alloc_copy_to_buffer(FAR struct binder_alloc *alloc,
                                FAR struct binder_buffer *buffer,
                                binder_size_t buffer_offset,
                                FAR void *src, size_t bytes);

FAR struct binder_buffer *binder_alloc_new_buf(
  FAR struct binder_alloc *alloc, size_t data_size, size_t secctx_sz,
  size_t offsets_size, int is_async, int pid, FAR int *ret);

void binder_alloc_free_buf(FAR struct binder_alloc *alloc,
                           FAR struct binder_buffer *buffer);
void binder_alloc_deferred_release(FAR struct binder_alloc *alloc);

/* function prototype define for binder_sched.c */

int binder_get_priority(pid_t pid, FAR struct binder_priority * priority);
void binder_set_priority(FAR struct binder_thread *thread,
                         FAR const struct binder_priority *desired);
void init_waitqueue_entry(FAR struct wait_queue_entry *wq_entry,
                          FAR void * arg, wait_queue_func_t func);
void prepare_to_wait(FAR struct list_node *wq_head,
                     FAR struct wait_queue_entry *wq_entry);
void finish_wait(FAR struct wait_queue_entry *wq_entry);
int  wait_event_interruptible(FAR struct list_node *wq_head,
                              unsigned int timeout);
void wait_wake_up(FAR struct list_node *wq_head, int sync);
void wake_up_pollfree(FAR struct binder_thread *thread);

/* function prototype define for binder_node.c */

FAR struct binder_node *binder_new_node(FAR struct binder_proc *proc,
                                        FAR struct flat_binder_object *fp);
FAR struct binder_node *binder_get_node(FAR struct binder_proc *proc,
                                        binder_uintptr_t ptr);
void binder_put_node(FAR struct binder_node *node);
bool binder_dec_node_nilocked(FAR struct binder_node *node, int strong,
                              int internal);
int binder_inc_node_nilocked(FAR struct binder_node *node,
                             int strong, int internal,
                             FAR struct list_node *target_list);
void binder_dec_node(FAR struct binder_node *node,
                     int strong, int internal);
int binder_inc_node(FAR struct binder_node *node,
                    int strong, int internal,
                    FAR struct list_node *target_list);
void binder_dec_node_tmpref(FAR struct binder_node *node);
int binder_node_release(FAR struct binder_node *node, int refs);

/* function prototype define for binder_ref.c */

int binder_inc_ref_for_node(FAR struct binder_proc *proc,
                            FAR struct binder_node *node, bool strong,
                            FAR struct list_node *target_list,
                            FAR struct binder_ref_data *rdata);
int binder_update_ref_for_handle(FAR struct binder_proc *proc, uint32_t desc,
                                 bool increment, bool strong,
                                 FAR struct binder_ref_data *rdata);
struct binder_ref *binder_get_ref_olocked(FAR struct binder_proc *proc,
                                          uint32_t desc,
                                          bool need_strong_ref);

int binder_dec_ref_for_handle(
  FAR struct binder_proc *proc, uint32_t desc,
  bool strong, FAR struct binder_ref_data *rdata);

struct binder_ref *binder_get_ref_for_node_olocked(
  FAR struct binder_proc *proc, FAR struct binder_node *node,
  FAR struct binder_ref *new_ref);
int binder_inc_ref_olocked(FAR struct binder_ref *ref, int strong,
                           FAR struct list_node *target_list);
FAR struct binder_node *
binder_get_node_from_ref(FAR struct binder_proc *proc,
                         uint32_t desc, bool need_strong_ref,
                         FAR struct binder_ref_data *rdata);
void binder_cleanup_ref_olocked(FAR struct binder_ref *ref);
void binder_free_ref(FAR struct binder_ref *ref);

/* function prototype define for binder_thread.c */

FAR struct binder_thread *binder_get_thread(FAR struct binder_proc *proc);
FAR struct binder_thread * binder_select_thread_ilocked(
  FAR struct binder_proc *proc);
int binder_thread_write(FAR struct binder_proc *proc,
                        FAR struct binder_thread *thread,
                        binder_uintptr_t binder_buffer, size_t size,
                        FAR binder_size_t *consumed);
int binder_thread_read(FAR struct binder_proc *proc,
                       FAR struct binder_thread *thread,
                       binder_uintptr_t binder_buffer, size_t size,
                       FAR binder_size_t *consumed, int non_block);
void binder_dequeue_work(FAR struct binder_proc *proc,
                         FAR struct binder_work *work);
void binder_enqueue_thread_work(FAR struct binder_thread *thread,
                                FAR struct binder_work *work);
int binder_thread_release(FAR struct binder_proc *proc,
                          FAR struct binder_thread *thread);
void binder_wakeup_proc_ilocked(FAR struct binder_proc *proc);
void binder_wakeup_thread_ilocked(FAR struct binder_proc *proc,
                                  FAR struct binder_thread *thread,
                                  bool sync);
void binder_thread_dec_tmpref(FAR struct binder_thread *thread);
void binder_proc_dec_tmpref(FAR struct binder_proc *proc);
void binder_release_work(FAR struct binder_proc *proc,
                         FAR struct list_node *list);
bool binder_has_work(FAR struct binder_thread *thread, bool do_proc_work);

/* function prototype define for binder_trans.c */

void binder_free_transaction(FAR struct binder_transaction *t);
void binder_transaction(FAR struct binder_proc *proc,
                        FAR struct binder_thread *thread,
                        FAR struct binder_transaction_data *tr, int reply);
void binder_cleanup_transaction(FAR struct binder_transaction *t,
                                FAR const char *reason, uint32_t error_code);
void binder_transaction_buffer_release(FAR struct binder_proc *proc,
                                       FAR struct binder_thread *thread,
                                       FAR struct binder_buffer *buffer,
                                       binder_size_t failed_at,
                                       bool is_failure);
void binder_transaction_priority(FAR struct binder_thread *thread,
                                 FAR struct binder_transaction *t,
                                 FAR struct binder_node *node);
void binder_deferred_fd_close(int fd);
void binder_send_failed_reply(FAR struct binder_transaction *t,
                              uint32_t error_code);

#endif /* __DRIVERS_BINDER_BINDER_INTERNAL_H__ */
