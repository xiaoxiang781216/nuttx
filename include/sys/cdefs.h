/****************************************************************************
 * include/sys/cdefs.h
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

#ifndef __INCLUDE_SYS_CDEFS_H
#define __INCLUDE_SYS_CDEFS_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

/* Include the toolchain's sys/cdefs.h (e.g. picolibc) only when
 * cross-compiling for NuttX.  Skip on host builds (ABI compliance
 * checks) where #include_next would pull in glibc headers that
 * conflict with NuttX types.
 */

#ifdef __NuttX__
#  include_next <sys/cdefs.h>
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef __BEGIN_DECLS
#  ifdef __cplusplus
#    define __BEGIN_DECLS extern "C" {
#    define __END_DECLS   }
#  else
#    define __BEGIN_DECLS
#    define __END_DECLS
#  endif
#endif

#endif /* __INCLUDE_SYS_CDEFS_H */
