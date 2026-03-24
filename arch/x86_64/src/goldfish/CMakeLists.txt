# ##############################################################################
# arch/x86_64/src/goldfish/CMakeLists.txt
#
# SPDX-License-Identifier: Apache-2.0
#
# Licensed to the Apache Software Foundation (ASF) under one or more contributor
# license agreements.  See the NOTICE file distributed with this work for
# additional information regarding copyright ownership.  The ASF licenses this
# file to you under the Apache License, Version 2.0 (the "License"); you may not
# use this file except in compliance with the License.  You may obtain a copy of
# the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
# License for the specific language governing permissions and limitations under
# the License.
#
# ##############################################################################

set(PLATFORM_FLAGS)

if(CONFIG_ARCH_ARM7TDMI)
  list(APPEND PLATFORM_FLAGS -mcpu=arm7tdmi)
  set(LLVM_CPUTYPE arm7tdmi)
elseif(CONFIG_ARCH_ARM920T)
  list(APPEND PLATFORM_FLAGS -mcpu=arm920t)
  set(LLVM_CPUTYPE arm920t)
elseif(CONFIG_ARCH_ARM926EJS)
  list(APPEND PLATFORM_FLAGS -mtune=arm926ej-s -march=armv5te)
  set(LLVM_CPUTYPE arm926ej-s)
elseif(CONFIG_ARCH_ARM1136J)
  list(APPEND PLATFORM_FLAGS -mcpu=arm1136j-s)
  set(LLVM_CPUTYPE arm1136j-s)
elseif(CONFIG_ARCH_ARM1156T2)
  list(APPEND PLATFORM_FLAGS -mcpu=arm1156t2-s)
  set(LLVM_CPUTYPE arm1156t2-s)
elseif(CONFIG_ARCH_ARM1176JZ)
  list(APPEND PLATFORM_FLAGS -mcpu=arm1176jz-s)
  set(LLVM_CPUTYPE arm1176jz-s)
endif()

set(LLVM_ARCHTYPE arm)
set(LLVM_ABITYPE eabi)

list(APPEND PLATFORM_FLAGS -mfloat-abi=soft)
