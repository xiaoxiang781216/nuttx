/****************************************************************************
 * include/fuse_kernel.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to you under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
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

#ifndef __INCLUDE_FUSE_KERNEL_H
#define __INCLUDE_FUSE_KERNEL_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

/* Compatibility shim: upstream software includes <fuse_kernel.h> to reach
 * the kernel wire protocol definitions.  On NuttX those live in
 * <nuttx/fuse.h>; this header simply forwards so that unmodified sources
 * compile in libc FUSE mode.
 */

#include <nuttx/fuse.h>

#endif /* __INCLUDE_FUSE_KERNEL_H */
