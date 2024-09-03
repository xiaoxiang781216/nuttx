/****************************************************************************
 * arch/arm/src/qemu/qemu_cpuboot.c
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

#include <nuttx/arch.h>
#include <nuttx/sched.h>
#include <nuttx/init.h>
#include <arch/irq.h>

#include "init/init.h"
#include "arm_internal.h"
#include "sctlr.h"
#include "scu.h"
#include "gic.h"
#ifdef CONFIG_ARM_MPU
#  include "mpu.h"
#endif
#include "smp.h"
#include "qemu_userspace.h"

/* Symbols defined via the linker script */

extern uint8_t _vector_start[]; /* Beginning of vector block */

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: arm_cpu_boot
 *
 * Description:
 *   Continues the C-level initialization started by the assembly language
 *   __cpu[n]_start function.  At a minimum, this function needs to
 *   initialize interrupt handling and, perhaps, wait on WFI for
 *   arm_cpu_start() to issue an SGI.
 *
 *   This function must be provided by the each ARMv7-A MCU and implement
 *   MCU-specific initialization logic.
 *
 * Input Parameters:
 *   cpu - The CPU index.  This is the same value that would be obtained by
 *      calling this_cpu();
 *
 * Returned Value:
 *   Does not return.
 *
 ****************************************************************************/

void arm_cpu_boot(int cpu)
{
  /* Initialize the FPU */

  arm_fpuconfig();

#ifdef CONFIG_ARM_MPU
  mpu_priv_flash((uintptr_t)_stext, (uintptr_t)_etext - (uintptr_t)_stext);
#endif

#ifdef CONFIG_ARCH_PERF_EVENTS
  /* Perf init */

  up_perf_init(0);
#endif

  /* Enable SMP cache coherency for the CPU */

  arm_enable_smp(cpu);

#ifdef CONFIG_PERCPU_SECTION
  memcpy((void *)((uintptr_t)_sdata_percpu + PERCPU_OFFSET * cpu),
         (void *)_ldata_percpu,
         (uintptr_t)_edata_percpu - (uintptr_t)_sdata_percpu);
  memset((void *)(uintptr_t)_sbss_percpu + PERCPU_OFFSET * cpu,
         0, (uintptr_t)_ebss_percpu - (uintptr_t)_sbss_percpu);
#endif

  /* Initialize the Generic Interrupt Controller (GIC) for CPUn (n != 0) */

  up_irqinitialize();

  /* Then transfer control to the IDLE task */

#ifdef CONFIG_BMP
#  undef g_nx_initstate
  while (OSINIT_OS_INITIALIZING());

#  ifdef CONFIG_BUILD_PROTECTED
  qemu_userspace();
#  endif

  nx_start();
#else

  /* Only SMP need secondary timer, for bmp cpu-N use timer as main timer */

  arm_timer_secondary_init(0);

  nx_idle_trampoline();
#endif
}
