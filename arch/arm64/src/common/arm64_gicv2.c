/****************************************************************************
 * arch/arm64/src/common/arm64_gicv2.c
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

/* Reference:
 *   Cortex™-A9 MPCore, Revision: r4p1, Technical Reference Manual, ARM DDI
 *   0407I (ID091612).
 *
 *   Includes some removed registers from the r2p2 version as well. ARM DDI
 *   0407F (ID050110)
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>
#include <stdint.h>
#include <assert.h>
#include <errno.h>
#include <debug.h>

#include <nuttx/arch.h>
#include <arch/irq.h>
#include <nuttx/pci/pci.h>

#include "arm64_internal.h"
#include "arm64_gic.h"

#if CONFIG_ARM64_GIC_VERSION == 2

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* SGI group assignment bitmask:
 *   IGROUPR_SGI_SECURE_VAL bit=1 -> Group 1 (non-secure)
 *   IGROUPR_SGI_SECURE_VAL bit=0 -> Group 0 (secure)
 *
 * Default: SGI0-11 are non-secure (0xFFF), SGI12-15 are secure.
 */

#define IGROUPR_SGI_SECURE_VAL 0x00000fffu

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: arm_gic_nlines
 *
 * Description:
 *   Return the number of interrupt lines supported by this GIC
 *   implementation (include both PPIs (32) and SPIs).
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   The number of interrupt lines.
 *
 ****************************************************************************/

static inline unsigned int arm_gic_nlines(void)
{
  uint32_t regval;
  uint32_t field;

  /* Get the number of interrupt lines. */

  regval = getreg32(GIC_ICDICTR);
  field  = (regval & GIC_ICDICTR_ITLINES_MASK) >> GIC_ICDICTR_ITLINES_SHIFT;
  return (field + 1) << 5;
}

#if defined(CONFIG_ARCH_TRUSTZONE_SECURE)
/****************************************************************************
 * Name: up_set_secure_irq
 *
 * Description:
 *   Secure an IRQ
 *
 ****************************************************************************/

void up_secure_irq(int irq, bool secure)
{
  unsigned int val;

  if (secure)
    {
      val = getreg32(GIC_ICDISR(irq)) & (~GIC_ICDISR_INT(irq));  /* group 0 */
    }
  else
    {
      val = getreg32(GIC_ICDISR(irq)) | GIC_ICDISR_INT(irq);     /* group 1 */
    }

  putreg32(val, GIC_ICDISR(irq));
}

/****************************************************************************
 * Name: up_secure_irq_all
 *
 * Description:
 *   Secure all IRQ
 *
 ****************************************************************************/

void up_secure_irq_all(bool secure)
{
  unsigned int nlines = arm_gic_nlines();
  unsigned int irq;

  for (irq = 0; irq < nlines; irq += 32)
    {
      if (secure)
        {
          putreg32(0x00000000, GIC_ICDISR(irq));   /* group 0 */
        }
      else
        {
          putreg32(0xffffffff, GIC_ICDISR(irq));   /* group 1 */
        }
    }
}
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: gic_validate_dist_version
 *
 * Description:
 *   Verify that GIC Version is 2.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   Zero (OK) if GIC Version is 2; -ENODEV if GIC Version isn't 2.
 *
 ****************************************************************************/

static int gic_validate_dist_version(void)
{
  uint32_t  reg;

  /* Read the CPU Interface Implementer ID Register */

  reg = getreg32(GIC_ICCIDR) & GIC_ICCIDR_ARCHNO_MASK;

  /* GIC Version should be 2 */

  if (reg == (0x2 << GIC_ICCIDR_ARCHNO_SHIFT))
    {
      sinfo("GICv2 detected\n");
      return 0;
    }

  /* Read the Peripheral ID2 Register (ICPIDR2) */

  reg = getreg32(GIC_ICDPIDR(GIC_ICPIDR2)) & GICD_PIDR2_ARCH_MASK;

  /* GIC Version should be 2 */

  if (reg == GICD_PIDR2_ARCH_GICV2)
    {
      sinfo("GICv2 detected\n");
      return 0;
    }

  sinfo("GICv2 not detected\n");

  return -ENODEV;
}

/****************************************************************************
 * Name: arm_gic0_initialize
 *
 * Description:
 *   Perform common, one-time GIC initialization on CPU0 only.  Both
 *   arm_gic0_initialize() must be called on CPU0; arm64_gic_init() must
 *   be called for all CPUs.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void arm_gic0_initialize(void)
{
  unsigned int nlines = arm_gic_nlines();
  unsigned int irq;

  /* Wipe stale GIC state left by a previous boot that died mid-IRQ: the
   * running priority (APRn) and the distributor active/pending bits
   * survive a warm reset, and an elevated running priority blocks all
   * lower-priority interrupts, hanging the next boot.
   */

  /* Secure APR: Group0 interrupts. */

  putreg32(0, GIC_ICCAPR1);
  putreg32(0, GIC_ICCAPR2);
  putreg32(0, GIC_ICCAPR3);
  putreg32(0, GIC_ICCAPR4);

  /* Non-secure APR: Group1 interrupts. */

  putreg32(0, GIC_ICCNSAPR1);
  putreg32(0, GIC_ICCNSAPR2);
  putreg32(0, GIC_ICCNSAPR3);
  putreg32(0, GIC_ICCNSAPR4);

  /* Distributor clear-active / clear-pending. */

  for (irq = 0; irq < nlines; irq += 32)
    {
      putreg32(0xffffffff, GIC_ICDCAR(irq));
      putreg32(0xffffffff, GIC_ICDICPR(irq));
    }

  /* Initialize SPIs.  The following should be done only by CPU0. */

  /* A processor in Secure State sets:
   *
   * 1. Which interrupts are non-secure (ICDISR).  All set to zero (group
   *    0).
   * 2. Trigger mode of the SPI (ICDICFR). All fields set to 0b01->Level
   *    sensitive, 1-N model.
   * 3. Interrupt Clear-Enable (ICDICER)
   * 3. Priority of the SPI using the priority set register (ICDIPR).
   *    Priority values are 8-bit unsigned binary. A GIC supports a
   *    minimum of 16 and a maximum of 256 priority levels. Here all
   *    are set to the middle priority 128 (0x80).
   * 4. Target that receives the SPI interrupt (ICDIPTR).  Set all to
   *    CPU0.
   */

  /* Registers with 1-bit per interrupt */

  for (irq = GIC_IRQ_SPI; irq < nlines; irq += 32)
    {
      putreg32(0x00000000, GIC_ICDISR(irq));   /* SPIs group 0 */
      putreg32(0xffffffff, GIC_ICDICER(irq));  /* SPIs disabled */
    }

  /* Registers with 2-bits per interrupt */

  for (irq = GIC_IRQ_SPI; irq < nlines; irq += 16)
    {
      putreg32(0x55555555, GIC_ICDICFR(irq));  /* SPIs level sensitive */
    }

  /* Registers with 8-bits per interrupt */

  for (irq = GIC_IRQ_SPI; irq < nlines; irq += 4)
    {
      putreg32(0x80808080, GIC_ICDIPR(irq));   /* SPI priority */
      putreg32(0x01010101, GIC_ICDIPTR(irq));  /* SPI on CPU0 */
    }

#ifdef CONFIG_ARM64_GICV2M
  arm64_gic_v2m_initialize();
#endif

#ifdef CONFIG_SMP
  /* Attach SGI interrupt handlers. This attaches the handler to all CPUs. */

  DEBUGVERIFY(irq_attach(GIC_SMP_SCHED, arm64_smp_sched_handler, NULL));
  DEBUGVERIFY(irq_attach(GIC_CPU_CALL, nxsched_cpu_call_handler, NULL));
#endif
}

/****************************************************************************
 * Name: arm64_gic_init
 *
 * Description:
 *   Perform common GIC initialization for the current CPU (all CPUs)
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void arm64_gic_init(void)
{
  uint32_t iccicr;
  uint32_t icddcr;

  /* Initialize PPIs.  The following steps need to be done by all CPUs */

  /* Initialize SGIs and PPIs.  NOTE: A processor in non-secure state cannot
   * program its interrupt security registers and must get a secure processor
   * to program the registers.
   */

  /* Registers with 1-bit per interrupt */

#ifdef CONFIG_ARCH_TRUSTZONE_SECURE
  /* per-CPU inerrupts config:
   * ID0-ID11(SGI)  for Non-secure interrupts, of which ID0-ID7 are taken
   *                by Linux for its architectural SMP IPIs.
   * ID12-ID15(SGI)  for Secure interrupts.
   * All PPI config as secure interrupts.
   *
   * An SGI raised towards Linux must come from ID8-ID11, which is why the
   * boundary sits at 12 rather than at 8.
   *
   * Keep the SGI split here in sync with the range up_trigger_irq() uses to
   * decide whether to ask for Group 1 delivery.
   */

  putreg32(IGROUPR_SGI_SECURE_VAL, GIC_ICDISR(0));
#else
  putreg32(0x00000000, GIC_ICDISR(0));      /* SGIs and PPIs secure */
#endif
  putreg32(0xfe000000, GIC_ICDICER(0));     /* PPIs disabled */

  /* Registers with 8-bits per interrupt */

  putreg32(0x80808080, GIC_ICDIPR(0));      /* SGI[3:0] priority */
  putreg32(0x80808080, GIC_ICDIPR(4));      /* SGI[4:7] priority */
  putreg32(0x80808080, GIC_ICDIPR(8));      /* SGI[8:11] priority */
  putreg32(0x00000000, GIC_ICDIPR(12));     /* SGI[12:15] priority */
  putreg32(0x80808000, GIC_ICDIPR(24));     /* PPI[0] priority */
  putreg32(0x80808080, GIC_ICDIPR(28));     /* PPI[1:4] priority */

  /* Set the binary point register.
   *
   * Priority values are 8-bit unsigned binary.  The binary point is a 3-bit
   * field; the value n (n=0-6) specifies that bits (n+1) through bit 7 are
   * used in the comparison for interrupt pre-emption.  A GIC supports a
   * minimum of 16 and a maximum of 256 priority levels so not all binary
   * point settings may be meaningul. The special value n=7
   * (GIC_ICCBPR_NOPREMPT) disables pre-emption.  We disable all pre-emption
   * here to prevent nesting of interrupt handling.
   */

  putreg32(GIC_ICCBPR_NOPREMPT, GIC_ICCBPR);

  /* Program the idle priority in the PMR */

  putreg32(GIC_ICCPMR_MASK, GIC_ICCPMR);

  /* Configure the  CPU Interface Control Register */

  iccicr  = getreg32(GIC_ICCICR);

#if defined(CONFIG_ARCH_TRUSTZONE_SECURE)
  /* Clear secure state ICCICR bits to be configured below */

  iccicr &= ~(GIC_ICCICRS_FIQEN | GIC_ICCICRS_ACKTCTL | GIC_ICCICRS_CBPR |
              GIC_ICCICRS_EOIMODES | GIC_ICCICRS_EOIMODENS |
              GIC_ICCICRS_ENABLEGRP0 | GIC_ICCICRS_ENABLEGRP1 |
              GIC_ICCICRS_FIQBYPDISGRP0 | GIC_ICCICRS_IRQBYPDISGRP0 |
              GIC_ICCICRS_FIQBYPDISGRP1 | GIC_ICCICRS_IRQBYPDISGRP1);

#else
  /* Clear non-secure state ICCICR bits to be configured below */

  iccicr &= ~(GIC_ICCICRU_EOIMODENS | GIC_ICCICRU_ENABLEGRP1 |
              GIC_ICCICRU_FIQBYPDISGRP1 | GIC_ICCICRU_IRQBYPDISGRP1);

#endif

#if defined(CONFIG_ARCH_TRUSTZONE_SECURE)
  /* Set FIQn=1 if secure interrupts are to signal using nfiq_c.
   *
   * NOTE:  Only for processors that operate in secure state.
   * REVISIT: Do I need to do this?
   */

  iccicr |= GIC_ICCICRS_FIQEN;

  /* Program the AckCtl bit to select the required interrupt acknowledge
   * behavior.
   *
   * NOTE: Only for processors that operate in both secure and non-secure
   * state.
   */

  iccicr |= GIC_ICCICRS_ACKTCTL;
#endif

#ifdef CONFIG_ARM_GIC_EOIMODE
#  if defined(CONFIG_ARCH_TRUSTZONE_SECURE)
  /* Set EnableS=1 to enable CPU interface to signal secure interrupts.
   *
   * NOTE:  Only for processors that operate in secure state.
   */

  iccicr |= GIC_ICCICRS_EOIMODES;
#  else
  /* Set EnableNS=1 to enable the CPU to signal non-secure interrupts.
   *
   * NOTE:  Only for processors that operate in non-secure state.
   */

  iccicr |= GIC_ICCICRU_EOIMODENS;
#  endif
#endif

#if defined(CONFIG_ARCH_TRUSTZONE_SECURE)
  /* Enable the Group 0 interrupts, FIQEn and disable Group 0/1
   * bypass.
   */

  iccicr |= (GIC_ICCICRS_ENABLEGRP0 | GIC_ICCICRS_ENABLEGRP1 |
             GIC_ICCICRS_FIQBYPDISGRP0 | GIC_ICCICRS_IRQBYPDISGRP0 |
             GIC_ICCICRS_FIQBYPDISGRP1 | GIC_ICCICRS_IRQBYPDISGRP1);
  icddcr  = (GIC_ICDDCR_ENABLEGRP0 | GIC_ICDDCR_ENABLEGRP1);

#else
  /* Enable the Group 1 interrupts and disable Group 1 bypass. */

  iccicr |= (GIC_ICCICRU_ENABLEGRP1 | GIC_ICCICRU_FIQBYPDISGRP1 |
             GIC_ICCICRU_IRQBYPDISGRP1);
  icddcr  = GIC_ICDDCR_ENABLE;

#endif

  /* Write the final ICCICR value to enable the GIC. */

  putreg32(iccicr, GIC_ICCICR);

  /* Write the ICDDCR value to enable the forwarding of interrupt by the
   * distributor.
   */

  putreg32(icddcr, GIC_ICDDCR);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: arm64_decodeirq
 *
 * Description:
 *   This function is called from the IRQ vector handler in arm_vectors.S.
 *   At this point, the interrupt has been taken and the registers have
 *   been saved on the stack.  This function simply needs to determine the
 *   the irq number of the interrupt and then to call arm_doirq to dispatch
 *   the interrupt.
 *
 *  Input Parameters:
 *   regs - A pointer to the register save area on the stack.
 *
 ****************************************************************************/

uint64_t *arm64_decodeirq(uint64_t * regs)
{
  uint32_t regval;
  int irq;

  /* Read the interrupt acknowledge register and get the interrupt ID */

  regval = getreg32(GIC_ICCIAR);
  irq    = (regval & GIC_ICCIAR_INTID_MASK) >> GIC_ICCIAR_INTID_SHIFT;

#ifdef CONFIG_ARM_GIC_EOIMODE
  putreg32(regval, GIC_ICCEOIR);
#endif

  /* Ignore spurions IRQs.  ICCIAR will report 1023 if there is no pending
   * interrupt.
   */

  DEBUGASSERT(irq < NR_IRQS || irq >= 1022);
  if (irq < NR_IRQS)
    {
      /* Dispatch the interrupt */

      regs = arm64_doirq(irq, regs);
    }

  /* Write to the end-of-interrupt register */

#ifdef CONFIG_ARM_GIC_EOIMODE
  putreg32(regval, GIC_ICCDIR);
#else
  putreg32(regval, GIC_ICCEOIR);
#endif
  return regs;
}

/****************************************************************************
 * Name: arm64_decodefiq
 *
 * Description:
 *   This function is called from the FIQ vector handler in arm_vectors.S.
 *   At this point, the interrupt has been taken and the registers have
 *   been saved on the stack.  This function simply needs to determine the
 *   the irq number of the interrupt and then to call arm_doirq to dispatch
 *   the interrupt.
 *
 *  Input Parameters:
 *   regs - A pointer to the register save area on the stack.
 *
 ****************************************************************************/

uint64_t *arm64_decodefiq(uint64_t *regs)
{
  uint32_t regval;
  int irq;

  /* Read the interrupt acknowledge register and get the interrupt ID */

  regval = getreg32(GIC_ICCIAR);
  irq    = (regval & GIC_ICCIAR_INTID_MASK) >> GIC_ICCIAR_INTID_SHIFT;

#ifdef CONFIG_ARM_GIC_EOIMODE
  putreg32(regval, GIC_ICCEOIR);
#endif

  /* Ignore spurions IRQs.  ICCIAR will report 1023 if there is no pending
   * interrupt.
   */

  DEBUGASSERT(irq < NR_IRQS || irq >= 1022);

  if (irq < NR_IRQS)
    {
      /* Dispatch the interrupt */

      regs = arm64_doirq(irq, regs);
    }

  /* Write to the end-of-interrupt register */

#ifdef CONFIG_ARM_GIC_EOIMODE
  putreg32(regval, GIC_ICCDIR);
#else
  putreg32(regval, GIC_ICCEOIR);
#endif

  return regs;
}

/****************************************************************************
 * Name: up_enable_irq
 *
 * Description:
 *   On many architectures, there are three levels of interrupt enabling: (1)
 *   at the global level, (2) at the level of the interrupt controller,
 *   and (3) at the device level.  In order to receive interrupts, they
 *   must be enabled at all three levels.
 *
 *   This function implements enabling of the device specified by 'irq'
 *   at the interrupt controller level if supported by the architecture
 *   (up_irq_restore() supports the global level, the device level is
 *   hardware specific).
 *
 *   Since this API is not supported on all architectures, it should be
 *   avoided in common implementations where possible.
 *
 ****************************************************************************/

void up_enable_irq(int irq)
{
  /* Ignore invalid interrupt IDs.  Also, in the Cortex-A9 MPCore, SGIs are
   * always enabled. The corresponding bits in the ICDISERn are read as
   * one, write ignored.
   */

  if (irq > GIC_IRQ_SGI15 && irq < NR_IRQS)
    {
      uintptr_t regaddr;

      /* Write '1' to the corresponding bit in the distributor Interrupt
       * Set-Enable Register (ICDISER)
       */

      regaddr = GIC_ICDISER(irq);
      putreg32(GIC_ICDISER_INT(irq), regaddr);
    }
}

/****************************************************************************
 * Name: up_disable_irq
 *
 * Description:
 *   This function implements disabling of the device specified by 'irq'
 *   at the interrupt controller level if supported by the architecture
 *   (up_irq_save() supports the global level, the device level is hardware
 *   specific).
 *
 *   Since this API is not supported on all architectures, it should be
 *   avoided in common implementations where possible.
 *
 ****************************************************************************/

void up_disable_irq(int irq)
{
  /* Ignore invalid interrupt IDs.  Also, in the Cortex-A9 MPCore, SGIs are
   * always enabled. The corresponding bits in the ICDISERn are read as
   * one, write ignored.
   */

  if (irq > GIC_IRQ_SGI15 && irq < NR_IRQS)
    {
      uintptr_t regaddr;

      /* Write '1' to the corresponding bit in the distributor Interrupt
       * Clear-Enable Register (ICDISER)
       */

      regaddr = GIC_ICDICER(irq);
      putreg32(GIC_ICDICER_INT(irq), regaddr);
    }
}

/****************************************************************************
 * Name: up_prioritize_irq
 *
 * Description:
 *   Set the priority of an IRQ.
 *
 *   Since this API is not supported on all architectures, it should be
 *   avoided in common implementations where possible.
 *
 ****************************************************************************/

int up_prioritize_irq(int irq, int priority)
{
  DEBUGASSERT(irq >= 0 && irq < NR_IRQS && priority >= 0 && priority <= 255);

  /* Ignore invalid interrupt IDs */

  if (irq >= 0 && irq < NR_IRQS)
    {
      uintptr_t regaddr;
      uint32_t regval;

      /* Write the new priority to the corresponding field in the in the
       * distributor Interrupt Priority Register (GIC_ICDIPR).
       */

      regaddr = GIC_ICDIPR(irq);
      regval  = getreg32(regaddr);
      regval &= ~GIC_ICDIPR_ID_MASK(irq);
      regval |= GIC_ICDIPR_ID(irq, priority);
      putreg32(regval, regaddr);

      return OK;
    }

  return -EINVAL;
}

/****************************************************************************
 * Name: up_affinity_irq
 *
 * Description:
 *   Set an IRQ affinity by software.
 *
 ****************************************************************************/

void up_affinity_irq(int irq, cpu_set_t cpuset)
{
  if (irq >= GIC_IRQ_SPI && irq < NR_IRQS)
    {
      uintptr_t regaddr;
      uint32_t regval;

      /* Write the new cpuset to the corresponding field in the in the
       * distributor Interrupt Processor Target Register (GIC_ICDIPTR).
       */

      regaddr = GIC_ICDIPTR(irq);
      regval  = getreg32(regaddr);
      regval &= ~GIC_ICDIPTR_ID_MASK(irq);
      regval |= GIC_ICDIPTR_ID(irq, cpuset);
      putreg32(regval, regaddr);
    }
}

/****************************************************************************
 * Name: up_set_irq_type
 *
 * Description:
 *   Set the trigger type for the specified IRQ source and the current CPU.
 *
 *   Since this API is not supported on all architectures, it should be
 *   avoided in common implementations where possible.
 *
 * Input Parameters:
 *   irq  - The interrupt request to modify.
 *   mode - Level sensitive or edge sensitive
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value is returned on any failure.
 *
 ****************************************************************************/

int up_set_irq_type(int irq, int mode)
{
  uintptr_t regaddr;
  uint32_t regval;
  uint32_t intcfg;

  if (!GIC_IS_SGI(irq))
    {
      if (mode == IRQ_HIGH_LEVEL || mode == IRQ_LOW_LEVEL)
        {
          intcfg = INT_ICDICFR_1N;
        }
      else
        {
          intcfg = INT_ICDICFR_EDGE | INT_ICDICFR_1N;
        }

      /* Get the address of the Interrupt Configuration Register for this
       * irq.
       */

      regaddr = GIC_ICDICFR(irq);

      /* Write the correct interrupt trigger to the Interrupt Configuration
       * Register.
       */

      regval  = getreg32(regaddr);
      regval &= ~GIC_ICDICFR_ID_MASK(irq);
      regval |= GIC_ICDICFR_ID(irq, intcfg);
      putreg32(regval, regaddr);

      return OK;
    }

  return -EINVAL;
}

/****************************************************************************
 * Name: arm64_gic_disable_irqs
 *
 * Description:
 *   Disable the secure or the non-secure interrupts and clear their
 *   pending state.  This GIC has no redistributor, so SGI, PPI and SPI
 *   are all configured in the distributor and the two sets are told
 *   apart by their interrupt security registers.
 *
 * Input Parameters:
 *   secure - True to disable the secure interrupts (Group0 and Group1
 *            Secure), false to disable the non-secure ones.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void arm64_gic_disable_irqs(bool secure)
{
  unsigned int nlines = arm_gic_nlines();
  unsigned int irq;

  for (irq = 0; irq < nlines; irq += GIC_NUM_INTR_PER_REG)
    {
      uint32_t isr  = getreg32(GIC_ICDISR(irq));
      uint32_t mask = secure ? ~isr : isr;

      putreg32(mask, GIC_ICDICER(irq));
      putreg32(mask, GIC_ICDICPR(irq));
    }
}

/****************************************************************************
 * Name: arm64_gic_initialize
 *
 * Description:
 *   Initialize GIC. Called by CPU0 only.
 *
 * Input Parameters
 *   None
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value is returned on any failure.
 *
 ****************************************************************************/

int arm64_gic_initialize(void)
{
  int err;

  /* Verify that GIC Version is 2 */

  err = gic_validate_dist_version();
  if (err)
    {
      sinfo("no distributor detected, giving up ret=%d\n", err);
      return err;
    }

  /* CPU0-specific initialization for GIC */

  if (sched_getcpu() == 0)
    {
      arm_gic0_initialize();
    }

  /* CPU-generic initialization for GIC */

  arm64_gic_init();

  return 0;
}

#ifdef CONFIG_SMP

/****************************************************************************
 * Name: arm64_gic_secondary_init
 *
 * Description:
 *   Initialize GIC. Called by all CPUs except CPU0.
 *
 * Input Parameters
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void arm64_gic_secondary_init(void)
{
  /* CPU-generic initialization for GIC */

  arm64_gic_init();
}

#endif /* CONFIG_SMP */

/****************************************************************************
 * Name: up_trigger_irq
 *
 * Description:
 *   Perform a Software Generated Interrupt (SGI).  If CONFIG_SMP is
 *   selected, then the SGI is sent to all CPUs specified in the CPU set.
 *   That set may include the current CPU.
 *
 *   If CONFIG_SMP is not selected, the cpuset is ignored and SGI is sent
 *   only to the current CPU.
 *
 * Input Parameters:
 *   irq    - The SGI interrupt ID (0-15)
 *   cpuset - The set of CPUs to receive the SGI
 *
 * Returned Value:
 *   OK is always returned at present.
 *
 ****************************************************************************/

void up_trigger_irq(int irq, cpu_set_t cpuset)
{
  uint32_t regval;

  if (irq >= 0 && irq <= GIC_IRQ_SGI15)
    {
#ifdef CONFIG_SMP
      regval = GIC_ICDSGIR_INTID(irq) | GIC_ICDSGIR_CPUTARGET(cpuset) |
               GIC_ICDSGIR_TGTFILTER_LIST;
#else
      regval = GIC_ICDSGIR_INTID(irq) | GIC_ICDSGIR_CPUTARGET(0) |
               GIC_ICDSGIR_TGTFILTER_THIS;
#endif

#if defined(CONFIG_ARCH_TRUSTZONE_SECURE)
      if (IGROUPR_SGI_SECURE_VAL & BIT(irq))
#endif
        {
          /* Set NSATT be 1: forward the SGI specified in the SGIINTID field
           * to a specified CPU interfaces only if the SGI is configured as
           * Group 1 on that interface.
           * For non-secure context, the configuration of
           * GIC_ICDSGIR_NSATT_GRP1 is not mandatory in the GICv2
           * specification, but for SMP scenarios,
           * this value needs to be configured, otherwise issues may occur
           * in the SMP scenario.
           */

          regval |= GIC_ICDSGIR_NSATT_GRP1;
        }

      putreg32(regval, GIC_ICDSGIR);
    }
}

/****************************************************************************
 * Name: up_get_legacy_irq
 *
 * Description:
 *   Reserve vector for legacy
 *
 ****************************************************************************/

int up_get_legacy_irq(uint32_t devfn, uint8_t line, uint8_t pin)
{
#if CONFIG_ARM64_GICV2_LEGACY_IRQ0 >= 0
  uint8_t slot;
  uint8_t tmp;

  UNUSED(line);
  slot = PCI_SLOT(devfn);
  tmp = (pin - 1 + slot) % 4;
  return CONFIG_ARM64_GICV2_LEGACY_IRQ0 + tmp;
#else
  return -ENOTSUP;
#endif
}

#endif /* CONFIG_ARM64_GIC_VERSION == 2 */
