/****************************************************************************
 * sched/group/group.h
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

#ifndef __SCHED_GROUP_GROUP_H
#define __SCHED_GROUP_GROUP_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>
#include <stdbool.h>
#include <sched.h>
#include <errno.h>

#include <nuttx/kmalloc.h>
#include <nuttx/mm/mm.h>
#include <nuttx/sched.h>
#include <nuttx/spinlock.h>

/****************************************************************************
 * Public Type Definitions
 ****************************************************************************/

typedef int (*foreachchild_t)(pid_t pid, FAR void *arg);

/****************************************************************************
 * Public Data
 ****************************************************************************/

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef CONFIG_SCHED_CHILD_STATUS
void task_initialize(void);
#else
#  define task_initialize()
#endif

/* Task group data structure management */

#ifdef CONFIG_MM_TASK_HEAP

#if defined(CONFIG_BUILD_PROTECTED) && defined(__KERNEL__)
#  define GROUP_HEAP USERSPACE_HEAP
#else
#  define GROUP_HEAP g_mmheap
#endif

static inline_function int
group_heap_initialize(FAR struct mm_heap_s **heap,
                      size_t alignment, size_t heapsize)
{
  if (heapsize == 0)
    {
      heapsize = CONFIG_MM_TASK_HEAP_DEFAULT_SIZE;
    }

  if (alignment == 0)
    {
      alignment = CONFIG_MM_TASK_HEAP_DEFAULT_ALIGN;
    }

  *heap = mm_memalign(GROUP_HEAP, alignment, heapsize);
  if (*heap != NULL)
    {
      mm_initialize("Task heap", *heap, heapsize);
    }
  else
    {
      return -ENOMEM;
    }

  return OK;
}

static inline_function int group_heap_size(FAR struct mm_heap_s *heap)
{
  return mm_malloc_size(GROUP_HEAP, heap);
}

static inline_function
void group_heap_uninitialize(FAR struct mm_heap_s *heap)
{
  mm_uninitialize(heap);
  if (mm_heapmember(GROUP_HEAP, heap))
    {
      mm_free(GROUP_HEAP, heap);
    }
}

#else
#  define group_heap_initialize(heap, alignment, heapsize) (0)
#  define group_heap_size(heap) (0)
#  define group_heap_uninitialize(heap)
#endif

int group_allocate(FAR struct tcb_s *tcb, int ttype, size_t heapsize);

void group_initialize(FAR struct tcb_s *tcb);
#ifndef CONFIG_DISABLE_PTHREAD
static inline_function void group_bind(FAR struct tcb_s *tcb)
{
  FAR struct tcb_s *ptcb = this_task();

  DEBUGASSERT(ptcb && tcb && ptcb->group && !tcb->group);

  /* Copy the group reference from the parent to the child */

  tcb->group = ptcb->group;
}

static inline_function void group_join(FAR struct tcb_s *tcb)
{
  FAR struct task_group_s *group;
  irqstate_t flags;

  DEBUGASSERT(tcb && tcb->group);

  /* Get the group from the TCB */

  group = tcb->group;

  /* Add the member to the group */

  flags = spin_lock_irqsave(&group->tg_lock);
  sq_addfirst(&tcb->member, &group->tg_members);
  spin_unlock_irqrestore(&group->tg_lock, flags);
}
#endif
void group_leave(FAR struct tcb_s *tcb);
void group_drop(FAR struct task_group_s *group);

#ifdef HAVE_GROUP_MEMBERS
int group_foreachchild(FAR struct task_group_s *group,
                       foreachchild_t handler, FAR void *arg);
int group_kill_children(FAR struct tcb_s *tcb);
#ifdef CONFIG_SIG_SIGSTOP_ACTION
int group_suspend_children(FAR struct tcb_s *tcb);
int group_continue(FAR struct tcb_s *tcb);
#endif
#endif

/* Signaling group members */

int group_signal(FAR struct task_group_s *group, FAR siginfo_t *siginfo);

/* Parent/child data management */

#ifdef CONFIG_SCHED_HAVE_PARENT
int task_reparent(pid_t ppid, pid_t chpid);

#ifdef CONFIG_SCHED_CHILD_STATUS
FAR struct child_status_s *group_alloc_child(void);
void group_free_child(FAR struct child_status_s *status);
void group_add_child(FAR struct task_group_s *group,
                     FAR struct child_status_s *child);
FAR struct child_status_s *group_exit_child(FAR struct task_group_s *group);
FAR struct child_status_s *group_find_child(FAR struct task_group_s *group,
                                            pid_t pid);
FAR struct child_status_s *group_remove_child(FAR struct task_group_s *group,
                                              pid_t pid);
void group_remove_children(FAR struct task_group_s *group);

#endif /* CONFIG_SCHED_CHILD_STATUS */
#endif /* CONFIG_SCHED_HAVE_PARENT */

/* Group data resource configuration */

int  group_setupidlefiles(void);
int  group_setuptaskfiles(FAR struct tcb_s *tcb,
                          FAR const posix_spawn_file_actions_t *actions,
                          bool cloexec);

#endif /* __SCHED_GROUP_GROUP_H */
