/****************************************************************************
 * arch/arm/src/arm_a_r/arm_syscall.c
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

#include <nuttx/addrenv.h>
#include <nuttx/arch.h>
#include <nuttx/macro.h>
#include <nuttx/sched.h>

#include "addrenv.h"
#include "arm.h"
#include "arm_internal.h"
#include "sched/sched.h"
#include "signal/signal.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: dump_syscall
 *
 * Description:
 *   Dump the syscall registers
 *
 ****************************************************************************/

static void dump_syscall(const char *tag, uint32_t cmd, const uint32_t *regs)
{
  /* The SVCall software interrupt is called with R0 = system call command
   * and R1..R7 =  variable number of arguments depending on the system call.
   */

#ifdef CONFIG_LIB_SYSCALL
  if (cmd >= CONFIG_SYS_RESERVED)
    {
      svcinfo("SYSCALL %s: regs: %p cmd: %" PRId32 " name: %s\n", tag,
              regs, cmd, g_funcnames[cmd - CONFIG_SYS_RESERVED]);
    }
  else
#endif
    {
      svcinfo("SYSCALL %s: regs: %p cmd: %" PRId32 "\n", tag, regs, cmd);
    }

  svcinfo("  R0: %08" PRIx32 " %08" PRIx32 " %08" PRIx32 " %08" PRIx32
          " %08" PRIx32 " %08" PRIx32 " %08" PRIx32 " %08" PRIx32 "\n",
          regs[REG_R0],  regs[REG_R1],  regs[REG_R2],  regs[REG_R3],
          regs[REG_R4],  regs[REG_R5],  regs[REG_R6],  regs[REG_R7]);
  svcinfo("  R8: %08" PRIx32 " %08" PRIx32 " %08" PRIx32 " %08" PRIx32
          " %08" PRIx32 " %08" PRIx32 " %08" PRIx32 " %08" PRIx32 "\n",
          regs[REG_R8],  regs[REG_R9],  regs[REG_R10], regs[REG_R11],
          regs[REG_R12], regs[REG_R13], regs[REG_R14], regs[REG_R15]);
  svcinfo("CPSR: %08" PRIx32 "\n", regs[REG_CPSR]);
}

/****************************************************************************
 * Name: arm_dispatch_ppccall
 ****************************************************************************/

#if defined(CONFIG_LIB_PPCCALL)
static bool arm_dispatch_ppccall(uint32_t *regs)
{
  struct ppccall_param_s *param = (struct ppccall_param_s *)regs[REG_R1];
  const struct ppccall_info_s *info = ppccall_find_info(param->nbr);
  uint32_t cpsr;

  if (info == NULL)
    {
      serr("ERROR: No ppc info found for func_id %d\n", param->nbr);
      regs[REG_R0] = -ENOENT;
      return false;
    }

#ifdef CONFIG_BUILD_FLAT
  regs[REG_PC] = (uint32_t)up_ppccall_handler & ~1;
#else
  cpsr = regs[REG_CPSR] & ~PSR_MODE_MASK;
  if ((atomic_read(&info->tcb->flags) & TCB_FLAG_TTYPE_MASK) ==
      TCB_FLAG_TTYPE_KERNEL)
    {
      regs[REG_CPSR] = cpsr | PSR_MODE_SYS;
      regs[REG_PC]   = (uint32_t)up_ppccall_handler & ~1;
    }
  else
    {
      regs[REG_CPSR] = cpsr | PSR_MODE_USR;
      regs[REG_PC]   = (uint32_t)USERSPACE->ppccall_handler & ~1;
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
 * Name: arm_syscall
 *
 * Description:
 *   SVC interrupts will vector here with insn=the SVC instruction and
 *   xcp=the interrupt context
 *
 *   The handler may get the SVC number be de-referencing the return
 *   address saved in the xcp and decoding the SVC instruction
 *
 ****************************************************************************/

uint32_t *arm_syscall(uint32_t *regs)
{
  struct tcb_s **running_task = &g_running_task;
  struct tcb_s *tcb = this_task();
  uint32_t cmd;
#ifndef CONFIG_BUILD_FLAT
  uint32_t cpsr;
#endif
  uintptr_t tpidr;

  /* Nested interrupts are not supported */

  DEBUGASSERT(!up_interrupt_context());

  /* Set irq flag */

  up_set_interrupt_context(true);

  /* Note to ram */

  sched_note_irqhandler(NR_IRQS, arm_syscall, true);

  /* The SYSCALL command is in R0 on entry.  Parameters follow in R1..R7 */

  cmd = regs[REG_R0];

  /* if cmd == SYS_restore_context (*running_task)->xcp.regs is valid
   * should not be overwriten
   */

  if (cmd != SYS_restore_context)
    {
      (*running_task)->xcp.regs = regs;
    }

  /* The SVCall software interrupt is called with R0 = system call command
   * and R1..R7 =  variable number of arguments depending on the system call.
   */

  dump_syscall("Entry", cmd, regs);

  /* Handle the SVCall according to the command in R0 */

  switch (cmd)
    {
      /* R0=SYS_syscall_return:  This a SYSCALL return command:
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
          struct tcb_s *rtcb = this_task();
          int index = (int)rtcb->xcp.nsyscalls - 1;

          /* Make sure that there is a saved SYSCALL return address. */

          DEBUGASSERT(index >= 0);

          /* Setup to return to the saved SYSCALL return address in
           * the original mode.
           */

          regs[REG_PC]        = rtcb->xcp.syscall[index].sysreturn;
#ifndef CONFIG_BUILD_FLAT
          regs[REG_CPSR]      = rtcb->xcp.syscall[index].cpsr |
                                (regs[REG_CPSR] & (PSR_F_BIT | PSR_I_BIT));
#endif
          /* The return value must be in R0-R1.  arm_dispatch_syscall()
           * temporarily moved the value for R0 into R2.
           */

          regs[REG_R0]         = regs[REG_R2];

          /* Restore R1-R3, R12 that were saved on syscall entry.
           * The two-stage syscall dispatch does not preserve these
           * caller-saved registers, but user code may rely on them.
           */

          regs[REG_R1]         = rtcb->xcp.syscall[index].r1;
          regs[REG_R2]         = rtcb->xcp.syscall[index].r2;
          regs[REG_R3]         = rtcb->xcp.syscall[index].r3;
          regs[REG_R12]        = rtcb->xcp.syscall[index].r12;

#ifdef CONFIG_ARCH_KERNEL_STACK
          /* If this is the outermost SYSCALL and if there is a saved user
           * stack pointer, then restore the user stack pointer on this
           * final return to user code.
           */

          regs[REG_R11]         = rtcb->xcp.syscall[index].r11;

          if (index == 0 && rtcb->xcp.ustkptr != NULL)
            {
              regs[REG_SP]      = (uint32_t)rtcb->xcp.ustkptr;
              rtcb->xcp.ustkptr = NULL;
            }
#endif

          /* Save the new SYSCALL nesting level */

          rtcb->xcp.nsyscalls   = index;
          regs                  = tcb->xcp.regs;
        }
        break;
#endif

      case SYS_switch_context:
      case SYS_restore_context:

#ifdef CONFIG_ARCH_ADDRENV
        /* The addrenv_switch may change this_task, for example addrenv drop
         * will post sem to hpwork, so we have to call before restore context
         */

        addrenv_switch(tcb);
        tcb = this_task();
#endif

        break_critical_section();
        *running_task = tcb;
        regs = tcb->xcp.regs;
        break;

      /* R0=SYS_task_start:  This a user task start
       *
       *   void up_task_start(main_t taskentry, int argc, char *argv[])
       *     noreturn_function;
       *
       * At this point, the following values are saved in context:
       *
       *   R0 = SYS_task_start
       *   R1 = taskentry
       *   R2 = argc
       *   R3 = argv
       */

#ifndef CONFIG_BUILD_FLAT
      case SYS_task_start:
        {
          /* Set up to return to the user-space _start function in
           * unprivileged mode.  We need:
           *
           *   R0   = argc
           *   R1   = argv
           *   PC   = taskentry
           *   CSPR = user mode
           */

#ifdef CONFIG_BUILD_KERNEL
          regs[REG_PC]   = regs[REG_R1];
          regs[REG_R0]   = regs[REG_R2];
          regs[REG_R1]   = regs[REG_R3];
#else
          regs[REG_PC]   = (uint32_t)USERSPACE->task_startup & ~1;
          regs[REG_R0]   = regs[REG_R1];
          regs[REG_R1]   = regs[REG_R2];
          regs[REG_R2]   = regs[REG_R3];
#endif

          /* LR should be zero in user mode
           * Make gdb end of stack backtrace correctly
           */

          regs[REG_LR]   = 0;
          cpsr           = regs[REG_CPSR] & ~PSR_MODE_MASK;
          regs[REG_CPSR] = cpsr | PSR_MODE_USR;
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
       *   R1 = entrypt
       *   R2 = arg
       */

#if !defined(CONFIG_BUILD_FLAT) && !defined(CONFIG_DISABLE_PTHREAD)
      case SYS_pthread_start:
        {
          /* Set up to enter the user-space pthread start-up function in
           * unprivileged mode. We need:
           *
           *   R0   = entrypt
           *   R1   = arg
           *   PC   = startup
           *   CSPR = user mode
           */

          regs[REG_PC]   = regs[REG_R1];
          regs[REG_R0]   = regs[REG_R2];
          regs[REG_R1]   = regs[REG_R3];

          cpsr           = regs[REG_CPSR] & ~PSR_MODE_MASK;
          regs[REG_CPSR] = cpsr | PSR_MODE_USR;
        }
        break;
#endif

#if !defined(CONFIG_BUILD_FLAT) && !defined(CONFIG_DISABLE_SIGNALS)
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
       *        ucontext (on the stack)
       */

      case SYS_signal_handler:
        {
          struct tcb_s *rtcb = this_task();

          /* Remember the caller's return address */

          DEBUGASSERT(rtcb->xcp.sigreturn == 0);
          rtcb->xcp.sigreturn  = regs[REG_PC];

          /* Set up to return to the user-space trampoline function in
           * unprivileged mode.
           */

#ifdef CONFIG_BUILD_KERNEL
          regs[REG_PC]   = (uint32_t)ARCH_DATA_RESERVE->ar_sigtramp;
#else
          regs[REG_PC]   = (uint32_t)USERSPACE->signal_handler & ~1;
#endif
          cpsr           = regs[REG_CPSR] & ~PSR_MODE_MASK;
          regs[REG_CPSR] = cpsr | PSR_MODE_USR;

          /* Change the parameter ordering to match the expectation of struct
           * userpace_s signal_handler.
           */

          regs[REG_R0]   = regs[REG_R1]; /* sighand */
          regs[REG_R1]   = regs[REG_R2]; /* signal */
          regs[REG_R2]   = regs[REG_R3]; /* info */
          regs[REG_R3]   = regs[REG_R4]; /* ucontext */

#ifdef CONFIG_ARCH_KERNEL_STACK
          /* If we are signalling a user process, then we must be operating
           * on the kernel stack now.  We need to switch back to the user
           * stack before dispatching the signal handler to the user code.
           * The existence of an allocated kernel stack is sufficient
           * information to make this decision.
           */

          if (rtcb->xcp.kstack != NULL)
            {
              uint32_t usp;

              DEBUGASSERT(rtcb->xcp.kstkptr == NULL);

              /* Copy "info" into user stack */

              if (rtcb->sigdeliver)
                {
                  usp = (uint32_t)rtcb->xcp.ustkptr;
                }
              else
                {
                  usp = rtcb->xcp.regs[REG_SP];
                }

              /* Create a frame for info and copy the kernel info */

              usp = usp - sizeof(siginfo_t);
              memcpy((void *)usp, (void *)regs[REG_R2], sizeof(siginfo_t));

              /* Now set the updated SP and user copy of "info" to R2 */

              rtcb->xcp.kstkptr = (uint32_t *)regs[REG_SP];
              regs[REG_SP]      = usp;
              regs[REG_R2]      = usp;
            }
#endif
        }
        break;
#endif /* !defined(CONFIG_BUILD_FLAT) && !defined(CONFIG_DISABLE_SIGNALS) */

#if !defined(CONFIG_BUILD_FLAT) && !defined(CONFIG_DISABLE_SIGNALS)
      /* R0=SYS_signal_handler_return:  This a user signal handler callback
       *
       *   void signal_handler_return(void);
       *
       * At this point, the following values are saved in context:
       *
       *   R0 = SYS_signal_handler_return
       */

      case SYS_signal_handler_return:
        {
          struct tcb_s *rtcb = this_task();

          /* Set up to return to the kernel-mode signal dispatching logic. */

          DEBUGASSERT(rtcb->xcp.sigreturn != 0);

          regs[REG_PC]         = rtcb->xcp.sigreturn;
          cpsr                 = regs[REG_CPSR] & ~PSR_MODE_MASK;
          regs[REG_CPSR]       = cpsr | PSR_MODE_SYS;
          rtcb->xcp.sigreturn  = 0;

#ifdef CONFIG_ARCH_KERNEL_STACK
          /* We must enter here be using the user stack.  We need to switch
           * to back to the kernel user stack before returning to the kernel
           * mode signal trampoline.
           */

          if (rtcb->xcp.kstack != NULL)
            {
              DEBUGASSERT(rtcb->xcp.kstkptr != NULL);

              regs[REG_SP]      = (uint32_t)rtcb->xcp.kstkptr;
              rtcb->xcp.kstkptr = NULL;
            }
#endif
        }
        break;
#endif /* !defined(CONFIG_BUILD_FLAT) && !defined(CONFIG_DISABLE_SIGNALS) */
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
          struct tcb_s *rtcb = this_task();
          int index = rtcb->xcp.nsyscalls;

          /* Make sure that there is a no saved SYSCALL return address.  We
           * cannot yet handle nested system calls.
           */

          DEBUGASSERT(index < CONFIG_SYS_NNEST);

          /* Setup to return to arm_dispatch_syscall in privileged mode. */

          rtcb->xcp.syscall[index].sysreturn = regs[REG_PC];
#  ifndef CONFIG_BUILD_FLAT
          rtcb->xcp.syscall[index].cpsr      = regs[REG_CPSR];
#  endif

          /* Save R1-R3, R12 so they can be restored on
           * SYS_syscall_return.  The two-stage syscall dispatch
           * (arm_dispatch_syscall) clobbers these caller-saved
           * registers, but user code may rely on them.
           */

          rtcb->xcp.syscall[index].r1        = regs[REG_R1];
          rtcb->xcp.syscall[index].r2        = regs[REG_R2];
          rtcb->xcp.syscall[index].r3        = regs[REG_R3];
          rtcb->xcp.syscall[index].r12       = regs[REG_R12];

          if (cmd == SYS_ppccall)
            {
              if (!arm_dispatch_ppccall(regs))
                {
                  break;
                }
            }
          else
            {
              regs[REG_PC]   = (uint32_t)arm_dispatch_syscall;
#    ifndef CONFIG_BUILD_FLAT
              cpsr           = regs[REG_CPSR] & ~PSR_MODE_MASK;
              regs[REG_CPSR] = cpsr | PSR_MODE_SYS;
#    endif
              /* Offset R0 to account for the reserved values */

              regs[REG_R0]  -= CONFIG_SYS_RESERVED;
            }

          /* Use ip to create a debug frame.
           * we can use gdb backtrace from syscall to user space.
           */

          regs[REG_IP] = rtcb->xcp.syscall[index].sysreturn;

#ifdef CONFIG_ARCH_KERNEL_STACK
          /* Save sp to help unwind through arm_dispatch_syscall */

          rtcb->xcp.syscall[index].r11 = regs[REG_R11];
          regs[REG_R11]                = regs[REG_SP];

          /* If this is the first SYSCALL and if there is an allocated
           * kernel stack, then switch to the kernel stack.
           */

          if (index == 0 && rtcb->xcp.kstack != NULL)
            {
              rtcb->xcp.ustkptr = (uint32_t *)regs[REG_SP];
              if (rtcb->xcp.kstkptr != NULL)
                {
                  regs[REG_SP]  = (uint32_t)rtcb->xcp.kstkptr;
                }
              else
                {
                  regs[REG_SP]  = (uint32_t)rtcb->xcp.kstack +
                                  ARCH_KERNEL_STACKSIZE;
                }
            }
#endif

          /* Save the new SYSCALL nesting level */

          rtcb->xcp.nsyscalls   = index + 1;
        }
#else
        svcerr("ERROR: Bad SYS call: 0x%" PRIx32 "\n", regs[REG_R0]);
#endif
        break;
    }

  /* Report what happened */

  dump_syscall("Exit", cmd, regs);

  /* Note to ram */

  sched_note_irqhandler(NR_IRQS, arm_syscall, false);

  /* Update the TLS pointer.
   * The TLS pointer must skip over the
   * tls_info_s structure to align with compiler-generated TLS
   * variable access. The compiler's built-in TLS support (for
   * thread_local variables) expects the TLS pointer to point directly
   * to the TLS data area, not to our manually-maintained tls_info_s
   * metadata structure. By adding sizeof(struct tls_info_s), we
   * ensure that compiler-generated code accesses the correct memory
   * location, bypassing our internal bookkeeping structure.
   */

  tpidr = (uintptr_t)tcb->stack_alloc_ptr + sizeof(struct tls_info_s);
  CP15_SET(TPIDRURW, tpidr);

  /* Set irq flag */

  up_set_interrupt_context(false);

  /* (*running_task)->xcp.regs is about to become invalid
   * and will be marked as NULL to avoid misusage.
   */

  (*running_task)->xcp.regs = NULL;

  /* Return the last value of curent_regs.  This supports context switches
   * on return from the exception.  That capability is only used with the
   * SYS_context_switch system call.
   */

  return regs;
}
