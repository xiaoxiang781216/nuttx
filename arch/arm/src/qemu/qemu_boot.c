/****************************************************************************
 * arch/arm/src/qemu/qemu_boot.c
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

#include "arm_internal.h"

#ifdef CONFIG_ARM_PSCI
#  include "arm_cpu_psci.h"
#endif

#include "qemu_irq.h"
#include "qemu_memorymap.h"
#include "qemu_userspace.h"
#include "smp.h"
#include "gic.h"

#ifdef CONFIG_DEVICE_TREE
#  include <nuttx/fdt.h>
#endif

#ifdef CONFIG_SCHED_INSTRUMENTATION
#  include <sched/sched.h>
#  include <nuttx/sched_note.h>
#endif

#ifndef CONFIG_ARCH_ARMV7A
#  include <nuttx/init.h>
#endif

#ifdef CONFIG_ARCH_ARMV8R
#  include "sctlr.h"
#endif

#include <nuttx/syslog/syslog_rpmsg.h>

/****************************************************************************
 * Preprocessor Definitions
 ****************************************************************************/

/****************************************************************************
 * Private Data
 ****************************************************************************/

#ifdef CONFIG_SYSLOG_RPMSG
static char g_syslog_rpmsg_buf[4096];
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: arm_boot
 *
 * Description:
 *   Complete boot operations started in arm_head.S
 *
 ****************************************************************************/

void arm_boot(void)
{
#ifdef CONFIG_ARCH_PERF_EVENTS
  /* Perf init */

  up_perf_init(0);
#endif

#ifdef CONFIG_ARCH_ARMV7A
  /* Set the page table for section */

  qemu_setupmappings();
#endif

  arm_fpuconfig();

#ifdef CONFIG_ARM_PSCI
  arm_psci_init("hvc");
#endif

#ifdef CONFIG_DEVICE_TREE
  fdt_register((const char *)0x40000000);
#endif

#ifdef USE_EARLYSERIALINIT
  /* Perform early serial initialization if we are going to use the serial
   * driver.
   */

  arm_earlyserialinit();
#endif

#ifdef CONFIG_SYSLOG_RPMSG
  syslog_rpmsg_init_early(g_syslog_rpmsg_buf, sizeof(g_syslog_rpmsg_buf));
#endif

#ifdef CONFIG_BUILD_PROTECTED
  qemu_userspace();
#endif

#ifndef CONFIG_ARCH_ARMV7A
  /* dont return per armv7-r/arm_head.S for armv7-r and armv8-r */

  nx_start();
#endif
}

#if defined(CONFIG_ARM_PSCI) && defined(CONFIG_SMP)
int up_cpu_start(int cpu)
{
#ifdef CONFIG_SCHED_INSTRUMENTATION
  /* Notify of the start event */

  sched_note_cpu_start(this_task(), cpu);
#endif

  return psci_cpu_on(cpu, (uintptr_t)__start);
}
#endif

#ifdef CONFIG_ARCH_ARMV8R
/****************************************************************************
 * Name: arm_el_init
 *
 * Description:
 *   The function called from arm_head.S at very early stage to:
 *   - Handling special hardware initialize routine which is need to
 *     run at high ELs
 *   - Initialize system software such as hypervisor or security firmware
 *     which is need to run at high ELs
 *
 ****************************************************************************/

void arm_el_init(void)
{
#if CONFIG_ARM_GIC_VERSION > 2
  CP15_SET(ICC_HSRE, ICC_SRE_ELX_SRE_BIT | ICC_SRE_ELX_DFB_BIT |
                     ICC_SRE_ELX_DIB_BIT | ICC_SRE_EL3_EN_BIT);

  UP_ISB();
#endif
}
#endif
