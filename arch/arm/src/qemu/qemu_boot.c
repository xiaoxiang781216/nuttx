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
#include <nuttx/init.h>
#include <nuttx/spinlock.h>

#include "arm.h"
#include "arm_internal.h"

#ifdef CONFIG_ARM_PSCI
#  include "arm_cpu_psci.h"
#endif

#include "qemu_irq.h"
#include "qemu_memorymap.h"
#include "qemu_userspace.h"
#include "smp.h"
#include "gic.h"
#include "scu.h"
#ifdef CONFIG_ARM_MPU
#  include "mpu.h"
#endif

#ifdef CONFIG_DEVICE_TREE
#  include <nuttx/fdt.h>
#endif

#ifdef CONFIG_SCHED_INSTRUMENTATION
#  include <sched/sched.h>
#  include <nuttx/sched_note.h>
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
  arm_fpuconfig();

  /* Percpu section init */

#ifdef CONFIG_PERCPU_SECTION
  int cpu = 0;
#  ifdef CONFIG_SMP
  for (; cpu < CONFIG_NCPUS; cpu++)
#  endif
    {
      memcpy((void *)((uintptr_t)_sdata_percpu + PERCPU_OFFSET * cpu),
             (void *)_ldata_percpu,
             (uintptr_t)_edata_percpu - (uintptr_t)_sdata_percpu);
      memset((void *)((uintptr_t)_sbss_percpu + PERCPU_OFFSET * cpu),
             0, (uintptr_t)_ebss_percpu - (uintptr_t)_sbss_percpu);
    }
#endif

#ifdef CONFIG_ARM_MPU
  mpu_priv_flash((uintptr_t)_stext, (uintptr_t)_etext - (uintptr_t)_stext);
#endif

#ifdef CONFIG_ARCH_PERF_EVENTS
  /* Perf init */

  up_perf_init(0);
#endif

#ifdef CONFIG_ARCH_ARMV7A
  /* Set the page table for section */

  qemu_setupmappings();
#endif

#ifdef CONFIG_SMP
  /* Enable SMP cache coherency for CPU0 */

  arm_enable_smp(0);
#endif

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

  arm_finish_busy_wait();

  nx_start();
}

#if defined(CONFIG_ARM_PSCI) && defined(CONFIG_SMP)
int up_cpu_start(int cpu)
{
#ifdef CONFIG_SCHED_INSTRUMENTATION
  /* Notify of the start event */

  sched_note_cpu_start(this_task(), cpu);
#endif

#ifdef CONFIG_ARCH_ADDRENV
  /* Copy cpu0 page table to target cpu. */

  memcpy((uint32_t *)(PGTABLE_BASE_VADDR + PGTABLE_SIZE * cpu),
          (uint32_t *)PGTABLE_BASE_VADDR, PGTABLE_SIZE);
  UP_DSB();
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
