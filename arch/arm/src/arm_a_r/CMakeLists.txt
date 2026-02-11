# ##############################################################################
# arch/arm/src/arm_a_r/CMakeLists.txt
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

target_sources(nuttx PRIVATE arm_vectortab.S)

set(CSRCS arm_vectors.S arm_saveusercontext.S)

if(NOT CONFIG_DISABLE_SIGNALS)
  list(APPEND CSRCS arm_schedulesigaction.c arm_sigdeliver.c)
endif()

if(CONFIG_ARCH_HAVE_DEBUG)
  list(APPEND CSRCS arm_hwdebug.c)
endif()

if(CONFIG_LIB_SYSCALL)
  list(APPEND CSRCS arm_dispatch_syscall.S)
endif()

if(CONFIG_PAGING)
  list(APPEND CSRCS arm_checkmapping.c)
endif()

if(CONFIG_ARM_HAVE_PTM)
  list(APPEND CSRCS arm_timer.c)
endif()

if(CONFIG_ARM_GIC_VERSION EQUAL 2)
  list(APPEND CSRCS arm_gicv2.c)
  if(CONFIG_ARM_GICv2M)
    list(APPEND CSRCS arm_gicv2m.c)
  endif()
endif()

if(CONFIG_ARCH_HIPRI_INTERRUPT)
  list(APPEND CSRCS arm_dofiq.c)
endif()

if(NOT CONFIG_UP)
  list(APPEND CSRCS arm_scu.c arm_smpcall.c arm_cpustart.c)
endif()

list(
  APPEND
  CSRCS
  arm_tcbinfo.c
  arm_doirq.c
  arm_dataabort.c
  arm_syscall.c
  arm_cpuidlestack.c
  arm_initialstate.c
  arm_undefinedinsn.c
  arm_allocpage.c
  arm_prefetchabort.c
  arm_gicv2_dump.c
  arm_cache.c
  arm_cpuinfo.c
  cp15_cacheops.c
  arm_perf.c)

target_sources(arch PRIVATE ${CSRCS})

if(NOT CONFIG_BUILD_FLAT AND CONFIG_ARCH_PERF_EVENTS_USER_ACCESS)
  target_sources(arch_interface PRIVATE arm_perf.c)
  target_include_directories(arch_interface PRIVATE ${NUTTX_CHIP_ABS_DIR})
endif()
