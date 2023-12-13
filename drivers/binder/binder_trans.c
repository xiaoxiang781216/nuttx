/****************************************************************************
 * drivers/binder/binder_trans.c
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

#define LOG_TAG  "BinderTrans"

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

#define to_flat_binder_object(hdr) \
  container_of(hdr, struct flat_binder_object, hdr)

#define to_binder_fd_object(hdr) \
  container_of(hdr, struct binder_fd_object, hdr)

#define to_binder_buffer_object(hdr) \
  container_of(hdr, struct binder_buffer_object, hdr)

#define to_binder_fd_array_object(hdr) \
  container_of(hdr, struct binder_fd_array_object, hdr)

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static bool binder_supported_policy(pid_t pid)
{
  int policy = nxsched_get_scheduler(pid);

  return policy == SCHED_FIFO || policy == SCHED_RR ||
         policy == SCHED_SPORADIC;
}

static uid_t geteuid_bypid(pid_t pid)
{
#ifdef CONFIG_SCHED_USER_IDENTITY
  /* We have effective UID support, then give the effective UID. */

  FAR struct tcb_s *tcb = nxsched_get_tcb(pid);
  FAR struct task_group_s *rgroup = tcb->group;

  DEBUGASSERT(rgroup != NULL);
  return rgroup->tg_euid;
#else
  /* Return user identity 'root' with a uid value of 0. */

  return 0;
#endif
}

/**
 * Name: file_tx_get
 *
 * Description:
 *   Given a file descriptor, return the corresponding instance of struct
 *   file and increment the inode reference conut of this file.
 *   Note: The function based on fs_getfilep, it is can be used to
 *       translate file descriptor between NuttX process
 *
 * Input Parameters:
 *   fd    - The file descriptor
 *   filep - The location to return the struct file instance
 *
 * Returned Value:
 *   Zero (OK) is returned on success; a negated errno value is returned on
 *   any failure.
 *
 */

static int file_tx_get(unsigned int fd, FAR struct file *filep)
{
  FAR struct file   *file;
  int                ret;

  ret = fs_getfilep(fd, &file);
  if (ret < 0)
    {
      return ret;
    }

  ret = file_dup2(file, filep);
  if (ret < 0)
    {
      return ret;
    }

  return ret;
}

/**
 * binder_get_object() - gets object and checks for valid metadata
 * @proc:  binder_proc owning the buffer
 * @u:    sender's user pointer to base of buffer
 * @buffer:  binder_buffer that we're parsing.
 * @offset:  offset in the @buffer at which to validate an object.
 * @object:  struct binder_object to read into
 *
 * Copy the binder object at the given offset into @object. If @u is
 * provided then the copy is from the sender's buffer. If not, then
 * it is copied from the target's @buffer.
 *
 * Return:  If there's a valid metadata object at @offset, the
 *    size of that object. Otherwise, it returns zero. The object
 *    is read into the struct binder_object pointed to by @object.
 */

static size_t binder_get_object(FAR struct binder_proc *proc,
                                FAR const void  *u,
                                FAR struct binder_buffer *buffer,
                                unsigned long offset,
                                FAR struct binder_object *object)
{
  size_t                             read_size;
  FAR struct binder_object_header   *hdr;
  size_t                             object_size = 0;

  read_size = min(sizeof(*object), buffer->data_size - offset);
  if (offset > buffer->data_size || read_size < sizeof(*hdr))
    {
      return 0;
    }

  if (u)
    {
      memcpy(object, u + offset, read_size);
    }
  else
    {
      if (binder_alloc_copy_from_buffer(&proc->alloc, object, buffer, offset,
                                        read_size))
        {
          return 0;
        }
    }

  /* Ok, now see if we read a complete object. */

  hdr = &object->hdr;
  switch (hdr->type)
  {
    case BINDER_TYPE_BINDER:
    case BINDER_TYPE_WEAK_BINDER:
    case BINDER_TYPE_HANDLE:
    case BINDER_TYPE_WEAK_HANDLE:
    {
      object_size = sizeof(struct flat_binder_object);
      break;
    }

    case BINDER_TYPE_FD:
    {
      object_size = sizeof(struct binder_fd_object);
      break;
    }

    case BINDER_TYPE_PTR:
    case BINDER_TYPE_FDA:
    default:
    {
      return 0;
    }
  }

  if (offset <= buffer->data_size - object_size &&
      buffer->data_size >= object_size)
    {
      return object_size;
    }
  else
    {
      return 0;
    }
}

/**
 * binder_get_txn_from() - safely extract the "from" thread in transaction
 * @t:  binder transaction for t->from
 *
 * Atomically return the "from" thread and increment the tmp_ref
 * count for the thread to ensure it stays alive until
 * binder_thread_dec_tmpref() is called.
 *
 * Return: the value of t->from
 */

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

static void binder_pop_transaction_ilocked(
  FAR struct binder_thread *target_thread, FAR struct binder_transaction *t)
{
  BUG_ON(!target_thread);
  BUG_ON(target_thread->transaction_stack != t);
  BUG_ON(target_thread->transaction_stack->from != target_thread);

  target_thread->transaction_stack =
    target_thread->transaction_stack->from_parent;
  t->from = NULL;
}

/**
 * binder_get_txn_from_and_acq_inner() - get t->from and acquire inner lock
 * @t:  binder transaction for t->from
 *
 * Same as binder_get_txn_from() except it also acquires the proc->inner_lock
 * to guarantee that the thread cannot be released while operating on it.
 * The caller must call binder_inner_proc_unlock() to release the inner lock
 * as well as call binder_dec_thread_txn() to release the reference.
 *
 * Return: the value of t->from
 */

static struct binder_thread *binder_get_txn_from_and_acq_inner(
  FAR struct binder_transaction *t)
{
  FAR struct binder_thread  *from;
  FAR struct binder_proc    *proc;

  from = binder_get_txn_from(t);
  if (!from)
    {
      return NULL;
    }

  proc = from->proc;
  nxmutex_lock(&proc->proc_lock);
  if (t->from)
    {
      nxmutex_unlock(&proc->proc_lock);
      BUG_ON(from != t->from);
      return from;
    }

  nxmutex_unlock(&proc->proc_lock);
  binder_thread_dec_tmpref(from);
  return NULL;
}

/**
 * binder_proc_transaction() - sends a transaction to a process and wakes it
 * up
 * @t:    transaction to send
 * @proc:  process to send the transaction to
 * @thread:  thread in @proc to send the transaction to (may be NULL)
 *
 * This function queues a transaction to the specified process. It will try
 * to find a thread in the target process to handle the transaction and
 * wake it up. If no thread is found, the work is queued to the proc
 * waitqueue.
 *
 * If the @thread parameter is not NULL, the transaction is always queued
 * to the waitlist of that specific thread.
 *
 * Return:  0 if the transaction was successfully queued
 *    BR_DEAD_REPLY if the target process or thread is dead
 *    BR_FROZEN_REPLY if the target process or thread is frozen
 */

static int binder_proc_transaction(FAR struct binder_transaction *t,
                                   FAR struct binder_proc *proc,
                                   FAR struct binder_thread *thread)
{
  FAR struct binder_node    *node           = t->buffer->target_node;
  bool                      oneway          = !!(t->flags & TF_ONE_WAY);
  bool                      pending_async   = false;

  BUG_ON(!node);
  nxmutex_lock(&node->node_lock);

  if (oneway)
    {
      BUG_ON(thread);
      if (node->has_async_transaction)
        {
          pending_async = true;
        }
      else
        {
          node->has_async_transaction = true;
        }
    }

  nxmutex_lock(&proc->proc_lock);
  if (proc->is_frozen)
    {
      proc->sync_recv   |= !oneway;
      proc->async_recv  |= oneway;
    }

  if ((proc->is_frozen && !oneway) || proc->is_dead ||
      (thread && thread->is_dead))
    {
      nxmutex_unlock(&proc->proc_lock);
      nxmutex_unlock(&node->node_lock);
      return proc->is_frozen ? BR_FROZEN_REPLY : BR_DEAD_REPLY;
    }

  if (!thread && !pending_async)
    {
      thread = binder_select_thread_ilocked(proc);
    }

  binder_debug(BINDER_DEBUG_TRANSACTION,
               "target %d:%d node %d code %d pending_async: %s oneway: %s\n",
               proc->pid, thread != NULL ? thread->tid : 0, node->debug_id,
               t->code, pending_async ? "true":"false",
               oneway ? "true":"false");

  if (thread)
    {
      binder_transaction_priority(thread, t, node);
      binder_enqueue_thread_work_ilocked(thread, &t->work);
    }
  else if (!pending_async)
    {
      binder_enqueue_work_ilocked(&t->work, &proc->todo_list);
    }
  else
    {
      binder_enqueue_work_ilocked(&t->work, &node->async_todo);
    }

  if (!pending_async)
    {
      binder_wakeup_thread_ilocked(proc, thread, !oneway /* sync */);
    }

  proc->outstanding_txns++;
  proc->tmp_ref--;
  nxmutex_unlock(&proc->proc_lock);
  nxmutex_unlock(&node->node_lock);

  return 0;
}

static int binder_translate_binder(FAR struct flat_binder_object *fp,
                                   FAR struct binder_transaction *t,
                                   FAR struct binder_thread *thread)
{
  FAR struct binder_node    *node;
  FAR struct binder_proc    *proc           = thread->proc;
  FAR struct binder_proc    *target_proc    = t->to_proc;
  struct binder_ref_data     rdata;
  int                        ret = 0;

  node = binder_get_node(proc, fp->binder);
  if (!node)
    {
      node = binder_new_node(proc, fp);
      if (!node)
        {
          return -ENOMEM;
        }
    }

  if (fp->cookie != node->cookie)
    {
      binder_debug(BINDER_DEBUG_ERROR,
                   "sending %"PRIx64" node %d, cookie mismatch "
                   "%"PRIx64" != %"PRIx64"\n",
                   fp->binder, node->debug_id, fp->cookie,
                   node->cookie);
      ret = -EINVAL;
      goto done;
    }

  ret = binder_inc_ref_for_node(target_proc, node,
                                fp->hdr.type == BINDER_TYPE_BINDER,
                                &thread->todo, &rdata);
  if (ret)
    {
      goto done;
    }

  if (fp->hdr.type == BINDER_TYPE_BINDER)
    {
      fp->hdr.type = BINDER_TYPE_HANDLE;
    }
  else
    {
      fp->hdr.type = BINDER_TYPE_WEAK_HANDLE;
    }

  fp->binder    = 0;
  fp->handle    = rdata.desc;
  fp->cookie    = 0;

  binder_debug(BINDER_DEBUG_TRANSACTION,
               "node %d %" PRIx64 " -> ref %d desc %d\n", node->debug_id,
               node->ptr, rdata.debug_id, (int)rdata.desc);
done:
  binder_put_node(node);
  return ret;
}

static int binder_translate_handle(struct flat_binder_object *fp,
                                   struct binder_transaction *t,
                                   struct binder_thread *thread)
{
  FAR struct binder_proc        *proc           = thread->proc;
  FAR struct binder_proc        *target_proc    = t->to_proc;
  FAR struct binder_node        *node;
  struct binder_ref_data         src_rdata;
  int                            ret = 0;

  node = binder_get_node_from_ref(proc, fp->handle,
                                  fp->hdr.type == BINDER_TYPE_HANDLE,
                                  &src_rdata);
  if (!node)
    {
      binder_debug(BINDER_DEBUG_ERROR,
                   "got transaction with invalid handle, %d\n",
                   (int)fp->handle);
      return -EINVAL;
    }

  nxmutex_lock(&node->node_lock);
  if (node->proc == target_proc)
    {
      if (fp->hdr.type == BINDER_TYPE_HANDLE)
        {
          fp->hdr.type = BINDER_TYPE_BINDER;
        }
      else
        {
          fp->hdr.type = BINDER_TYPE_WEAK_BINDER;
        }

      fp->binder    = node->ptr;
      fp->cookie    = node->cookie;
      if (node->proc)
        {
          nxmutex_lock(&node->proc->proc_lock);
        }

      binder_inc_node_nilocked(node, fp->hdr.type == BINDER_TYPE_BINDER, 0,
                               NULL);
      if (node->proc)
        {
          nxmutex_unlock(&node->proc->proc_lock);
        }

      binder_debug(BINDER_DEBUG_TRANSACTION,
                   "ref %d desc %d -> node %d %" PRIx64 "\n",
                   src_rdata.debug_id, (int)src_rdata.desc, node->debug_id,
                   node->ptr);
      nxmutex_unlock(&node->node_lock);
    }
  else
    {
      struct binder_ref_data dest_rdata;

      nxmutex_unlock(&node->node_lock);
      ret = binder_inc_ref_for_node(target_proc, node,
                                    fp->hdr.type == BINDER_TYPE_HANDLE, NULL,
                                    &dest_rdata);
      if (ret)
        {
          goto done;
        }

      fp->binder    = 0;
      fp->handle    = dest_rdata.desc;
      fp->cookie    = 0;
      binder_debug(BINDER_DEBUG_TRANSACTION,
                   "ref %d desc %d -> ref %d desc %d (node %d)\n",
                   src_rdata.debug_id, (int)src_rdata.desc,
                   dest_rdata.debug_id, (int)dest_rdata.desc,
                   node->debug_id);
    }

done:
  binder_put_node(node);
  return ret;
}

static int binder_translate_fd(uint32_t fd, binder_size_t fd_offset,
                               FAR struct binder_transaction *t,
                               FAR struct binder_thread *thread,
                               FAR struct binder_transaction *in_reply_to)
{
  FAR struct binder_txn_fd_fixup    *fixup;
  int                                ret = 0;
  bool                               target_allows_fd;

  if (in_reply_to)
    {
      target_allows_fd = !!(in_reply_to->flags & TF_ACCEPT_FDS);
    }
  else
    {
      target_allows_fd = t->buffer->target_node->accept_fds;
    }

  if (!target_allows_fd)
    {
      binder_debug(BINDER_DEBUG_ERROR,
                   "got %s with fd, %"PRId32", "
                   "but target does not allow fds\n",
                   in_reply_to ? "reply" : "transaction", fd);
      ret = -EPERM;
      goto err_fd_not_accepted;
    }

  /* Add fixup record for this transaction. The allocation
   * of the fd in the target needs to be done from a
   * target thread.
   */

  fixup = kmm_zalloc(sizeof(*fixup));
  if (!fixup)
    {
      ret = -ENOMEM;
      goto err_alloc;
    }

  list_initialize(&fixup->fixup_entry);
  ret = file_tx_get(fd, &fixup->file);
  if (ret < 0)
    {
      binder_debug(BINDER_DEBUG_ERROR,
                   "got transaction with invalid fd, %" PRId32 "\n", fd);
      ret = -EBADF;
      goto err_fget;
    }

  fixup->offset = fd_offset;
  list_add_tail(&fixup->fixup_entry, &t->fd_fixups);

  return ret;

err_fget:
  kmm_free(fixup);
err_alloc:
err_fd_not_accepted:
  return ret;
}

/**
 * binder_get_node_refs_for_txn() - Get required refs on node for txn
 * @node:         struct binder_node for which to get refs
 * @proc:         returns @node->proc if valid
 * @error:        if no @proc then returns BR_DEAD_REPLY
 *
 * User-space normally keeps the node alive when creating a transaction
 * since it has a reference to the target. The local strong ref keeps it
 * alive if the sending process dies before the target process processes
 * the transaction. If the source process is malicious or has a reference
 * counting bug, relying on the local strong ref can fail.
 *
 * Since user-space can cause the local strong ref to go away, we also take
 * a tmpref on the node to ensure it survives while we are constructing
 * the transaction. We also need a tmpref on the proc while we are
 * constructing the transaction, so we take that here as well.
 *
 * Return: The target_node with refs taken or NULL if no @node->proc is NULL.
 * Also sets @proc if valid. If the @node->proc is NULL indicating that the
 * target proc has died, @error is set to BR_DEAD_REPLY
 */

static struct binder_node *binder_get_node_refs_for_txn(
  FAR struct binder_node *node, FAR struct binder_proc **procp,
  FAR int *error)
{
  FAR struct binder_node *target_node = NULL;

  nxmutex_lock(&node->node_lock);
  if (node->proc)
    {
      target_node = node;
      binder_inc_node_nilocked(node, 1, 0, NULL);
      binder_inc_node_tmpref_ilocked(node);
      node->proc->tmp_ref++;
      *procp = node->proc;
    }
  else
    {
      *error = BR_DEAD_REPLY;
    }

  nxmutex_unlock(&node->node_lock);

  return target_node;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void binder_transaction_buffer_release(FAR struct binder_proc *proc,
                                       FAR struct binder_thread *thread,
                                       FAR struct binder_buffer *buffer,
                                       binder_size_t failed_at,
                                       bool is_failure)
{
  binder_size_t off_start_offset;
  binder_size_t buffer_offset;
  binder_size_t off_end_offset;

  binder_debug(BINDER_DEBUG_TRANSACTION,
               "buffer release %d, size %d-%d, failed at %" PRIx64 "\n",
               buffer->debug_id, buffer->data_size, buffer->offsets_size,
               failed_at);

  if (buffer->target_node)
    {
      binder_dec_node(buffer->target_node, 1, 0);
    }

  off_start_offset  = ALIGN(buffer->data_size, sizeof(void *));
  off_end_offset    = is_failure &&
                      failed_at ? failed_at :off_start_offset +
                      buffer->offsets_size;
  for (buffer_offset = off_start_offset; buffer_offset < off_end_offset;
       buffer_offset += sizeof(binder_size_t))
    {
      struct binder_object_header   *hdr;
      size_t                        object_size = 0;
      struct binder_object          object;
      binder_size_t                 object_offset;

      if (!binder_alloc_copy_from_buffer(
               &proc->alloc, &object_offset, buffer,
               buffer_offset, sizeof(object_offset)))
        {
          object_size = binder_get_object(proc, NULL, buffer,
                                         object_offset, &object);
        }

      if (object_size == 0)
        {
          binder_debug(BINDER_DEBUG_ERROR,
                       "transaction release %d bad object at "
                       "offset %" PRId64 ", size %d\n",
                       buffer->debug_id, object_offset,
                       buffer->data_size);
          continue;
        }

      hdr = &object.hdr;
      switch (hdr->type)
      {
        case BINDER_TYPE_BINDER:
        case BINDER_TYPE_WEAK_BINDER:
        {
          FAR struct flat_binder_object *fp;
          FAR struct binder_node        *node;

          fp    = to_flat_binder_object(hdr);
          node  = binder_get_node(proc, fp->binder);
          if (node == NULL)
            {
              binder_debug(BINDER_DEBUG_ERROR,
                           "transaction release %d bad node %"PRIx64"\n",
                           node->debug_id, fp->binder);
              break;
            }

          binder_debug(BINDER_DEBUG_TRANSACTION, "node %d %" PRIx64 "\n",
                       node->debug_id, node->ptr);
          binder_dec_node(node, hdr->type == BINDER_TYPE_BINDER, 0);
          binder_put_node(node);
          break;
        }

        case BINDER_TYPE_HANDLE:
        case BINDER_TYPE_WEAK_HANDLE:
        {
          FAR struct flat_binder_object *fp;
          struct binder_ref_data         rdata;
          int                            ret;

          fp    = to_flat_binder_object(hdr);
          ret   = binder_dec_ref_for_handle(proc, fp->handle,
                                            hdr->type == BINDER_TYPE_HANDLE,
                                            &rdata);
          if (ret)
            {
              binder_debug(BINDER_DEBUG_ERROR,
                           "transaction release %d "
                           "bad handle %d, ret = %d\n",
                           rdata.debug_id, (int)fp->handle, ret);
              break;
            }

          binder_debug(BINDER_DEBUG_TRANSACTION, "ref %d desc %d\n",
                       rdata.debug_id, (int)rdata.desc);
          break;
        }

        case BINDER_TYPE_FD:
        {
          /* No need to close the file here since user-space
           * closes it for successfully delivered
           * transactions. For transactions that weren't
           * delivered, the new fd was never allocated so
           * there is no need to close and the fput on the
           * file is done when the transaction is torn
           * down.
           */

          break;
        }

        case BINDER_TYPE_PTR:
        case BINDER_TYPE_FDA:
        default:
        {
          binder_debug(BINDER_DEBUG_ERROR,
                       "transaction release %d bad object type %d\n",
                       buffer->debug_id, (int)hdr->type);
          break;
        }
      }
    }
}

/**
 * binder_deferred_fd_close()
 *  - schedule a close for the given file-descriptor
 * @fd:    file-descriptor to close
 */

void binder_deferred_fd_close(int fd)
{
  int ret;

  ret = nx_close(fd);
  if (ret < 0)
    {
      binder_debug(BINDER_DEBUG_ERROR, "errno nx_close, ret =%d\n", ret);
    }
}

void binder_transaction_priority(FAR struct binder_thread *thread,
                                 FAR struct binder_transaction *t,
                                 FAR struct binder_node *node)
{
  struct binder_priority        desired =
  {
    .sched_policy   = t->priority.sched_policy,
    .sched_prio     = t->priority.sched_prio,
  };

  const struct binder_priority  node_prio =
  {
    .sched_policy   = node->sched_policy,
    .sched_prio = node->min_priority,
  };

  if (t->set_priority_called)
    {
      return;
    }

  t->set_priority_called = true;

  binder_get_priority(thread->tid, &t->saved_priority);

  if (node_prio.sched_prio > desired.sched_prio)
    {
      binder_set_priority(thread, &node_prio);
    }
  else
    {
      binder_set_priority(thread, &desired);
    }
}

void binder_send_failed_reply(FAR struct binder_transaction *t,
                              uint32_t error_code)
{
  FAR struct binder_thread      *target_thread;
  FAR struct binder_transaction *next;

  BUG_ON(t->flags & TF_ONE_WAY);
  while (1)
    {
      target_thread = binder_get_txn_from_and_acq_inner(t);
      if (target_thread)
        {
          binder_debug(BINDER_DEBUG_FAILED_TRANSACTION,
                       "send failed reply for transaction %d to %d:%d\n",
                       t->debug_id, target_thread->proc->pid,
                       target_thread->tid);

          binder_pop_transaction_ilocked(target_thread, t);
          if (target_thread->reply_error.cmd == BR_OK)
            {
              target_thread->reply_error.cmd = error_code;
              binder_enqueue_thread_work_ilocked(
                  target_thread, &target_thread->reply_error.work);
              wait_wake_up(&target_thread->wait, 0);
            }
          else
            {
              /* Cannot get here for normal operation, but
               * we can if multiple synchronous transactions
               * are sent without blocking for responses.
               * Just ignore the 2nd error in this case.
               */

              binder_debug(BINDER_DEBUG_WARNING,
                           "Unexpected reply error: %d\n",
                           (int)target_thread->reply_error.cmd);
            }

          if (nxmutex_is_hold(&target_thread->proc->proc_lock))
            {
              nxmutex_unlock(&target_thread->proc->proc_lock);
            }

          binder_thread_dec_tmpref(target_thread);
          binder_free_transaction(t);
          return;
        }

      next = t->from_parent;

      binder_debug(BINDER_DEBUG_FAILED_TRANSACTION,
                   "send failed reply for transaction %d, target dead\n",
                   t->debug_id);

      binder_free_transaction(t);
      if (next == NULL)
        {
          binder_debug(BINDER_DEBUG_DEAD_BINDER,
                       "reply failed, no target thread at root\n");
          return;
        }

      t = next;
      binder_debug(BINDER_DEBUG_DEAD_BINDER,
                   "reply failed, no target thread -- retry %d\n",
                   t->debug_id);
    }
}

/**
 * binder_cleanup_transaction() - cleans up undelivered transaction
 * @t:    transaction that needs to be cleaned up
 * @reason:  reason the transaction wasn't delivered
 * @error_code:  error to return to caller (if synchronous call)
 */

void binder_cleanup_transaction(FAR struct binder_transaction *t,
                                FAR const char *reason, uint32_t error_code)
{
  if (t->buffer->target_node && !(t->flags & TF_ONE_WAY))
    {
      binder_send_failed_reply(t, error_code);
    }
  else
    {
      binder_debug(BINDER_DEBUG_DEAD_TRANSACTION,
                   "undelivered transaction %d, %s\n", t->debug_id, reason);
      binder_free_transaction(t);
    }
}

void binder_free_transaction(FAR struct binder_transaction *t)
{
  FAR struct binder_proc *target_proc = t->to_proc;

  if (target_proc)
    {
      nxmutex_lock(&target_proc->proc_lock);
      target_proc->outstanding_txns--;
      if (target_proc->outstanding_txns < 0)
        {
          binder_debug(BINDER_DEBUG_WARNING,
                       "Unexpected outstanding_txns %d\n",
                       target_proc->outstanding_txns);
        }

      if (!target_proc->outstanding_txns && target_proc->is_frozen)
        {
          wait_wake_up(&target_proc->freeze_wait, 0);
        }

      if (t->buffer)
        {
          t->buffer->transaction = NULL;
        }

      nxmutex_unlock(&target_proc->proc_lock);
    }

  kmm_free(t);
}

void binder_transaction(FAR struct binder_proc *proc,
                        FAR struct binder_thread *thread,
                        FAR struct binder_transaction_data *tr, int reply)
{
  int                            ret                = 0;
  FAR struct binder_transaction *t;
  FAR struct binder_work        *w;
  FAR struct binder_work        *tcomplete;
  binder_size_t                  buffer_offset      = 0;
  binder_size_t                  off_min;
  binder_size_t                  user_offset        = 0;
  FAR struct binder_proc        *target_proc        = NULL;
  FAR struct binder_thread      *target_thread      = NULL;
  FAR struct binder_node        *target_node        = NULL;
  FAR struct binder_transaction *in_reply_to        = NULL;
  int                            return_error       = 0;
  FAR struct binder_context     *context            = proc->context;
  FAR const void *user_buffer = (const void *)(uintptr_t)tr->data.ptr.buffer;
  FAR char *secctx            = NULL;
  uint32_t secctx_sz          = 0;

  if (reply)
    {
      nxmutex_lock(&proc->proc_lock);
      in_reply_to = thread->transaction_stack;
      if (in_reply_to == NULL)
        {
          nxmutex_unlock(&proc->proc_lock);
          binder_debug(BINDER_DEBUG_ERROR,
                       "[%s][%d:%d]:"
                       "got reply transaction with no transaction stack\n",
                       LOG_TAG, getpid(), gettid());
          return_error          = BR_FAILED_REPLY;
          goto err_empty_call_stack;
        }

      if (in_reply_to->to_thread != thread)
        {
          nxmutex_lock(&in_reply_to->lock);
          binder_debug(BINDER_DEBUG_ERROR,
                       "got reply transaction with bad transaction stack, "
                       "transaction %d has target %d:%d\n",
                       in_reply_to->debug_id,
                       in_reply_to->to_proc ?
                       in_reply_to->to_proc->pid : 0,
                       in_reply_to->to_thread ?
                       in_reply_to->to_thread->tid : 0);
          nxmutex_unlock(&in_reply_to->lock);
          nxmutex_unlock(&proc->proc_lock);
          return_error          = BR_FAILED_REPLY;
          in_reply_to           = NULL;
          goto err_bad_call_stack;
        }

      thread->transaction_stack = in_reply_to->to_parent;
      nxmutex_unlock(&proc->proc_lock);
      target_thread = binder_get_txn_from_and_acq_inner(in_reply_to);
      if (target_thread == NULL)
        {
          /* annotation for sparse */

          return_error      = BR_DEAD_REPLY;
          goto err_dead_binder;
        }

      if (target_thread->transaction_stack != in_reply_to)
        {
          binder_debug(BINDER_DEBUG_ERROR,
                       "got reply transaction with bad target "
                       "transaction stack %d, expected %d\n",
                       target_thread->transaction_stack ?
                       target_thread->transaction_stack->debug_id : 0,
                       in_reply_to->debug_id);

          if (nxmutex_is_hold(&target_thread->proc->proc_lock))
            {
              nxmutex_unlock(&target_thread->proc->proc_lock);
            }

          return_error          = BR_FAILED_REPLY;
          in_reply_to           = NULL;
          target_thread         = NULL;
          goto err_dead_binder;
        }

      target_proc = target_thread->proc;
      target_proc->tmp_ref++;
      if (nxmutex_is_hold(&target_thread->proc->proc_lock))
        {
          nxmutex_unlock(&target_thread->proc->proc_lock);
        }
    }
  else
    {
      if (tr->target.handle)
        {
          struct binder_ref *ref;

          /* There must already be a strong ref on this node.
           * If so, do a strong increment on the node to ensure it
           * stays alive until the transaction is done.
           */

          nxmutex_lock(&proc->proc_lock);
          ref = binder_get_ref_olocked(proc, tr->target.handle, true);
          if (ref)
            {
              target_node = binder_get_node_refs_for_txn(
                 ref->node, &target_proc, &return_error);
            }
          else
            {
              binder_debug(BINDER_DEBUG_ERROR,
                           "got transaction to invalid handle, "
                           "%"PRIu32"\n",
                           tr->target.handle);
              return_error = BR_FAILED_REPLY;
            }

          nxmutex_unlock(&proc->proc_lock);
        }
      else
        {
          nxmutex_lock(&context->context_lock);
          target_node = context->mgr_node;
          if (target_node)
            {
              target_node = binder_get_node_refs_for_txn(
                  target_node, &target_proc, &return_error);
            }
          else
            {
              return_error = BR_DEAD_REPLY;
            }

          nxmutex_unlock(&context->context_lock);
          if (target_node && target_proc->pid == proc->pid)
            {
              binder_debug(BINDER_DEBUG_ERROR,
                           "[%s][%d:%d]:"
                           "got transaction to context manager "
                           "from process owning it\n",
                           LOG_TAG, getpid(), gettid());
              return_error          = BR_FAILED_REPLY;
              goto err_invalid_target_handle;
            }
        }

      if (!target_node)
        {
          /* return_error is set above */

          goto err_dead_binder;
        }

      if (proc == target_proc)
        {
          WARN_ON(1);
          return_error          = BR_FAILED_REPLY;
          goto err_invalid_target_handle;
        }

      nxmutex_lock(&proc->proc_lock);
      w = list_first_entry_or_null(&thread->todo, struct binder_work,
                                   entry_node);
      if (!(tr->flags & TF_ONE_WAY) && w &&
          w->type == BINDER_WORK_TRANSACTION)
        {
          /* Do not allow new outgoing transaction from a
           * thread that has a transaction at the head of
           * its todo list. Only need to check the head
           * because binder_select_thread_ilocked picks a
           * thread from proc->waiting_threads to enqueue
           * the transaction, and nothing is queued to the
           * todo list while the thread is on waiting_threads.
           */

          binder_debug(BINDER_DEBUG_ERROR,
                       "[%s][%d:%d]:"
                       "new transaction not allowed when there is "
                       "a transaction on thread todo\n",
                       LOG_TAG, getpid(), gettid());
          nxmutex_unlock(&proc->proc_lock);
          return_error          = BR_FAILED_REPLY;
          goto err_bad_todo_list;
        }

      if (!(tr->flags & TF_ONE_WAY) && thread->transaction_stack)
        {
          FAR struct binder_transaction *tmp;
          tmp = thread->transaction_stack;
          if (tmp->to_thread != thread)
            {
              nxmutex_lock(&tmp->lock);
              binder_debug(BINDER_DEBUG_ERROR,
                           "got new transaction with bad transaction "
                           "stack, transaction %d has target %d:%d\n",
                           tmp->debug_id,
                           tmp->to_proc ? tmp->to_proc->pid : 0,
                           tmp->to_thread ? tmp->to_thread->tid : 0);
              nxmutex_unlock(&tmp->lock);
              nxmutex_unlock(&proc->proc_lock);
              return_error          = BR_FAILED_REPLY;
              goto err_bad_call_stack;
            }

          while (tmp)
            {
              FAR struct binder_thread *from;
              nxmutex_lock(&tmp->lock);
              from = tmp->from;
              if (from && from->proc == target_proc)
                {
                  from->tmp_ref++;
                  target_thread = from;
                  nxmutex_unlock(&tmp->lock);
                  break;
                }

              nxmutex_unlock(&tmp->lock);
              tmp = tmp->from_parent;
            }
        }

      nxmutex_unlock(&proc->proc_lock);
    }

  /* TODO: reuse incoming transaction for reply */

  t = kmm_zalloc(sizeof(struct binder_transaction));
  if (t == NULL)
    {
      return_error          = BR_FAILED_REPLY;
      goto err_alloc_t_failed;
    }

  list_initialize(&t->fd_fixups);
  list_initialize(&t->work.entry_node);
  nxmutex_init(&t->lock);
  tcomplete = kmm_zalloc(sizeof(struct binder_work));
  if (tcomplete == NULL)
    {
      return_error          = BR_FAILED_REPLY;
      goto err_alloc_tcomplete_failed;
    }

  list_initialize(&tcomplete->entry_node);

  if (reply)
    {
      binder_debug(BINDER_DEBUG_TRANSACTION,
                   "%d:%d BC_REPLY -> %d:%d, data %"PRIx64"-%"PRIx64" "
                   "size %"PRId64"-%"PRId64"\n",
                   proc->pid, thread->tid, target_proc->pid,
                   target_thread->tid, tr->data.ptr.buffer,
                   tr->data.ptr.offsets, tr->data_size,
                   tr->offsets_size);
    }
  else
    {
      binder_debug(BINDER_DEBUG_TRANSACTION,
                   "%d:%d BC_TRANSACTION -> %d-node %d, "
                   "data %"PRIx64"-%"PRIx64" "
                   "size %"PRId64"-%"PRId64"\n",
                   proc->pid, thread->tid, target_proc->pid,
                   target_node->debug_id, tr->data.ptr.buffer,
                   tr->data.ptr.offsets, tr->data_size,
                   tr->offsets_size);
    }

  if (!reply && !(tr->flags & TF_ONE_WAY))
    {
      t->from = thread;
    }
  else
    {
      t->from = NULL;
    }

  t->sender_euid    = geteuid_bypid(proc->pid);
  t->to_proc        = target_proc;
  t->to_thread      = target_thread;
  t->code           = tr->code;
  t->flags          = tr->flags;

  if (!(t->flags & TF_ONE_WAY) && binder_supported_policy(gettid()))
    {
      binder_get_priority(gettid(), &t->priority);
    }
  else
    {
      /* Otherwise, fall back to the default priority */

      t->priority.sched_policy  = target_proc->default_priority.sched_policy;
      t->priority.sched_prio    = target_proc->default_priority.sched_prio;
    }

  if (target_node && target_node->txn_security_ctx)
    {
      secctx = "0";
      secctx_sz = 1;
    }

  t->buffer             =
    binder_alloc_new_buf(&target_proc->alloc, tr->data_size,
                         tr->offsets_size, secctx_sz,
                         !reply && (t->flags & TF_ONE_WAY),
                         gettid(), &ret);
  if (ret < 0)
    {
      /* -ESRCH indicates VMA cleared. The target is dying. */

      return_error = ret ==
                     -ESRCH ?BR_DEAD_REPLY : BR_FAILED_REPLY;
      t->buffer         = NULL;
      goto err_binder_alloc_buf_failed;
    }

  if (secctx)
    {
      int err;
      size_t buf_offset = ALIGN(tr->data_size, sizeof(void *)) +
                          ALIGN(tr->offsets_size, sizeof(void *));

      t->security_ctx = (uintptr_t)t->buffer->user_data + buf_offset;
      err = binder_alloc_copy_to_buffer(&target_proc->alloc,
                                        t->buffer, buf_offset,
                                        secctx, secctx_sz);
      if (err)
        {
          t->security_ctx = 0;
          WARN_ON(1);
        }
    }

  t->buffer->debug_id       = t->debug_id;
  t->buffer->transaction    = t;
  t->buffer->target_node    = target_node;
  t->buffer->clear_on_free  = !!(t->flags & TF_CLEAR_BUF);

  if (binder_alloc_copy_to_buffer(&target_proc->alloc, t->buffer,
                      ALIGN(tr->data_size, sizeof(void *)),
                      (FAR void *)(uintptr_t)tr->data.ptr.offsets,
                      tr->offsets_size))
    {
      binder_debug(BINDER_DEBUG_ERROR,
                   "[%s][%d:%d]:"
                   "got transaction with invalid offsets ptr\n",
                   LOG_TAG, getpid(), gettid());
      return_error          = BR_FAILED_REPLY;
      goto err_copy_data_failed;
    }

  if (!IS_ALIGNED(tr->offsets_size, sizeof(binder_size_t)))
    {
      binder_debug(BINDER_DEBUG_ERROR,
                   "got transaction with invalid offsets size, "
                   "%"PRId64"\n", tr->offsets_size);
      return_error          = BR_FAILED_REPLY;
      goto err_bad_offset;
    }

  off_min           = 0;
  for (buffer_offset = ALIGN(tr->data_size, sizeof(void *));
       buffer_offset <
       ALIGN(tr->data_size, sizeof(void *)) + tr->offsets_size;
       buffer_offset += sizeof(binder_size_t))
    {
      FAR struct binder_object_header   *hdr;
      size_t                             object_size;
      struct binder_object               object;
      binder_size_t                      object_offset;
      binder_size_t                      copy_size;

      if (binder_alloc_copy_from_buffer(&target_proc->alloc, &object_offset,
                                        t->buffer, buffer_offset,
                                        sizeof(object_offset)))
        {
          return_error          = BR_FAILED_REPLY;
          goto err_bad_offset;
        }

      /* Copy the source user buffer up to the next object
       * that will be processed.
       */

      copy_size = object_offset - user_offset;
      if (copy_size &&
          (user_offset > object_offset ||
           binder_alloc_copy_to_buffer(&target_proc->alloc, t->buffer,
                                       user_offset,
                                       (void *)(user_buffer + user_offset),
                                       copy_size)))
        {
          binder_debug(BINDER_DEBUG_ERROR,
                       "[%s][%d:%d]:"
                       "got transaction with invalid data ptr\n",
                       LOG_TAG, getpid(), gettid());
          return_error          = BR_FAILED_REPLY;
          goto err_copy_data_failed;
        }

      object_size = binder_get_object(target_proc, user_buffer, t->buffer,
                                      object_offset, &object);
      if (object_size == 0 || object_offset < off_min)
        {
          binder_debug(BINDER_DEBUG_ERROR,
                       "got transaction with invalid offset "
                       "(%"PRId64", min %"PRId64" max %d) or object.\n",
                       object_offset, off_min, t->buffer->data_size);
          return_error          = BR_FAILED_REPLY;
          goto err_bad_offset;
        }

      /* Set offset to the next buffer fragment to be copied */

      user_offset   = object_offset + object_size;
      hdr           = &object.hdr;
      off_min       = object_offset + object_size;
      switch (hdr->type)
      {
        case BINDER_TYPE_BINDER:
        case BINDER_TYPE_WEAK_BINDER:
        {
          FAR struct flat_binder_object *fp;
          fp    = to_flat_binder_object(hdr);
          ret   = binder_translate_binder(fp, t, thread);
          if (ret < 0 ||
              binder_alloc_copy_to_buffer(&target_proc->alloc, t->buffer,
                                          object_offset, fp, sizeof(*fp)))
            {
              return_error          = BR_FAILED_REPLY;
              goto err_translate_failed;
            }
          break;
        }

        case BINDER_TYPE_HANDLE:
        case BINDER_TYPE_WEAK_HANDLE:
        {
          FAR struct flat_binder_object *fp;
          fp    = to_flat_binder_object(hdr);
          ret   = binder_translate_handle(fp, t, thread);
          if (ret < 0 ||
              binder_alloc_copy_to_buffer(&target_proc->alloc, t->buffer,
                                          object_offset, fp, sizeof(*fp)))
            {
              return_error          = BR_FAILED_REPLY;
              goto err_translate_failed;
            }

          break;
        }

        case BINDER_TYPE_FD:
        {
          FAR struct binder_fd_object   *fp     = to_binder_fd_object(hdr);
          binder_size_t             fd_offset   = object_offset +
                                                  (uintptr_t)&fp->fd -
                                                  (uintptr_t)fp;
          int ret_local = binder_translate_fd(fp->fd,
                                              fd_offset,
                                              t, thread,
                                              in_reply_to);
          fp->pad_binder = 0;
          if (ret_local < 0 ||
              binder_alloc_copy_to_buffer(&target_proc->alloc, t->buffer,
                                          object_offset, fp, sizeof(*fp)))
            {
              return_error          = BR_FAILED_REPLY;
              goto err_translate_failed;
            }
          break;
        }

        case BINDER_TYPE_FDA:
        case BINDER_TYPE_PTR:
        {
          binder_debug(BINDER_DEBUG_ERROR,
                       "BINDER_TYPE_FDA or BINDER_TYPE_PTR "
                       "Unsupport for NuttX, "
                       "%" PRIx32 "\n", hdr->type);
          BUG_ON(1);
          break;
        }

        default:
        {
          binder_debug(BINDER_DEBUG_ERROR,
                       "got transaction with invalid object type, "
                       "%" PRIx32 "\n", hdr->type);
          return_error          = BR_FAILED_REPLY;
          goto err_bad_object_type;
        }
      }
    }

  /* Done processing objects, copy the rest of the buffer */

  if (binder_alloc_copy_to_buffer(&target_proc->alloc,
              t->buffer, user_offset, (void *)(user_buffer + user_offset),
              tr->data_size - user_offset))
    {
      binder_debug(BINDER_DEBUG_ERROR,
                   "[%s][%d:%d]:"
                   "got transaction with invalid data ptr\n",
                   LOG_TAG, getpid(), gettid());
      return_error          = BR_FAILED_REPLY;
      goto err_copy_data_failed;
    }

  tcomplete->type   = BINDER_WORK_TRANSACTION_COMPLETE;
  t->work.type      = BINDER_WORK_TRANSACTION;
  if (reply)
    {
      binder_enqueue_thread_work(thread, tcomplete);
      nxmutex_lock(&target_proc->proc_lock);
      if (target_thread->is_dead)
        {
          return_error = BR_DEAD_REPLY;
          nxmutex_unlock(&target_proc->proc_lock);
          goto err_dead_proc_or_thread;
        }

      BUG_ON(t->buffer->async_transaction != 0);
      binder_pop_transaction_ilocked(target_thread, in_reply_to);
      binder_enqueue_thread_work_ilocked(target_thread, &t->work);
      target_proc->outstanding_txns++;
      target_proc->tmp_ref--;
      nxmutex_unlock(&target_proc->proc_lock);
      wait_wake_up(&target_thread->wait, 0);
      binder_set_priority(thread, &in_reply_to->saved_priority);
      binder_free_transaction(in_reply_to);
    }
  else if (!(t->flags & TF_ONE_WAY))
    {
      BUG_ON(t->buffer->async_transaction != 0);
      nxmutex_lock(&proc->proc_lock);

      /* Defer the TRANSACTION_COMPLETE, so we don't return to
       * userspace immediately; this allows the target process to
       * immediately start processing this transaction, reducing
       * latency. We will then return the TRANSACTION_COMPLETE when
       * the target replies (or there is an error).
       */

      binder_enqueue_deferred_thread_work_ilocked(thread, tcomplete);
      t->need_reply             = 1;
      t->from_parent            = thread->transaction_stack;
      thread->transaction_stack = t;
      nxmutex_unlock(&proc->proc_lock);
      return_error = binder_proc_transaction(t, target_proc, target_thread);
      if (return_error)
        {
          nxmutex_lock(&proc->proc_lock);
          binder_pop_transaction_ilocked(thread, t);
          nxmutex_unlock(&proc->proc_lock);
          goto err_dead_proc_or_thread;
        }
    }
  else
    {
      BUG_ON(target_node == NULL);
      BUG_ON(t->buffer->async_transaction != 1);
      binder_enqueue_thread_work(thread, tcomplete);
      return_error = binder_proc_transaction(t, target_proc, NULL);
      if (return_error)
        {
          goto err_dead_proc_or_thread;
        }
    }

  if (target_thread)
    {
      binder_thread_dec_tmpref(target_thread);
    }

  if (target_node)
    {
      binder_dec_node_tmpref(target_node);
    }

  return;

err_dead_proc_or_thread:
  binder_dequeue_work(proc, tcomplete);
err_translate_failed:
err_bad_object_type:
err_bad_offset:
err_copy_data_failed:
  binder_transaction_buffer_release(target_proc, NULL, t->buffer,
                                    buffer_offset, true);
  if (target_node)
    {
      binder_dec_node_tmpref(target_node);
    }

  target_node               = NULL;
  t->buffer->transaction    = NULL;
  binder_alloc_free_buf(&target_proc->alloc, t->buffer);
err_binder_alloc_buf_failed:
  kmm_free(tcomplete);
err_alloc_tcomplete_failed:
  kmm_free(t);
err_alloc_t_failed:
err_bad_todo_list:
err_bad_call_stack:
err_empty_call_stack:
err_dead_binder:
err_invalid_target_handle:
  if (target_thread)
    {
      binder_thread_dec_tmpref(target_thread);
    }

  if (target_proc)
    {
      binder_proc_dec_tmpref(target_proc);
    }

  if (target_node)
    {
      binder_dec_node(target_node, 1, 0);
      binder_dec_node_tmpref(target_node);
    }

  if (return_error != BR_FROZEN_REPLY)
    {
      binder_debug(BINDER_DEBUG_WARNING,
                   "[%s][%d:%d]:" "transaction failed %d, "
                   "size %"PRId64"-%"PRId64"\n",
                   LOG_TAG, getpid(), gettid(),
                   return_error, tr->data_size, tr->offsets_size);
    }

  BUG_ON(thread->return_error.cmd != BR_OK);
  if (in_reply_to)
    {
      binder_set_priority(thread, &in_reply_to->saved_priority);
      thread->return_error.cmd = BR_TRANSACTION_COMPLETE;
      binder_enqueue_thread_work(thread, &thread->return_error.work);
      binder_send_failed_reply(in_reply_to, return_error);
    }
  else
    {
      thread->return_error.cmd = return_error;
      binder_enqueue_thread_work(thread, &thread->return_error.work);
    }
}
