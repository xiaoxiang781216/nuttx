# ##############################################################################
# libs/libc/machine/arm/CMakeLists.txt
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

set(SRCS)

if(CONFIG_ARCH_ARM_A_R)
  add_subdirectory(arm_a_r)
elseif(CONFIG_ARCH_ARM_M) # All ARM-M
  add_subdirectory(arm_m)
endif()

if(CONFIG_LIBC_ARCH_ELF)
  list(APPEND SRCS arch_elf.c)
endif()

list(APPEND SRCS aeabi_memclr.c aeabi_memclr4.c aeabi_memclr8.c)
list(APPEND SRCS aeabi_memcpy.c aeabi_memcpy4.c aeabi_memcpy8.c)
list(APPEND SRCS aeabi_memmove.c aeabi_memmove4.c aeabi_memmove8.c)
list(APPEND SRCS aeabi_memset.c aeabi_memset4.c aeabi_memset8.c)

if(NOT CONFIG_LIBSUPCXX_TOOLCHAIN)
  list(APPEND SRCS arch_atexit.c)
endif()

if(CONFIG_ARCH_TOOLCHAIN_GHS)
  list(APPEND SRCS arch_dummy.c)
endif()

if(CONFIG_CXX_EXCEPTION)
  list(APPEND SRCS arch_unwind_find_exidx.c)
endif()

if(CONFIG_ARCH_SETJMP_H)
  if(CONFIG_ARCH_CHIP_TLSR82)
    list(APPEND SRCS tc32_setjmp.S)
  else()
    list(APPEND SRCS arch_setjmp.S)
  endif()
endif()

if(CONFIG_PROFILE_MINI)
  list(APPEND SRCS arch_mcount.S)
endif()

if(CONFIG_LIBC_ARCH_CRC32)
  list(APPEND SRCS arch_crc32.c)
endif()

if(CONFIG_SCHED_THREAD_LOCAL AND CONFIG_ARCH_ARM_M)
  list(APPEND SRCS arm_tls.c)
endif()

target_sources(c PRIVATE ${SRCS})
