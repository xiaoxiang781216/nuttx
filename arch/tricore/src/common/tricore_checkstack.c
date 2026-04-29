/****************************************************************************
 * arch/tricore/src/common/tricore_checkstack.c
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

#include <nuttx/addrenv.h>
#include <nuttx/arch.h>

#include "sched/sched.h"
#include "tricore_internal.h"

#ifdef CONFIG_STACK_COLORATION

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: tricore_find_first_color
 *
 * Description:
 *   Scan the space used by csa and locate first STACK_COLOR.
 *
 ****************************************************************************/

static inline_function
uint32_t *tricore_find_first_color(uint32_t *ptr, uint32_t *end)
{
  while (ptr < end && *(ptr + 1) != STACK_COLOR)
    {
      ptr += TC_CONTEXT_REGS;
    }

  return ptr;
}

/****************************************************************************
 * Name: tricore_count_check_region
 *
 * Description:
 *   Count remaining space within the check region.
 *
 ****************************************************************************/

static inline_function
size_t tricore_count_check_region(uint32_t *ptr, uint32_t *end,
                                  size_t checksize)
{
  while (checksize > 0 && ptr < end)
    {
      if (*ptr != STACK_COLOR && !TC_CONTEXT_ALIGNED(ptr))
        {
          break;
        }

      checksize--;
      ptr++;
    }

  return checksize;
}

/****************************************************************************
 * Name: tricore_stack_check
 *
 * Description:
 *   Determine (approximately) how much stack has been used be searching the
 *   stack memory for a high water mark.  That is, the deepest level of the
 *   stack that clobbered some recognizable marker in the stack memory.
 *
 * Input Parameters:
 *   alloc - Allocation base address of the stack
 *   size - The size of the stack in bytes
 *   checksize - The size of check region
 *
 * Returned Value:
 *   The estimated amount of stack space used.
 *
 ****************************************************************************/

static inline_function
size_t tricore_stack_check(uintptr_t alloc, size_t size, size_t checksize)
{
  uint32_t *ptr;
  uint32_t *end;
  uint32_t *snapshot;
  size_t    cnt;

  if (size == 0)
    {
      return 0;
    }

  /* Get aligned addresses of the top and bottom of the stack */

  ptr = (uint32_t *)((alloc + 3) & ~3);
  end = (uint32_t *)((alloc + size) & ~3);

  /* Get the adjusted size of the checksize */

  checksize = (checksize & ~3) >> 2;
  cnt = checksize;

  /* Locate the first STACK_COLOR from the bottom of the stack */

  ptr = tricore_find_first_color(ptr, end);

  while (ptr < end)
    {
      snapshot = ptr;
      cnt = tricore_count_check_region(ptr, end, checksize);

      /* Re-locate from the snapshot position. */

      ptr = tricore_find_first_color(ptr, end);
      if (ptr == snapshot)
        {
          break;
        }
    }

  return cnt << 2;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_check_tcbstack and friends
 *
 * Description:
 *   Determine (approximately) how much stack has been used be searching the
 *   stack memory for a high water mark.  That is, the deepest level of the
 *   stack that clobbered some recognizable marker in the stack memory.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   The estimated amount of stack space used.
 *
 ****************************************************************************/

size_t up_check_tcbstack(struct tcb_s *tcb, size_t check_size)
{
  size_t size;

#ifdef CONFIG_ARCH_ADDRENV
  struct addrenv_s *oldenv = NULL;

  if (tcb != g_running_task && tcb->group->tg_addrenv_own != NULL)
    {
      addrenv_select(tcb->group->tg_addrenv_own, &oldenv);
    }
#endif

  size = tricore_stack_check((uintptr_t)tcb->stack_base_ptr,
                             tcb->adj_stack_size, check_size);

#ifdef CONFIG_ARCH_ADDRENV
  if (tcb != g_running_task && tcb->group->tg_addrenv_own != NULL)
    {
      addrenv_restore(oldenv);
    }
#endif

  return size;
}

#if CONFIG_ARCH_INTERRUPTSTACK > 15
size_t up_check_intstack(int cpu, size_t check_size)
{
  if (check_size == 0)
    {
      check_size = CONFIG_ARCH_INTERRUPTSTACK & ~15;
    }

  return tricore_stack_check((uintptr_t)up_get_intstackbase(cpu),
                             (CONFIG_ARCH_INTERRUPTSTACK & ~15),
                             check_size);
}
#endif

#endif /* CONFIG_STACK_COLORATION */
