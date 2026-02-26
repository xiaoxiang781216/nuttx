/****************************************************************************
 * arch/tricore/src/common/tricore_cpucall.c
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

#include <nuttx/arch.h>

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_send_cpu_call
 *
 * Description:
 *   Send cpu call to target cpu.
 *
 * Input Parameters:
 *   cpuset - The set of CPUs to receive the service request.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

void up_send_cpu_call(cpu_set_t cpuset)
{
  volatile Ifx_SRC_SRCR *src;
  int i;

  for (i = 0; i < CONFIG_NCPUS; i++)
    {
      if (CPU_ISSET(i, &cpuset))
        {
          src = TRICORE_CPUCALL_SRC(i);
          src->B.SETR = 1;
        }
    }
}
