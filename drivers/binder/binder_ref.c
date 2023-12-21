/****************************************************************************
 * drivers/binder/binder_ref.c
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

#define LOG_TAG  "BinderRef"

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
 * Private Data
 ****************************************************************************/

/****************************************************************************
 * Private Functions
 ****************************************************************************/

struct binder_ref *binder_get_ref_olocked(
  FAR struct binder_proc *proc, uint32_t desc, bool need_strong_ref)
{
  FAR struct binder_ref *ref = NULL;

  list_for_every_entry(&proc->refs_by_desc, ref, struct binder_ref,
                       rb_node_desc)
    {
      if (desc == ref->data.desc)
        {
          if (need_strong_ref && !ref->data.strong)
            {
              binder_debug(BINDER_DEBUG_ERROR,
                           "tried to use weak ref as strong ref\n");
              return NULL;
            }
          else
            {
              return ref;
            }
        }
    }

  return NULL;
}

/****************************************************************************
 * Name: binder_get_ref_for_node_olocked
 *
 * Description:
 *   get the ref associated with given node.
 *   Look up the ref for the given node and return it if it exists.
 *   If it doesn't exist and the caller provides a newly allocated
 *   ref, initialize the fields of the newly allocated ref and insert
 *   into the given proc rb_trees and node refs list.
 *
 * Input Parameters:
 *   proc    - binder_proc that owns the ref
 *   node    - binder_node of target
 *   new_ref - newly allocated binder_ref to be initialized or %NULL
 *
 * Returned Value:
 *   the ref for node. It is possible that another thread
 *   allocated/initialized the ref first in which case the
 *   returned ref would be different than the passed-in
 *   new_ref. new_ref must be kfree'd by the caller in
 *   this case.
 *
 ****************************************************************************/

FAR struct binder_ref *
binder_get_ref_for_node_olocked(FAR struct binder_proc *proc,
                                FAR struct binder_node *node,
                                FAR struct binder_ref *new_ref)
{
  bool                       insert_ref = false;
  FAR struct binder_context *context    = proc->context;
  FAR struct binder_ref     *ref        = NULL;
  FAR struct binder_ref     *n          = NULL;

  list_for_every_entry(&proc->refs_by_node, ref, struct binder_ref,
                       rb_node_node)
    {
      if (ref->node == node)
        {
          return ref;
        }
    }

  if (!new_ref)
    {
      return NULL;
    }

  new_ref->data.debug_id    = binder_last_debug_id++;
  new_ref->proc             = proc;
  new_ref->node             = node;
  list_add_head(&proc->refs_by_node, &new_ref->rb_node_node);

  new_ref->data.desc = (node == context->mgr_node) ? 0 : 1;
  list_for_every_entry(&proc->refs_by_desc, n, struct binder_ref,
                       rb_node_desc)
    {
      if (n->data.desc > new_ref->data.desc)
        {
          list_add_before(&n->rb_node_desc, &new_ref->rb_node_desc);
          insert_ref = true;
          break;
        }

      new_ref->data.desc = n->data.desc + 1;
    }

  if (!insert_ref)
    {
      list_add_tail(&proc->refs_by_desc, &new_ref->rb_node_desc);
    }

  nxmutex_lock(&node->node_lock);
  list_add_head(&node->refs, &new_ref->node_entry);

  binder_debug(BINDER_DEBUG_INTERNAL_REFS,
               "%d new ref %d desc %d for node %d\n", proc->pid,
               new_ref->data.debug_id, (int)new_ref->data.desc,
               node->debug_id);
  nxmutex_unlock(&node->node_lock);
  return new_ref;
}

void binder_cleanup_ref_olocked(FAR struct binder_ref *ref)
{
  bool                       delete_node = false;
  FAR struct binder_node    *node;

  binder_debug(BINDER_DEBUG_INTERNAL_REFS,
               "%d delete ref %d desc %d for node %d\n", ref->proc->pid,
               ref->data.debug_id, (int)ref->data.desc, ref->node->debug_id);

  list_delete_init(&ref->rb_node_desc);
  list_delete_init(&ref->rb_node_node);

  node = ref->node;
  nxmutex_lock(&node->node_lock);
  if (ref->data.strong)
    {
      binder_dec_node_nilocked(ref->node, 1, 1);
    }

  list_delete_init(&ref->node_entry);
  delete_node = binder_dec_node_nilocked(ref->node, 0, 1);
  nxmutex_unlock(&node->node_lock);

  /* Clear ref->node unless we want the caller to free the node */

  if (!delete_node)
    {
      /* The caller uses ref->node to determine
       * whether the node needs to be freed. Clear
       * it since the node is still alive.
       */

      ref->node = NULL;
    }

  if (ref->death)
    {
      binder_debug(BINDER_DEBUG_DEAD_BINDER,
                   "%d delete ref %d desc %d has death notification\n",
                   ref->proc->pid, ref->data.debug_id, (int)ref->data.desc);
      binder_dequeue_work(ref->proc, &ref->death->work);
    }
}

/****************************************************************************
 * Name: binder_inc_ref_olocked
 *
 * Description:
 *   increment the ref for given handle.
 *   Increment the ref. ref->proc->outer_lock must be held on entry
 *
 * Input Parameters:
 *   ref         - ref to be incremented
 *   strong      - if true, strong increment, else weak
 *   target_list - list to queue node work on
 *
 * Returned Value:
 *   0, if successful, else errno
 *
 ****************************************************************************/

int binder_inc_ref_olocked(FAR struct binder_ref *ref, int strong,
                           FAR struct list_node *target_list)
{
  int ret;

  if (strong)
    {
      if (ref->data.strong == 0)
        {
          ret = binder_inc_node(ref->node, 1, 1, target_list);
          if (ret)
            {
              return ret;
            }
        }

      ref->data.strong++;
    }
  else
    {
      if (ref->data.weak == 0)
        {
          ret = binder_inc_node(ref->node, 0, 1, target_list);
          if (ret)
            {
              return ret;
            }
        }

      ref->data.weak++;
    }

  return 0;
}

/****************************************************************************
 * Name: binder_dec_ref
 *
 * Description:
 *   dec the ref for given handle.
 *   Decrement the ref.
 *
 * Input Parameters:
 *   ref    - ref to be decremented
 *   strong - if true, strong decrement, else weak
 *
 * Returned Value:
 *   true if ref is cleaned up and ready to be freed
 *
 ****************************************************************************/

static bool binder_dec_ref_olocked(FAR struct binder_ref *ref, int strong)
{
  if (strong)
    {
      if (ref->data.strong == 0)
        {
          binder_debug(BINDER_DEBUG_ERROR,
                       "%d invalid dec strong, "
                       "ref %d desc %d s %d w %d\n",
                       ref->proc->pid, ref->data.debug_id,
                       (int)ref->data.desc, ref->data.strong,
                       ref->data.weak);
          return false;
        }

      ref->data.strong--;
      if (ref->data.strong == 0)
        {
          binder_dec_node(ref->node, strong, 1);
        }
    }
  else
    {
      if (ref->data.weak == 0)
        {
          binder_debug(BINDER_DEBUG_ERROR,
                       "%d invalid dec weak, "
                       "ref %d desc %d s %d w %d\n",
                       ref->proc->pid, ref->data.debug_id,
                       (int)ref->data.desc, ref->data.strong,
                       ref->data.weak);
          return false;
        }

      ref->data.weak--;
    }

  if (ref->data.strong == 0 && ref->data.weak == 0)
    {
      binder_cleanup_ref_olocked(ref);
      return true;
    }

  return false;
}

/****************************************************************************
 * Name: binder_free_ref
 *
 * Description:
 *   free the binder_ref. Free the binder_node indicated by ref->node
 *   (if non-NULL) and the binder_ref_death indicated by ref->death.
 *
 * Input Parameters:
 *   ref:  ref to free
 *
 ****************************************************************************/

void binder_free_ref(FAR struct binder_ref *ref)
{
  if (ref->node)
    {
      binder_free_node(ref->node);
    }

  kmm_free(ref->death);
  kmm_free(ref);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: binder_inc_ref_for_node
 *
 * Description:
 *   increment the ref for given proc/node.
 *   Given a proc and node, increment the ref. Create the ref if it
 *   doesn't already exist
 *
 * Input Parameters:
 *   proc        - proc containing the ref
 *   node        - target node
 *   strong      - true=strong reference, false=weak reference
 *   target_list - worklist to use if node is incremented
 *   rdata       - the id/refcount data for the ref
 *
 * Returned Value:
 *   0 if successful, else errno
 *
 ****************************************************************************/

int binder_inc_ref_for_node(FAR struct binder_proc *proc,
                            FAR struct binder_node *node, bool strong,
                            FAR struct list_node *target_list,
                            FAR struct binder_ref_data *rdata)
{
  FAR struct binder_ref *ref;
  FAR struct binder_ref *new_ref     = NULL;
  int                    ret         = 0;

  nxmutex_lock(&proc->proc_lock);
  ref = binder_get_ref_for_node_olocked(proc, node, NULL);
  if (!ref)
    {
      nxmutex_unlock(&proc->proc_lock);
      new_ref = kmm_zalloc(sizeof(struct binder_ref));
      if (!new_ref)
        {
          return -ENOMEM;
        }

      nxmutex_lock(&proc->proc_lock);
      list_initialize(&new_ref->rb_node_desc);
      list_initialize(&new_ref->rb_node_node);
      list_initialize(&new_ref->node_entry);
      ref = binder_get_ref_for_node_olocked(proc, node, new_ref);
    }

  ret       = binder_inc_ref_olocked(ref, strong, target_list);
  *rdata    = ref->data;
  nxmutex_unlock(&proc->proc_lock);
  if (new_ref && ref != new_ref)
    {
      /* Another thread created the ref first so
       * free the one we allocated
       */

      kmm_free(new_ref);
    }

  return ret;
}

/****************************************************************************
 * Name: binder_update_ref_for_handle
 *
 * Description:
 *   inc/dec the ref for given handle
 *   Given a proc and ref handle, increment or decrement the ref
 *   according to "increment" arg.
 *
 * Input Parameters:
 *   proc      - proc containing the ref
 *   desc      - the handle associated with the ref
 *   increment - true=inc reference, false=dec reference
 *   strong    - true=strong reference, false=weak reference
 *   rdata     - the id/refcount data for the ref
 *
 * Returned Value:
 *   0 if successful, else errno
 *
 ****************************************************************************/

int binder_update_ref_for_handle(FAR struct binder_proc *proc, uint32_t desc,
                                 bool increment, bool strong,
                                 FAR struct binder_ref_data *rdata)
{
  int                    ret = 0;
  FAR struct binder_ref *ref;
  bool                   delete_ref = false;

  nxmutex_lock(&proc->proc_lock);
  ref = binder_get_ref_olocked(proc, desc, strong);
  if (!ref)
    {
      ret = -EINVAL;
      goto err_no_ref;
    }

  if (increment)
    {
      ret = binder_inc_ref_olocked(ref, strong, NULL);
    }
  else
    {
      delete_ref = binder_dec_ref_olocked(ref, strong);
    }

  if (rdata)
    {
      *rdata = ref->data;
    }

  nxmutex_unlock(&proc->proc_lock);

  if (delete_ref)
    {
      binder_free_ref(ref);
    }

  return ret;

err_no_ref:
  nxmutex_unlock(&proc->proc_lock);
  return ret;
}

/****************************************************************************
 * Name: binder_dec_ref_for_handle
 *
 * Description:
 *   dec the ref for given handle
 *   Just calls binder_update_ref_for_handle() to decrement the ref.
 *
 * Input Parameters:
 *   proc   - proc containing the ref
 *   desc   - the handle associated with the ref
 *   strong - true=strong reference, false=weak reference
 *   rdata  - the id/refcount data for the ref
 *
 * Returned Value:
 *   0 if successful, else errno
 *
 ****************************************************************************/

int binder_dec_ref_for_handle(FAR struct binder_proc *proc, uint32_t desc,
                              bool strong, FAR struct binder_ref_data *rdata)
{
  return binder_update_ref_for_handle(proc, desc, false, strong, rdata);
}
