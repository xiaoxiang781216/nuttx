/****************************************************************************
 * arch/arm64/src/imx9/imx9_acrn_boot.c
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
 * ACRN guest (VM0) boot / MMU table for i.MX95
 *
 * When NuttX is booted as an ACRN guest, the chip-level defaults from
 * arch/arm64/include/imx9/chip.h (DDR at 0xa0100000 / 512 MiB, plus PCI
 * DMA/OB windows, FSPI, OCRAM, etc.) do not match the HPA window carved
 * out for the guest by the hypervisor scenario XML. Rather than add
 * per-board #ifndef overrides in chip.h, this file provides a dedicated
 * g_mmu_regions[] / g_mmu_config and a minimal arm64_chip_boot() that
 * skip all chip-level hardware bring-up (DDR training, TRDC, MIX
 * power-up, native LPUART setup) and cover only what the guest actually
 * sees:
 *
 *   - DEVICE_REGION at 0x40000000 (512 MiB) — covers the vuart (0x44380000
 *     / 0x44390000) and the emulated GIC distributor (0x48000000) and
 *     redistributor (0x48060000).
 *   - DRAM0_S0       at 0x91000000 (240 MiB) — matches <size_hpa> for
 *     VM0 in partitioned-imx95-2c.xml (HV leaves [0x91000000, 0x91000000+
 *     240 MiB) for the NuttX guest; kern_load_addr = 0x91280000).
 *
 * The file is compiled in place of imx9_boot.c when
 * CONFIG_ARCH_BOARD_IMX95_A55_ACRN is selected (see CMakeLists.txt /
 * Make.defs). The two are mutually exclusive to keep g_mmu_regions /
 * g_mmu_config / arm64_chip_boot symbols unique.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <arch/chip/chip.h>

#include "arm64_internal.h"
#include "arm64_mmu.h"

#include "imx9_boot.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* HPA window assigned to VM0 by the ACRN scenario XML.
 * Keep in sync with <start_hpa>/<size_hpa> in partitioned-imx95*.xml.
 *
 * HV RAM occupies [HV_RAM_START, HV_RAM_START + 28 MiB) = [0x90400000,
 * 0x92000000). VM0's HPA must start at or above 0x92000000 to avoid
 * overlap with the hypervisor's dynamic allocations (EPT page pool,
 * PPT pages, bitmaps, etc.).
 */

#define IMX95_ACRN_VM_RAM_ADDR   0x92000000UL
#define IMX95_ACRN_VM_RAM_SIZE   MB(224)

/* Device I/O window covering the emulated peripherals the guest touches:
 * vuart (0x44380000 / 0x44390000), GICD (0x48000000), GICR (0x48060000).
 * 0x40000000 + 512 MiB comfortably wraps all of them.
 */

#define IMX95_ACRN_DEVICE_ADDR   0x40000000UL
#define IMX95_ACRN_DEVICE_SIZE   MB(512)

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct arm_mmu_region g_mmu_regions[] =
{
  MMU_REGION_FLAT_ENTRY("DEVICE_REGION",
                        IMX95_ACRN_DEVICE_ADDR, IMX95_ACRN_DEVICE_SIZE,
                        MT_DEVICE_NGNRNE | MT_RW | MT_SECURE),

  MMU_REGION_FLAT_ENTRY("DRAM0_S0",
                        IMX95_ACRN_VM_RAM_ADDR, IMX95_ACRN_VM_RAM_SIZE,
                        MT_NORMAL | MT_RW | MT_SECURE),
};

const struct arm_mmu_config g_mmu_config =
{
  .num_regions = nitems(g_mmu_regions),
  .mmu_regions = g_mmu_regions,
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/

#ifdef CONFIG_SMP

/****************************************************************************
 * Name: arm64_get_mpid
 *
 * Description:
 *   Map a NuttX cpu index to an mpidr_el1 value.
 *
 *   Note: ACRN exposes vCPUs with vcpu_id packed into Aff0 rather than
 *   into the Aff1 cluster field that the bare-metal i.MX95 EVK uses.
 *   See hypervisor/arch/aarch64/guest/vcpu.c:
 *       vmpidr_el2 = (read_mpidr_el1() & MPIDR_SMP_MT_MASK) | vcpu_id;
 *   and hypervisor/arch/aarch64/guest/vsmc.c PSCI_FN_CPU_ON handling
 *   (target_vcpu_id = mpidr & 0xFF).
 *
 *   So the guest mapping is aff_level=0, not aff_level=1.
 *
 ****************************************************************************/

uint64_t arm64_get_mpid(int cpu)
{
  return CORE_TO_MPID(cpu, 0);
}

/****************************************************************************
 * Name: arm64_get_cpuid
 *
 * Description:
 *   Inverse of arm64_get_mpid(). Extract Aff0 directly rather than
 *   going through the chip-level MPID_TO_CORE() macro, which the
 *   i.MX95 chip header overrides to read Aff1 (matching EVK bare
 *   metal, not the ACRN guest layout).
 *
 ****************************************************************************/

int arm64_get_cpuid(uint64_t mpid)
{
  return (int)((mpid >> MPIDR_AFF0_SHIFT) & MPIDR_AFFLVL_MASK);
}

#endif /* CONFIG_SMP */

/****************************************************************************
 * Name: arm64_el_init
 *
 * Description:
 *   Called very early from arm64_head.S. As an ACRN guest NuttX runs at
 *   EL1 (ACRN itself owns EL2), so there is nothing EL3-specific to do;
 *   cntfrq_el0 is inherited from the hypervisor's virtual timer setup.
 *
 ****************************************************************************/

void arm64_el_init(void)
{
}

/****************************************************************************
 * Name: arm64_chip_boot
 *
 * Description:
 *   Complete boot operations started in arm64_head.S.
 *
 *   In the ACRN guest there is no DDR training, no TRDC/MIX power-up, no
 *   native LPUART pin muxing — ACRN has already prepared all of that and
 *   the console is an emulated PL011 vuart. Just invoke the board-level
 *   hook so the port has a chance to do any guest-side init.
 *
 ****************************************************************************/

void arm64_chip_boot(void)
{
  imx9_board_initialize();
}
