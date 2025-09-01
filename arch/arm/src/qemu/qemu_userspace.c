/****************************************************************************
 * arch/arm/src/qemu/qemu_userspace.c
 *
 * SPDX-License-Identifier: Apache-2.0
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
#include <sys/param.h>
#include <nuttx/userspace.h>
#include <arch/barriers.h>

#include <arch/board/board_memorymap.h>

#include "mpu.h"
#include "qemu_userspace.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: qemu_userspace
 *
 * Description:
 *   For the case of the separate user-/kernel-space build, perform whatever
 *   platform specific initialization of the user memory is required.
 *   Normally this just means initializing the user space .data and .bss
 *   segments.
 *
 * Assumptions:
 *   The D-Cache has not yet been enabled.
 *
 ****************************************************************************/

void qemu_userspace(void)
{
  uint8_t   *src;
  uint8_t   *start;
  uint8_t   *end;
  uintptr_t  datastart;
  uintptr_t  dataend;

#ifdef CONFIG_PERCPU_SECTION
  uintptr_t  offset = USERSPACE->us_offset_percpu * this_cpu();
#endif

  if (this_cpu() == 0)
    {
      /* Clear all of user-space .bss */

      DEBUGASSERT(USERSPACE->us_bssstart != 0 &&
                  USERSPACE->us_bssend != 0 &&
                  USERSPACE->us_bssstart <= USERSPACE->us_bssend);

      start = (uint8_t *)USERSPACE->us_bssstart;
      end   = (uint8_t *)USERSPACE->us_bssend;

      memset(start, 0, end - start);

      /* Initialize all of user-space .data */

      DEBUGASSERT(USERSPACE->us_datasource != 0 &&
                  USERSPACE->us_datastart != 0 &&
                  USERSPACE->us_dataend != 0 &&
                  USERSPACE->us_datastart <= USERSPACE->us_dataend);

      src   = (uint8_t *)USERSPACE->us_datasource;
      start = (uint8_t *)USERSPACE->us_datastart;
      end   = (uint8_t *)USERSPACE->us_dataend;

      memcpy(start, src, end - start);

      DEBUGASSERT(USERSPACE->us_textend >= USERSPACE->us_textstart);
    }

#ifdef CONFIG_PERCPU_SECTION
  if (USERSPACE->us_bssend_percpu - USERSPACE->us_bssstart_percpu != 0)
    {
      start = (uint8_t *)USERSPACE->us_bssstart_percpu;
      end   = (uint8_t *)USERSPACE->us_bssend_percpu;
      memset(start + offset, 0, end - start);
    }

  if (USERSPACE->us_dataend_percpu - USERSPACE->us_datastart_percpu != 0)
    {
      src   = (uint8_t *)USERSPACE->us_datasource_percpu;
      start = (uint8_t *)USERSPACE->us_datastart_percpu;
      end   = (uint8_t *)USERSPACE->us_dataend_percpu;
      memcpy(start + offset, src, end - start);
    }

  if (USERSPACE->us_bssend_percpu - USERSPACE->us_datastart_percpu != 0)
    {
      start = (uint8_t *)USERSPACE->us_datastart_percpu;
      end   = (uint8_t *)USERSPACE->us_bssend_percpu;
#ifdef CONFIG_BMP
      start += offset;
      end   += offset;
#else
      /* When SMP able to access all percpu-data by all cpu */

      end   += USERSPACE->us_offset_percpu * CONFIG_NCPUS;
#endif

      mpu_user_intsram((uintptr_t)start, end - start);
    }
#endif

  DEBUGASSERT(USERSPACE->us_textend >= USERSPACE->us_textstart);

  datastart = MIN(USERSPACE->us_datastart, USERSPACE->us_bssstart);
  dataend   = MAX(USERSPACE->us_dataend,   USERSPACE->us_bssend);

#ifdef CONFIG_SMP
  if (this_cpu() != 0)
    {
      /* When SMP core-N will not do heap init again. */

      dataend = USRAM_END;
    }
#endif

  DEBUGASSERT(dataend >= datastart);

  /* Configure user FLASH and SRAM spaces */

  mpu_user_intsram(datastart, dataend - datastart);

  mpu_showtype();
  mpu_user_flash(UFLASH_START, UFLASH_SIZE);
  mpu_control(true);
}
