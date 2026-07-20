/****************************************************************************
 * boards/boardctl_mcal.c
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

#include <debug.h>
#include <errno.h>

#include <nuttx/board.h>
#include <nuttx/cache.h>
#include <sys/boardctl.h>

#include "Mcu.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Private Types
 ****************************************************************************/

/****************************************************************************
 * Private Data
 ****************************************************************************/

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: board_reset_cause
 *
 * Description:
 *   Get the cause of the last board reset. This function wraps the MCAL
 *   Mcu_GetResetReason() API to provide NuttX-compatible reset cause
 *   information.
 *
 * Input Parameters:
 *   cause - Pointer to structure to receive the reset cause information.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure.
 *
 ****************************************************************************/

#ifdef CONFIG_BOARDCTL_RESET_CAUSE
int board_reset_cause(FAR struct boardioc_reset_cause_s *cause)
{
  Mcu_ResetType resettype;

  /* Validate input parameter */

  if (cause == NULL)
    {
      return -EINVAL;
    }

  resettype = Mcu_GetResetReason();

  switch (resettype)
    {
#ifdef MCU_POWER_ON_RESET
      case MCU_POWER_ON_RESET:

        /* Power-On Reset */

        cause->cause = BOARDIOC_RESETCAUSE_SYS_CHIPPOR;
        break;
#endif

#ifdef MCU_SW_RESET
      case MCU_SW_RESET:

        /* Software Reset */

        cause->cause = BOARDIOC_RESETCAUSE_CORE_SOFT;
        break;
#endif

#ifdef MCU_WATCHDOG_RESET
      case MCU_WATCHDOG_RESET:

        /* Internal Watchdog Timer Reset */

        cause->cause = BOARDIOC_RESETCAUSE_CORE_MWDT;
        break;
#endif

      default:

        /* Unknown or undefined reset reason */

        cause->cause = BOARDIOC_RESETCAUSE_UNKOWN;
        break;
    }

  _info("Reset cause: %d (MCAL: %lu)\n", cause->cause, resettype);

  return OK;
}
#endif /* CONFIG_BOARDCTL_RESET_CAUSE */

/****************************************************************************
 * Name: board_reset
 *
 * Description:
 *   Reset the board. This function wraps the MCAL Mcu_PerformReset() API
 *   to provide NuttX-compatible board reset functionality.
 *
 * Input Parameters:
 *   status - Status information provided with the reset event. This
 *            meaning of this status information is board-specific.
 *
 * Returned Value:
 *   If this function returns, then it was not possible to reset the
 *   board due to some constraints. The return value in this case is a
 *   board-specific reason for the failure to reset.
 *
 ****************************************************************************/

#ifdef CONFIG_BOARDCTL_RESET
int board_reset(int status)
{
  _info("Board reset requested with status: %d\n", status);

#ifdef CONFIG_ARCH_DCACHE
  /* Flush any pending data before reset */

  up_flush_dcache_all();
#endif

  /* Perform the reset using MCAL API. */

  Mcu_PerformReset();

  while (1)
    {
      /* Wait for reset to complete */
    }

  return 0;
}
#endif /* CONFIG_BOARDCTL_RESET */
