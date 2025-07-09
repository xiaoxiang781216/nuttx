/****************************************************************************
 * arch/x86_64/src/intel64/intel64_hwp.c
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

#include <nuttx/arch.h>
#include <nuttx/irq.h>

#include "x86_64_internal.h"
#include "intel64_hwp.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* MSR_IA32_HWP_CAPABILITIES: 0x771h R/W
 * HWP Performance Range Enumeration.
 * Required CPUID.06H:EAX[7] == 1
 */

typedef union
{
  struct
    {
      uint8_t highest_perf_lvl    : 8;  /* 0:7 Highest_Performance */
      uint8_t guaranteed_perf_lvl : 8;  /* 8:15 Guaranteed_Performance */
      uint8_t efficient_perf_lvl  : 8;  /* 16:23 Most_Efficient_Performance */
      uint8_t lowest_perf_pref    : 8;  /* 24:31 Lowest_Performance */
      uint32_t reserved           : 32; /* 32:63 Reserved */
    };
  uint64_t reg_val;
} intel64_ia32_hwp_capability_reg_t;

/* MSR_IA32_HWP_REQEUST_PKG: 0x772h R/W
 * Power Management Control Hints for All Logical Processors in a Package.
 * Required CPUID.06H:EAX[7] == 1
 *
 * MSR_IA32_HWP_REQEUST: 0x774h R/W
 * Power Management Control Hints to a Logical Processor.
 * Required CPUID.06H:EAX[7] == 1
 */

typedef union
{
  struct
    {
      uint8_t min_perf_lvl     : 8;  /* 0:7 Minimum_Performance */
      uint8_t max_perf_lvl     : 8;  /* 8:15 Maximum_Performance */
      uint8_t desired_perf_lvl : 8;  /* 16:23 Desired_Performance */
      uint8_t energy_perf_pref : 8;  /* 24:31 Energy_Performance_Preference */
      uint16_t activity_window : 10; /* 32:41 Activity_Window */
      uint8_t package_control  : 1;  /* 42 Package_Control */
      uint32_t reserved        : 21; /* 43:63 Reserved */
    };
  uint64_t reg_val;
} intel64_ia32_hwp_request_reg_t;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void intel64_hwp_init(void)
{
  if (x86_64_hwp_is_available())
    {
      intel64_ia32_hwp_capability_reg_t cap;

      /* Enable HWP if available. */

      write_msr(MSR_IA32_PM_ENABLE, 1);

      /* Get performance level from HWP capability */

      cap.reg_val = read_msr(MSR_IA32_HWP_CAPABILITIES);

      /* Set performance level
       * 0x80: Balanced E-P Preference | Min Perf Level | Max Perf Level
       * | 0x0: Desired Perf Level Auto
       */

      intel64_hwp_setfreq(0x80, cap.lowest_perf_pref, cap.highest_perf_lvl,
                          0x0);
    }
}

void intel64_hwp_setfreq(uint8_t energy_pref, uint8_t min_lvl,
                         uint8_t max_lvl, uint8_t desired_lvl)
{
  intel64_ia32_hwp_request_reg_t reg;

  if (x86_64_hwp_is_available())
    {
      /* Make sure that reserved part is zeroed */

      reg.reg_val = 0;

      /* Energy Performance Preferences:
       * 0x0: Performance <--> 0xFF: Energy Efficient
       * 0x80: Balanced
       */

      reg.energy_perf_pref = energy_pref;

      /* Desired Performance level:
       * 0x0 : Auto
       */

      reg.desired_perf_lvl = desired_lvl;
      reg.max_perf_lvl = max_lvl;
      reg.min_perf_lvl = min_lvl;

      write_msr(MSR_IA32_HWP_REQUEST, reg.reg_val);
    }
}
