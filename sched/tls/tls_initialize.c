/****************************************************************************
 * sched/tls/tls_initialize.c
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

#ifdef CONFIG_BUILD_PROTECTED
#  include <nuttx/userspace.h>
#endif

#include "tls.h"

#ifdef CONFIG_BUILD_FLAT
extern uint8_t _ertonly[];
extern uint8_t _stdata[];
extern uint8_t _etdata[];
extern uint8_t _stbss[];
extern uint8_t _etbss[];
#elif defined(CONFIG_BUILD_PROTECTED)
#  define _ertonly ((FAR uint8_t *)USERSPACE->us_tls_datasource)
#  define _stdata ((FAR uint8_t *)USERSPACE->us_tls_datastart)
#  define _etdata ((FAR uint8_t *)USERSPACE->us_tls_dataend)
#  define _stbss ((FAR uint8_t *)USERSPACE->us_tls_bssstart)
#  define _etbss ((FAR uint8_t *)USERSPACE->us_tls_bssend)
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: tls_size
 *
 * Description:
 *
 *   Get TLS size, including struct tls_info_s, thread local section (tdata
 *   and tbss), plus arch specific offset/paddings around the thread local
 *   section.
 *
 *   Arch should define:
 *   - ARCH_TP_OFFSET for the gap between tls_info_s and _stdata
 *   - ARCH_TP_PADDING for the padding after thread local section (e.g. x64)
 *
 * Returned Value:
 *
 *   Size of thread local data (normally below stack_base_ptr).
 *
 ****************************************************************************/

size_t tls_size(void)
{
  return sizeof(struct tls_info_s) +
         ARCH_TP_OFFSET + _etbss - _stdata + ARCH_TP_PADDING;
}

/****************************************************************************
 * Name: tls_initialize
 *
 * Description:
 *   Initialize thread local region.
 *
 * Input Parameters:
 *   info - The TLS structure to initialize.
 *
 * Note:
 *  Must be called after allocating the TLS region. In that case, the tbss
 *  area is initialized to be zeroed.
 *
 ****************************************************************************/

void tls_initialize(struct tls_info_s *info)
{
  uint8_t *tls_data = (uint8_t *)(info + 1) + ARCH_TP_OFFSET;
  uint32_t tdata_len = _etdata - _stdata;
  memcpy(tls_data, _ertonly, tdata_len);
}
