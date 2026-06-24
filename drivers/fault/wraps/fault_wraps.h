/****************************************************************************
 * drivers/fault/wraps/fault_wraps.h
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
 * Unified fault wrapper symbol list.
 *
 * All sub-module wrap symbols are collected here, guarded by their
 * respective CONFIG_DRIVERS_FAULT_INJECT_WRAP_XXX macros.  The build
 * system only needs to preprocess this single file to produce one
 * fault_wrapsymbol.cmd — no per-module .cmd files required.
 *
 * When adding a new fault wrapper module:
 *   1. Add a new #ifdef CONFIG_DRIVERS_FAULT_INJECT_WRAP_XXX section below
 *   2. List the symbols with UP_WRAPOPT(symbol)
 *   3. Add CONFIG option in drivers/fault/Kconfig
 *   4. Add source file in drivers/fault/wraps/ and update Make.defs/CMake
 *   No changes to tools/Config.mk, tools/Unix.mk, or root CMakeLists.txt
 *   are needed.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifndef UP_WRAPOPT
#define UP_WRAPOPT(f) --wrap=f
#endif

WRAPOPTSTARTS
