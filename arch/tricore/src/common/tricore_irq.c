/****************************************************************************
 * arch/tricore/src/common/tricore_irq.c
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

#include <stdint.h>
#include <assert.h>
#include <debug.h>

#include <nuttx/arch.h>
#include <nuttx/irq.h>

#include "tricore_internal.h"

#include "IfxSrc.h"
#include "IfxCpu.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#ifdef CONFIG_ARCH_HAVE_IRQTRIGGER
/****************************************************************************
 * Name: tricore_gpsrinitialize
 *
 * Description:
 *   Perform gpsr initialization for the CPU
 *
 ****************************************************************************/

static void tricore_gpsrinitialize(void)
{
  volatile Ifx_SRC_SRCR *src = &SRC_GPSR0_SR0 + up_cpu_index();
  int i;

  for (i = 0; i < TRICORE_GPSR_NUM; i++)
    {
#ifdef CONFIG_ARCH_CHIP_AURIX_TC3XX
      IfxSrc_init(src, IfxSrc_Tos_cpu0 + up_cpu_index(),
                  IRQ_TO_NDX(TRICORE_SRC2IRQ(src)));
#else
      IfxSrc_init(src, IfxSrc_Tos_cpu0 + up_cpu_index(),
                  IRQ_TO_NDX(TRICORE_SRC2IRQ(src)),
                  IfxSrc_VmId_none);
#endif

      src += TRICORE_SRCNUM_PER_GPSR;
    }
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_irq_enable
 *
 * Description:
 *   Enable interrupts globally.
 *
 ****************************************************************************/

void up_irq_enable(void)
{
  IfxCpu_enableInterrupts();
}

/****************************************************************************
 * Name: up_irqinitialize
 ****************************************************************************/

void up_irqinitialize(void)
{
#ifdef CONFIG_ARCH_HAVE_IRQTRIGGER
  tricore_gpsrinitialize();
#endif

  up_irq_enable();
}

/****************************************************************************
 * Name: up_disable_irq
 *
 * Description:
 *   Disable the IRQ specified by 'irq'
 *
 ****************************************************************************/

void up_disable_irq(int irq)
{
  volatile Ifx_SRC_SRCR *src = &SRC_CPU_CPU0_SB + up_cpu_index() + irq;

  IfxSrc_disable(src);
}

/****************************************************************************
 * Name: up_enable_irq
 *
 * Description:
 *   Enable the IRQ specified by 'irq'
 *
 ****************************************************************************/

void up_enable_irq(int irq)
{
  volatile Ifx_SRC_SRCR *src = &SRC_CPU_CPU0_SB + up_cpu_index() + irq;

#ifdef CONFIG_ARCH_CHIP_AURIX_TC3XX
  IfxSrc_init(src, IfxSrc_Tos_cpu0 + up_cpu_index(), IRQ_TO_NDX(irq));
#else
  IfxSrc_init(src, IfxSrc_Tos_cpu0 + up_cpu_index(), IRQ_TO_NDX(irq),
              IfxSrc_VmId_none);
#endif
  IfxSrc_enable(src);
}

/****************************************************************************
 * Name: tricore_ack_irq
 *
 * Description:
 *   Acknowledge the IRQ
 *
 ****************************************************************************/

void tricore_ack_irq(int irq)
{
  volatile Ifx_SRC_SRCR *src = &SRC_CPU_CPU0_SB + up_cpu_index() + irq;
  IfxSrc_clearRequest(src);
}

#ifdef CONFIG_ARCH_HAVE_IRQTRIGGER
/****************************************************************************
 * Name: up_trigger_irq
 *
 * Description:
 *   Trigger an IRQ by software. May not be supported by all architectures.
 *
 ****************************************************************************/

void up_trigger_irq(int irq, cpu_set_t cpuset)
{
  volatile Ifx_INT_SRB *srb = &INT_SRB0 + up_cpu_index();
  srb->U = cpuset;
}
#endif