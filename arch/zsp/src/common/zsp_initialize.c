/****************************************************************************
 * arch/zsp/src/common/zsp_initialize.c
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
#include <stdint.h>

#include "zsp_internal.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void up_initialize(void)
{
}

/****************************************************************************
 * Name: zsp_data_initialize
 *
 * Description:
 *   Copy .data from load address (_eronly) to exec address (_sdata.._edata).
 *   Only needed when running from flash (XIP) where .data load address
 *   differs from exec address. Called from head.S before nx_start.
 *
 ****************************************************************************/

#ifdef CONFIG_BOOT_RUNFROMFLASH
extern uint32_t _eronly;
extern uint32_t _sdata;
extern uint32_t _edata;

void zsp_data_initialize(void)
{
  const uint32_t *src = &_eronly;
  uint32_t *dest = &_sdata;

  while (dest < &_edata)
    {
      *dest++ = *src++;
    }
}
#endif
