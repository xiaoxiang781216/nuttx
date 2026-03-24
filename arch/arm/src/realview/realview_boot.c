/****************************************************************************
 * arch/arm/src/realview/realview_boot.c
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

#include <nuttx/board.h>

#include "chip.h"
#include "arm_internal.h"
#include "arm.h"
#include "mmu.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct section_mapping_s g_section_mapping[] =
{
  /* Peripheral I/O region: 0x10000000, 1 MiB, non-cacheable */

  {
    REALVIEW_PERIPH_PADDR, REALVIEW_PERIPH_VSECTION,
    MMU_IOFLAGS, REALVIEW_PERIPH_NSECTIONS
  },

  /* Ethernet region: 0x4e000000, 1 MiB, non-cacheable */

  {
    REALVIEW_ETH_PADDR, REALVIEW_ETH_VSECTION,
    MMU_IOFLAGS, REALVIEW_ETH_NSECTIONS
  },
};

#define NMAPPINGS (sizeof(g_section_mapping) / sizeof(struct section_mapping_s))

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: realview_setupmappings
 *
 * Description:
 *   Set up additional MMU mappings for the RealView-EB peripheral regions.
 *   The basic SDRAM mapping is already established by arm_head.S.
 *
 ****************************************************************************/

static void realview_setupmappings(void)
{
  uint32_t *pgtable = (uint32_t *)PGTABLE_BASE_VADDR;
  int i;
  int j;

  for (i = 0; i < NMAPPINGS; i++)
    {
      uint32_t sect_paddr = g_section_mapping[i].physbase;
      uint32_t sect_vaddr = g_section_mapping[i].virtbase;
      uint32_t mmuflags   = g_section_mapping[i].mmuflags;

      for (j = 0; j < g_section_mapping[i].nsections; j++)
        {
          pgtable[sect_vaddr >> 20] = (sect_paddr | mmuflags);
          sect_paddr += SECTION_SIZE;
          sect_vaddr += SECTION_SIZE;
        }
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: arm_boot
 *
 * Description:
 *   Complete boot operations after the startup code has initialized the
 *   system.  On MMU-capable cores this adds peripheral mappings; on all
 *   cores it copies the vector block and performs early serial
 *   initialization.
 *
 ****************************************************************************/

void arm_boot(void)
{
#ifdef CONFIG_ARCH_HAVE_MMU
  /* Set up additional MMU mappings for peripheral I/O regions */

  realview_setupmappings();
#endif

#ifdef CONFIG_ARCH_LEDS
  board_autoled_initialize();
#endif

  /* Perform early serial initialization */

#ifdef USE_EARLYSERIALINIT
  arm_earlyserialinit();
#endif
}
