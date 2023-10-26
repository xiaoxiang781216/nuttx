/****************************************************************************
 * drivers/binder/binder.c
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

#define LOG_TAG  "Binder"

#include <nuttx/config.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/param.h>
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

#ifdef CONFIG_DRIVERS_BINDER_DEBUG

uint32_t binder_debug_mask = BINDER_DEBUG_WARNING |
                             BINDER_DEBUG_ERROR;

char binder_debug_log[BINDER_LOG_BUFSIZE];

void binder_syslog(FAR char *buff, int pos, FAR const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  vsnprintf(buff + pos, BINDER_LOG_BUFSIZE - pos -1, fmt, ap);
  va_end(ap);

  syslog(LOG_INFO, "%s", buff);
}

#endif /* CONFIG_BINDER_DRIVER_DEBUG */

unsigned int binder_last_debug_id = 1;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int binder_set_ctx_mgr(FAR struct binder_proc * proc,
                              FAR struct flat_binder_object *fbo)
{
  int                       ret = 0;

  FAR struct binder_context *context = proc->context;
  FAR struct binder_node    *new_node;

  nxmutex_lock(&context->context_lock);
  if (context->mgr_node)
    {
      binder_debug(BINDER_DEBUG_ERROR,
                   "BINDER_SET_CONTEXT_MGR already set\n");
      ret = -EBUSY;
      goto out;
    }

  new_node = binder_new_node(proc, fbo);
  if (!new_node)
    {
      ret = -ENOMEM;
      goto out;
    }

  nxmutex_lock(&new_node->node_lock);
  new_node->local_weak_refs++;
  new_node->local_strong_refs++;
  new_node->has_strong_ref  = 1;
  new_node->has_weak_ref    = 1;
  context->mgr_node         = new_node;
  nxmutex_unlock(&new_node->node_lock);
  binder_put_node(new_node);
out:
  nxmutex_unlock(&context->context_lock);
  return ret;
}

static int binder_write_read(FAR struct binder_proc *proc,
                             FAR struct binder_thread *thread,
                             unsigned long arg, int oflag)
{
  int                           ret = 0;
  FAR struct binder_write_read  * bwr;
  bwr = (struct binder_write_read *)arg;

  binder_debug(BINDER_DEBUG_READ_WRITE,
               "write %d at %"PRIx64" read %d at %"PRIx64"\n",
               (int)bwr->write_size, bwr->write_buffer,
               (int)bwr->read_size, bwr->read_buffer);

  if (bwr->write_size > 0)
    {
      ret = binder_thread_write(proc, thread, bwr->write_buffer,
                                bwr->write_size, &bwr->write_consumed);
      if (ret < 0)
        {
          bwr->read_consumed = 0;
          goto out;
        }
    }

  if (bwr->read_size > 0)
    {
      ret = binder_thread_read(proc, thread, bwr->read_buffer,
                               bwr->read_size, &bwr->read_consumed,
                               oflag & O_NONBLOCK);

      nxmutex_lock(&proc->proc_lock);
      if (!list_is_empty(&proc->todo_list))
        {
          binder_wakeup_proc_ilocked(proc);
        }

      nxmutex_unlock(&proc->proc_lock);

      if (ret < 0)
        {
          if (ret == -EAGAIN && bwr->write_consumed != 0)
            ret = 0;
          goto out;
        }
    }

  binder_debug(BINDER_DEBUG_READ_WRITE,
               "binder_rw wrote %d of %d, read return %d of %d\n",
               (int)bwr->write_consumed, (int)bwr->write_size,
               (int)bwr->read_consumed,  (int)bwr->read_size);

out:
  return ret;
}

static int binder_get_node_info_for_ref(
  FAR struct binder_proc *proc,
  FAR struct binder_node_info_for_ref *info)
{
  FAR struct binder_node    *node;
  FAR struct binder_context *context    = proc->context;
  uint32_t                  handle      = info->handle;

  if (info->strong_count || info->weak_count || info->reserved1 ||
      info->reserved2 || info->reserved3)
    {
      binder_debug(BINDER_DEBUG_ERROR,
                   "%d BINDER_GET_NODE_INFO_FOR_REF: "
                   "only handle may be non-zero.",
                   proc->pid);
      return -EINVAL;
    }

  /* This ioctl may only be used by the context manager */

  nxmutex_lock(&context->context_lock);
  if (!context->mgr_node || context->mgr_node->proc != proc)
    {
      nxmutex_unlock(&context->context_lock);
      return -EPERM;
    }

  nxmutex_unlock(&context->context_lock);

  node = binder_get_node_from_ref(proc, handle, true, NULL);
  if (!node)
    {
      return -EINVAL;
    }

  info->strong_count = node->local_strong_refs +
                       node->internal_strong_refs;
  info->weak_count = node->local_weak_refs;

  binder_put_node(node);

  return 0;
}

static int binder_flush(FAR struct file *filp)
{
  FAR struct binder_proc *proc = filp->f_priv;
  FAR struct binder_thread  *thread;
  FAR struct binder_thread  *thread_itr;
  int wake_count = 0;

  nxmutex_lock(&proc->proc_lock);

  list_for_every_entry_safe(&proc->threads, thread, thread_itr,
                            struct binder_thread, thread_node)
  {
    thread->looper_need_return = true;
    if (thread->looper & BINDER_LOOPER_STATE_WAITING)
    {
      wait_wake_up(&thread->wait, 0);
      wake_count++;
    }
  }

  nxmutex_unlock(&proc->proc_lock);

  binder_debug(BINDER_DEBUG_OPEN_CLOSE,
         "binder_flush: %d woke up %d threads\n", proc->pid,
         wake_count);

  return 0;
}

static int binder_ioctl(FAR struct file *filp, int cmd, unsigned long arg)
{
  int                       ret;

  FAR struct binder_proc    *proc = filp->f_priv;
  FAR struct binder_thread  *thread;

  thread = binder_get_thread(proc);
  if (thread == NULL)
    {
      return -ENOMEM;
    }

  if (arg == 0 && (cmd == BINDER_WRITE_READ || cmd == BINDER_SET_MAX_THREADS
      || cmd == BINDER_SET_CONTEXT_MGR || cmd == BINDER_SET_CONTEXT_MGR_EXT
      || cmd == BINDER_VERSION || cmd == BINDER_GET_NODE_INFO_FOR_REF))
    {
      return -EFAULT;
    }

  binder_debug(BINDER_DEBUG_READ_WRITE, "%d:%d %d\n",
               proc->pid, thread->tid, _IOC_NR(cmd));

  switch (cmd)
  {
    case BINDER_WRITE_READ:
    {
      unsigned int size = BINDER_IOC_SIZE(cmd);
      if (size != sizeof(struct binder_write_read))
        {
          ret = -EINVAL;
          break;
        }

      ret = binder_write_read(proc, thread, arg, filp->f_oflags);
      break;
    }

    case BINDER_SET_MAX_THREADS:
    {
      FAR int *p_int;
      p_int = (int *)arg;
      nxmutex_lock(&proc->proc_lock);
      proc->max_threads = MIN(*p_int, CONFIG_DRIVERS_BINDER_MAX_THREADS);
      ret               = 0;
      nxmutex_unlock(&proc->proc_lock);
      break;
    }

    case BINDER_SET_CONTEXT_MGR_EXT:
    case BINDER_SET_CONTEXT_MGR:
    {
      unsigned int size = BINDER_IOC_SIZE(cmd);
      FAR struct flat_binder_object * fbo = NULL;
      if (cmd == BINDER_SET_CONTEXT_MGR_EXT)
        {
          if (size != sizeof(struct flat_binder_object))
            {
              ret = -EINVAL;
              break;
            }

          fbo = (struct flat_binder_object *)arg;
        }

      ret = binder_set_ctx_mgr(proc, fbo);
      break;
    }

    case BINDER_THREAD_EXIT:
    {
      binder_debug(BINDER_DEBUG_THREADS, "%d:%d exit\n", proc->pid,
                   thread->tid);
      ret       = binder_thread_release(proc, thread);
      thread    = NULL;
      break;
    }

    case BINDER_VERSION:
    {
      FAR struct binder_version *ver;
      ver                   = (FAR struct binder_version *)arg;
      ver->protocol_version = BINDER_CURRENT_PROTOCOL_VERSION;
      ret                   = 0;
      break;
    }

    case BINDER_GET_NODE_INFO_FOR_REF:
    {
      FAR struct binder_node_info_for_ref *pinfo;
      pinfo = (FAR struct binder_node_info_for_ref *)arg;
      ret   = binder_get_node_info_for_ref(proc, pinfo);
      break;
    }

    case BINDER_GET_NODE_DEBUG_INFO:
    case BINDER_FREEZE:
    case BINDER_GET_FROZEN_INFO:
    {
      binder_debug(BINDER_DEBUG_ERROR,
                   "ERROR: Binder Not support cmd %d\n",
                   _IOC_NR(cmd));
      ret = -EINVAL;
      break;
    }

    case BINDER_ENABLE_ONEWAY_SPAM_DETECTION:
    {
      /* Do nothing for this ioctl */

      ret = 0;
      break;
    }

    case BIOC_FLUSH:
    {
      ret = binder_flush(filp);
      break;
    }

    default:
    {
      ret = -ENOTTY;
      break;
    }
   }

  if (thread)
    {
      thread->looper_need_return = false;
    }

  return ret;
}

static int poll_wake_function(FAR void * arg, unsigned mode)
{
  FAR struct wait_queue_entry  *wq_entry;
  int                           ret = 0;
  FAR struct pollfd            *fds;

  wq_entry  = (FAR struct wait_queue_entry *)arg;
  fds = (FAR struct pollfd *)wq_entry->private;
  poll_notify(&fds, 1, POLLIN | POLLOUT);

  binder_debug(BINDER_DEBUG_SCHED, "wq_entry=%p, ret=%d\n", wq_entry, ret);
  return ret;
}

static int binder_munmap(FAR struct task_group_s *group,
                         FAR struct mm_map_entry_s *entry,
                         FAR void *start, size_t length)
{
  FAR struct binder_proc   *proc = entry->priv.p;
  struct binder_mmap_area   vma;

  vma.area_start = start;
  vma.area_size  = length;
  binder_alloc_unmmap(&proc->alloc, &vma);
  return mm_map_remove(get_group_mm(group), entry);
}

static int binder_mmap(FAR struct file *filep,
                       FAR struct mm_map_entry_s *map)
{
  FAR struct binder_proc    *proc = filep->f_priv;
  struct binder_mmap_area   vma;

  vma.area_start = map->vaddr;
  vma.area_size  = MIN(map->length, CONFIG_DRIVERS_BINDER_MAX_VMSIZE);

  binder_alloc_mmap(&proc->alloc, &vma);

  map->munmap = binder_munmap;
  map->priv.p = (void *)proc;
  map->vaddr  = vma.area_start;
  map->length = vma.area_size;

  mm_map_add(get_current_mm(), map);

  return 0;
}

static int binder_poll(FAR struct file *filp,
                       FAR struct pollfd *fds,
                       bool setup)
{
  FAR struct binder_proc    *proc   = filp->f_priv;
  FAR struct binder_thread  *thread = NULL;
  bool                      wait_for_proc_work;

  thread = binder_get_thread(proc);
  if (!thread)
    {
      return -EINVAL;
    }

  if (setup)
    {
      init_waitqueue_entry(&thread->wq_entry, (void *)fds,
                           poll_wake_function);
      prepare_to_wait(&thread->wait, &thread->wq_entry);
      list_add_tail(&proc->waiting_threads,
                    &thread->waiting_thread_node);
      binder_debug(BINDER_DEBUG_SCHED, "%d:%d poll setup\n",
                   proc->pid, thread->tid);

      nxmutex_lock(&thread->proc->proc_lock);
      thread->looper        |= BINDER_LOOPER_STATE_POLL;
      wait_for_proc_work    = binder_available_for_proc_work_ilocked(thread);
      nxmutex_unlock(&thread->proc->proc_lock);

      if (binder_has_work(thread, wait_for_proc_work))
        {
          wait_wake_up(&thread->wait, 0);
        }
    }
  else
    {
      binder_debug(BINDER_DEBUG_SCHED, "%d:%d poll finish\n",
                   proc->pid, thread->tid);
      list_delete_init(&thread->waiting_thread_node);
      finish_wait(&thread->wq_entry);
    }

  return OK;
}

static int binder_open(FAR struct file *filep)
{
  FAR struct binder_proc    *proc;
  FAR struct inode          *inode      = filep->f_inode;
  FAR struct binder_device  *binder_dev = inode->i_private;

  binder_debug(BINDER_DEBUG_OPEN_CLOSE, "pid=%d\n", getpid());

  proc = kmm_zalloc(sizeof(struct binder_proc));
  if (proc == NULL)
    {
      binder_debug(BINDER_DEBUG_ERROR,
                   "ERROR: Failed to alloc binder proc\n");
      return -ENOMEM;
    }

  nxmutex_init(&proc->proc_lock);
  proc->pid = getpid();
  list_initialize(&proc->threads);
  list_initialize(&proc->nodes);
  list_initialize(&proc->freeze_wait);
  list_initialize(&proc->todo_list);
  list_initialize(&proc->delivered_death);
  list_initialize(&proc->waiting_threads);
  list_initialize(&proc->refs_by_desc);
  list_initialize(&proc->refs_by_node);
  list_initialize(&proc->proc_node);

  if (binder_get_priority(proc->pid, &proc->default_priority) < 0)
    {
      binder_debug(BINDER_DEBUG_ERROR,
                   "ERROR: Failed to get binder priority\n");
      kmm_free(proc);
      return -EINVAL;
    }

  proc->context = &binder_dev->context;
  binder_alloc_init(&proc->alloc, proc->pid);

  nxmutex_lock(&binder_dev->binder_procs_lock);
  binder_dev->ref_count++;
  list_add_head(&binder_dev->binder_procs_list, &proc->proc_node);
  filep->f_priv = proc;
  nxmutex_unlock(&binder_dev->binder_procs_lock);
  return 0;
}

static int binder_close(FAR struct file *filep)
{
  FAR struct binder_proc    *proc       = filep->f_priv;
  FAR struct inode          *inode      = filep->f_inode;
  FAR struct binder_device  *binder_dev = inode->i_private;
  FAR struct binder_context *context    = proc->context;
  FAR struct binder_thread  *thread;
  FAR struct binder_node    *node;
  FAR struct binder_ref     *ref;
  FAR struct binder_thread  *thread_itr;
  FAR struct binder_node    *node_itr;
  FAR struct binder_ref     *ref_itr;
  int                       threads;
  int                       nodes;
  int                       incoming_refs;
  int                       outgoing_refs;
  int                       active_transactions;

  if (binder_flush(filep) < 0)
    {
      binder_debug(BINDER_DEBUG_ERROR, "Binder Flush error\n");
    }

  nxmutex_lock(&binder_dev->binder_procs_lock);
  list_delete_init(&proc->proc_node);
  nxmutex_unlock(&binder_dev->binder_procs_lock);

  nxmutex_lock(&context->context_lock);
  if (context->mgr_node && context->mgr_node->proc == proc)
    {
      binder_debug(BINDER_DEBUG_DEAD_BINDER,
                   "%d context_mgr_node gone\n",
                   proc->pid);
      context->mgr_node = NULL;
    }

  nxmutex_unlock(&context->context_lock);

  nxmutex_lock(&proc->proc_lock);

  /* Make sure proc stays alive after we
   * remove all the threads
   */

  proc->tmp_ref++;

  proc->is_dead         = true;
  proc->is_frozen       = false;
  proc->sync_recv       = false;
  proc->async_recv      = false;
  threads               = 0;
  active_transactions   = 0;

  list_for_every_entry_safe(&proc->threads, thread, thread_itr,
                            struct binder_thread, thread_node)
  {
    nxmutex_unlock(&proc->proc_lock);
    threads++;
    active_transactions += binder_thread_release(proc, thread);
    nxmutex_lock(&proc->proc_lock);
  }

  nodes         = 0;
  incoming_refs = 0;

  list_for_every_entry_safe(&proc->nodes, node, node_itr,
                            struct binder_node, rb_node)
  {
    nodes++;

    binder_inc_node_tmpref_ilocked(node);
    list_delete_init(&node->rb_node);
    nxmutex_unlock(&proc->proc_lock);
    incoming_refs = binder_node_release(node, incoming_refs);
    nxmutex_lock(&proc->proc_lock);
  }

  nxmutex_unlock(&proc->proc_lock);

  outgoing_refs = 0;
  nxmutex_lock(&proc->proc_lock);
  list_for_every_entry_safe(&proc->refs_by_desc, ref, ref_itr,
                            struct binder_ref, rb_node_desc)
  {
    outgoing_refs++;
    binder_cleanup_ref_olocked(ref);
    nxmutex_unlock(&proc->proc_lock);
    binder_free_ref(ref);
    nxmutex_lock(&proc->proc_lock);
  }

  nxmutex_unlock(&proc->proc_lock);

  binder_release_work(proc, &proc->todo_list);
  binder_release_work(proc, &proc->delivered_death);

  binder_debug(BINDER_DEBUG_OPEN_CLOSE,
               "%d threads %d, nodes %d (ref %d), refs %d, "
               "active transactions %d\n",
               proc->pid, threads, nodes,
               incoming_refs, outgoing_refs,
               active_transactions);

  binder_proc_dec_tmpref(proc);
  return 0;
}

static ssize_t binder_read(FAR struct file *filep, FAR char *buffer,
                           size_t len)
{
  return 0;
}

static ssize_t binder_write(FAR struct file *filep, FAR const char *buffer,
                            size_t len)
{
#ifdef CONFIG_DRIVERS_BINDER_DEBUG
  if (buffer && (*buffer != '\n'))
    {
      binder_debug_mask = strtoul(buffer, NULL, 10);
    }
#endif

  return len;
}

static const struct file_operations g_binder_fops =
{
  binder_open,      /* open */
  binder_close,     /* close */
  binder_read,      /* read */
  binder_write,     /* write */
  NULL,             /* seek */
  binder_ioctl,     /* ioctl */
  binder_mmap,      /* mmap */
  NULL,             /* truncate */
  binder_poll,      /* poll */
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int binder_initialize(void)
{
  FAR struct binder_device  * device;
  int                       ret = OK;

  device = kmm_zalloc(sizeof(struct binder_device));
  if (!device)
    {
      return -ENOMEM;
    }

  nxmutex_init(&device->context.context_lock);
  nxmutex_init(&device->binder_procs_lock);
  list_initialize(&device->binder_procs_list);

  /* Register the device node. */

  ret = register_driver("/dev/binder", &g_binder_fops, 0666, device);
  if (ret < 0)
    {
      binder_debug(BINDER_DEBUG_ERROR,
                   "ERROR: Failed to register binder interface: %d\n", ret);
      nxmutex_destroy(&device->context.context_lock);
      nxmutex_destroy(&device->binder_procs_lock);
      kmm_free(device);
    }

  return ret;
}
