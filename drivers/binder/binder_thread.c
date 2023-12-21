/****************************************************************************
 * drivers/binder/binder_thread.c
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

#define LOG_TAG  "BinderThread"

#include <nuttx/config.h>

#include <sys/types.h>
#include <sys/ioctl.h>
#include <string.h>
#include <poll.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <debug.h>
#include <sched.h>

#include <nuttx/fs/fs.h>
#include <nuttx/android/binder.h>
#include <nuttx/mutex.h>
#include <nuttx/nuttx.h>
#include <nuttx/kmalloc.h>
#include <nuttx/semaphore.h>
#include <nuttx/wqueue.h>

#include "binder_internal.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Private Function
 ****************************************************************************/

static FAR struct binder_work *binder_dequeue_work_head_ilocked(
  FAR struct list_node *list)
{
  FAR struct binder_work *w;

  w = list_first_entry_or_null(list, struct binder_work, entry_node);
  if (w)
    {
      list_delete_init(&w->entry_node);
    }
  return w;
}

static inline bool binder_has_work_ilocked(FAR struct binder_thread *thread,
                                           bool do_proc_work)
{
  return thread->process_todo || thread->looper_need_return ||
         (do_proc_work &&
          !binder_worklist_empty_ilocked(&thread->proc->todo_list));
}

static void binder_wakeup_poll_threads_ilocked(FAR struct binder_proc *proc,
                                               bool sync)
{
  FAR struct binder_thread *thread;

  list_for_every_entry(&proc->threads, thread, struct binder_thread,
                       thread_node)
  {
    if (thread->looper & BINDER_LOOPER_STATE_POLL &&
        binder_available_for_proc_work_ilocked(thread))
      {
        if (sync)
          {
            wait_wake_up(&thread->wait, 1);
          }
        else
          {
            wait_wake_up(&thread->wait, 0);
          }
      }
  }
}

static int binder_wake_function(FAR void * arg, unsigned mode)
{
  FAR struct wait_queue_entry   *wq_entry;
  int                            ret;
  FAR sem_t                     *waitsem;

  wq_entry  = (FAR struct wait_queue_entry *)arg;
  waitsem   = (FAR sem_t *)wq_entry->private;
  ret       = nxsem_post(waitsem);

  binder_debug(BINDER_DEBUG_SCHED, "wq_entry=%p, ret=%d\n", wq_entry, ret);
  return ret;
}

static int binder_wait_for_work(FAR struct binder_thread *thread,
                                bool do_proc_work)
{
  struct wait_queue_entry      wait;
  FAR struct binder_proc      *proc   = thread->proc;
  int                          ret     = 0;
  sem_t                        waitsem;

  nxsem_init(&waitsem, 0, 0);
  init_waitqueue_entry(&wait, (void *)&waitsem, binder_wake_function);
  nxmutex_lock(&proc->proc_lock);

  for (; ; )
    {
      prepare_to_wait(&thread->wait, &wait);
      if (binder_has_work_ilocked(thread, do_proc_work))
        {
          break;
        }

      if (do_proc_work)
        {
          list_add_tail(&proc->waiting_threads,
                        &thread->waiting_thread_node);
        }

      binder_debug(BINDER_DEBUG_THREADS,
                   "%d:%d enter wait do_proc_work=%s\n",
                   proc->pid, thread->tid,
                   do_proc_work? "true":"false");

      nxmutex_unlock(&proc->proc_lock);
      ret = nxsem_wait(&waitsem);
      nxmutex_lock(&proc->proc_lock);
      list_delete_init(&thread->waiting_thread_node);
      binder_debug(BINDER_DEBUG_THREADS, "%d:%d finish wait ret=%d\n",
                   proc->pid, thread->tid, ret);
      if (ret < 0)
        {
          if (ret != -EINTR)
            {
              binder_debug(BINDER_DEBUG_ERROR,
                           "error with binder_wait_for_work, ret=%d\n",
                           ret);
            }
          break;
        }
    }

  finish_wait(&wait);
  nxmutex_unlock(&proc->proc_lock);

  return ret;
}

/****************************************************************************
 * Name: binder_free_buf
 *
 * Description:
 *   free the specified buffer.
 *   If buffer for an async transaction, enqueue the next async
 *   transaction from the node.
 *   Cleanup buffer and free it.
 *
 * Input Parameters:
 *   proc       - binder proc that owns buffer
 *   buffer     - buffer to be freed
 *   is_failure - failed to send transaction
 *
 ****************************************************************************/

static void binder_free_buf(FAR struct binder_proc *proc,
                            FAR struct binder_thread *thread,
                            FAR struct binder_buffer *buffer,
                            bool is_failure)
{
  nxmutex_lock(&proc->proc_lock);
  if (buffer->transaction)
    {
      buffer->transaction->buffer   = NULL;
      buffer->transaction           = NULL;
    }

  nxmutex_unlock(&proc->proc_lock);
  if (buffer->async_transaction && buffer->target_node)
    {
      FAR struct binder_node    *buf_node;
      FAR struct binder_work    *w;

      buf_node = buffer->target_node;
      nxmutex_lock(&buf_node->node_lock);
      BUG_ON(!buf_node->has_async_transaction);
      BUG_ON(buf_node->proc != proc);
      w = binder_dequeue_work_head_ilocked(&buf_node->async_todo);
      if (!w)
        {
          buf_node->has_async_transaction = false;
        }
      else
        {
          binder_enqueue_work_ilocked(w, &proc->todo_list);
          binder_wakeup_proc_ilocked(proc);
        }
      nxmutex_unlock(&buf_node->node_lock);
    }

  binder_transaction_buffer_release(proc, thread, buffer, 0, is_failure);
  binder_alloc_free_buf(&proc->alloc, buffer);
}

/****************************************************************************
 * Name: binder_get_txn_from
 *
 * Description:
 *   safely extract the "from" thread in transaction.
 *
 *   Atomically return the "from" thread and increment the tmp_ref
 *   count for the thread to ensure it stays alive until
 *   binder_thread_dec_tmpref() is called.
 *
 * Input Parameters:
 *   t - binder transaction for t->from
 *
 * Returned Value:
 *   the value of t->from
 *
 ****************************************************************************/

static struct binder_thread * binder_get_txn_from(
  FAR struct binder_transaction *t)
{
  FAR struct binder_thread *from;

  nxmutex_lock(&t->lock);
  from = t->from;
  if (from)
    {
      from->tmp_ref++;
    }
  nxmutex_unlock(&t->lock);
  return from;
}

static void binder_free_proc(FAR struct binder_proc *proc)
{
  FAR struct binder_device *device;

  BUG_ON(!list_is_empty(&proc->todo_list));
  BUG_ON(!list_is_empty(&proc->delivered_death));
  if (proc->outstanding_txns)
    {
      binder_debug(BINDER_DEBUG_WARNING,
                   "%s: Unexpected outstanding_txns %d\n", __func__,
                   proc->outstanding_txns);
      device = container_of(proc->context, struct binder_device, context);
      nxmutex_lock(&device->binder_procs_lock);
      device->ref_count--;
      if (device->ref_count == 0)
        {
          nxmutex_unlock(&device->binder_procs_lock);
          kmm_free(device);
        }
      else
        {
          nxmutex_unlock(&device->binder_procs_lock);
        }
    }
  binder_alloc_deferred_release(&proc->alloc);
  kmm_free(proc);
}

static void binder_free_thread(FAR struct binder_thread *thread)
{
  BUG_ON(!list_is_empty(&thread->todo));
  binder_proc_dec_tmpref(thread->proc);
  kmm_free(thread);
}

static int binder_put_node_cmd(FAR struct binder_proc *proc,
                               FAR struct binder_thread *thread,
                               FAR void **ptrp,
                               binder_uintptr_t node_ptr,
                               binder_uintptr_t node_cookie,
                               int node_debug_id, uint32_t cmd,
                               const char *cmd_name)
{
  FAR void *ptr = *ptrp;

  put_value(cmd, (uint32_t  *)ptr);
  ptr += sizeof(uint32_t);

  put_value(node_ptr, (binder_uintptr_t *)ptr);
  ptr += sizeof(binder_uintptr_t);

  put_value(node_cookie, (binder_uintptr_t *)ptr);
  ptr += sizeof(binder_uintptr_t);

  binder_debug(BINDER_DEBUG_USER_REFS, "%s %d %" PRIx64 " %" PRIx64 "\n",
               cmd_name, node_debug_id, node_ptr, node_cookie);

  *ptrp = ptr;
  return 0;
}

/****************************************************************************
 * Name: binder_apply_fd_fixups
 *
 * Description:
 *   finish fd translation.
 *
 *   Now that we are in the context of the transaction target
 *   process, we can allocate and install fds. Process the
 *   list of fds to translate and fixup the buffer with the
 *   new fds.
 *
 *   If we fail to allocate an fd, then free the resources by
 *   fput'ing files that have not been processed and ksys_close'ing
 *   any fds that have already been allocated.
 *
 * Input Parameters:
 *   proc - binder_proc associated t->buffer
 *   t    - binder transaction with list of fd fixups
 *
 * Returned Value:
 *   the value of t->from
 *
 ****************************************************************************/

static int binder_apply_fd_fixups(FAR struct binder_proc *proc,
                                  FAR struct binder_transaction *t)
{
  FAR struct binder_txn_fd_fixup    *fixup;
  FAR struct binder_txn_fd_fixup    *tmp;
  int                               ret = 0;
  int                               fd;
  int                               err;

  list_for_every_entry(&t->fd_fixups, fixup, struct binder_txn_fd_fixup,
                       fixup_entry)
  {
    fd = file_dup(&fixup->file, 0, 0);
    file_close(&fixup->file);

    if (fd < 0)
      {
        binder_debug(BINDER_DEBUG_TRANSACTION,
                     "failed fd fixup txn %d fd %d\n", t->debug_id, fd);
        ret = -ENOMEM;
        break;
      }

    binder_debug(BINDER_DEBUG_TRANSACTION, "fd fixup txn %d fd %d\n",
                 t->debug_id, fd);

    if (binder_alloc_copy_to_buffer(&proc->alloc, t->buffer, fixup->offset,
                                    &fd, sizeof(uint32_t)))
      {
        ret = -EINVAL;
        break;
      }
  }

  list_for_every_entry_safe(&t->fd_fixups, fixup, tmp,
                            struct binder_txn_fd_fixup, fixup_entry)
    {
      if (ret)
        {
          err = binder_alloc_copy_from_buffer(&proc->alloc, &fd, t->buffer,
                                              fixup->offset, sizeof(fd));
          WARN_ON(err);
          if (!err)
            {
              binder_deferred_fd_close(fd);
            }
        }

      list_delete_init(&fixup->fixup_entry);
      kmm_free(fixup);
    }

  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

bool binder_has_work(FAR struct binder_thread *thread, bool do_proc_work)
{
  bool has_work;

  nxmutex_lock(&thread->proc->proc_lock);
  has_work = binder_has_work_ilocked(thread, do_proc_work);
  nxmutex_unlock(&thread->proc->proc_lock);

  return has_work;
}

/****************************************************************************
 * Name: binder_dequeue_work
 *
 * Description:
 *   Removes an item from the work list
 *
 *   Removes the specified work item from whatever list it is on.
 *   Can safely be called if work is not on any list.
 *
 * Input Parameters:
 *   proc - binder_proc associated with list
 *   work - struct binder_work to remove from list
 *
 ****************************************************************************/

void binder_dequeue_work(FAR struct binder_proc *proc,
                         FAR struct binder_work *work)
{
  nxmutex_lock(&proc->proc_lock);
  binder_dequeue_work_ilocked(work);
  nxmutex_unlock(&proc->proc_lock);
}

/****************************************************************************
 * Name: binder_select_thread_ilocked
 *
 * Description:
 *   selects a thread for doing proc work.
 *
 * Input Parameters:
 *   proc - process to select a thread from
 *
 * Returned Value:
 *   If there's a thread currently waiting for process work,
 *   returns that thread. Otherwise returns NULL.
 *
 ****************************************************************************/

FAR struct binder_thread *binder_select_thread_ilocked(
  FAR struct binder_proc *proc)
{
  FAR struct binder_thread *thread;

  thread = list_first_entry_or_null(&proc->waiting_threads,
                                    struct binder_thread,
                                    waiting_thread_node);

  /* If the thread is calling IPCThreadState::waitForResponse() and
   * is polling after received EAGAIN, then:
   * - thread is not NULL
   * - binder_available_for_proc_work_ilocked(thread) is false
   *
   * In this case, set thread to NULL. That is, there is no thread
   * currently waiting for process work.
   */

  if (thread && binder_available_for_proc_work_ilocked(thread))
    {
      list_delete_init(&thread->waiting_thread_node);
    }
  else
    {
      thread = NULL;
    }

  return thread;
}

/****************************************************************************
 * Name: binder_wakeup_thread_ilocked
 *
 * Description:
 *   wakes up a thread for doing proc work.
 *
 *   This function wakes up a thread in the proc process.
 *   The caller may provide a specific thread to wake-up in
 *   the thread parameter. If thread is NULL, this function
 *   will wake up threads that have called poll().
 *
 *   Note that for this function to work as expected, callers
 *   should first call binder_select_thread() to find a thread
 *   to handle the work (if they don't have a thread already),
 *   and pass the result into the thread parameter.
 *
 * Input Parameters:
 *   proc   - process to wake up a thread in
 *   thread - specific thread to wake-up (may be NULL)
 *   sync   - whether to do a synchronous wake-up
 *
 ****************************************************************************/

void binder_wakeup_thread_ilocked(FAR struct binder_proc *proc,
                                  FAR struct binder_thread *thread,
                                  bool sync)
{
  binder_debug(BINDER_DEBUG_THREADS, "wake up %d:%d sync:%s\n", proc->pid,
               thread != NULL ? thread->tid : 0, sync ? "true":"false");
  if (thread)
    {
      if (sync)
        {
          wait_wake_up(&thread->wait, 1);
        }
      else
        {
          wait_wake_up(&thread->wait, 0);
        }

      return;
    }

  /* Didn't find a thread waiting for proc work; this can happen
   * in two scenarios:
   * 1. All threads are busy handling transactions
   *    In that case, one of those threads should call back into
   *    the kernel driver soon and pick up this work.
   * 2. Threads are using the (e)poll interface, in which case
   *    they may be blocked on the waitqueue without having been
   *    added to waiting_threads. For this case, we just iterate
   *    over all threads not handling transaction work, and
   *    wake them all up. We wake all because we don't know whether
   *    a thread that called into (e)poll is handling non-binder
   *    work currently.
   */

  binder_wakeup_poll_threads_ilocked(proc, sync);
}

/****************************************************************************
 * Name: binder_enqueue_thread_work
 *
 * Description:
 *   Adds the work to the todo list of the thread, and enables processing
 *   of the todo queue.
 *
 * Input Parameters:
 *   thread - thread to queue work to
 *   work   - struct binder_work to add to list
 *
 ****************************************************************************/

void binder_enqueue_thread_work(FAR struct binder_thread *thread,
                                FAR struct binder_work *work)
{
  FAR struct binder_proc *proc = thread->proc;

  nxmutex_lock(&proc->proc_lock);
  binder_enqueue_thread_work_ilocked(thread, work);
  nxmutex_unlock(&proc->proc_lock);
}

void binder_wakeup_proc_ilocked(FAR struct binder_proc *proc)
{
  FAR struct binder_thread *thread = binder_select_thread_ilocked(proc);

  binder_wakeup_thread_ilocked(proc, thread, /* sync = */ false);
}

/****************************************************************************
 * Name: binder_proc_dec_tmpref
 *
 * Description:
 *   A binder_proc needs to be kept alive while being used to create or
 *   handle a transaction. proc->tmp_ref is incremented when
 *   creating a new transaction or the binder_proc is currently in-use
 *   by threads that are being released. When done with the binder_proc,
 *   this function is called to decrement the counter and free the
 *   proc if appropriate (proc has been released, all threads have
 *   been released and not currenly in-use to process a transaction).
 *
 * Input Parameters:
 *   proc - proc to decrement
 *
 ****************************************************************************/

void binder_proc_dec_tmpref(FAR struct binder_proc *proc)
{
  nxmutex_lock(&proc->proc_lock);
  proc->tmp_ref--;
  if (proc->is_dead && list_is_empty(&proc->threads) && !proc->tmp_ref)
    {
      nxmutex_unlock(&proc->proc_lock);
      binder_free_proc(proc);
      return;
    }

  nxmutex_unlock(&proc->proc_lock);
}

/****************************************************************************
 * Name: binder_thread_dec_tmpref
 *
 * Description:
 *   A thread needs to be kept alive while being used to create or
 *   handle a transaction. binder_get_txn_from() is used to safely
 *   extract t->from from a binder_transaction and keep the thread
 *   indicated by t->from from being freed. When done with that
 *   binder_thread, this function is called to decrement the
 *   tmp_ref and free if appropriate (thread has been released
 *   and no transaction being processed by the driver)
 *
 * Input Parameters:
 *   thread - thread to decrement
 *
 ****************************************************************************/

void binder_thread_dec_tmpref(FAR struct binder_thread *thread)
{
  FAR struct binder_proc *proc;

  /* atomic is used to protect the counter value while
   * it cannot reach zero or thread->is_dead is false
   */

  proc = thread->proc;
  nxmutex_lock(&proc->proc_lock);
  thread->tmp_ref--;
  if (thread->is_dead && thread->tmp_ref == 0)
    {
      nxmutex_unlock(&proc->proc_lock);
      binder_free_thread(thread);
      return;
    }

  nxmutex_unlock(&proc->proc_lock);
}

FAR struct binder_thread *binder_get_thread(FAR struct binder_proc *proc)
{
  FAR struct binder_thread  *itr        = NULL;
  FAR struct binder_thread  *thread     = NULL;
  pid_t                      thread_pid  = gettid();

  nxmutex_lock(&proc->proc_lock);
  list_for_every_entry(&proc->threads, itr,
                       struct binder_thread, thread_node)
    {
      if (itr->tid == thread_pid)
        {
          thread = itr;
          break;
        }
    }

  nxmutex_unlock(&proc->proc_lock);

  if (thread == NULL)
    {
      thread            = kmm_zalloc(sizeof(struct binder_thread));
      if (thread == NULL)
        {
          return NULL;
        }

      thread->proc      = proc;
      thread->tid       = thread_pid;
      thread->tmp_ref   = 0;
      list_initialize(&thread->thread_node);
      list_initialize(&thread->wait);
      list_initialize(&thread->todo);
      list_initialize(&thread->waiting_thread_node);

      thread->return_error.work.type = BINDER_WORK_RETURN_ERROR;
      thread->return_error.cmd       = BR_OK;
      list_initialize(&thread->return_error.work.entry_node);

      thread->reply_error.work.type = BINDER_WORK_RETURN_ERROR;
      thread->reply_error.cmd       = BR_OK;
      list_initialize(&thread->reply_error.work.entry_node);

      nxmutex_lock(&proc->proc_lock);
      list_add_head(&proc->threads, &thread->thread_node);
      nxmutex_unlock(&proc->proc_lock);
    }

  return thread;
}

int binder_thread_write(FAR struct binder_proc *proc,
                        FAR struct binder_thread *thread,
                        binder_uintptr_t binder_buffer, size_t size,
                        FAR binder_size_t *consumed)
{
  uint32_t                   cmd;
  FAR struct binder_context *context = proc->context;
  FAR void                  *buffer  = (FAR void *)(uintptr_t)binder_buffer;
  FAR void                  *ptr     = buffer + *consumed;
  FAR void                  *end     = buffer + size;

  while (ptr < end && thread->return_error.cmd == BR_OK)
    {
      int ret;

      get_value(cmd, (uint32_t *)ptr);
      ptr += sizeof(uint32_t);
      switch (cmd)
      {
        case BC_INCREFS:
        case BC_ACQUIRE:
        case BC_RELEASE:
        case BC_DECREFS:
        {
          uint32_t                  target;
          FAR __attribute__((unused)) const char *debug_string;
          bool                      strong =
            (cmd == BC_ACQUIRE || cmd == BC_RELEASE);
          bool                      increment =
            (cmd == BC_INCREFS || cmd == BC_ACQUIRE);
          struct binder_ref_data    rdata;

          get_value(target, (uint32_t *)ptr);
          ptr   += sizeof(uint32_t);
          ret   = -1;
          if (increment && !target)
            {
              FAR struct binder_node *ctx_mgr_node;

              nxmutex_lock(&context->context_lock);
              ctx_mgr_node = context->mgr_node;
              if (ctx_mgr_node)
                {
                  if (ctx_mgr_node->proc == proc)
                    {
                      binder_debug(BINDER_DEBUG_ERROR, "context manager "
                      "tried to acquire desc 0\n");
                      nxmutex_unlock(&context->context_lock);
                      return -EINVAL;
                    }

                  ret = binder_inc_ref_for_node(proc, ctx_mgr_node, strong,
                                                NULL, &rdata);
                }

              nxmutex_unlock(&context->context_lock);
            }

          if (ret != 0)
            {
              ret = binder_update_ref_for_handle(proc, target, increment,
                                                 strong, &rdata);
            }

          if (!ret && rdata.desc != target)
            {
              binder_debug(BINDER_DEBUG_ERROR,
                           "tried to acquire reference to desc %"PRId32", "
                           "got %"PRId32" instead\n",
                           target, rdata.desc);
            }

          switch (cmd)
            {
              case BC_INCREFS:
              {
                debug_string = "IncRefs";
                break;
              }

              case BC_ACQUIRE:
              {
                debug_string = "Acquire";
                break;
              }

              case BC_RELEASE:
              {
                debug_string = "Release";
                break;
              }

              case BC_DECREFS:
              default:
              {
                debug_string = "DecRefs";
                break;
              }
            }

          if (ret)
            {
              binder_debug(BINDER_DEBUG_ERROR,
                           "%s %d refcount change on invalid"
                           " ref %"PRId32" ret %d\n",
                           debug_string, strong, target, ret);
              break;
            }

          binder_debug(BINDER_DEBUG_USER_REFS,
                       "%s ref %d desc %"PRId32" s %d w %d\n",
                       debug_string, rdata.debug_id,
                       rdata.desc, rdata.strong, rdata.weak);
          break;
        }

        case BC_INCREFS_DONE:
        case BC_ACQUIRE_DONE:
        {
          binder_uintptr_t           node_ptr;
          binder_uintptr_t           cookie;
          FAR struct binder_node    *node;

          get_value(node_ptr, (binder_uintptr_t *)ptr);
          ptr += sizeof(binder_uintptr_t);
          get_value(cookie, (binder_uintptr_t *)ptr);
          ptr += sizeof(binder_uintptr_t);
          node = binder_get_node(proc, node_ptr);
          if (node == NULL)
            {
              binder_debug(BINDER_DEBUG_ERROR, "%s %" PRIx64 " no match\n",
                           cmd == BC_INCREFS_DONE ?
                           "BC_INCREFS_DONE" :"BC_ACQUIRE_DONE",
                           node_ptr);
              break;
            }

          if (cookie != node->cookie)
            {
              binder_debug(BINDER_DEBUG_ERROR,
                           "%s %"PRIx64" node %d cookie mismatch"
                           " %"PRIx64" != %"PRIx64"\n",
                           cmd == BC_INCREFS_DONE ?
                           "BC_INCREFS_DONE" : "BC_ACQUIRE_DONE",
                           node_ptr, node->debug_id, cookie,
                           node->cookie);
              binder_put_node(node);
              break;
            }

          nxmutex_lock(&node->node_lock);
          if (cmd == BC_ACQUIRE_DONE)
            {
              if (node->pending_strong_ref == 0)
                {
                  binder_debug(BINDER_DEBUG_ERROR,
                               "BC_ACQUIRE_DONE node %d has no "
                               "pending acquire request\n",
                               node->debug_id);
                  nxmutex_unlock(&node->node_lock);
                  binder_put_node(node);
                  break;
                }

              node->pending_strong_ref = 0;
            }
          else
            {
              if (node->pending_weak_ref == 0)
                {
                  binder_debug(BINDER_DEBUG_ERROR,
                               "BC_INCREFS_DONE node %d has no pending "
                               "increfs request\n",
                               node->debug_id);
                  nxmutex_unlock(&node->node_lock);
                  binder_put_node(node);
                  break;
                }

              node->pending_weak_ref = 0;
            }

          WARN_ON(binder_dec_node_nilocked(node, cmd == BC_ACQUIRE_DONE, 0));
          binder_debug(BINDER_DEBUG_USER_REFS,
                       "%s node %d ls %d lw %d tr %d\n",
                       cmd == BC_INCREFS_DONE ?
                       "BC_INCREFS_DONE" : "BC_ACQUIRE_DONE",
                       node->debug_id, node->local_strong_refs,
                       node->local_weak_refs, node->tmp_refs);
          nxmutex_unlock(&node->node_lock);
          binder_put_node(node);
          break;
        }

        case BC_ATTEMPT_ACQUIRE:
        case BC_ACQUIRE_RESULT:
        {
          binder_debug(BINDER_DEBUG_ERROR, "not supported\n");
          return -EINVAL;
        }

        case BC_FREE_BUFFER:
        {
          binder_uintptr_t          data_ptr;
          FAR struct binder_buffer  *b_buffer;

          get_value(data_ptr, (binder_uintptr_t *)ptr);
          ptr     += sizeof(binder_uintptr_t);
          b_buffer = binder_alloc_prepare_to_free(&proc->alloc, data_ptr);
          if (b_buffer == NULL)
            {
              binder_debug(BINDER_DEBUG_ERROR,
                           "BC_FREE_BUFFER %"PRIx64" no match\n",
                           data_ptr);
              break;
            }

          binder_debug(BINDER_DEBUG_FREE_BUFFER,
                       "BC_FREE_BUFFER %"PRIx64" found buffer %d "
                       "for %s transaction\n",
                       data_ptr, b_buffer->debug_id,
                       b_buffer->transaction ? "active" : "finished");
          binder_free_buf(proc, thread, b_buffer, false);
          break;
        }

        case BC_TRANSACTION:
        case BC_REPLY:
        {
          FAR struct binder_transaction_data *binder_tr;

          binder_tr = (struct binder_transaction_data *)ptr;
          ptr       += sizeof(struct binder_transaction_data);
          binder_transaction(proc, thread, binder_tr, cmd == BC_REPLY);
          break;
        }

        case BC_REGISTER_LOOPER:
        {
          binder_debug(BINDER_DEBUG_THREADS, "BC_REGISTER_LOOPER\n");
          nxmutex_lock(&proc->proc_lock);
          if (thread->looper & BINDER_LOOPER_STATE_ENTERED)
            {
              thread->looper |= BINDER_LOOPER_STATE_INVALID;
              binder_debug(BINDER_DEBUG_ERROR,
                           "ERROR: BC_REGISTER_LOOPER called"
                           "after BC_ENTER_LOOPER\n");
            }
          else if (proc->requested_threads == 0)
            {
              thread->looper |= BINDER_LOOPER_STATE_INVALID;
              binder_debug(BINDER_DEBUG_ERROR,
                           "ERROR: BC_REGISTER_LOOPER called"
                           "without request\n");
            }
          else
            {
              proc->requested_threads--;
              proc->requested_threads_started++;
            }

          thread->looper |= BINDER_LOOPER_STATE_REGISTERED;
          nxmutex_unlock(&proc->proc_lock);
          break;
        }

        case BC_ENTER_LOOPER:
        {
          binder_debug(BINDER_DEBUG_THREADS, "BC_ENTER_LOOPER\n");
          if (thread->looper & BINDER_LOOPER_STATE_REGISTERED)
            {
              thread->looper |= BINDER_LOOPER_STATE_INVALID;
              binder_debug(BINDER_DEBUG_ERROR,
                           "ERROR: BC_ENTER_LOOPER called"
                           "after BC_REGISTER_LOOPER\n");
            }

          thread->looper |= BINDER_LOOPER_STATE_ENTERED;
          break;
        }

        case BC_EXIT_LOOPER:
        {
          binder_debug(BINDER_DEBUG_THREADS, "BC_EXIT_LOOPER\n");
          thread->looper |= BINDER_LOOPER_STATE_EXITED;
          break;
        }

        case BC_REQUEST_DEATH_NOTIFICATION:
        case BC_CLEAR_DEATH_NOTIFICATION:
        {
          uint32_t                       target;
          binder_uintptr_t               cookie;
          FAR struct binder_ref         *ref;
          FAR struct binder_ref_death   *death = NULL;

          get_value(target, (uint32_t *)ptr);
          ptr += sizeof(uint32_t);
          get_value(cookie, (binder_uintptr_t *)ptr);
          ptr += sizeof(binder_uintptr_t);

          if (cmd == BC_REQUEST_DEATH_NOTIFICATION)
            {
              /* Allocate memory for death notification
               * before taking lock
               */

              death = kmm_zalloc(sizeof(struct binder_ref_death));
              if (death == NULL)
                {
                  WARN_ON(thread->return_error.cmd != BR_OK);
                  thread->return_error.cmd = BR_ERROR;
                  binder_enqueue_thread_work(thread,
                                             &thread->return_error.work);
                  binder_debug(BINDER_DEBUG_FAILED_TRANSACTION,
                               "BC_REQUEST_DEATH_NOTIFICATION failed\n");
                  break;
                }

              list_initialize(&death->work.entry_node);
            }

          nxmutex_lock(&proc->proc_lock);
          ref = binder_get_ref_olocked(proc, target, false);
          if (ref == NULL)
            {
              binder_debug(BINDER_DEBUG_ERROR, "%s invalid ref %"PRId32"\n",
                           cmd == BC_REQUEST_DEATH_NOTIFICATION ?
                           "BC_REQUEST_DEATH_NOTIFICATION"
                            :"BC_CLEAR_DEATH_NOTIFICATION",
                           target);
              nxmutex_unlock(&proc->proc_lock);
              kmm_free(death);
              break;
            }

          binder_debug(BINDER_DEBUG_DEATH_NOTIFICATION,
                       "%s %"PRIx64" ref %d desc %"PRId32" "
                       "s %d w %d for node %d\n",
                       cmd == BC_REQUEST_DEATH_NOTIFICATION ?
                       "BC_REQUEST_DEATH_NOTIFICATION" :
                       "BC_CLEAR_DEATH_NOTIFICATION",
                       cookie, ref->data.debug_id, ref->data.desc,
                       ref->data.strong, ref->data.weak,
                       ref->node->debug_id);
          nxmutex_lock(&ref->node->node_lock);
          if (cmd == BC_REQUEST_DEATH_NOTIFICATION)
            {
              if (ref->death)
                {
                  binder_debug(BINDER_DEBUG_ERROR,
                               "BC_REQUEST_DEATH_NOTIFICATION death"
                               "notification already set\n");
                  nxmutex_unlock(&ref->node->node_lock);
                  nxmutex_unlock(&proc->proc_lock);
                  kmm_free(death);
                  break;
                }

              death->cookie = cookie;
              ref->death    = death;
              if (ref->node->proc == NULL)
                {
                  ref->death->work.type = BINDER_WORK_DEAD_BINDER;
                  binder_enqueue_work_ilocked(&ref->death->work,
                                              &proc->todo_list);
                  binder_wakeup_proc_ilocked(proc);
                }
            }
          else
            {
              if (ref->death == NULL)
                {
                  binder_debug(BINDER_DEBUG_ERROR,
                               "BC_CLEAR_DEATH_NOTIFICATION death "
                               "notification not active\n");
                  nxmutex_unlock(&ref->node->node_lock);
                  nxmutex_unlock(&proc->proc_lock);
                  break;
                }

              death = ref->death;
              if (death->cookie != cookie)
                {
                  binder_debug(BINDER_DEBUG_ERROR,
                               "BC_CLEAR_DEATH_NOTIFICATION death "
                               "notification cookie mismatch"
                               " %"PRIx64" != %"PRIx64"\n",
                               death->cookie, cookie);
                  nxmutex_unlock(&ref->node->node_lock);
                  nxmutex_unlock(&proc->proc_lock);
                  break;
                }

              ref->death = NULL;
              if (list_is_empty(&death->work.entry_node))
                {
                  death->work.type = BINDER_WORK_CLEAR_DEATH_NOTIFICATION;
                  if (thread->looper &
                      (BINDER_LOOPER_STATE_REGISTERED |
                       BINDER_LOOPER_STATE_ENTERED))
                    {
                      binder_enqueue_thread_work_ilocked(thread,
                                                         &death->work);
                    }
                  else
                    {
                      binder_enqueue_work_ilocked(&death->work,
                                                  &proc->todo_list);
                      binder_wakeup_proc_ilocked(proc);
                    }
                }
              else
                {
                  BUG_ON(death->work.type != BINDER_WORK_DEAD_BINDER);
                  death->work.type = BINDER_WORK_DEAD_BINDER_AND_CLEAR;
                }
            }

          nxmutex_unlock(&ref->node->node_lock);
          nxmutex_unlock(&proc->proc_lock);
          break;
        }

        case BC_DEAD_BINDER_DONE:
        {
          FAR struct binder_work        *w;
          binder_uintptr_t               cookie;
          FAR struct binder_ref_death   *death = NULL;

          get_value(cookie, (binder_uintptr_t *)ptr);
          ptr += sizeof(binder_uintptr_t);

          nxmutex_lock(&proc->proc_lock);
          list_for_every_entry(&proc->delivered_death, w,
                               struct binder_work, entry_node)
          {
            struct binder_ref_death *tmp_death =
                        container_of(w, struct binder_ref_death, work);

            if (tmp_death->cookie == cookie)
              {
                death = tmp_death;
                break;
              }
          }

          binder_debug(BINDER_DEBUG_DEAD_BINDER,
                       "BC_DEAD_BINDER_DONE %" PRIx64 " found %p\n",
                       cookie, death);
          if (death == NULL)
            {
              binder_debug(BINDER_DEBUG_ERROR,
                           "BC_DEAD_BINDER_DONE %" PRIx64 " not found\n",
                           cookie);
              nxmutex_unlock(&proc->proc_lock);
              break;
            }

          binder_dequeue_work_ilocked(&death->work);
          if (death->work.type == BINDER_WORK_DEAD_BINDER_AND_CLEAR)
            {
              death->work.type = BINDER_WORK_CLEAR_DEATH_NOTIFICATION;
              if (thread->looper &
                  (BINDER_LOOPER_STATE_REGISTERED |
                   BINDER_LOOPER_STATE_ENTERED))
                {
                  binder_enqueue_thread_work_ilocked(thread, &death->work);
                }
              else
                {
                  binder_enqueue_work_ilocked(&death->work,
                                              &proc->todo_list);
                  binder_wakeup_proc_ilocked(proc);
                }
            }

          nxmutex_unlock(&proc->proc_lock);
          break;
        }

        default:
        {
          binder_debug(BINDER_DEBUG_ERROR, "unknown command %" PRId32 "\n",
                       cmd);
          return -EINVAL;
        }
      }

      *consumed = ptr - buffer;
    }

  return 0;
}

int binder_thread_read(FAR struct binder_proc *proc,
                       FAR struct binder_thread *thread,
                       binder_uintptr_t binder_buffer, size_t size,
                       FAR binder_size_t *consumed, int non_block)
{
  FAR void  *buffer = (void *)(uintptr_t)binder_buffer;
  FAR void  *ptr    = buffer + *consumed;
  FAR void  *end    = buffer + size;
  int        ret    = 0;
  int        wait_for_proc_work;

  if (*consumed == 0)
    {
      put_value(BR_NOOP, (uint32_t *)ptr);
      ptr += sizeof(uint32_t);
    }

retry:
  nxmutex_lock(&proc->proc_lock);
  wait_for_proc_work = binder_available_for_proc_work_ilocked(thread);
  nxmutex_unlock(&proc->proc_lock);

  thread->looper |= BINDER_LOOPER_STATE_WAITING;

  if (wait_for_proc_work)
    {
      if (!(thread->looper &
            (BINDER_LOOPER_STATE_REGISTERED | BINDER_LOOPER_STATE_ENTERED)))
        {
          binder_debug(BINDER_DEBUG_ERROR,
                       "ERROR: Thread waiting for process "
                       "work before calling BC_REGISTER_LOOPER "
                       "or BC_ENTER_LOOPER (state %x)\n",
                       thread->looper);
        }

      binder_set_priority(thread, &proc->default_priority);
    }

  if (non_block)
    {
      if (!binder_has_work(thread, wait_for_proc_work))
        {
          ret = -EAGAIN;
        }
    }
  else
    {
      ret = binder_wait_for_work(thread, wait_for_proc_work);
    }

  thread->looper &= ~BINDER_LOOPER_STATE_WAITING;

  if (ret != 0)
    {
      return ret;
    }

  while (1)
    {
      struct binder_transaction_data_secctx  tr;
      FAR struct binder_transaction_data    *trd    = &tr.transaction_data;
      FAR struct binder_work                *w      = NULL;
      FAR struct list_node                  *list   = NULL;
      FAR struct binder_transaction         *t      = NULL;
      FAR struct binder_thread              *t_from;
      size_t                                 trsize = sizeof(*trd);
      uint32_t                               cmd;

      memset(&tr, 0x0, sizeof(struct binder_transaction_data_secctx));
      nxmutex_lock(&proc->proc_lock);
      if (!binder_worklist_empty_ilocked(&thread->todo))
        {
          list = &thread->todo;
        }
      else if (!binder_worklist_empty_ilocked(&proc->todo_list) &&
               wait_for_proc_work)
        {
          list = &proc->todo_list;
        }
      else
        {
          nxmutex_unlock(&proc->proc_lock);

          /* no data added */

          if (ptr - buffer == 4 && !thread->looper_need_return)
            {
              goto retry;
            }

          if (ptr - buffer == 4 && thread->looper_need_return)
            {
              put_value(BR_FINISHED, (FAR uint32_t *)buffer);
            }
          break;
        }

      if (end - ptr < sizeof(tr) + 4)
        {
          nxmutex_unlock(&proc->proc_lock);
          break;
        }

      w = binder_dequeue_work_head_ilocked(list);
      if (binder_worklist_empty_ilocked(&thread->todo))
        {
          thread->process_todo = false;
        }

      switch (w->type)
      {
        case BINDER_WORK_TRANSACTION:
        {
          nxmutex_unlock(&proc->proc_lock);
          t = container_of(w, struct binder_transaction, work);
        }
        break;

        case BINDER_WORK_RETURN_ERROR:
        {
          FAR struct binder_error *e =
                    container_of(w, struct binder_error, work);

          WARN_ON(e->cmd == BR_OK);
          nxmutex_unlock(&proc->proc_lock);
          put_value(e->cmd, (uint32_t *)ptr);
          cmd       = e->cmd;
          e->cmd    = BR_OK;
          ptr       += sizeof(uint32_t);
        }
        break;

        case BINDER_WORK_TRANSACTION_COMPLETE:
        case BINDER_WORK_TRANSACTION_ONEWAY_SPAM_SUSPECT:
        {
          cmd = BR_TRANSACTION_COMPLETE;
          nxmutex_unlock(&proc->proc_lock);
          kmm_free(w);
          put_value(cmd, (uint32_t *)ptr);
          ptr += sizeof(uint32_t);

          binder_debug(BINDER_DEBUG_TRANSACTION_COMPLETE,
                       "%d:%d BR_TRANSACTION_COMPLETE\n",
                       proc->pid, thread->tid);
        }
        break;

        case BINDER_WORK_NODE:
        {
          FAR struct binder_node    *node =
                  container_of(w, struct binder_node, work);
          int                        strong;
          int                        weak;
          binder_uintptr_t           node_ptr        = node->ptr;
          binder_uintptr_t           node_cookie     = node->cookie;
          int                        node_debug_id   = node->debug_id;
          int                        has_weak_ref;
          int                        has_strong_ref;
          FAR void                  *orig_ptr = ptr;

          BUG_ON(proc != node->proc);
          strong = node->internal_strong_refs ||
                   node->local_strong_refs;
          weak = !list_is_empty(&node->refs) ||
                 node->local_weak_refs || node->tmp_refs || strong;
          has_strong_ref    = node->has_strong_ref;
          has_weak_ref      = node->has_weak_ref;

          if (weak && !has_weak_ref)
            {
              node->has_weak_ref        = 1;
              node->pending_weak_ref    = 1;
              node->local_weak_refs++;
            }

          if (strong && !has_strong_ref)
            {
              node->has_strong_ref      = 1;
              node->pending_strong_ref  = 1;
              node->local_strong_refs++;
            }

          if (!strong && has_strong_ref)
            {
              node->has_strong_ref = 0;
            }

          if (!weak && has_weak_ref)
            {
              node->has_weak_ref = 0;
            }

          if (!weak && !strong)
            {
              binder_debug(BINDER_DEBUG_INTERNAL_REFS,
                           "%d:%d node %d %" PRIx64 " %" PRIx64 " deleted\n",
                           proc->pid, thread->tid, node_debug_id, node_ptr,
                           node_cookie);

              list_delete_init(&node->rb_node);

              nxmutex_unlock(&proc->proc_lock);
              nxmutex_lock(&node->node_lock);

              /* Acquire the node lock before freeing the
               * node to serialize with other threads that
               * may have been holding the node lock while
               * decrementing this node (avoids race where
               * this thread frees while the other thread
               * is unlocking the node after the final
               * decrement)
               * TODO: it is need for NuttX??
               */

              nxmutex_unlock(&node->node_lock);
              binder_free_node(node);
            }
          else
            {
              nxmutex_unlock(&proc->proc_lock);
            }

          if (weak && !has_weak_ref)
            {
              ret = binder_put_node_cmd(proc, thread, &ptr, node_ptr,
                                        node_cookie, node_debug_id,
                                        BR_INCREFS, "BR_INCREFS");
            }

          if (!ret && strong && !has_strong_ref)
            {
              ret = binder_put_node_cmd(proc, thread, &ptr, node_ptr,
                                        node_cookie, node_debug_id,
                                        BR_ACQUIRE, "BR_ACQUIRE");
            }

          if (!ret && !strong && has_strong_ref)
            {
              ret = binder_put_node_cmd(proc, thread, &ptr, node_ptr,
                                        node_cookie, node_debug_id,
                                        BR_RELEASE, "BR_RELEASE");
            }

          if (!ret && !weak && has_weak_ref)
            {
              ret = binder_put_node_cmd(proc, thread, &ptr, node_ptr,
                                        node_cookie, node_debug_id,
                                        BR_DECREFS, "BR_DECREFS");
            }

          if (orig_ptr == ptr)
            {
              binder_debug(BINDER_DEBUG_INTERNAL_REFS,
                           "node %d %"PRIx64" %"PRIx64" state unchanged\n",
                           node_debug_id, node_ptr, node_cookie);
            }

          if (ret)
            {
              return ret;
            }
        }
        break;

        case BINDER_WORK_DEAD_BINDER:
        case BINDER_WORK_DEAD_BINDER_AND_CLEAR:
        case BINDER_WORK_CLEAR_DEATH_NOTIFICATION:
        {
          FAR struct binder_ref_death *death;
          binder_uintptr_t cookie;

          death = container_of(w, struct binder_ref_death, work);
          if (w->type == BINDER_WORK_CLEAR_DEATH_NOTIFICATION)
            {
              cmd = BR_CLEAR_DEATH_NOTIFICATION_DONE;
            }
          else
            {
              cmd = BR_DEAD_BINDER;
            }

          cookie = death->cookie;

          binder_debug(BINDER_DEBUG_DEATH_NOTIFICATION, "%s %"PRIx64"\n",
                       cmd == BR_DEAD_BINDER ?
                       "BR_DEAD_BINDER" :
                       "BR_CLEAR_DEATH_NOTIFICATION_DONE",
                       cookie);
          if (w->type == BINDER_WORK_CLEAR_DEATH_NOTIFICATION)
            {
              nxmutex_unlock(&proc->proc_lock);
              kmm_free(death);
           }
          else
            {
              binder_enqueue_work_ilocked(w, &proc->delivered_death);
              nxmutex_unlock(&proc->proc_lock);
            }

          put_value(cmd, (uint32_t *)ptr);
          ptr += sizeof(uint32_t);

          put_value(cookie, (binder_uintptr_t *)ptr);
          ptr += sizeof(binder_uintptr_t);

          if (cmd == BR_DEAD_BINDER)
            {
              goto done;
            }
        }
        break;

        default:
        {
          nxmutex_unlock(&proc->proc_lock);
          binder_debug(BINDER_DEBUG_ERROR, "bad work type %d\n", w->type);
          break;
        }
      }

      if (!t)
        {
          continue;
        }

      BUG_ON(t->buffer == NULL);
      if (t->buffer->target_node)
        {
          FAR struct binder_node *target_node = t->buffer->target_node;

          trd->target.ptr   = target_node->ptr;
          trd->cookie       =  target_node->cookie;
          binder_transaction_priority(thread, t, target_node);
          cmd = BR_TRANSACTION;
        }
      else
        {
          trd->target.ptr   = 0;
          trd->cookie       = 0;
          cmd               = BR_REPLY;
        }

      trd->code         = t->code;
      trd->flags        = t->flags;
      trd->sender_euid  = geteuid();

      t_from = binder_get_txn_from(t);
      if (t_from)
        {
          trd->sender_pid = t_from->tid;
        }
      else
        {
          trd->sender_pid = 0;
        }

      ret = binder_apply_fd_fixups(proc, t);
      if (ret)
        {
          struct binder_buffer *bbuffer = t->buffer;
          if (t_from)
            {
              binder_thread_dec_tmpref(t_from);
            }

          bbuffer->transaction = NULL;
          binder_cleanup_transaction(t, "fd fixups failed", BR_FAILED_REPLY);
          binder_free_buf(proc, thread, bbuffer, true);
          binder_debug(BINDER_DEBUG_FAILED_TRANSACTION,
                       "%stransaction %d fd fixups failed %d/%d, line %d\n",
                       (!!(t->flags & TF_ONE_WAY)) ? "async " :
                       (cmd == BR_REPLY ? "reply " : ""),
                       t->debug_id, BR_FAILED_REPLY, ret, __LINE__);

          if (cmd == BR_REPLY)
            {
              cmd = BR_FAILED_REPLY;
              put_value(cmd, (uint32_t *)ptr);
              ptr += sizeof(uint32_t);
              break;
            }

          continue;
        }

      trd->data_size        = t->buffer->data_size;
      trd->offsets_size     = t->buffer->offsets_size;
      trd->data.ptr.buffer  = (uintptr_t)t->buffer->user_data;
      trd->data.ptr.offsets = trd->data.ptr.buffer +
                      ALIGN(t->buffer->data_size, sizeof(void *));

      tr.secctx = t->security_ctx;
      if (t->security_ctx)
        {
          cmd       = BR_TRANSACTION_SEC_CTX;
          trsize    = sizeof(tr);
        }

      put_value(cmd, (uint32_t *)ptr);
      ptr += sizeof(uint32_t);

      memcpy(ptr, &tr, trsize);
      ptr += trsize;
      binder_debug(BINDER_DEBUG_TRANSACTION,
                   "%s %d %d:%d, cmd %"PRId32" size"
                   " %d-%d ptr %"PRIx64"-%"PRIx64"\n",
                   (cmd == BR_TRANSACTION) ?"BR_TRANSACTION" :
                   (cmd == BR_TRANSACTION_SEC_CTX) ?
                   "BR_TRANSACTION_SEC_CTX" : "BR_REPLY",
                   t->debug_id, t_from ? t_from->proc->pid : 0,
                   t_from ? t_from->tid : 0,
                   cmd, t->buffer->data_size, t->buffer->offsets_size,
                   trd->data.ptr.buffer, trd->data.ptr.offsets);

      if (t_from)
        {
          binder_thread_dec_tmpref(t_from);
        }

      t->buffer->allow_user_free = 1;
      if (cmd != BR_REPLY && !(t->flags & TF_ONE_WAY))
        {
          nxmutex_lock(&thread->proc->proc_lock);
          t->to_parent              = thread->transaction_stack;
          t->to_thread              = thread;
          thread->transaction_stack = t;

          nxmutex_unlock(&thread->proc->proc_lock);
        }
      else
        {
          binder_free_transaction(t);
        }
      break;
    }

done:
  *consumed = ptr - buffer;

  nxmutex_lock(&proc->proc_lock);
  if (proc->requested_threads == 0 &&
      list_is_empty(&thread->proc->waiting_threads) &&
      proc->requested_threads_started < proc->max_threads &&
      (thread->looper &
       (BINDER_LOOPER_STATE_REGISTERED | BINDER_LOOPER_STATE_ENTERED)))
    {
      proc->requested_threads++;

      nxmutex_unlock(&proc->proc_lock);
      binder_debug(BINDER_DEBUG_THREADS, "BR_SPAWN_LOOPER\n");
      put_value(BR_SPAWN_LOOPER, (uint32_t *)buffer);
    }
  else
    {
      nxmutex_unlock(&proc->proc_lock);
    }

  return 0;
}

void binder_release_work(FAR struct binder_proc *proc,
                         FAR struct list_node *list)
{
  FAR struct binder_work    *w;

  while (1)
    {
      nxmutex_lock(&proc->proc_lock);
      w     = binder_dequeue_work_head_ilocked(list);
      nxmutex_unlock(&proc->proc_lock);
      if (!w)
        {
          return;
        }

      switch (w->type)
      {
        case BINDER_WORK_TRANSACTION:
        {
          FAR struct binder_transaction *t;

          t = container_of(w, struct binder_transaction, work);

          binder_cleanup_transaction(t, "process died.", BR_DEAD_REPLY);
        }
        break;

        case BINDER_WORK_RETURN_ERROR:
        {
#ifdef CONFIG_BINDER_DRIVER_DEBUG
          struct binder_error *e =
                   container_of(w, struct binder_error, work);
          binder_debug(BINDER_DEBUG_DEAD_TRANSACTION,
                       "undelivered TRANSACTION_ERROR: %" PRIu32 "\n",
                       e->cmd);
#endif
        }
        break;

        case BINDER_WORK_TRANSACTION_COMPLETE:
        {
          binder_debug(BINDER_DEBUG_DEAD_TRANSACTION,
                       "undelivered TRANSACTION_COMPLETE\n");
          kmm_free(w);
        }
        break;

        case BINDER_WORK_DEAD_BINDER_AND_CLEAR:
        case BINDER_WORK_CLEAR_DEATH_NOTIFICATION:
        {
          FAR struct binder_ref_death *death;

          death = container_of(w, struct binder_ref_death, work);
          binder_debug(BINDER_DEBUG_DEAD_TRANSACTION,
                       "undelivered death notification, %" PRIx64 "\n",
                       death->cookie);
          kmm_free(death);
        }
        break;

        case BINDER_WORK_NODE:
        {
          break;
        }

        default:
        {
          binder_debug(BINDER_DEBUG_ERROR,
                       "unexpected work type, %d, not freed\n", w->type);
          break;
        }
      }
    }
}

int binder_thread_release(FAR struct binder_proc *proc,
                          FAR struct binder_thread *thread)
{
  FAR struct binder_transaction *t;
  FAR struct binder_transaction *send_reply         = NULL;
  int                            active_transactions = 0;
  FAR struct binder_transaction *last_t             = NULL;

  nxmutex_lock(&thread->proc->proc_lock);

  /* take a ref on the proc so it survives
   * after we remove this thread from proc->threads.
   * The corresponding dec is when we actually
   * free the thread in binder_free_thread()
   */

  proc->tmp_ref++;

  /* take a ref on this thread to ensure it
   * survives while we are releasing it
   */

  thread->tmp_ref++;

  list_delete_init(&thread->thread_node);
  t = thread->transaction_stack;
  if (t)
    {
      nxmutex_lock(&t->lock);
      if (t->to_thread == thread)
        {
          send_reply = t;
        }
    }

  thread->is_dead = true;

  while (t)
    {
      last_t = t;
      active_transactions++;
      binder_debug(BINDER_DEBUG_DEAD_TRANSACTION,
                   "release %d:%d transaction %d %s, still active\n",
                   proc->pid, thread->tid, t->debug_id,
                   (t->to_thread == thread) ? "in" : "out");

      if (t->to_thread == thread)
        {
          thread->proc->outstanding_txns--;
          t->to_proc    = NULL;
          t->to_thread  = NULL;
          if (t->buffer)
            {
              t->buffer->transaction    = NULL;
              t->buffer                 = NULL;
            }

          t = t->to_parent;
        }
      else if (t->from == thread)
        {
          t->from   = NULL;
          t         = t->from_parent;
        }
      else
        {
          BUG_ON(1);
        }

      nxmutex_unlock(&last_t->lock);
      if (t)
        {
          nxmutex_lock(&t->lock);
        }
    }

  /* If this thread used poll, make sure we remove the waitqueue from any
   * poll data structures holding it.
   */

  if (thread->looper & BINDER_LOOPER_STATE_POLL)
    {
      wake_up_pollfree(thread);
    }

  nxmutex_unlock(&thread->proc->proc_lock);

  if (send_reply)
    {
      binder_send_failed_reply(send_reply, BR_DEAD_REPLY);
    }

  binder_release_work(proc, &thread->todo);
  binder_thread_dec_tmpref(thread);
  return active_transactions;
}
