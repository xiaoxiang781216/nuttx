/****************************************************************************
 * sched/task/task_argvstr.c
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

#include <sched.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <nuttx/addrenv.h>
#include <nuttx/tls.h>

#include "sched/sched.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Upper bound for the number of argv[] entries we will walk.  Matches the
 * MAX_STACK_ARGS limit enforced in nxtask_setup_stackargs(); any TCB with a
 * valid argv vector will have fewer entries than this.  Bounding the walk
 * keeps a corrupted (non-NUL-terminated) argv[] array from looping through
 * garbage during a panic dump.
 */

#define NXTASK_ARGVSTR_MAX_ARGS 64

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: nxtask_argvstr
 *
 * Description:
 *   Safely read the contents of a task's argument vector, into a a safe
 *   buffer. Function skips the process's name.
 *
 * Input Parameters:
 *   tcb  - tcb of the task.
 *   args - Output buffer for the argument vector.
 *   size - Size of the buffer.
 *
 * Returned Value:
 *   The actual string length that was written.
 *
 ****************************************************************************/

size_t nxtask_argvstr(FAR struct tcb_s *tcb, FAR char *args, size_t size)
{
  size_t n = 0u;
#ifdef CONFIG_ARCH_ADDRENV
  FAR struct addrenv_s *oldenv = NULL;
#endif

  /* Sanity checks and idle tasks */

  if (!tcb || size < 1u || is_idle_task(tcb) ||
      tcb->task_state == TSTATE_TASK_INVALID)
    {
      *args = '\0';
    }
  else
    {
#ifdef CONFIG_ARCH_ADDRENV
      if (tcb->group->tg_addrenv_own != NULL)
        {
          addrenv_select(tcb->group->tg_addrenv_own, &oldenv);
        }
#endif

#ifndef CONFIG_DISABLE_PTHREAD
      if ((atomic_read(&tcb->flags) & TCB_FLAG_TTYPE_MASK) ==
          TCB_FLAG_TTYPE_PTHREAD)
        {
          FAR struct pthread_entry_s *entry =
            (FAR struct pthread_entry_s *)(tcb + 1);

          int ret = snprintf(args, size, " %p %p",
                             tcb->entry.main, entry->arg);
          if (ret > 0)
            {
              n += (size_t)ret;
            }
        }
      else
#endif
        {
          FAR char **argv = nxsched_get_stackargs(tcb);
          uintptr_t stack_lo = (uintptr_t)tcb->stack_base_ptr;
          uintptr_t stack_hi = stack_lo + (uintptr_t)tcb->adj_stack_size;

          /* nxtask_setup_stackargs() places the argv[] array and all of
           * its strings on the task's own stack, so every valid pointer we
           * are about to dereference here must fall within
           * [stack_base_ptr, stack_base_ptr + adj_stack_size).  This
           * invariant lets us guard the panic-dump path: when a task's
           * stack has overflowed or its TLS is corrupted, tl_argv (and the
           * array it points at) can contain arbitrary garbage.
           * Dereferencing that garbage through snprintf("%s", ...) would
           * data-abort inside strnlen, triggering a recursive assert that
           * resets the board before the original crash info is flushed.
           * Reject out-of-range pointers instead so the original assert is
           * allowed to complete its dump.
           */

          if (argv != NULL && stack_hi > stack_lo &&
              (uintptr_t)argv >= stack_lo &&
              (uintptr_t)(argv + 1) <= stack_hi)
            {
              int count;

              argv++; /* Skip argv[0], the task name. */

              for (count = 0; count < NXTASK_ARGVSTR_MAX_ARGS; count++)
                {
                  FAR const char *arg;
                  int ret;

                  /* The array cell itself must still lie within the
                   * task's stack.
                   */

                  if ((uintptr_t)argv < stack_lo ||
                      (uintptr_t)(argv + 1) > stack_hi)
                    {
                      break;
                    }

                  arg = *argv++;
                  if (arg == NULL || n >= size)
                    {
                      break;
                    }

                  /* The string arg itself must lie within the task's
                   * stack (that is where nxtask_setup_stackargs()
                   * copied it).  Anything else indicates corruption.
                   * Also cap the read length to the remaining stack
                   * range so snprintf never scans past stack_hi
                   * looking for a NUL terminator.
                   */

                  if ((uintptr_t)arg < stack_lo ||
                      (uintptr_t)arg >= stack_hi)
                    {
                      break;
                    }

                  ret = snprintf(args + n, size - n, " %.*s",
                                 (int)(stack_hi - (uintptr_t)arg),
                                 arg);
                  if (ret < 0)
                    {
                      break;
                    }

                  n += (size_t)ret;
                }
            }
        }

#ifdef CONFIG_ARCH_ADDRENV
      if (tcb->group->tg_addrenv_own != NULL)
        {
          addrenv_restore(oldenv);
        }
#endif

      if (n >= size - 1u)
        {
          n = size - 1u;
        }
    }

  return n;
}
