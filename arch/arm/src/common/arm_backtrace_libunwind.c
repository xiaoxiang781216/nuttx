/****************************************************************************
 * arch/arm/src/common/arm_backtrace_libunwind.c
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
#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <nuttx/sched.h>

#include <string.h>
#include <libunwind.h>

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: backtrace_unwind
 *
 * Description:
 *   Perform stack unwinding using libunwind from a given cursor.
 *
 ****************************************************************************/

static int backtrace_unwind(unw_cursor_t *cursor,
                            void **buffer, int size, int *skip)
{
  unw_word_t prev_pc = 0;
  int i = 0;

  while (i < size)
    {
      unw_word_t pc;

      if (unw_get_reg(cursor, UNW_REG_IP, &pc) < 0 || pc == 0)
        {
          break;
        }

      if (pc == prev_pc)
        {
          break;
        }

      prev_pc = pc;

      if ((*skip) > 0)
        {
          (*skip)--;
        }
      else
        {
          buffer[i++] = (void *)pc;
        }

      if (unw_step(cursor) <= 0)
        {
          break;
        }
    }

  return i;
}

/****************************************************************************
 * Name: backtrace_current
 *
 * Description:
 *   Backtrace the current task using unw_getcontext.
 *
 ****************************************************************************/

static int backtrace_current(void **buffer, int size, int *skip)
{
  unw_context_t context;
  unw_cursor_t cursor;

  if (unw_getcontext(&context) < 0)
    {
      return 0;
    }

  if (unw_init_local(&cursor, &context) < 0)
    {
      return 0;
    }

  return backtrace_unwind(&cursor, buffer, size, skip);
}

/****************************************************************************
 * Name: backtrace_from_context
 *
 * Description:
 *   Backtrace from a saved register context (other task or interrupt frame).
 *
 ****************************************************************************/

static int backtrace_from_context(uintptr_t fp, uintptr_t sp, uintptr_t pc,
                                  uintptr_t lr, void **buffer, int size,
                                  int *skip)
{
  unw_context_t context;
  unw_cursor_t cursor;

  if (unw_getcontext(&context) < 0)
    {
      return 0;
    }

  if (unw_init_local(&cursor, &context) < 0)
    {
      return 0;
    }

  if (unw_set_reg(&cursor, UNW_REG_IP, pc) < 0 ||
      unw_set_reg(&cursor, UNW_REG_SP, sp) < 0 ||
#ifdef CONFIG_ARM_THUMB
      unw_set_reg(&cursor, UNW_ARM_R7, fp) < 0 ||
#else
      unw_set_reg(&cursor, UNW_ARM_R11, fp) < 0 ||
#endif
      unw_set_reg(&cursor, UNW_ARM_LR, lr) < 0)
    {
      return 0;
    }

  return backtrace_unwind(&cursor, buffer, size, skip);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_backtrace
 *
 * Description:
 *   up_backtrace() returns a backtrace for the TCB, in the array
 *   pointed to by buffer.
 *
 * Input Parameters:
 *   tcb    - Address of the task's TCB, or NULL for current task
 *   buffer - Return address array
 *   size   - Maximum number of addresses to return
 *   skip   - Number of initial frames to skip
 *
 * Returned Value:
 *   Number of return addresses stored in buffer
 *
 ****************************************************************************/

int up_backtrace(struct tcb_s *tcb,
                 void **buffer, int size, int skip)
{
  struct tcb_s *rtcb = running_task();
  int ret;

  if (size <= 0 || !buffer)
    {
      return 0;
    }

  if (tcb == NULL || tcb == rtcb)
    {
      ret = backtrace_current(buffer, size, &skip);
      if (up_interrupt_context())
        {
          if (ret < size && running_regs())
            {
              ret += backtrace_from_context(
#ifdef CONFIG_ARM_THUMB
                       (uintptr_t)running_regs()[REG_R7],
#else
                       (uintptr_t)running_regs()[REG_R11],
#endif
                       (uintptr_t)running_regs()[REG_SP],
                       (uintptr_t)running_regs()[REG_PC],
                       (uintptr_t)running_regs()[REG_LR],
                       &buffer[ret], size - ret, &skip);
            }
        }
    }
  else
    {
      ret = backtrace_from_context(
#ifdef CONFIG_ARM_THUMB
              (uintptr_t)tcb->xcp.regs[REG_R7],
#else
              (uintptr_t)tcb->xcp.regs[REG_R11],
#endif
              (uintptr_t)tcb->xcp.regs[REG_SP],
              (uintptr_t)tcb->xcp.regs[REG_PC],
              (uintptr_t)tcb->xcp.regs[REG_LR],
              buffer, size, &skip);

#ifdef CONFIG_LIB_SYSCALL
      if (ret < size && tcb->xcp.sregs)
        {
          ret += backtrace_from_context(
                   (uintptr_t)tcb->xcp.sregs[REG_FP],
                   (uintptr_t)tcb->xcp.sregs[REG_SP],
                   (uintptr_t)tcb->xcp.sregs[REG_PC],
                   (uintptr_t)tcb->xcp.sregs[REG_LR],
                   &buffer[ret], size - ret, &skip);
        }
#endif
    }

  return ret;
}
