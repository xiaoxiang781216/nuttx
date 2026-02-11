# ##############################################################################
# arch/arm/src/armv7-r/CMakeLists.txt
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

# The vector table

target_sources(nuttx PRIVATE arm_vectortab.S)

# Common assembly language files

list(APPEND SRCS arm_head.S arm_saveusercontext.S)

# Common C source files

list(APPEND SRCS arm_cpuinfo.c cp15_cacheops.c)

if(CONFIG_BUILD_PROTECTED)
  list(APPEND SRCS arm_mpu.c)
endif()

if(CONFIG_ARMV7R_L2CC_PL310)
  list(APPEND SRCS arm_l2cc_pl310.c)
endif()

if(CONFIG_ARCH_FPU)
  list(APPEND SRCS arm_fpucmp.c arm_fpuconfig.S)
endif()

if(CONFIG_SMP)
  list(APPEND SRCS arm_cpuhead.S arm_cpustart.c arm_smpcall.c)
endif()

if(CONFIG_ARCH_ADDRENV)
  list(APPEND SRCS arm_addrenv.c)
endif()

target_sources(arch PRIVATE ${SRCS})
