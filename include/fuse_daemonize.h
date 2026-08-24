/****************************************************************************
 * include/fuse_daemonize.h
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

#ifndef __INCLUDE_FUSE_DAEMONIZE_H
#define __INCLUDE_FUSE_DAEMONIZE_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdbool.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define FUSE_DAEMONIZE_NO_CHDIR      (1 << 0)
#define FUSE_DAEMONIZE_NO_BACKGROUND (1 << 1)

/****************************************************************************
 * Inline Functions
 ****************************************************************************/

static inline int fuse_daemonize_early_start(unsigned int flags)
{
  return 0;
}

static inline void fuse_daemonize_early_success(void)
{
}

static inline void fuse_daemonize_early_fail(int err)
{
}

static inline bool fuse_daemonize_early_is_active(void)
{
  return false;
}

#endif /* __INCLUDE_FUSE_DAEMONIZE_H */
