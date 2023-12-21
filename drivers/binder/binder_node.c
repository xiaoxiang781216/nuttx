/****************************************************************************
 * drivers/binder/binder_node.c
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

#define LOG_TAG  "BinderNode"

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

static struct list_node binder_dead_nodes = LIST_INITIAL_VALUE(
  binder_dead_nodes);
static mutex_t          binder_dead_nodes_lock = NXMUTEX_INITIALIZER;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static struct binder_node *binder_init_node_ilocked(
  FAR struct binder_proc *proc, FAR struct binder_node *node,
  FAR struct flat_binder_object *fp)
{
  binder_uintptr_t  ptr     = fp ? fp->binder : 0;
  binder_uintptr_t  cookie  = fp ? fp->cookie : 0;
  uint32_t          flags   = fp ? fp->flags : 0;
  signed char       priority;

  node->tmp_refs++;
  node->debug_id        = binder_last_debug_id++;
  node->proc            = proc;
  node->ptr             = ptr;
  node->cookie          = cookie;
  node->work.type       = BINDER_WORK_NODE;
  priority              = flags & FLAT_BINDER_FLAG_PRIORITY_MASK;
  node->sched_policy    = (flags & FLAT_BINDER_FLAG_SCHED_POLICY_MASK) >>
                          FLAT_BINDER_FLAG_SCHED_POLICY_SHIFT;

  if (node->sched_policy == 0)
    {
      struct binder_priority proc_priority;
      binder_get_priority(proc->pid, &proc_priority);
      node->sched_policy    = proc_priority.sched_policy;
      node->min_priority    = proc_priority.sched_prio;
    }
  else
    {
      node->min_priority = priority;
    }

  node->accept_fds          = !!(flags & FLAT_BINDER_FLAG_ACCEPTS_FDS);
  node->inherit_rt          = !!(flags & FLAT_BINDER_FLAG_INHERIT_RT);
  node->txn_security_ctx    = !!(flags & FLAT_BINDER_FLAG_TXN_SECURITY_CTX);
  nxmutex_init(&node->node_lock);
  list_initialize(&node->work.entry_node);
  list_initialize(&node->async_todo);
  list_initialize(&node->rb_node);
  list_initialize(&node->refs);
  list_add_head(&proc->nodes, &node->rb_node);

  binder_debug(BINDER_DEBUG_INTERNAL_REFS,
               "%d:%d node %d %"PRIx64" %"PRIx64" created\n",
               proc->pid, gettid(), node->debug_id,
               node->ptr, node->cookie);

  return node;
}

/****************************************************************************
 * Pubilc Functions
 ****************************************************************************/

FAR struct binder_node *binder_get_node(FAR struct binder_proc *proc,
                                        binder_uintptr_t ptr)
{
  FAR struct binder_node    *itr;
  FAR struct binder_node    *node;

  nxmutex_lock(&proc->proc_lock);
  node = NULL;
  list_for_every_entry(&proc->nodes, itr, struct binder_node, rb_node)
    {
      if (ptr == itr->ptr)
        {
          /* take an implicit weak reference to ensure
           * node stays alive until call to binder_put_node()
           */

          node = itr;
          binder_inc_node_tmpref_ilocked(node);
          break;
        }
    }

  nxmutex_unlock(&proc->proc_lock);
  return node;
}

int binder_inc_node_nilocked(FAR struct binder_node *node, int strong,
                             int internal, FAR struct list_node *target_list)
{
  if (strong)
    {
      if (internal)
        {
          if (target_list == NULL && node->internal_strong_refs == 0 &&
              !(node->proc && node == node->proc->context->mgr_node &&
                node->has_strong_ref))
            {
              binder_debug(BINDER_DEBUG_ERROR,
                           "invalid inc strong node for %d\n",
                           node->debug_id);
              return -EINVAL;
            }

          node->internal_strong_refs++;
        }
      else
        {
          node->local_strong_refs++;
        }

      if (!node->has_strong_ref && target_list)
        {
          struct binder_thread *thread = container_of(target_list,
                                                      struct binder_thread,
                                                      todo);
          binder_dequeue_work_ilocked(&node->work);
          BUG_ON(&thread->todo != target_list);
          binder_enqueue_deferred_thread_work_ilocked(thread, &node->work);
        }
    }
  else
    {
      if (!internal)
        {
          node->local_weak_refs++;
        }

      if (!node->has_weak_ref && list_is_empty(&node->work.entry_node))
        {
          if (target_list == NULL)
            {
              binder_debug(BINDER_DEBUG_ERROR,
                           "invalid inc weak node for %d\n", node->debug_id);
              return -EINVAL;
            }

          /* See comment above */

          binder_enqueue_work_ilocked(&node->work, target_list);
        }
    }

  return 0;
}

int binder_inc_node(FAR struct binder_node *node, int strong, int internal,
                    FAR struct list_node *target_list)
{
  int ret;

  nxmutex_lock(&node->node_lock);
  ret = binder_inc_node_nilocked(node, strong, internal, target_list);
  nxmutex_unlock(&node->node_lock);

  return ret;
}

bool binder_dec_node_nilocked(FAR struct binder_node *node, int strong,
                              int internal)
{
  FAR struct binder_proc *proc = node->proc;

  if (strong)
    {
      if (internal)
        {
          node->internal_strong_refs--;
        }
      else
        {
          node->local_strong_refs--;
        }

      if (node->local_strong_refs || node->internal_strong_refs)
        {
          return false;
        }
    }
  else
    {
      if (!internal)
        {
          node->local_weak_refs--;
        }

      if (node->local_weak_refs || node->tmp_refs ||
          !list_is_empty(&node->refs))
        {
          return false;
        }
    }

  if (proc && (node->has_strong_ref || node->has_weak_ref))
    {
      if (list_is_empty(&node->work.entry_node))
        {
          binder_enqueue_work_ilocked(&node->work, &proc->todo_list);
          binder_wakeup_proc_ilocked(proc);
        }
    }
  else
    {
      if (list_is_empty(&node->refs) && !node->local_strong_refs &&
          !node->local_weak_refs && !node->tmp_refs)
        {
          if (proc)
            {
              binder_dequeue_work_ilocked(&node->work);
              list_delete_init(&node->rb_node);
              binder_debug(BINDER_DEBUG_INTERNAL_REFS,
                           "refless node %d deleted\n", node->debug_id);
            }
          else
            {
              BUG_ON(!list_is_empty(&node->work.entry_node));
              nxmutex_lock(&binder_dead_nodes_lock);

              /* tmp_refs could have changed so
               * check it again
               */

              if (node->tmp_refs)
                {
                  nxmutex_unlock(&binder_dead_nodes_lock);
                  return false;
                }

              list_delete_init(&node->dead_node);
              nxmutex_unlock(&binder_dead_nodes_lock);
              binder_debug(BINDER_DEBUG_INTERNAL_REFS,
                           "dead node %d deleted\n", node->debug_id);
            }

          return true;
        }
    }

  return false;
}

void binder_dec_node(FAR struct binder_node *node, int strong, int internal)
{
  bool free_node;

  nxmutex_lock(&node->node_lock);
  free_node = binder_dec_node_nilocked(node, strong, internal);
  nxmutex_unlock(&node->node_lock);
  if (free_node)
    {
      binder_free_node(node);
    }
}

/****************************************************************************
 * Name: binder_inc_node_tmpref
 *
 * Description:
 *    Take reference on node to prevent the node from being freed while
 *    referenced only by a local variable. The inner lock is needed to
 *    serialize with the node work on the queue (which isn't needed after
 *    the node is dead).
 *
 *    If the node is dead (node->proc is NULL), use
 *    binder_dead_nodes_lock to protect node->tmp_refs against
 *    dead-node-only cases where the node lock cannot be acquired
 *    (eg traversing the dead node list to print nodes)
 *
 ****************************************************************************/

static void binder_inc_node_tmpref(FAR struct binder_node *node)
{
  FAR struct binder_proc *proc;

  proc = node->proc;

  nxmutex_lock(&node->node_lock);
  if (proc != NULL)
    {
      nxmutex_lock(&proc->proc_lock);
    }
  else
    {
      nxmutex_lock(&binder_dead_nodes_lock);
    }

  binder_inc_node_tmpref_ilocked(node);

  if (proc != NULL)
    {
      nxmutex_unlock(&proc->proc_lock);
    }
  else
    {
      nxmutex_unlock(&binder_dead_nodes_lock);
    }

  nxmutex_unlock(&node->node_lock);
}

/****************************************************************************
 * Name: binder_dec_node_tmpref
 *
 * Description:
 *   remove a temporary reference on node to reference. Release temporary
 *   reference on node taken via binder_inc_node_tmpref()
 *
 ****************************************************************************/

void binder_dec_node_tmpref(FAR struct binder_node *node)
{
  bool free_node;

  nxmutex_lock(&node->node_lock);
  node->tmp_refs--;
  BUG_ON(node->tmp_refs < 0);

  /* Call binder_dec_node() to check if all refcounts are 0
   * and cleanup is needed. Calling with strong=0 and internal=1
   * causes no actual reference to be released in binder_dec_node().
   * If that changes, a change is needed here too.
   */

  free_node = binder_dec_node_nilocked(node, 0, 1);
  nxmutex_unlock(&node->node_lock);
  if (free_node)
    {
      binder_free_node(node);
    }
}

void binder_put_node(FAR struct binder_node *node)
{
  binder_dec_node_tmpref(node);
}

FAR struct binder_node *binder_new_node(FAR struct binder_proc *proc,
                                        FAR struct flat_binder_object *fp)
{
  FAR struct binder_node    *itr;
  FAR struct binder_node    *node;
  FAR struct binder_node    *new_node;
  binder_uintptr_t          ptr = fp ? fp->binder : 0;

  nxmutex_lock(&proc->proc_lock);
  node = NULL;
  list_for_every_entry(&proc->nodes, itr, struct binder_node, rb_node)
    {
      if (ptr == itr->ptr)
        {
          /* A matching node is already in the node list of process.
           * The node was already added by another thread.
           * Abandon the init and return it.
           */

          binder_inc_node_tmpref_ilocked(node);
          node = itr;
          break;
        }
    }

  nxmutex_unlock(&proc->proc_lock);

  if (node == NULL)
    {
      new_node = kmm_zalloc(sizeof(struct binder_node));
      if (new_node == NULL)
        {
          goto out;
        }

      nxmutex_lock(&proc->proc_lock);
      node = binder_init_node_ilocked(proc, new_node, fp);
      nxmutex_unlock(&proc->proc_lock);
    }

out:
  return node;
}

/****************************************************************************
 * Name: binder_get_node_from_ref
 *
 * Description:
 *   get the node from the given proc/desc
 *
 * Input Parameters:
 *   proc            - proc containing the ref
 *   desc            - the handle associated with the ref
 *   need_strong_ref - if true, only return node if ref is strong
 *   rdata           - the id/refcount data for the ref
 *
 * Returned Value:
 *   a binder_node or NULL if not found or not strong when strong required
 *
 ****************************************************************************/

FAR struct binder_node *
binder_get_node_from_ref(FAR struct binder_proc *proc,
                         uint32_t desc, bool need_strong_ref,
                         FAR struct binder_ref_data *rdata)
{
  FAR struct binder_node    *node;
  FAR struct binder_ref     *ref;

  nxmutex_lock(&proc->proc_lock);
  ref = binder_get_ref_olocked(proc, desc, need_strong_ref);
  if (!ref)
    {
      goto err_no_ref;
    }

  node = ref->node;

  /* Take an implicit reference on the node to ensure
   * it stays alive until the call to binder_put_node()
   */

  binder_inc_node_tmpref(node);

  if (rdata)
    {
      *rdata = ref->data;
    }

  nxmutex_unlock(&proc->proc_lock);

  return node;

err_no_ref:
  nxmutex_unlock(&proc->proc_lock);
  return NULL;
}

int binder_node_release(FAR struct binder_node *node, int refs)
{
  FAR struct binder_ref     *ref;
  int                        death   = 0;
  FAR struct binder_proc    *proc   = node->proc;

  binder_release_work(proc, &node->async_todo);

  nxmutex_lock(&node->node_lock);
  nxmutex_lock(&proc->proc_lock);
  binder_dequeue_work_ilocked(&node->work);

  /* The caller must have taken a temporary ref on the node */

  BUG_ON(!node->tmp_refs);
  if (list_is_empty(&node->refs) && node->tmp_refs == 1)
    {
      nxmutex_unlock(&proc->proc_lock);
      nxmutex_unlock(&node->node_lock);
      binder_free_node(node);

      return refs;
    }

  node->proc                = NULL;
  node->local_strong_refs   = 0;
  node->local_weak_refs     = 0;

  nxmutex_unlock(&proc->proc_lock);

  nxmutex_lock(&binder_dead_nodes_lock);
  list_add_head(&binder_dead_nodes, &node->dead_node);
  nxmutex_unlock(&binder_dead_nodes_lock);

  list_for_every_entry(&node->refs, ref, struct binder_ref, node_entry)
    {
      refs++;

      /* Need the node lock to synchronize with new
       * notification requests and the inner lock to
       * synchronize with queued death notifications.
       */

      nxmutex_lock(&ref->proc->proc_lock);
      if (!ref->death)
        {
          nxmutex_unlock(&ref->proc->proc_lock);
          continue;
        }

      death++;
      BUG_ON(!list_is_empty(&ref->death->work.entry_node));
      ref->death->work.type = BINDER_WORK_DEAD_BINDER;
      binder_enqueue_work_ilocked(&ref->death->work, &ref->proc->todo_list);
      binder_wakeup_proc_ilocked(ref->proc);
      nxmutex_unlock(&ref->proc->proc_lock);
    }

  binder_debug(BINDER_DEBUG_DEAD_BINDER,
               "node %d now dead, refs %d, death %d\n", node->debug_id, refs,
               death);
  nxmutex_unlock(&node->node_lock);
  binder_put_node(node);

  return refs;
}
