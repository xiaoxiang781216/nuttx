/****************************************************************************
 * arch/arm/src/arm_a_r/arm_boot.c
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

#include <nuttx/cache.h>
#include <nuttx/init.h>
#include <nuttx/tls.h>

#include "arm.h"
#include "arm_internal.h"
#include "init/init.h"
#include "scu.h"
#include "smp.h"

#ifdef CONFIG_ARM_MPU
#  include "mpu.h"
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void arm_tp_boot(int cpu)
{
  uintptr_t val;

  val = g_idle_topstack -
        CONFIG_IDLETHREAD_STACKSIZE * (CONFIG_NCPUS - cpu) +
        sizeof(struct tls_info_s);

  CP15_SET(TPIDRURO, val);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: arm_boot_primary
 *
 * Description:
 *   Complete boot sequence for the primary CPU on ARM-A/R platforms.
 *   Called from arm_head.S after MMU is enabled. This function does
 *   not return.
 *
 ****************************************************************************/

void arm_boot_primary(void)
{
#ifdef CONFIG_ARCH_RAMFUNCS
  const uint32_t *src;
  uint32_t *dest;
#endif
#ifdef CONFIG_PERCPU_SECTION
  int cpu = 0;
#endif

  arm_tp_boot(0);
  arm_fpuconfig();

#ifdef CONFIG_ARM_MPU
  mpu_priv_flash((uintptr_t)_stext,
                 (uintptr_t)_etext - (uintptr_t)_stext);
#endif

#ifdef CONFIG_ARCH_PERF_EVENTS
  up_perf_init(0);
#endif

  arm_boot();

#ifdef CONFIG_SMP
  arm_enable_smp(0);
#endif

#ifdef CONFIG_PERCPU_SECTION
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

#ifdef CONFIG_ARCH_RAMFUNCS
  for (src = (const uint32_t *)_framfuncs,
       dest = (uint32_t *)_sramfuncs; dest < (uint32_t *)_eramfuncs;
      )
    {
      *dest++ = *src++;
    }

  up_clean_dcache((uintptr_t)_sramfuncs, (uintptr_t)_eramfuncs);
#endif

#ifdef USE_EARLYSERIALINIT
  arm_earlyserialinit();
#endif

  arm_finish_busy_wait();

#ifdef CONFIG_BUILD_PROTECTED
  arm_userspace();
#endif

  nx_start();
}

/****************************************************************************
 * Name: arm_boot_secondary
 *
 * Description:
 *   Complete boot sequence for secondary CPUs on ARM-A/R SMP/BMP
 *   platforms. Called from arm_cpuhead.S. This function does not return.
 *   Chip-specific initialization is performed by arm_cpu_boot().
 *
 ****************************************************************************/

#if defined(CONFIG_SMP) || defined(CONFIG_BMP)
void arm_boot_secondary(int cpu)
{
  arm_tp_boot(cpu);
  arm_fpuconfig();

#ifdef CONFIG_PERCPU_SECTION
  CP15_SET(TPIDRPRW, PERCPU_OFFSET * cpu);
#  ifdef CONFIG_BUILD_PROTECTED
  CP15_SET(TPIDRURW, USERSPACE->us_offset_percpu * cpu);
#  endif

#  ifdef CONFIG_BMP
  memcpy((void *)((uintptr_t)_sdata_percpu + PERCPU_OFFSET * cpu),
         (void *)_ldata_percpu,
         (uintptr_t)_edata_percpu - (uintptr_t)_sdata_percpu);
  memset((void *)((uintptr_t)_sbss_percpu + PERCPU_OFFSET * cpu),
         0, (uintptr_t)_ebss_percpu - (uintptr_t)_sbss_percpu);
#  endif
#endif

#ifdef CONFIG_ARM_MPU
  mpu_priv_flash((uintptr_t)_stext,
                 (uintptr_t)_etext - (uintptr_t)_stext);
#endif

#ifdef CONFIG_ARCH_PERF_EVENTS
  up_perf_init(0);
#endif

  arm_enable_smp(cpu);

  /* Chip-specific secondary CPU initialization */

  arm_cpu_boot(cpu);

#undef g_nx_initstate
  while (!OSINIT_MM_READY());

  up_irqinitialize();

#ifdef CONFIG_BMP
#  ifdef CONFIG_BUILD_PROTECTED
  arm_userspace();
#  endif
  nx_start();
#else
#  ifdef CONFIG_ARM_HAVE_PTM
  arm_timer_secondary_init(0);
#  endif
  nx_idle_trampoline();
#endif
}
#endif
