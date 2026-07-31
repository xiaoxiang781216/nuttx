/****************************************************************************
 * arch/zsp/src/common/zsp_usestack.c
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
#include <nuttx/arch.h>

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int up_use_stack(struct tcb_s *tcb, void *stack, size_t stack_size,
                 int ttype)
{
  uintptr_t top;

  UNUSED(ttype);

  /* Is there already a stack allocated? */

  if (tcb->stack_alloc_ptr)
    {
      up_release_stack(tcb, ttype);
    }

  /* Save the new stack allocation */

  tcb->stack_alloc_ptr = stack;

  /* ZSP uses a push-down stack: the stack grows toward lower addresses.
   * The stack pointer register (a7) points to the lowest valid address.
   */

  top = (uintptr_t)stack + stack_size;
  top = STACK_ALIGN_DOWN(top);

  tcb->stack_base_ptr = stack;
  tcb->adj_stack_size = top - (uintptr_t)stack;

  return OK;
}
