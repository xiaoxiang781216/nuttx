/****************************************************************************
 * arch/zsp/src/common/zsp_stackframe.c
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
#include <string.h>

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_stack_frame
 *
 * Description:
 *   Allocate a stack frame in the TCB's stack to hold thread-specific data
 *   such as TLS info and argv strings. The allocation is from the bottom
 *   of the stack (stack_base_ptr moves upward).
 *
 ****************************************************************************/

void *up_stack_frame(struct tcb_s *tcb, size_t frame_size)
{
  void *ret;

  frame_size = STACK_ALIGN_UP(frame_size);

  /* Is there already a stack allocated? Is it big enough? */

  if (!tcb->stack_alloc_ptr || tcb->adj_stack_size <= frame_size)
    {
      return NULL;
    }

  /* Allocate from the bottom of the stack (stack_base_ptr) */

  ret = tcb->stack_base_ptr;
  memset(ret, 0, frame_size);

  /* Adjust the stack base and available size */

  tcb->stack_base_ptr  = (uint8_t *)tcb->stack_base_ptr + frame_size;
  tcb->adj_stack_size -= frame_size;

  return ret;
}
