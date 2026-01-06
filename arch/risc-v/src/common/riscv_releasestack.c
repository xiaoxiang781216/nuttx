/****************************************************************************
 * arch/risc-v/src/common/riscv_releasestack.c
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
#include <debug.h>

#include <nuttx/arch.h>
#include <nuttx/kmalloc.h>
#include <nuttx/atomic.h>

#include "riscv_internal.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: release_shadow_stack
 *
 * Description:
 *   Release the shadow stack allocated for a task and clear shadow
 *   stack-related information in the TCB.
 *
 *   The following TCB fields are cleared by this function:
 *
 *   - sstack_alloc_ptr: Pointer to allocated shadow stack
 *   - sstack_top_ptr: Adjusted shadow stack top pointer
 *
 * Input Parameters:
 *   - dtcb: The TCB containing information about the shadow stack to be
 *       released
 *
 ****************************************************************************/

#ifdef CONFIG_ARCH_RV_SHADOW_STACK
static void release_shadow_stack(struct tcb_s *dtcb)
{
  /* Is there a shadow stack allocated? */

  if (dtcb->xcp.sstack_alloc_ptr)
    {
      /* Free the shadow stack */

      kmm_free(dtcb->xcp.sstack_alloc_ptr);

      /* Clear the shadow stack pointers */

      dtcb->xcp.sstack_alloc_ptr = NULL;
      dtcb->xcp.sstack_top_ptr = NULL;
    }
}
#endif /* CONFIG_ARCH_RV_SHADOW_STACK */

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_release_stack
 *
 * Description:
 *   A task has been stopped. Free all stack related resources retained in
 *   the defunct TCB.
 *
 * Input Parameters:
 *   - dtcb:  The TCB containing information about the stack to be released
 *   - ttype:  The thread type.  This may be one of following (defined in
 *     include/nuttx/sched.h):
 *
 *       TCB_FLAG_TTYPE_TASK     Normal user task
 *       TCB_FLAG_TTYPE_PTHREAD  User pthread
 *       TCB_FLAG_TTYPE_KERNEL   Kernel thread
 *
 *     This thread type is normally available in the flags field of the TCB,
 *     however, there are certain error recovery contexts where the TCB may
 *     not be fully initialized when up_release_stack is called.
 *
 *     If CONFIG_BUILD_KERNEL is defined, then this thread type may affect
 *     how the stack is freed.  For example, kernel thread stacks may have
 *     been allocated from protected kernel memory.  Stacks for user tasks
 *     and threads must have come from memory that is accessible to user
 *     code.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void up_release_stack(struct tcb_s *dtcb, int ttype)
{
  /* Is there a stack allocated? */

  if (dtcb->stack_alloc_ptr &&
      (atomic_read(&dtcb->flags) & TCB_FLAG_FREE_STACK))
    {
      group_delayfree(dtcb->group, dtcb->stack_alloc_ptr);
    }

  /* Mark the stack freed */

  atomic_and(&dtcb->flags, ~TCB_FLAG_FREE_STACK);
  dtcb->stack_alloc_ptr = NULL;
  dtcb->stack_base_ptr = NULL;
  dtcb->adj_stack_size = 0;

  /* Release vector register context */

#if defined(CONFIG_ARCH_RV_ISA_V) && (CONFIG_ARCH_RV_VECTOR_BYTE_LENGTH == 0)
  if (dtcb->xcp.vregs != NULL)
    {
      kmm_free(dtcb->xcp.vregs);
    }
#endif

  /* Release shadow stack */

#ifdef CONFIG_ARCH_RV_SHADOW_STACK
  release_shadow_stack(dtcb);
#endif /* CONFIG_ARCH_RV_SHADOW_STACK */
}
