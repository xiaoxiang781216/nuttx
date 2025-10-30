/****************************************************************************
 * arch/risc-v/src/common/riscv_usestack.c
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

#include <sys/types.h>
#include <stdint.h>
#include <sched.h>
#include <assert.h>
#include <debug.h>

#include <nuttx/kmalloc.h>
#include <nuttx/arch.h>
#include <nuttx/tls.h>

#include "riscv_internal.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: create_shadow_stack
 *
 * Description:
 *   Allocate a shadow stack for a new thread and setup shadow stack-related
 *   information in the TCB.
 *
 *   The following TCB fields must be initialized by this function:
 *
 *   - sstack_alloc_ptr: Pointer to allocated shadow stack
 *   - sstack_top_ptr: Adjusted shadow stack top pointer after alignment
 *
 * Input Parameters:
 *   - tcb: The TCB of new task
 *   - stack_size: The requested shadow stack size. At least this much
 *     must be allocated.
 *
 ****************************************************************************/

#ifdef CONFIG_ARCH_RV_SHADOW_STACK
static int create_shadow_stack(struct tcb_s *tcb, size_t stack_size)
{
  /* Allocate shadow stack for all thread types */

  tcb->xcp.sstack_alloc_ptr = kmm_malloc(stack_size);

#ifdef CONFIG_DEBUG_FEATURES
  /* Was the allocation successful? */

  if (!tcb->xcp.sstack_alloc_ptr)
    {
      serr("ERROR: Failed to allocate shadow stack, size %zu\n", stack_size);
    }
#endif /* CONFIG_DEBUG_FEATURES */

  /* Did we successfully allocate a shadow stack? */

  if (tcb->xcp.sstack_alloc_ptr)
    {
      uintreg_t top_of_sstack;

      /* RISC-V uses a push-down stack: the stack grows toward lower
       * addresses in memory. The stack pointer register points to the
       * lowest, valid working address (the "top" of the stack). Items on
       * the stack are referenced as positive word offsets from SP.
       */

      top_of_sstack = (uintreg_t)tcb->xcp.sstack_alloc_ptr + stack_size;

      /* The RISC-V shadow stack must be aligned at 16-byte boundaries.
       * If necessary top_of_sstack must be rounded down to the
       * next boundary.
       */

      top_of_sstack = STACKFRAME_ALIGN_DOWN(top_of_sstack);

      /* Save the adjusted shadow stack values in the struct tcb_s */

      tcb->xcp.sstack_top_ptr = (uintreg_t *)top_of_sstack;

      return OK;
    }

  return ERROR;
}
#endif /* CONFIG_ARCH_RV_SHADOW_STACK */

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_use_stack
 *
 * Description:
 *   Setup up stack-related information in the TCB using pre-allocated stack
 *   memory.  This function is called only from nxtask_init() when a task or
 *   kernel thread is started (never for pthreads).
 *
 *   The following TCB fields must be initialized:
 *
 *   - adj_stack_size: Stack size after adjustment for hardware,
 *     processor, etc.  This value is retained only for debug
 *     purposes.
 *   - stack_alloc_ptr: Pointer to allocated stack
 *   - stack_base_ptr: Adjusted stack base pointer after the TLS Data and
 *     Arguments has been removed from the stack allocation.
 *
 * Input Parameters:
 *   - tcb: The TCB of new task
 *   - stack_size:  The allocated stack size.
 *
 *   NOTE:  Unlike up_stack_create() and up_stack_release, this function
 *   does not require the task type (ttype) parameter.  The TCB flags will
 *   always be set to provide the task type to up_use_stack() if it needs
 *   that information.
 *
 ****************************************************************************/

int up_use_stack(struct tcb_s *tcb, void *stack, size_t stack_size,
                 int ttype)
{
  uintptr_t top_of_stack;
  size_t size_of_stack;

#ifdef CONFIG_TLS_ALIGNED
  /* The allocated stack size must not exceed the maximum possible for the
   * TLS feature.
   */

  DEBUGASSERT(stack_size <= TLS_MAXSTACK);
  if (stack_size >= TLS_MAXSTACK)
    {
      stack_size = TLS_MAXSTACK;
    }

  if (ttype == TCB_FLAG_TTYPE_PTHREAD)
    {
      /* Make certain that the userspace stack is properly aligned */

      DEBUGASSERT(((uintptr_t)stack & STACK_ALIGN_MASK) == 0);
    }
  else
#endif
    {
      /* With Kernel space or Flat build, we can always get tls by tcb */

      DEBUGASSERT(((uintptr_t)stack & STACKFRAME_ALIGN_MASK) == 0);
    }

  /* Is there already a stack allocated? */

  if (tcb->stack_alloc_ptr)
    {
      /* Yes.. Release the old stack allocation */

      up_release_stack(tcb, ttype);
    }

  /* Save the new stack allocation */

  tcb->stack_alloc_ptr = stack;

  /* RISC-V uses a push-down stack: the stack grows toward lower addresses in
   * memory. The stack pointer register, points to the lowest, valid work
   * address (the "top" of the stack). Items on the stack are referenced
   * as positive word offsets from SP.
   */

  top_of_stack = (uintptr_t)tcb->stack_alloc_ptr + stack_size;

  /* The RISC-V stack must be aligned at 128-bit (16-byte) boundaries.
   * If necessary top_of_stack must be rounded down to the next boundary.
   */

  top_of_stack = STACKFRAME_ALIGN_DOWN(top_of_stack);
  size_of_stack = top_of_stack - (uintptr_t)tcb->stack_alloc_ptr;

  /* Save the adjusted stack values in the struct tcb_s */

  tcb->stack_base_ptr = tcb->stack_alloc_ptr;
  tcb->adj_stack_size = size_of_stack;

#if defined(CONFIG_STACK_COLORATION)
  /* If stack debug is enabled, then fill the stack with a
   * recognizable value that we can use later to test for high
   * water marks.
   */

  riscv_stack_color(tcb->stack_base_ptr, tcb->adj_stack_size);
#endif

#ifdef CONFIG_ARCH_RV_SHADOW_STACK
  /* Create shadow stack for this thread.
   * Shadow stack only needs to store link registers, so use half
   * the size of the main stack.
   */

  if (create_shadow_stack(tcb, stack_size / 2) != OK)
    {
      serr("ERROR: Failed to create shadow stack\n");
      return ERROR;
    }
#endif /* CONFIG_ARCH_RV_SHADOW_STACK */

  return OK;
}
