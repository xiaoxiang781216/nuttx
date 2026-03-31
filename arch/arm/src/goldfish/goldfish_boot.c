/****************************************************************************
 * arch/arm/src/goldfish/goldfish_boot.c
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
#include "arm_cpu_psci.h"

#include "goldfish_irq.h"
#include "goldfish_memorymap.h"
#include "smp.h"
#include "gic.h"
#include "scu.h"

#ifdef CONFIG_DEVICE_TREE
#  include <nuttx/fdt.h>
#endif

#ifdef CONFIG_SCHED_INSTRUMENTATION
#  include <sched/sched.h>
#  include <nuttx/sched_note.h>
#endif

#include <nuttx/syslog/syslog_rpmsg.h>

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
  /* Set the page table for section */

  goldfish_setupmappings();

#ifdef CONFIG_ARM_PSCI
  arm_psci_init("smc");
#endif

#ifdef CONFIG_DEVICE_TREE
  fdt_register((const char *)0x40000000);
#endif
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
