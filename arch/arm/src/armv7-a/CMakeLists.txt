# ##############################################################################
# arch/arm/src/armv7-a/CMakeLists.txt
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

# Common C source files

list(APPEND SRCS arm_mmu.c)

if(CONFIG_LEGACY_PAGING)
  list(APPEND SRCS arm_pginitialize.c arm_va2pte.c arm_pghead.S)
else()
  list(APPEND SRCS arm_head.S)
endif()

if(CONFIG_ARCH_TRUSTZONE_SECURE)
  list(APPEND SRCS arm_sm_runtime.c arm_sm_vector.S)
endif()

if(CONFIG_ARCH_ADDRENV)
  list(
    APPEND
    SRCS
    arm_addrenv.c
    arm_addrenv_utils.c
    arm_addrenv_perms.c
    arm_pgalloc.c
    arm_addrenv_pgmap.c)
  if(CONFIG_ARCH_STACK_DYNAMIC)
    list(APPEND SRCS arm_addrenv_ustack.c)
  endif()
  if(CONFIG_ARCH_KERNEL_STACK)
    list(APPEND SRCS arm_addrenv_kstack.c)
  endif()
  if(CONFIG_ARCH_VMA_MAPPING)
    list(APPEND SRCS arm_addrenv_shm.c)
  endif()
endif()

if(CONFIG_MM_PGALLOC)
  list(APPEND SRCS arm_physpgaddr.c)
  if(CONFIG_ARCH_PGPOOL_MAPPING)
    list(APPEND SRCS arm_virtpgaddr.c)
  endif()
endif()

if(CONFIG_ARM_PSCI)
  list(APPEND SRCS arm_cpu_psci.c arm_smccc.S)
endif()

target_sources(arch PRIVATE ${SRCS})
