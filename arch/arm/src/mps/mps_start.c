/****************************************************************************
 * arch/arm/src/mps/mps_start.c
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

#include <nuttx/binfmt/elf_fixup.h>
#include <nuttx/cache.h>
#include <nuttx/init.h>
#include <arch/barriers.h>
#include <arch/board/board.h>

#include "hardware/mps_memorymap.h"
#include "arm_internal.h"
#include "nvic.h"
#include "mps_irq.h"
#include "mps_userspace.h"
#include "mpu.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

#define HEAP_BASE ((uintptr_t)_ebss + CONFIG_IDLETHREAD_STACKSIZE)
#define MPS_PERIPH_BASE    0x40000000
#define MPS_PERIPH_SIZE    0x20000000

/****************************************************************************
 * Public Data
 ****************************************************************************/

/* g_idle_topstack: _sbss is the start of the BSS region as defined by the
 * linker script. _ebss lies at the end of the BSS region. The idle task
 * stack starts at the end of BSS and is of size CONFIG_IDLETHREAD_STACKSIZE.
 * The IDLE thread is the thread that the system boots on and, eventually,
 * becomes the IDLE, do nothing task that runs only when there is nothing
 * else to run.  The heap continues from there until the end of memory.
 * g_idle_topstack is a read-only variable the provides this computed
 * address.
 */

const uintptr_t g_idle_topstack = HEAP_BASE;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: mps_tcmenable
 *
 * Description:
 *   Enable/disable tightly coupled memories.  Size of tightly coupled
 *   memory regions is controlled by GPNVM Bits 7-8.
 *
 ****************************************************************************/

static inline void mps_tcmenable(void)
{
  uint32_t regval;

  UP_MB();

  /* Enabled/disabled ITCM */

#ifdef CONFIG_ARMV7M_ITCM
  regval  = NVIC_TCMCR_EN | NVIC_TCMCR_RMW | NVIC_TCMCR_RETEN;
#else
  regval  = getreg32(NVIC_ITCMCR);
  regval &= ~NVIC_TCMCR_EN;
#endif
  putreg32(regval, NVIC_ITCMCR);

  /* Enabled/disabled DTCM */

#ifdef CONFIG_ARMV7M_DTCM
  regval  = NVIC_TCMCR_EN | NVIC_TCMCR_RMW | NVIC_TCMCR_RETEN;
#else
  regval  = getreg32(NVIC_DTCMCR);
  regval &= ~NVIC_TCMCR_EN;
#endif
  putreg32(regval, NVIC_DTCMCR);

  UP_MB();
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: __start
 *
 * Description:
 *   This is the reset entry point.
 *
 ****************************************************************************/

void __start(void)
{
#ifndef CONFIG_BUILD_PIC
  const uint32_t *src;
  uint32_t *dest;
#endif

  /* If enabled reset the MPU */

  mpu_early_reset();
#ifdef CONFIG_ARM_MPU
  mpu_showtype();
  mpu_priv_flash((uintptr_t)_stext, (uintptr_t)_etext - (uintptr_t)_stext);
#endif
  arm_fpuconfig();

  /* If used the PIC, then the PIC will have already been configured */

#ifndef CONFIG_BUILD_PIC

  /* Set bss to zero */

#ifndef CONFIG_ARCH_SKIP_ZERO_BSS
  for (dest = (uint32_t *)_sbss; dest < (uint32_t *)_ebss; )
    {
      *dest++ = 0;
    }
#endif

  /* Copy the program from FLASH to RAM. */

  for (src = (const uint32_t *)_eronly,
       dest = (uint32_t *)_sdata; dest < (uint32_t *)_edata;
      )
    {
      *dest++ = *src++;
    }
#endif

  /* Perform early serial initialization */

#ifdef USE_EARLYSERIALINIT
  arm_earlyserialinit();
#endif

  /* Enable/disable tightly coupled memories */

  mps_tcmenable();

#ifdef CONFIG_ARMV7M_DCACHE
  /* Memory barrier */

  UP_DMB();

#endif

#ifdef CONFIG_BUILD_PROTECTED
  /* For the case of the separate user-/kernel-space build, perform whatever
   * platform specific initialization of the user memory is required.
   * Normally this just means initializing the user space .data and .bss
   * segments.
   */

  mps_userspace();
#endif

#ifdef CONFIG_ARM_MPU
  /* Then enable the MPU */

  mpu_control(true, false, true);
#endif

#if defined(CONFIG_ARCH_ADDRENV) && defined(CONFIG_BUILD_FLAT)
  mpu_control(false, false, false);
  mpu_priv_intsram((uintptr_t)_sdata,
                   elf_fixup_ramstart() - (uintptr_t)_sdata);
  mpu_priv_intsram((uintptr_t)g_intstackalloc, CONFIG_ARCH_INTERRUPTSTACK);
  mpu_user_flash((uintptr_t)CONFIG_ELF_FIXUP_FLASH_START,
                 CONFIG_ELF_FIXUP_FLASH_SIZE);
  mpu_peripheral(MPS_PERIPH_BASE, MPS_PERIPH_SIZE);
  mpu_control(true, false, false);
#endif

  /* Enable I- and D-Caches */

  up_enable_icache();
  up_enable_dcache();

  /* Then start NuttX */

  nx_start();

  /* Shouldn't get here */

  for (; ; );
}
