/****************************************************************************
 * arch/arm/src/arm_m/arm_svcall.c
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

#include <inttypes.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <debug.h>
#include <syscall.h>

#include <arch/irq.h>
#include <nuttx/addrenv.h>
#include <nuttx/macro.h>
#include <nuttx/ppccall.h>
#include <nuttx/sched.h>
#include <nuttx/userspace.h>

#include "sched/sched.h"
#include "signal/signal.h"
#include "exc_return.h"
#include "arm_internal.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#if defined(CONFIG_LIB_PPCCALL)
static bool arm_dispatch_ppccall(uint32_t *regs)
{
  struct ppccall_param_s *param = (struct ppccall_param_s *)regs[REG_R1];
  const struct ppccall_info_s *info = ppccall_find_info(param->nbr);

  if (info == NULL)
    {
      serr("ERROR: No ppc info found for func_id %d\n", param->nbr);
      regs[REG_R0] = -ENOENT;
      return false;
    }

#ifdef CONFIG_BUILD_FLAT
  regs[REG_PC] = (uint32_t)up_ppccall_handler & ~1;
#else
  if ((atomic_read(&info->tcb->flags) & TCB_FLAG_TTYPE_MASK) ==
      TCB_FLAG_TTYPE_KERNEL)
    {
      regs[REG_CONTROL] &= ~CONTROL_NPRIV;
      regs[REG_PC]       = (uint32_t)up_ppccall_handler & ~1;
    }
  else
    {
      regs[REG_CONTROL] |= CONTROL_NPRIV;
      regs[REG_PC]       = (uint32_t)USERSPACE->ppccall_handler & ~1;
    }
#endif

  regs[REG_R0] = info->func;
  return true;
}
#elif defined(CONFIG_LIB_SYSCALL)
static inline_function bool arm_dispatch_ppccall(uint32_t *regs)
{
  serr("ERROR: PPCALL not supported\n");
  regs[REG_R0] = -ENOENT;
  return false;
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: arm_svcall
 *
 * Description:
 *   This is SVCall exception handler that performs context switching
 *
 ****************************************************************************/

int arm_svcall(int irq, void *context, void *arg)
{
  uint32_t *regs    = (uint32_t *)context;
  struct tcb_s *tcb = this_task();
  uint32_t cmd      = regs[REG_R0];

  /* The SVCall software interrupt is called with R0 = system call command
   * and R1..R7 =  variable number of arguments depending on the system call.
   */

#ifdef CONFIG_DEBUG_SYSCALL_INFO
#  ifndef CONFIG_DEBUG_SVCALL
  if (cmd > SYS_switch_context)
#  endif
    {
      svcinfo("SVCALL Entry: regs: %p cmd: %d\n", regs, cmd);
      svcinfo("  R0: %08x %08x %08x %08x %08x %08x %08x %08x\n",
              regs[REG_R0],  regs[REG_R1],  regs[REG_R2],  regs[REG_R3],
              regs[REG_R4],  regs[REG_R5],  regs[REG_R6],  regs[REG_R7]);
      svcinfo("  R8: %08x %08x %08x %08x %08x %08x %08x %08x\n",
              regs[REG_R8],  regs[REG_R9],  regs[REG_R10], regs[REG_R11],
              regs[REG_R12], regs[REG_R13], regs[REG_R14], regs[REG_R15]);
      svcinfo(" PSR: %08x EXC_RETURN: %08x CONTROL: %08x\n",
              regs[REG_XPSR], regs[REG_EXC_RETURN], regs[REG_CONTROL]);
    }
#endif

  /* Handle the SVCall according to the command in R0 */

  switch (cmd)
    {
      case SYS_restore_context:
      case SYS_switch_context:
        {
#ifdef CONFIG_ARCH_ADDRENV
          addrenv_switch(tcb); /* this_task maybe changed */
          tcb = this_task();
#endif
          break_critical_section();

#ifdef CONFIG_DEBUG_SYSCALL_INFO
          regs = tcb->xcp.regs;
#endif
        }
        break;

      /* R0=SYS_syscall_return:  This a syscall return command:
       *
       *   void arm_syscall_return(void);
       *
       * At this point, the following values are saved in context:
       *
       *   R0 = SYS_syscall_return
       *
       * We need to restore the saved return address and return in
       * unprivileged thread mode.
       */

#if defined(CONFIG_LIB_SYSCALL) || defined(CONFIG_LIB_PPCCALL)
      case SYS_syscall_return:
        {
          int index = (int)tcb->xcp.nsyscalls - 1;

          /* Make sure that there is a saved syscall return address. */

          DEBUGASSERT(index >= 0);

          /* Setup to return to the saved syscall return address in
           * the original mode.
           */

          regs[REG_PC]         = tcb->xcp.syscall[index].sysreturn;
          regs[REG_EXC_RETURN] = tcb->xcp.syscall[index].excreturn;
          regs[REG_CONTROL]    = tcb->xcp.syscall[index].ctrlreturn;
          tcb->xcp.nsyscalls  = index;

          /* The return value must be in R0-R1.  arm_dispatch_syscall()
           * temporarily moved the value for R0 into R2.
           */

          regs[REG_R0]         = regs[REG_R2];

          /* Handle any signal actions that were deferred while processing
           * the system call.
           */

          atomic_and(&tcb->flags, ~TCB_FLAG_SYSCALL);
          nxsig_unmask_pendingsignal();

#ifdef CONFIG_ARCH_KERNEL_STACK
          /* If this is the outermost SYSCALL and if there is a saved user
           * stack pointer, then restore the user stack pointer on this
           * final return to user code.
           */

          if (index == 0 && tcb->xcp.ustkptr != NULL)
            {
              regs[REG_SP] = (uint32_t)tcb->xcp.ustkptr;
              memcpy(tcb->xcp.ustkptr, regs, XCPTCONTEXT_SIZE);
              tcb->xcp.regs = tcb->xcp.ustkptr;
              tcb->xcp.ustkptr = NULL;
            }
#endif
        }
        break;
#endif

      /* R0=SYS_task_start:  This a user task start
       *
       *   void up_task_start(main_t taskentry, int argc, char *argv[])
       *          noreturn_function;
       *
       * At this point, the following values are saved in context:
       *
       *   R0 = SYS_task_start
       *   R1 = taskentry
       *   R2 = argc
       *   R3 = argv
       */

#ifdef CONFIG_BUILD_PROTECTED
      case SYS_task_start:
        {
          /* Set up to return to the user-space task start-up function in
           * unprivileged mode.
           */

          regs[REG_PC]         = (uint32_t)USERSPACE->task_startup & ~1;
          regs[REG_EXC_RETURN] = EXC_RETURN_THREAD;

          /* Return unprivileged mode */

          regs[REG_CONTROL]    = getcontrol() | CONTROL_NPRIV;

          /* Change the parameter ordering to match the expectation of struct
           * userpace_s task_startup:
           */

          regs[REG_R0]         = regs[REG_R1]; /* Task entry */
          regs[REG_R1]         = regs[REG_R2]; /* argc */
          regs[REG_R2]         = regs[REG_R3]; /* argv */
        }
        break;
#endif

      /* R0=SYS_pthread_start:  This a user pthread start
       *
       *   void up_pthread_start(pthread_startroutine_t entrypt,
       *                         pthread_addr_t arg) noreturn_function;
       *
       * At this point, the following values are saved in context:
       *
       *   R0 = SYS_pthread_start
       *   R1 = startup (trampoline)
       *   R2 = entrypt
       *   R3 = arg
       */

#if !defined(CONFIG_BUILD_FLAT) && !defined(CONFIG_DISABLE_PTHREAD)
      case SYS_pthread_start:
        {
          /* Set up to return to the user-space pthread start-up function in
           * unprivileged mode.
           */

          regs[REG_PC]         = (uint32_t)regs[REG_R1] & ~1;  /* startup */
          regs[REG_EXC_RETURN] = EXC_RETURN_THREAD;

          /* Return unprivileged mode */

          regs[REG_CONTROL]    = getcontrol() | CONTROL_NPRIV;

          /* Change the parameter ordering to match the expectation of the
           * user space pthread_startup:
           */

          regs[REG_R0]         = regs[REG_R2]; /* pthread entry */
          regs[REG_R1]         = regs[REG_R3]; /* arg */
        }
        break;
#endif

      /* R0=SYS_signal_handler:  This a user signal handler callback
       *
       * void signal_handler(_sa_sigaction_t sighand, int signo,
       *                     siginfo_t *info, void *ucontext);
       *
       * At this point, the following values are saved in context:
       *
       *   R0 = SYS_signal_handler
       *   R1 = sighand
       *   R2 = signo
       *   R3 = info
       *   R4 = ucontext
       */

#if defined(CONFIG_BUILD_PROTECTED) && !defined(CONFIG_DISABLE_SIGNALS)
      case SYS_signal_handler:
        {
          /* Remember the caller's return address */

          DEBUGASSERT(tcb->xcp.sigreturn == 0);
          tcb->xcp.sigreturn  = regs[REG_PC];

          /* Set up to return to the user-space trampoline function in
           * unprivileged mode.
           */

          regs[REG_PC]         = (uint32_t)USERSPACE->signal_handler & ~1;
          regs[REG_EXC_RETURN] = EXC_RETURN_THREAD;

          /* Return unprivileged mode */

          regs[REG_CONTROL]    = getcontrol() | CONTROL_NPRIV;

          /* Change the parameter ordering to match the expectation of struct
           * userpace_s signal_handler.
           */

          regs[REG_R0]         = regs[REG_R1]; /* sighand */
          regs[REG_R1]         = regs[REG_R2]; /* signal */
          regs[REG_R2]         = regs[REG_R3]; /* info */
          regs[REG_R3]         = regs[REG_R4]; /* ucontext */

#ifdef CONFIG_ARCH_KERNEL_STACK
          /* If we are signalling a user process, then we must be operating
           * on the kernel stack now.  We need to switch back to the user
           * stack before dispatching the signal handler to the user code.
           * The existence of an allocated kernel stack is sufficient
           * information to make this decision.
           */

          if (tcb->xcp.kstack != NULL)
            {
              uint32_t usp;

              DEBUGASSERT(tcb->xcp.kstkptr == NULL);

              /* Copy "info" into user stack */

              usp = (uint32_t)tcb->xcp.ustkptr;

              /* Create a frame for info and copy the kernel info */

              usp = usp - sizeof(siginfo_t);
              memcpy((void *)usp, (void *)regs[REG_R2], sizeof(siginfo_t));

              /* Now set the updated SP and user copy of "info" to R2 */

              tcb->xcp.kstkptr = (uint32_t *)regs;

              regs[REG_SP] = usp + sizeof(siginfo_t);
              regs[REG_R2] = usp;
              memcpy((void *)regs[REG_SP], regs, XCPTCONTEXT_SIZE);
              tcb->xcp.regs = (void *)regs[REG_SP];
            }
#endif
        }
        break;
#endif

      /* R0=SYS_signal_handler_return:  This a user signal handler callback
       *
       *   void signal_handler_return(void);
       *
       * At this point, the following values are saved in context:
       *
       *   R0 = SYS_signal_handler_return
       */

#if defined(CONFIG_BUILD_PROTECTED) && !defined(CONFIG_DISABLE_SIGNALS)
      case SYS_signal_handler_return:
        {
          /* Set up to return to the kernel-mode signal dispatching logic. */

          DEBUGASSERT(tcb->xcp.sigreturn != 0);

          regs[REG_PC]         = tcb->xcp.sigreturn & ~1;
          regs[REG_EXC_RETURN] = EXC_RETURN_THREAD;

          /* Return privileged mode */

          regs[REG_CONTROL]    = getcontrol() & ~CONTROL_NPRIV;
          tcb->xcp.sigreturn  = 0;
#ifdef CONFIG_ARCH_KERNEL_STACK
          /* We must enter here be using the user stack.  We need to switch
           * to back to the kernel user stack before returning to the kernel
           * mode signal trampoline.
           */

          if (tcb->xcp.kstack != NULL)
            {
              DEBUGASSERT(tcb->xcp.kstkptr != NULL);

              regs[REG_SP]      = (uint32_t)tcb->xcp.kstkptr;
              tcb->xcp.kstkptr = NULL;

              tcb->xcp.regs = (void *)regs[REG_SP];
              memcpy((void *)tcb->xcp.regs, regs, XCPTCONTEXT_SIZE);
            }
#endif
        }
        break;
#endif
      case SYS_assert_handler:
        {
          _assert((const char *)regs[REG_R1], (int)regs[REG_R2],
                  (const char *)regs[REG_R3], (void *)running_regs(), false);
        }
        break;

      /* This is not an architecture-specific system call.  If NuttX is built
       * as a standalone kernel with a system call interface, then all of the
       * additional system calls must be handled as in the default case.
       */

      default:
#if defined(CONFIG_LIB_SYSCALL) || defined(CONFIG_LIB_PPCCALL)
        /* Verify that the SYS call number is within range */

#  ifdef CONFIG_LIB_SYSCALL
        DEBUGASSERT(cmd >= CONFIG_SYS_RESERVED && cmd < SYS_maxsyscall);
#  endif
      case SYS_ppccall:
        {
          int index = tcb->xcp.nsyscalls;

          /* Make sure that there is a no saved syscall return address.  We
           * cannot yet handle nested system calls.
           */

          DEBUGASSERT(index < CONFIG_SYS_NNEST);

          /* Setup to return to arm_dispatch_syscall in privileged mode. */

          tcb->xcp.syscall[index].sysreturn  = regs[REG_PC];
          tcb->xcp.syscall[index].excreturn  = regs[REG_EXC_RETURN];
          tcb->xcp.syscall[index].ctrlreturn = regs[REG_CONTROL];

          if (cmd == SYS_ppccall)
            {
              if (!arm_dispatch_ppccall(regs))
                {
                  break;
                }
            }
          else
            {
              regs[REG_PC]      = (uint32_t)arm_dispatch_syscall & ~1;

              /* Return privileged mode */

              regs[REG_CONTROL] = getcontrol() & ~CONTROL_NPRIV;

              /* Offset R0 to account for the reserved values */

              regs[REG_R0]     -= CONFIG_SYS_RESERVED;
            }

          tcb->xcp.nsyscalls  = index + 1;

          /* Use ip to create a debug frame.
           * we can use gdb backtrace from syscall to user space.
           */

          regs[REG_IP]         = tcb->xcp.syscall[index].sysreturn;
          regs[REG_EXC_RETURN] = EXC_RETURN_THREAD;

          /* Indicate that we are in a syscall handler. */

          atomic_or(&tcb->flags, TCB_FLAG_SYSCALL);

#  ifdef CONFIG_ARCH_KERNEL_STACK
          /* If this is the first SYSCALL and if there is an allocated
           * kernel stack, then switch to the kernel stack.
           */

          if (index == 0 && tcb->xcp.kstack)
            {
              regs[REG_SP] = (uint32_t)tcb->xcp.kstack +
                             ARCH_KERNEL_STACKSIZE - XCPTCONTEXT_SIZE;
              memcpy((void *)regs[REG_SP], tcb->xcp.regs, XCPTCONTEXT_SIZE);
              tcb->xcp.ustkptr = tcb->xcp.regs;
              tcb->xcp.regs = (void *)regs[REG_SP];
            }
#  endif
        }
#else
        svcerr("ERROR: Bad SYS call: %" PRId32 "\n", regs[REG_R0]);
#endif
        break;
    }

  /* Report what happened.  That might difficult in the case of a context
   * switch.
   */

#ifdef CONFIG_DEBUG_SYSCALL_INFO
#  ifndef CONFIG_DEBUG_SVCALL
  if (cmd > SYS_switch_context)
#  endif
    {
      svcinfo("SVCall Return:\n");
      svcinfo("  R0: %08x %08x %08x %08x %08x %08x %08x %08x\n",
              regs[REG_R0],  regs[REG_R1], regs[REG_R2],  regs[REG_R3],
              regs[REG_R4],  regs[REG_R5], regs[REG_R6],  regs[REG_R7]);
      svcinfo("  R8: %08x %08x %08x %08x %08x %08x %08x %08x\n",
              regs[REG_R8],  regs[REG_R9], regs[REG_R10], regs[REG_R11],
              regs[REG_R12], regs[REG_R13], regs[REG_R14], regs[REG_R15]);
      svcinfo(" PSR: %08x EXC_RETURN: %08x CONTROL: %08x\n",
              regs[REG_XPSR], regs[REG_EXC_RETURN], regs[REG_CONTROL]);
    }
#endif

  UNUSED(tcb);
  return OK;
}
