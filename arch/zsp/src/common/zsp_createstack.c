/****************************************************************************
 * arch/zsp/src/common/zsp_createstack.c
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
#include <nuttx/kmalloc.h>
#include <string.h>

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int up_create_stack(struct tcb_s *tcb, size_t stack_size, int ttype)
{
  /* Align stack size */

  stack_size = STACK_ALIGN_UP(stack_size);

  /* Is there already a stack allocated of a different size? */

  if (tcb->stack_alloc_ptr && tcb->adj_stack_size != stack_size)
    {
      up_release_stack(tcb, ttype);
    }

  /* Allocate a new stack if needed */

  if (!tcb->stack_alloc_ptr)
    {
      tcb->stack_alloc_ptr =
        group_memalign(tcb->group, STACK_ALIGNMENT, stack_size);
    }

  if (tcb->stack_alloc_ptr)
    {
      uintptr_t top;
      top = (uintptr_t)tcb->stack_alloc_ptr + stack_size;
      top = STACK_ALIGN_DOWN(top);

      tcb->stack_base_ptr = tcb->stack_alloc_ptr;
      tcb->adj_stack_size = top - (uintptr_t)tcb->stack_alloc_ptr;

#ifdef CONFIG_STACK_COLORATION
      memset(tcb->stack_alloc_ptr, 0xaa, tcb->adj_stack_size);
#endif

      atomic_or(&tcb->flags, TCB_FLAG_FREE_STACK);
      return OK;
    }

  return -ENOMEM;
}

