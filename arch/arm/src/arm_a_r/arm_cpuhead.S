/****************************************************************************
 * arch/arm/src/arm_a_r/arm_cpuhead.S
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

#include <arch/irq.h>

#include "arm.h"
#include "cp15_cacheops.h"
#include "sctlr.h"
#ifdef CONFIG_ARCH_HAVE_MMU
#include "mmu.h"
#endif
#include "smp.h"
#include "chip.h"
#include "arm_internal.h"

#ifndef CONFIG_UP

  .file "arm_cpuhead.S"

/****************************************************************************
 * Configuration
 ****************************************************************************/

#ifdef CONFIG_ARCH_HAVE_MMU
/* Check for the identity mapping:  For this configuration, this would be
 * the case where the virtual beginning of RAM is the same as the physical
 * beginning of RAM.
 */

#if !defined(CONFIG_RAM_START) || !defined(CONFIG_RAM_VSTART)
#  error "CONFIG_RAM_START or CONFIG_RAM_VSTART is not defined"
#endif

#if CONFIG_RAM_START == CONFIG_RAM_VSTART
#  define CONFIG_IDENTITY_TEXTMAP 1
#endif
#endif /* CONFIG_ARCH_HAVE_MMU */

/* There are three operational memory configurations:
 *
 * 1. We execute in place in FLASH (CONFIG_BOOT_RUNFROMFLASH=y).  In this case
 *    the boot logic must:
 *
 *    - Configure SDRAM (if present),
 *    - Initialize the .data section in RAM, and
 *    - Clear .bss section
 *
 * 2. We boot in FLASH but copy ourselves to SDRAM from better performance.
 *    (CONFIG_BOOT_RUNFROMFLASH=n && CONFIG_BOOT_COPYTORAM=y).  In this case
 *    the boot logic must:
 *
 *    - Configure SDRAM (if present),
 *    - Copy ourself to DRAM, and
 *    - Clear .bss section (data should be fully initialized)
 *
 *   In this case, we assume that the logic within this file executes from FLASH.
 *
 * 3. There is bootloader that copies us to SDRAM (but probably not to the beginning)
 *    (CONFIG_BOOT_RUNFROMFLASH=n && CONFIG_BOOT_COPYTORAM=n). In this case SDRAM
 *    was initialized by the boot loader, and this boot logic must:
 *
 *    - Clear .bss section (data should be fully initialized)
 */

/* Beginning (BOTTOM/BASE) and End+1 (TOP) of the IDLE stack.
 *
 * The IDLE stack is the stack that is used during initialization and,
 * eventually, becomes the stack of the IDLE task when initialization
 * is complete.
 *
 * REVISIT:  There are issues here in some configurations.  The stack
 * pointer is initialized very early in the boot sequence.  But in some
 * architectures the memory supporting the stack may not yet be
 * initialized (SDRAM, for example, would not be ready yet).  In that
 * case, ideally the IDLE stack should be in some other memory that does
 * not require initialization (such as internal SRAM)
 */

/****************************************************************************
 * .text
 ****************************************************************************/

  .text
  .syntax unified
  .arm

/****************************************************************************
 * Name: __cpu[n]_start
 *
 * Description:
 *   Boot functions for each CPU (other than CPU0).  These functions set up
 *   the ARM operating mode, the initial stack, and configure co-processor
 *   registers.  At the end of the boot, arm_cpu_boot() is called.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   Do not return.
 *
 ****************************************************************************/

#if CONFIG_NCPUS > 1
  .global __cpu1_start
  .type   __cpu1_start, #function
__cpu1_start:
#ifdef CONFIG_ARM_HAVE_EL2
  ldr   sp, .Lcpu1_stackpointer
  b     __el2_boot
#else
  b     __el1_boot
#endif

  .globl  cpu1_start
  .type  cpu1_start, object

  cpu1_start:
  .long  __cpu1_start
#endif

#if CONFIG_NCPUS > 2
  .global __cpu2_start
  .type   __cpu2_start, #function
__cpu2_start:
#ifdef CONFIG_ARM_HAVE_EL2
  ldr   sp, .Lcpu2_stackpointer
  b     __el2_boot
#else
  b     __el1_boot
#endif

  .globl  cpu2_start
  .type  cpu2_start, object

  cpu2_start:
  .long  __cpu2_start
#endif

#if CONFIG_NCPUS > 3
  .global __cpu3_start
  .type   __cpu3_start, #function
__cpu3_start:
#ifdef CONFIG_ARM_HAVE_EL2
  ldr   sp, .Lcpu3_stackpointer
  b     __el2_boot
#else
  b     __el1_boot
#endif

  .globl  cpu3_start
  .type  cpu3_start, object

  cpu3_start:
  .long  __cpu3_start
#endif

#if CONFIG_NCPUS > 4
  .global __cpu4_start
  .type   __cpu4_start, #function
__cpu4_start:
#ifdef CONFIG_ARM_HAVE_EL2
  ldr   sp, .Lcpu4_stackpointer
  b     __el2_boot
#else
  b     __el1_boot
#endif

  .globl  cpu4_start
  .type  cpu4_start, object

  cpu4_start:
  .long  __cpu4_start
#endif

#if CONFIG_NCPUS > 5
  .global __cpu5_start
  .type   __cpu5_start, #function
__cpu5_start:
#ifdef CONFIG_ARM_HAVE_EL2
  ldr   sp, .Lcpu5_stackpointer
  b     __el2_boot
#else
  b     __el1_boot
#endif

  .globl  cpu5_start
  .type  cpu5_start, object

  cpu5_start:
  .long  __cpu5_start
#endif

#if CONFIG_NCPUS > 6
  .global __cpu6_start
  .type   __cpu6_start, #function
__cpu6_start:
#ifdef CONFIG_ARM_HAVE_EL2
  ldr   sp, .Lcpu6_stackpointer
  b     __el2_boot
#else
  b     __el1_boot
#endif

  .globl  cpu6_start
  .type  cpu6_start, object

  cpu6_start:
  .long  __cpu6_start
#endif

#if CONFIG_NCPUS > 7
  .global __cpu7_start
  .type   __cpu7_start, #function
__cpu7_start:
#ifdef CONFIG_ARM_HAVE_EL2
  ldr   sp, .Lcpu7_stackpointer
  b     __el2_boot
#else
  b     __el1_boot
#endif

  .globl  cpu7_start
  .type  cpu7_start, object

  cpu7_start:
  .long  __cpu7_start
#endif

#if CONFIG_NCPUS > 8
#  error Need work for CONFIG_NCPUS > 8
#endif

#ifdef CONFIG_ARM_HAVE_EL2
/****************************************************************************
 * Name: __el2_boot
 * Desc: Setup el2 and jump to __el1_boot
 ****************************************************************************/

__el2_boot:
  ldr   r0, .Lhypvectorstart
  mcr   CP15_HVBAR(r0)     /* Set Hyp/PL2 Vectortable base register */

  bl    hsctlr_initialize  /* Init Hyp control register */

  ldr   r0, =HACTLR_INIT
  mcr   CP15_HACTLR(r0)    /* Enable EL1 access to IMP DEFINED regs */

  bl    arm_el_init        /* Platform hook for highest EL */

  /* Switch PL1 SYS with all exceptions masked */

  mov   r0, #(PSR_MODE_SYS | PSR_I_BIT | PSR_F_BIT | PSR_A_BIT)
  msr   spsr_hyp, r0

  adr   r0, __el1_boot
  msr   elr_hyp, r0
  dsb
  isb
  eret
#endif /* CONFIG_ARM_HAVE_EL2 */

/****************************************************************************
 * Name: __el1_boot
 * Desc: dispatch to __cpu[n]_boot per cpuid
 ****************************************************************************/

__el1_boot:
  mrc   CP15_MPIDR(r0)      /* Get cpuindex */
  and   r0, r0, #0x7
  cmp   r0, #1
  beq   __cpu1_boot

#if CONFIG_NCPUS > 2
  cmp   r0, #2
  beq   __cpu2_boot
#endif

#if CONFIG_NCPUS > 3
  cmp   r0, #3
  beq   __cpu3_boot
#endif

#if CONFIG_NCPUS > 4
  cmp   r0, #4
  beq   __cpu4_boot
#endif

#if CONFIG_NCPUS > 5
  cmp   r0, #5
  beq   __cpu5_boot
#endif

#if CONFIG_NCPUS > 6
  cmp   r0, #6
  beq   __cpu6_boot
#endif

#if CONFIG_NCPUS > 7
  cmp   r0, #7
  beq   __cpu7_boot
#endif

/****************************************************************************
 * Name: __cpu[n]_boot
 *
 * Description:
 *   Boot functions for each CPU (other than CPU0) after EL2 setup.
 *   These functions set up the ARM operating mode, the initial stack,
 *   and configure co-processor registers.  At the end of the boot,
 *   arm_cpu_boot() is called.
 *
 ****************************************************************************/

#if CONFIG_NCPUS > 1

__cpu1_boot:
  /* Make sure that we are in SYS mode with IRQs and FIQs disabled */

  cpsid   if, #PSR_MODE_SYS

  /* Set up the stack pointer and the CPU index */

  ldr   sp, .Lcpu1_stackpointer
  mov   r5, #1

  /* Then branch to the common startup logic (PC-relative) */

  b   .Lcpu_start

.Lcpu1_stackpointer:
  .long .Lcpu1_stacktop
  .size __cpu1_start, .-__cpu1_start

#if CONFIG_NCPUS > 2

__cpu2_boot:
  /* Make sure that we are in SYS mode with IRQs and FIQs disabled */

  cpsid   if, #PSR_MODE_SYS

  /* Set up the stack pointer and the CPU index */

  ldr   sp, .Lcpu2_stackpointer
  mov   r5, #2

  /* Then branch to the common startup logic (PC-relative) */

  b   .Lcpu_start

.Lcpu2_stackpointer:
  .long .Lcpu2_stacktop
  .size __cpu2_start, .-__cpu2_start

#if CONFIG_NCPUS > 3

__cpu3_boot:
  /* Make sure that we are in SYS mode with IRQs and FIQs disabled */

  cpsid   if, #PSR_MODE_SYS

  /* Set up the stack pointer and the CPU index */

  ldr   sp, .Lcpu3_stackpointer
  mov   r5, #3

  /* Then branch to the common startup logic (PC-relative) */

  b   .Lcpu_start

.Lcpu3_stackpointer:
  .long .Lcpu3_stacktop
  .size __cpu3_start, .-__cpu3_start

#if CONFIG_NCPUS > 4

__cpu4_boot:
  /* Make sure that we are in SYS mode with IRQs and FIQs disabled */

  cpsid   if, #PSR_MODE_SYS

  /* Set up the stack pointer and the CPU index */

  ldr   sp, .Lcpu4_stackpointer
  mov   r5, #4

  /* Then branch to the common startup logic (PC-relative) */

  b   .Lcpu_start

.Lcpu4_stackpointer:
  .long .Lcpu4_stacktop
  .size __cpu4_start, .-__cpu4_start

#if CONFIG_NCPUS > 5

__cpu5_boot:
  /* Make sure that we are in SYS mode with IRQs and FIQs disabled */

  cpsid   if, #PSR_MODE_SYS

  /* Set up the stack pointer and the CPU index */

  ldr   sp, .Lcpu5_stackpointer
  mov   r5, #5

  /* Then branch to the common startup logic (PC-relative) */

  b   .Lcpu_start

.Lcpu5_stackpointer:
  .long .Lcpu5_stacktop
  .size __cpu5_start, .-__cpu5_start

#if CONFIG_NCPUS > 6

__cpu6_boot:
  /* Make sure that we are in SYS mode with IRQs and FIQs disabled */

  cpsid   if, #PSR_MODE_SYS

  /* Set up the stack pointer and the CPU index */

  ldr   sp, .Lcpu6_stackpointer
  mov   r5, #6

  /* Then branch to the common startup logic (PC-relative) */

  b   .Lcpu_start

.Lcpu6_stackpointer:
  .long .Lcpu6_stacktop
  .size __cpu6_start, .-__cpu6_start

#if CONFIG_NCPUS > 7

__cpu7_boot:
  /* Make sure that we are in SYS mode with IRQs and FIQs disabled */

  cpsid   if, #PSR_MODE_SYS

  /* Set up the stack pointer and the CPU index */

  ldr   sp, .Lcpu7_stackpointer
  mov   r5, #7

  /* Then branch to the common startup logic (PC-relative) */

  b   .Lcpu_start

.Lcpu7_stackpointer:
  .long .Lcpu7_stacktop
  .size __cpu7_start, .-__cpu7_start

#if CONFIG_NCPUS > 8
#  error This logic needs to extended for CONFIG_NCPUS > 8

#endif /* CONFIG_NCPUS > 8 */
#endif /* CONFIG_NCPUS > 7 */
#endif /* CONFIG_NCPUS > 6 */
#endif /* CONFIG_NCPUS > 5 */
#endif /* CONFIG_NCPUS > 4 */
#endif /* CONFIG_NCPUS > 3 */
#endif /* CONFIG_NCPUS > 2 */
#endif /* CONFIG_NCPUS > 1 */

/****************************************************************************
 * Name: .Lcpu_start
 *
 * Description:
 *   Common CPUn startup logic (n > 0)
 *
 * On input:
 *   SP = Set to top of CPU IDLE stack
 *   R5 = CPU number
 *
 ****************************************************************************/

  .type .Lcpu_start, #function

.Lcpu_start:
  /* The MMU/MPU and caches should be disabled */

  mrc   CP15_SCTLR(r0)
  bic   r0, r0, #(SCTLR_M | SCTLR_C)
  bic   r0, r0, #(SCTLR_I)
  mcr   CP15_SCTLR(r0)

  /* Invalidate caches and TLBs.
   *
   *   NOTE: "The ARMv7 Virtual Memory System Architecture (VMSA) does not
   *   support a CP15 operation to invalidate the entire data cache. ...
   *   In normal usage the only time the entire data cache has to be
   *   invalidated is on reset."
   *
   * The instruction cache is virtually indexed and physically tagged but
   * the data cache is physically indexed and physically tagged.  So it
   * should not be an issue if the system comes up with a dirty Dcache;
   * the ICache, however, must be invalidated.
   */

  mov   r0, #0
  mcr   CP15_TPIDRPRW(r0) /* Initialize percpu reg TPIDRPRW */
  mcr   CP15_TPIDRURW(r0) /* Initialize percpu reg TPIDRURW */
  mcr   CP15_TPIDRURO(r0) /* Initialize percpu reg TPIDRURO */
#ifdef CONFIG_ARCH_HAVE_MMU
#ifdef CONFIG_ARM_HAVE_MPCORE
  mcr  CP15_TLBIALLIS(r0)  /* Invalidate the entire unified TLB */
  mcr  CP15_BPIALLIS(r0)  /* Invalidate entire branch prediction array */
  mcr  CP15_ICIALLUIS(r0)  /* Invalidate I-cache */
#else
  mcr  CP15_TLBIALL(r0,c7)  /* Invalidate the entire unified TLB */
  mcr  CP15_TLBIALL(r0,c6)
  mcr  CP15_TLBIALL(r0,c5)
  mcr  CP15_BPIALL(r0)  /* Invalidate entire branch prediction array */
  mcr  CP15_ICIALLU(r0)  /* Invalidate I-cache */
#endif
#else
  mcr   CP15_BPIALL(r0)   /* Invalidate entire branch prediction array */
#endif
  isb

#ifdef CONFIG_ARCH_HAVE_MMU
  /* Load the page table address.
   *
   * NOTES:
   * - Here we assume that the page table address is aligned to at least
   *   least a 16KB boundary (bits 0-13 are zero).  No masking is provided
   *   to protect against an unaligned page table address.
   * - The ARMv7-A has two page table address registers, TTBR0 and 1.
   *   Only TTBR0 is used in this implementation but both are initialized.
   */

  ldr  r1, .LCppgtable  /* r1=phys. page table */
#ifdef CONFIG_ARCH_ADDRENV
  mov  r2, #PGTABLE_SIZE
  mla  r1, r2, r5, r1  /* page table of cpu1,2,3 */
#endif
  orr  r1, r1, #(TTBR0_RGN_WBWA | TTBR0_IRGN0)  /* Select cache properties */
  mcr  CP15_TTBR0(r1)
  mcr  CP15_TTBR1(r1)

  /* Set the TTB control register (TTBCR) to indicate that we are using
   * TTBR0.  r0 still holds the value of zero.
   *
   *   N   : 0=Selects TTBR0 and 16KB page table size indexed by VA[31:20]
   *   PD0 : 0=Perform translation table walks using TTBR0
   *   PD1 : 0=Perform translation table walks using TTBR1 (but it is disabled)
   *   EAE : 0=Use 32-bit translation system
   */

  mcr  CP15_TTBCR(r0)

  /* Enable the MMU and caches
   * lr = Resume at .Lcpu_vstart with the MMU enabled
   */

  ldr  lr, .LCcpu_vstart  /* Abs. virtual address */

  /* Configure the domain access register (see mmu.h).  Only domain 0 is
   * supported and it uses the permissions in the TLB.
   */

  mov  r0, #DACR_CLIENT(0)
  mcr  CP15_DACR(r0)    /* Set domain access register */
#else
  /* Set lr = Resume at .Lcpu_vstart */

  ldr   lr, .LCcpu_vstart   /* Abs. address */
#endif /* CONFIG_ARCH_HAVE_MMU */

  /* Configure the system control register (see sctrl.h) */

  mrc   CP15_SCTLR(r0)      /* Get control register */

  /* Clear bits to reset values.  This is only necessary in situations like, for
   * example, we get here via a bootloader and the control register is in some
   * unknown state.
   *
   *   SCTLR_M        Bit 0:  MMU/MPU enable bit
   *   SCTLR_A        Bit 1:  Strict alignment disabled
   *   SCTLR_C        Bit 2:  DCache disabled
   *   SCTLR_CCP15BEN Bit 5:  CP15 barrier enable
   *   SCTLR_B        Bit 7:  Should be zero on ARMv7R
   *
   *   SCTLR_SW       Bit 10: SWP/SWPB not enabled
   *   SCTLR_I        Bit 12: ICache disabled
   *   SCTLR_V        Bit 13: Assume low vectors
   *   SCTLR_RR       Bit 14: Round-robin replacement strategy.
   *
   *   SCTLR_BR       Bit 17: Background Region bit
   *   SCTLR_DZ       Bit 19: Divide by Zero fault enable bit
   *   SCTLR_FI       Bit 21: Fast interrupts configuration enable bit
   *   SCTLR_U        Bit 22: Unaligned access model (always one)
   *
   *   SCTLR_VE       Bit 24: Interrupt Vectors Enable bit
   *   SCTLR_EE       Bit 25: 0=Little endian.
   *   SCTLR_NMFI     Bit 27: Non-maskable FIQ (NMFI) support
   *   SCTLR_TE       Bit 30: All exceptions handled in ARM state.
   */

#ifdef CONFIG_ARCH_HAVE_MMU
  bic  r0, r0, #(SCTLR_A  | SCTLR_C)
  bic  r0, r0, #(SCTLR_SW | SCTLR_I   | SCTLR_V   | SCTLR_RR | SCTLR_HA)
  bic  r0, r0, #(SCTLR_EE | SCTLR_TRE | SCTLR_AFE | SCTLR_TE)
#else
  bic   r0, r0, #(SCTLR_A  | SCTLR_C  | SCTLR_CCP15BEN | SCTLR_B)
  bic   r0, r0, #(SCTLR_SW | SCTLR_I  | SCTLR_V        | SCTLR_RR)
  bic   r0, r0, #(SCTLR_BR | SCTLR_DZ | SCTLR_FI)
  bic   r0, r0, #(SCTLR_VE | SCTLR_EE | SCTLR_NMFI     | SCTLR_TE)
#endif

  /* Set bits to enable the MMU/MPU
   *
   *   SCTLR_M     Bit 0:  Enable the MMU/MPU
   */

  orr   r0, r0, #(SCTLR_M)

#if defined(CONFIG_ARCH_HAVE_MMU) && !defined(CONFIG_ARCH_CORTEXA5)
  /* Program flow prediction
   *
   *   SCTLR_Z     Bit 11: Program flow prediction control
   */

  orr  r0, r0, #(SCTLR_Z)
#endif

  /* Set configured bits */

#ifdef CONFIG_ARM_ALIGNMENT_TRAP
  /* Alignment abort enable
   *
   *   SCTLR_A Bit 1:  Strict alignment enabled
   */

  orr   r0, r0, #(SCTLR_A)
#endif

#ifndef CONFIG_ARCH_HAVE_MMU
#ifdef CONFIG_ARM_SCTLR_CCP15BEN
  /* Enable memory barriers
   *
   *  SCTLR_CCP15BEN Bit 5: CP15 barrier enable
   */

  orr   r0, r0, #(SCTLR_CCP15BEN)
#endif
#endif

#ifndef CONFIG_ARCH_LOWVECTORS
  /* Position vectors to 0xffff0000 if so configured.
   *
   *   SCTLR_V    Bit 13: High vectors
   */

  orr   r0, r0, #(SCTLR_V)
#endif

#ifdef CONFIG_ARM_CACHE_ROUND_ROBIN
#ifndef CONFIG_ARCH_CORTEXA5
  /* Round Robin cache replacement
   *
   *   SCTLR_RR   Bit 14: Round-robin replacement strategy.
   */

  orr   r0, r0, #(SCTLR_RR)
#endif
#endif

#ifndef CONFIG_ARCH_HAVE_MMU
#ifdef CONFIG_ARM_BACKGROUND_REGION
  /* Allow PL1 access to back region when MPU is enabled
   *
   *  SCTLR_BR Bit 17: Background Region bit
   */

  orr   r0, r0, #(SCTLR_BR)
#endif

#ifdef CONFIG_ARM_DIV0_FAULT
  /* Enable divide by zero faults
   *
   *   SCTLR_DZ Bit 19: Divide by Zero fault enable bit
   */

  orr   r0, r0, #(SCTLR_DZ)
#endif

#ifdef CONFIG_ARM_FAST_INTERRUPT
  /* Fast interrupts configuration enable bit
   *
   *   SCTLR_FI Bit 21: Fast interrupts configuration enable bit
   */

  orr   r0, r0, #(SCTLR_FI)
#endif

#ifdef CONFIG_ARM_IMPL_VECTORS
  /* Implementation defined interrupt vectors
   *
   *   SCTLR_VE Bit 24: Interrupt Vectors Enable bit
   */

  orr   r0, r0, #(SCTLR_VE)
#endif
#endif /* !CONFIG_ARCH_HAVE_MMU */

#ifdef CONFIG_ARCH_HAVE_MMU
  /* In SMP configurations, the data cache will not be enabled until later
   * after SMP cache coherency has been setup.
   */

#if !defined(CONFIG_ARM_DCACHE_DISABLE) && !defined(CONFIG_SMP)
  /* Dcache enable
   *
   *   SCTLR_C    Bit 2:  DCache enable
   */

  orr  r0, r0, #(SCTLR_C)
#endif

#if !defined(CONFIG_ARM_ICACHE_DISABLE) && !defined(CONFIG_SMP)
  /* Icache enable
   *
   *   SCTLR_I    Bit 12: ICache enable
   */

  orr  r0, r0, #(SCTLR_I)
#endif
#endif /* CONFIG_ARCH_HAVE_MMU */
#ifdef CONFIG_ENDIAN_BIG
  /* Big endian mode
   *
   *   SCTLR_EE       Bit 25: 1=Big endian.
   */

  orr   r0, r0, #(SCTLR_EE)
#endif

#ifndef CONFIG_ARCH_HAVE_MMU
#ifdef CONFIG_ARM_NONMASKABLE_FIQ
  /* Non-maskable FIQ support
   *
   *   SCTLR_NMFI Bit 27: Non-maskable FIQ (NMFI) support
   */

  orr   r0, r0, #(SCTLR_NMFI)
#endif
#endif

#ifdef CONFIG_ARMV7A_AFE_ENABLE
  /* AP[0:2] Permissions model
   *
   *   SCTLR_AFE  Bit 29: Full, legacy access permissions behavior (reset value).
   *
   * When AFE=1, the page table AP[0] is used as an access flag and AP[2:1]
   * control.  When AFE=0, AP[2:0] control access permissions.
   */

  orr  r0, r0, #(SCTLR_AFE)
#endif
  /* Then write the configured control register */

  mcr    CP15_SCTLR(r0)      /* Write control reg */
  isb
  .rept  12        /* Some CPUs want want lots of NOPs here */
  nop
  .endr

  /* And "jump" to .Lcpu_vstart in the new virtual address space */

  mov    pc, lr

/****************************************************************************
 * PC_Relative Data
 ****************************************************************************/
#ifdef CONFIG_ARCH_HAVE_MMU
  /* The physical base address of the page table */

  .type  .LCppgtable, %object
.LCppgtable:
  .long  PGTABLE_BASE_PADDR  /* Physical start of page table */
  .size  .LCppgtable, . -.LCppgtable
#endif

  /* The start address of the second phase boot logic */

  .type .LCcpu_vstart, %object
.LCcpu_vstart:
  .long .Lcpu_vstart
  .size .LCcpu_vstart, . -.LCcpu_vstart

  .size .Lcpu_start, .-.Lcpu_start

/****************************************************************************
 * Name: .Lcpu_vstart
 *
 * Description:
 *   Continue initialization after the MMU/MPU has been enabled.
 *
 *   The following is executed after the MMU/MPU has been enabled. This uses
 *   absolute addresses; this is not position independent.
 *
 * On input:
 *   SP = Set to top of CPU IDLE stack
 *   R5 = CPU number
 *
 ****************************************************************************/

  .align  8
  .globl  arm_cpu_boot
  .type .Lcpu_vstart, %function

.Lcpu_vstart:

#ifdef CONFIG_ARCH_TRUSTZONE_SECURE
  /* Perform init the secure monitor runtime. As the following arm_boot may
   * using smc to perform psci related func, so the secure monitor init
   * procedure should be placed before arm_boot
   */

  bl  arm_sm_init
#endif

  /* Branch to continue C level CPU initialization */

  mov   fp, #0        /* Clear framepointer */
  mov   lr, #0        /* LR = return address (none) */
  mov   r0, r5        /* Input parameter = CPU index */
  b     arm_cpu_boot  /* Branch to C level CPU initialization */
  .size .Lcpu_vstart, .-.Lcpu_vstart

/***************************************************************************
 * Text-section constants
 ***************************************************************************/

#ifdef CONFIG_ARM_HAVE_EL2
.Lhypvectorstart:
  .long _hyp_vector_start
.Lsysvectorstart:
  .long _vector_start
#endif

/***************************************************************************
 * .noinit section data
 ***************************************************************************/

  .section  .noinit, "aw"

#if CONFIG_NCPUS > 1
  .align  8
  .globl  g_cpu1_idlestack
  .type g_cpu1_idlestack, object

g_cpu1_idlestack:
  .space  SMP_STACK_SIZE
.Lcpu1_stacktop:
  .size g_cpu1_idlestack, .Lcpu1_stacktop-g_cpu1_idlestack

#if CONFIG_NCPUS > 2
  .align  8
  .globl  g_cpu2_idlestack
  .type g_cpu2_idlestack, object

g_cpu2_idlestack:
  .space  SMP_STACK_SIZE
.Lcpu2_stacktop:
  .size g_cpu2_idlestack, .Lcpu2_stacktop-g_cpu2_idlestack

#if CONFIG_NCPUS > 3
  .align  8
  .globl  g_cpu3_idlestack
  .type g_cpu3_idlestack, object

g_cpu3_idlestack:
  .space  SMP_STACK_SIZE
.Lcpu3_stacktop:
  .size g_cpu3_idlestack, .Lcpu3_stacktop-g_cpu3_idlestack

#if CONFIG_NCPUS > 4
  .align  8
  .globl  g_cpu4_idlestack
  .type g_cpu4_idlestack, object

g_cpu4_idlestack:
  .space  SMP_STACK_SIZE
.Lcpu4_stacktop:
  .size g_cpu4_idlestack, .Lcpu4_stacktop-g_cpu4_idlestack

#if CONFIG_NCPUS > 5
  .align  8
  .globl  g_cpu5_idlestack
  .type g_cpu5_idlestack, object

g_cpu5_idlestack:
  .space  SMP_STACK_SIZE
.Lcpu5_stacktop:
  .size g_cpu5_idlestack, .Lcpu5_stacktop-g_cpu5_idlestack

#if CONFIG_NCPUS > 6
  .align  8
  .globl  g_cpu6_idlestack
  .type g_cpu6_idlestack, object

g_cpu6_idlestack:
  .space  SMP_STACK_SIZE
.Lcpu6_stacktop:
  .size g_cpu6_idlestack, .Lcpu6_stacktop-g_cpu6_idlestack

#if CONFIG_NCPUS > 7
  .align  8
  .globl  g_cpu7_idlestack
  .type g_cpu7_idlestack, object

g_cpu7_idlestack:
  .space  SMP_STACK_SIZE
.Lcpu7_stacktop:
  .size g_cpu7_idlestack, .Lcpu7_stacktop-g_cpu7_idlestack

#if CONFIG_NCPUS > 8
#  error This logic needs to extended for CONFIG_NCPUS > 8

#endif /* CONFIG_NCPUS > 8 */
#endif /* CONFIG_NCPUS > 7 */
#endif /* CONFIG_NCPUS > 6 */
#endif /* CONFIG_NCPUS > 5 */
#endif /* CONFIG_NCPUS > 4 */
#endif /* CONFIG_NCPUS > 3 */
#endif /* CONFIG_NCPUS > 2 */
#endif /* CONFIG_NCPUS > 1 */
#endif /* !CONFIG_UP */
  .end
