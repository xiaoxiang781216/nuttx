# ##############################################################################
# arch/arm/src/cmake/arm_a_r.cmake
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

# Unified ARM A/R cmake toolchain definitions

set(PLATFORM_FLAGS)

# ##############################################################################
# ARMv7-A
# ##############################################################################

if(CONFIG_ARCH_ARMV7A)

  if(CONFIG_ARCH_CORTEXA5)
    list(APPEND PLATFORM_FLAGS -mcpu=cortex-a5)
    set(LLVM_CPUTYPE cortex-a5)
  elseif(CONFIG_ARCH_CORTEXA7)
    list(APPEND PLATFORM_FLAGS -mcpu=cortex-a7)
    set(LLVM_CPUTYPE cortex-a7)
  elseif(CONFIG_ARCH_CORTEXA8)
    list(APPEND PLATFORM_FLAGS -mcpu=cortex-a8)
    set(LLVM_CPUTYPE cortex-a8)
  elseif(CONFIG_ARCH_CORTEXA9)
    list(APPEND PLATFORM_FLAGS -mcpu=cortex-a9)
    set(LLVM_CPUTYPE cortex-a9)
  endif()

  if(CONFIG_ARM_THUMB)
    set(LLVM_ARCHTYPE thumbv7a)
  else()
    set(LLVM_ARCHTYPE armv7-a)
  endif()

  if(CONFIG_ARCH_FPU)
    set(LLVM_ABITYPE eabihf)
  else()
    set(LLVM_ABITYPE eabi)
  endif()

  if(NOT CONFIG_ARM_DPFPU32)
    set(ARCHFPUD16 -d16)
  endif()

  # ~~~
  # | Cortex | FPU Option 1       | FPU Option 2           | FPU Option 3          |
  # |--------|--------------------|------------------------|-----------------------|
  # | A5     | -mfpu=vfpv4-fp16   | -mfpu=vfpv4-d16-fp16   | -mfpu=neon-fp16       |
  # | A7     | -mfpu=vfpv4        | -mfpu=vfpv4-d16        | -mfpu=neon-vfpv4      |
  # | A8     | -mfpu=vfpv3        |                        | -mfpu=neon (alias for neon-vfpv3) |
  # | A9     | -mfpu=vfpv3-fp16   | -mfpu=vfpv3-d16-fp16   | -mfpu=neon-fp16       |
  # | A15    | -mfpu=vfpv4        |                        | -mfpu=neon-vfpv4      |
  # ~~~

  if(CONFIG_ARCH_FPU)
    if(CONFIG_ARM_FPU_ABI_SOFT)
      list(APPEND PLATFORM_FLAGS -mfloat-abi=softfp)
    else()
      list(APPEND PLATFORM_FLAGS -mfloat-abi=hard)
    endif()

    if(CONFIG_ARM_NEON)
      set(ARCHNEON neon-)
    endif()
    if(CONFIG_ARCH_CORTEXA8)
      set(ARCHFPU vfpv3)
    elseif(CONFIG_ARCH_CORTEXA9)
      set(ARCHFPU vfpv3)
    else()
      set(ARCHFPU vfpv4)
    endif()

    list(APPEND PLATFORM_FLAGS -mfpu=${ARCHNEON}${ARCHFPU}${ARCHFPUD16})

  else()
    list(APPEND PLATFORM_FLAGS -mfloat-abi=soft)
  endif()

  # ############################################################################
  # ARMv7-R
  # ############################################################################

elseif(CONFIG_ARCH_ARMV7R)

  if(CONFIG_ARCH_CORTEXR4)
    list(APPEND PLATFORM_FLAGS -mcpu=cortex-r4)
    set(LLVM_CPUTYPE cortex-r4)
  elseif(CONFIG_ARCH_CORTEXR5)
    list(APPEND PLATFORM_FLAGS -mcpu=cortex-r5)
    set(LLVM_CPUTYPE cortex-r5)
  elseif(CONFIG_ARCH_CORTEXR7)
    list(APPEND PLATFORM_FLAGS -mcpu=cortex-r7)
    set(LLVM_CPUTYPE cortex-r7)
  endif()

  if(CONFIG_ARM_THUMB)
    set(LLVM_ARCHTYPE thumbv7r)
  else()
    set(LLVM_ARCHTYPE armv7r)
  endif()

  if(NOT CONFIG_ARM_DPFPU32)
    set(ARCHFPUD16 -d16)
  endif()

  if(CONFIG_ARCH_FPU)
    set(LLVM_ABITYPE eabihf)
    if(CONFIG_ARM_FPU_ABI_SOFT)
      list(APPEND PLATFORM_FLAGS -mfloat-abi=softfp)
    else()
      list(APPEND PLATFORM_FLAGS -mfloat-abi=hard)
    endif()

    if(CONFIG_ARM_NEON)
      set(ARCHNEON neon-)
    endif()
    list(APPEND PLATFORM_FLAGS -mfpu=${ARCHNEON}vfpv3${ARCHFPUD16})

  else()
    set(LLVM_ABITYPE eabi)
    list(APPEND PLATFORM_FLAGS -mfloat-abi=soft)
  endif()

  # ############################################################################
  # ARMv8-R
  # ############################################################################

elseif(CONFIG_ARCH_ARMV8R)

  set(LLVM_CPUTYPE cortex-r52)

  if(CONFIG_ARM_THUMB)
    set(LLVM_ARCHTYPE thumbv8r)
  else()
    set(LLVM_ARCHTYPE armv8r)
  endif()

  if(CONFIG_ARCH_HAVE_DPFPU)
    list(APPEND PLATFORM_FLAGS -march=armv8-r)
  else()
    list(APPEND PLATFORM_FLAGS -march=armv8-r+fp.sp)
  endif()

  if(CONFIG_ARCH_FPU)
    set(LLVM_ABITYPE eabihf)

    if(CONFIG_ARM_NEON)
      list(APPEND PLATFORM_FLAGS -mfpu=neon-fp-armv8)
    elseif(CONFIG_ARCH_HAVE_DPFPU)
      list(APPEND PLATFORM_FLAGS -mfpu=fp-armv8)
    else()
      list(APPEND PLATFORM_FLAGS -mfpu=fpv5-sp-d16)
    endif()
    if(CONFIG_ARM_FPU_ABI_SOFT)
      list(APPEND PLATFORM_FLAGS -mfloat-abi=softfp)
    else()
      list(APPEND PLATFORM_FLAGS -mfloat-abi=hard)
    endif()
  else()
    set(LLVM_ABITYPE eabi)
    list(APPEND PLATFORM_FLAGS -mfloat-abi=soft)
  endif()

  # ############################################################################
  # Legacy ARM (ARM7TDMI, ARM9, ARM11, etc.)
  # ############################################################################

else()

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

endif()

if(CONFIG_ARCH_INSTRUMENT_ALL AND NOT CONFIG_ARM_STACKCHECK)
  list(APPEND PLATFORM_FLAGS -finstrument-functions)
endif()

add_compile_options(${PLATFORM_FLAGS})
