/****************************************************************************
 * arch/tricore/include/irq.h
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

/* This file should never be included directly but, rather, only indirectly
 * through nuttx/irq.h
 */

#ifndef __ARCH_TRICORE_INCLUDE_IRQ_H
#define __ARCH_TRICORE_INCLUDE_IRQ_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <sys/types.h>
#ifndef __ASSEMBLY__
#  include <stdbool.h>
#  include <syscall.h>
#endif

/* Include NuttX-specific IRQ definitions */

#include <nuttx/percpu.h>
#include <nuttx/irq.h>

/* Include chip-specific IRQ definitions (including IRQ numbers) */

#include <arch/chip/irq.h>
#include <arch/arch.h>

#include <IfxCpu.h>

/****************************************************************************
 * Pre-processor Prototypes
 ****************************************************************************/

/* Upper CSA */

#define REG_UPCXI           0
#define REG_PSW             1
#define REG_A10             2
#define REG_UA11            3
#define REG_D8              4
#define REG_D9              5
#define REG_D10             6
#define REG_D11             7
#define REG_A12             8
#define REG_A13             9
#define REG_A14             10
#define REG_A15             11
#define REG_D12             12
#define REG_D13             13
#define REG_D14             14
#define REG_D15             15

/* Lower CSA */

#define REG_LPCXI           0
#define REG_LA11            1
#define REG_A2              2
#define REG_A3              3
#define REG_D0              4
#define REG_D1              5
#define REG_D2              6
#define REG_D3              7
#define REG_A4              8
#define REG_A5              9
#define REG_A6              10
#define REG_A7              11
#define REG_D4              12
#define REG_D5              13
#define REG_D6              14
#define REG_D7              15

#define REG_RA              REG_UA11
#define REG_SP              REG_A10
#define REG_UPC             REG_UA11
#define REG_LPC             REG_LA11

#define TC_CONTEXT_REGS     (16)
#define TC_CONTEXT_SIZE     (sizeof(void *) * TC_CONTEXT_REGS)
#define TC_CONTEXT_ALIGNED(addr) \
  (((uintptr_t)(addr) & (TC_CONTEXT_SIZE - 1)) == 0)

#define XCPTCONTEXT_REGS    (TC_CONTEXT_REGS * 2)
#define XCPTCONTEXT_SIZE    (sizeof(void *) * XCPTCONTEXT_REGS)

/* PSW: Program Status Word Register */

#define PSW_CDE             (1 << 7) /* Bits 7: Call Depth Count Enable */
#define PSW_GW              (1 << 8) /* Bits 8: Global address register write permission */
#define PSW_IS              (1 << 9) /* Bits 9: Interrupt Stack Control */
#define PSW_IO              (10)     /* Bits 10-11: Access Privilege Level Control (I/O Privilege) */
#  define PSW_MODE_MASK     (3 << PSW_IO)
#  define PSW_IO_USER0      (0 << PSW_IO)
#  define PSW_IO_USER1      (1 << PSW_IO)
#  define PSW_IO_SUPERVISOR (2 << PSW_IO)

#define PSW_PRS_KERNEL_SET  (0)
#define PSW_PRS_USER_SET    (1)
#ifdef CONFIG_ARCH_HAVE_PPRS
  #define PSW_PRS_MASK                           (0)
  #define PSW_PRS_USER                           (0)
  #define tricore_change_pprs(tcb, val)          (tcb)->xcp.pprs = (val)
  #define tricore_load_pprs(tcb)                 (tcb)->xcp.pprs
  #define tricore_restore_pprs(tcb)              __mtcr(CPU_PPRS, (tcb)->xcp.pprs)
  #define tricore_store_pprs(tcb)                (tcb)->xcp.pprs = __mfcr(CPU_PPRS)
  #define tricore_syscall_load_pprs(tcb ,index)  (tcb)->xcp.syscall_pprs[index]
  #define tricore_syscall_store_pprs(tcb ,index) (tcb)->xcp.syscall_pprs[index] = __mfcr(CPU_PPRS)
  #define tricore_sig_change_pprs(tcb ,val)      (tcb)->xcp.saved_pprs = (val)
  #define tricore_sig_load_pprs(tcb)             (tcb)->xcp.saved_pprs
  #define tricore_pprs_to_prs(pprs)              (((pprs & 3) << 12) | ((pprs & 4) << 13))
  #define tricore_prs_to_pprs(prs)               (((prs >> 12) & 3) | ((prs >> 13) & 4))
#else
  #define PSW_PRS_MASK                           (3 << 12) | (1 << 15)
  #define PSW_PRS_USER                           (((PSW_PRS_USER_SET & 3) << 12) | ((PSW_PRS_USER_SET & 4) << 13))
  #define tricore_change_pprs(tcb, val)          do { } while (0)
  #define tricore_load_pprs(tcb)                 (0)
  #define tricore_restore_pprs(tcb)              do { } while (0)
  #define tricore_store_pprs(tcb)                do { } while (0)
  #define tricore_syscall_load_pprs(tcb ,index)  (0)
  #define tricore_syscall_store_pprs(tcb ,index) do { } while (0)
  #define tricore_sig_change_pprs(tcb ,val)      do { } while (0)
  #define tricore_sig_load_pprs(tcb)             (0)
  #define tricore_pprs_to_prs(pprs)              (0)
  #define tricore_prs_to_pprs(prs)               (0)
#endif

/* PCXI: Previous Context Information and Pointer Register */

#define PCXI_UL             (1 << 20) /* Bits 20: Upper or Lower Context Tag */
#define PCXI_PIE            (1 << 21) /* Bits 21: Previous Interrupt Enable */

/* FCX: Free CSA List Head Pointer Register */

#define FCX_FCXO            (0)       /* Bits 0-15: FCX Offset Address */
#define FCX_FCXS            (16)      /* Bits 16-19: FCX Segment Address */
#define FCX_FCXO_MASK       (0xffff << FCX_FCXO)
#define FCX_FCXS_MASK       (0xf    << FCX_FCXS)
#define FCX_FREE            (FCX_FCXS_MASK | FCX_FCXO_MASK) /* Free CSA manipulation */

#define TRICORE_SRCNUM_PER_GPSR  8
#define TRICORE_SRC2IRQ(src_addr) \
  (((uintptr_t)(src_addr) - (uintptr_t)&MODULE_SRC) / 4)
#define TRICORE_GPSR_IRQNUM(src_cpu, dest_cpu)  \
  TRICORE_SRC2IRQ(&SRC_GPSR0_SR0 + src_cpu * 8 + dest_cpu)

/* For use with EABI and floating point, the stack must be aligned to 8-byte
 * addresses.
 */

#define STACKFRAME_ALIGN  TC_CONTEXT_SIZE

/* tls_info */

#define up_tls_info()  ((struct tls_info_s *)read_sysreg(a8))

#ifndef __ASSEMBLY__

#ifdef __cplusplus
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct xcptcontext
{
#ifndef CONFIG_DISABLE_SIGNALS
  /* These are saved copies of the context used during
   * signal processing.
   */

  uintptr_t *saved_regs;
#endif

  /* Register save area with XCPTCONTEXT_SIZE, only valid when:
   * 1.The task isn't running or
   * 2.The task is interrupted
   * otherwise task is running, and regs contain the stale value.
   */

  uintptr_t *regs;

#ifdef CONFIG_LIB_SYSCALL
  /* The following array holds information needed to return from each nested
   * system call.
   */

  int nsyscalls;
  uintptr_t *syscall_regs[CONFIG_SYS_NNEST];
#ifdef CONFIG_ARCH_HAVE_PPRS
  uint32_t syscall_pprs[CONFIG_SYS_NNEST];
#endif
#endif

#ifdef CONFIG_BUILD_PROTECTED
  /* This is the saved address to use when returning from a user-space
   * signal handler.
   */

  uint32_t sigreturn;

#endif

#ifdef CONFIG_ARCH_HAVE_PPRS
  uint32_t pprs;
  uint32_t saved_pprs;
#endif
};

/****************************************************************************
 * Public Data
 ****************************************************************************/

/* g_interrupt_context store irq status */

DECLARE_PER_CPU(volatile bool, g_interrupt_context);
#define g_interrupt_context this_cpu_var(g_interrupt_context)

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: up_cpu_index
 *
 * Description:
 *   Return the real core number regardless CONFIG_SMP setting
 *
 ****************************************************************************/

#ifdef CONFIG_ARCH_HAVE_MULTICPU
noinstrument_function
static inline_function int up_cpu_index(void)
{
  return IfxCpu_getCoreIndex();
}
#endif /* CONFIG_ARCH_HAVE_MULTICPU */

/****************************************************************************
 * Name: up_irq_enable
 *
 * Description:
 *   Enable interrupts globally.
 *
 ****************************************************************************/

void up_irq_enable(void);

/****************************************************************************
 * Inline functions
 ****************************************************************************/

/* Return program counter */

noinstrument_function static inline_function uintptr_t up_getpc(void)
{
#ifdef CONFIG_TRICORE_TOOLCHAIN_TASKING
  return (uintptr_t)__mfcr(CPU_PC);
#else
  uintptr_t pc;
  __asm__ volatile ("mfcr %0,%1": "=d" (pc) :"i"(CPU_PC): "memory");
  return pc;
#endif
}

noinstrument_function static inline_function uintptr_t up_getsp(void)
{
#ifdef CONFIG_TRICORE_TOOLCHAIN_TASKING
  return (uintptr_t)__get_sp();
#else
  return (uintptr_t)__builtin_frame_address(0);
#endif
}

/****************************************************************************
 * Name: up_irq_save
 *
 * Description:
 *   Disable interrupts and return the previous value of the mstatus register
 *
 ****************************************************************************/

noinstrument_function static inline_function irqstate_t up_irq_save(void)
{
  return __disable_and_save();
}

/****************************************************************************
 * Name: up_irq_restore
 *
 * Description:
 *   Restore the value of the mstatus register
 *
 ****************************************************************************/

noinstrument_function static inline_function
void up_irq_restore(irqstate_t flags)
{
  __restore(flags);
}

/****************************************************************************
 * Inline Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_set_interrupt_context
 *
 * Description:
 *   Set the interrupt handler context.
 *
 ****************************************************************************/

noinstrument_function
static inline_function void up_set_interrupt_context(bool flag)
{
  g_interrupt_context = flag;
}

/****************************************************************************
 * Name: up_interrupt_context
 *
 * Description:
 *   Return true is we are currently executing in the interrupt
 *   handler context.
 *
 ****************************************************************************/

noinstrument_function
static inline_function bool up_interrupt_context(void)
{
#ifdef CONFIG_SMP
  irqstate_t flags = up_irq_save();
#endif
  bool ret = g_interrupt_context;
#ifdef CONFIG_SMP
  up_irq_restore(flags);
#endif
  return ret;
}

/****************************************************************************
 * Name: up_switch_context
 ****************************************************************************/

#define up_switch_context(tcb, rtcb)                              \
  do {                                                            \
    if (!up_interrupt_context())                                  \
      {                                                           \
        sys_call0(SYS_switch_context);                            \
      }                                                           \
      UNUSED(rtcb);                                               \
  } while (0)

/****************************************************************************
 * Name: up_getusrsp
 ****************************************************************************/

static inline_function uintptr_t up_getusrsp(void *regs)
{
  return ((uintptr_t *)regs)[REG_SP];
}

/****************************************************************************
 * Name: up_getusrpc
 ****************************************************************************/

#define up_getusrpc(regs) \
    (((uint32_t *)((regs) ? (regs) : running_regs()))[REG_UPC])

#undef EXTERN
#ifdef __cplusplus
}
#endif
#endif /* __ASSEMBLY__ */

#endif /* __ARCH_TRICORE_INCLUDE_IRQ_H */
