# ##############################################################################
# arch/arm/src/cmake/arm_m.cmake
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

# ============================================================================
# ARMv6-M
# ============================================================================

if(CONFIG_ARCH_ARMV6M)

  # LLVM
  set(LLVM_ARCHTYPE thumbv6m)
  set(LLVM_CPUTYPE cortex-m0)

  # Platform flags
  if(CONFIG_ARCH_TOOLCHAIN_GHS)
    list(APPEND PLATFORM_FLAGS -cpu=cortexm0)
  else()
    list(APPEND PLATFORM_FLAGS -mcpu=cortex-m0 -mthumb)
  endif()

  # ============================================================================
  # ARMv7-M
  # ============================================================================

elseif(CONFIG_ARCH_ARMV7M)

  # LLVM
  if(CONFIG_ARCH_CORTEXM3)
    set(LLVM_ARCHTYPE thumbv7m)
    set(LLVM_CPUTYPE cortex-m3)
  else()
    set(LLVM_ARCHTYPE thumbv7em)
    if(CONFIG_ARCH_CORTEXM4)
      set(LLVM_CPUTYPE cortex-m4)
    elseif(CONFIG_ARCH_CORTEXM7)
      set(LLVM_CPUTYPE cortex-m7)
    endif()
  endif()

  # Platform flags
  if(CONFIG_ARCH_TOOLCHAIN_GHS)

    if(CONFIG_ARCH_CORTEXM4)
      list(APPEND PLATFORM_FLAGS -cpu=cortexm4)
      if(CONFIG_ARCH_FPU)
        if(CONFIG_ARCH_DPFPU)
          list(APPEND PLATFORM_FLAGS -fpu=vfpv4)
        else()
          list(APPEND PLATFORM_FLAGS -fpu=vfpv4_d16)
        endif()
      endif()
    elseif(CONFIG_ARCH_CORTEXM7)
      list(APPEND PLATFORM_FLAGS -cpu=cortexm7)
      if(CONFIG_ARCH_FPU)
        if(CONFIG_ARCH_DPFPU)
          list(APPEND PLATFORM_FLAGS -fpu=vfpv5)
        else()
          list(APPEND PLATFORM_FLAGS -fpu=vfpv5_d16)
        endif()
      endif()
    else()
      list(APPEND PLATFORM_FLAGS -cpu=cortexm3)
    endif()

  else() # GCC / Clang / ARM Clang

    if(CONFIG_ARCH_CORTEXM4)
      list(APPEND PLATFORM_FLAGS -mtune=cortex-m4 -march=armv7e-m)
      if(CONFIG_ARCH_FPU)
        list(APPEND PLATFORM_FLAGS -mfpu=fpv4-sp-d16)
      endif()
    elseif(CONFIG_ARCH_CORTEXM7)
      list(APPEND PLATFORM_FLAGS -mtune=cortex-m7 -march=armv7e-m)
      if(CONFIG_ARCH_FPU)
        if(CONFIG_ARCH_DPFPU)
          list(APPEND PLATFORM_FLAGS -mfpu=fpv5-d16)
        else()
          list(APPEND PLATFORM_FLAGS -mfpu=fpv5-sp-d16)
        endif()
      endif()
    else()
      list(APPEND PLATFORM_FLAGS -mtune=cortex-m3 -march=armv7-m)
    endif()

    # Clang configuration files
    if(CONFIG_ARCH_TOOLCHAIN_CLANG)
      set(ARCHFLAGS)

      if(CONFIG_ARCH_CORTEXM4)
        if(CONFIG_ARCH_FPU)
          string(APPEND ARCHFLAGS "--config armv7em_hard_fpv4_sp_d16.cfg")
        else()
          string(APPEND ARCHFLAGS "--config armv7em_soft_nofp.cfg")
        endif()
      elseif(CONFIG_ARCH_CORTEXM7)
        if(CONFIG_ARCH_FPU)
          string(APPEND ARCHFLAGS "--config armv7em_hard_fpv5.cfg")
        else()
          string(APPEND ARCHFLAGS "--config armv7em_soft_nofp.cfg")
        endif()
      else()
        string(APPEND ARCHFLAGS "--config armv7em_soft_nofp.cfg")
      endif()

      if(NOT "${CMAKE_C_FLAGS}" STREQUAL "" AND NOT "${ARCHFLAGS}" STREQUAL "")
        string(REGEX MATCH "${ARCHFLAGS}" EXISTS_FLAGS "${CMAKE_C_FLAGS}")
      endif()

      if(NOT EXISTS_FLAGS)
        set(CMAKE_ASM_FLAGS
            "${CMAKE_ASM_FLAGS} ${ARCHFLAGS}"
            CACHE STRING "" FORCE)
        set(CMAKE_C_FLAGS
            "${CMAKE_C_FLAGS} ${ARCHFLAGS}"
            CACHE STRING "" FORCE)
        set(CMAKE_CXX_FLAGS
            "${CMAKE_CXX_FLAGS} ${ARCHFLAGS}"
            CACHE STRING "" FORCE)
      endif()
    endif()

  endif() # GHS vs others

  # ============================================================================
  # ARMv8-M
  # ============================================================================

elseif(CONFIG_ARCH_ARMV8M)

  # LLVM
  if(CONFIG_ARCH_CORTEXM23)
    set(LLVM_ARCHTYPE thumbv8m.base)
    set(LLVM_CPUTYPE cortex-m23)
  elseif(CONFIG_ARCH_CORTEXM33)
    set(LLVM_ARCHTYPE thumbv8m.main)
    set(LLVM_CPUTYPE cortex-m33)
  elseif(CONFIG_ARCH_CORTEXM35P)
    set(LLVM_ARCHTYPE thumbv8m.main)
    set(LLVM_CPUTYPE cortex-m35p)
  elseif(CONFIG_ARCH_CORTEXM55)
    set(LLVM_ARCHTYPE thumbv8.1m.main)
    set(LLVM_CPUTYPE cortex-m55)
  elseif(CONFIG_ARCH_CORTEXM85)
    set(LLVM_ARCHTYPE thumbv8.1m.main)
    set(LLVM_CPUTYPE cortex-m85)
  endif()

  # Platform flags
  if(CONFIG_ARM_DSP)
    set(EXTCPUFLAGS +dsp)
  endif()

  if(CONFIG_ARCH_TOOLCHAIN_GHS)

    if(CONFIG_ARCH_CORTEXM23)
      list(APPEND PLATFORM_FLAGS -cpu=cortexm23)
    elseif(CONFIG_ARCH_CORTEXM33)
      list(APPEND PLATFORM_FLAGS -cpu=cortexm33)
      if(CONFIG_ARCH_FPU)
        list(APPEND PLATFORM_FLAGS -fpu=fpv5_d16)
      endif()
    elseif(CONFIG_ARCH_CORTEXM35P)
      list(APPEND PLATFORM_FLAGS -cpu=cortexm35p)
      if(CONFIG_ARCH_FPU)
        list(APPEND PLATFORM_FLAGS -fpu=fpv5_d16)
      endif()
    elseif(CONFIG_ARCH_CORTEXM55)
      list(APPEND PLATFORM_FLAGS -cpu=cortexm55)
      if(CONFIG_ARCH_FPU)
        list(APPEND PLATFORM_FLAGS -fpu=fpv5)
      endif()
    elseif(CONFIG_ARCH_CORTEXM85)
      list(APPEND PLATFORM_FLAGS -cpu=cortexm85)
      if(CONFIG_ARCH_FPU)
        list(APPEND PLATFORM_FLAGS -fpu=fpv5)
      endif()
    endif()

  else() # GCC / Clang / ARM Clang

    if(CONFIG_ARCH_CORTEXM23)
      list(APPEND PLATFORM_FLAGS -mtune=cortex-m23 -march=armv8-m.main)
    elseif(CONFIG_ARCH_CORTEXM33)
      list(APPEND PLATFORM_FLAGS -mtune=cortex-m33
           -march=armv8-m.main${EXTCPUFLAGS})
      if(CONFIG_ARCH_FPU)
        list(APPEND PLATFORM_FLAGS -mfpu=fpv5-sp-d16)
      endif()
    elseif(CONFIG_ARCH_CORTEXM35P)
      list(APPEND PLATFORM_FLAGS -mtune=cortex-m35p
           -march=armv8-m.main${EXTCPUFLAGS})
      if(CONFIG_ARCH_FPU)
        list(APPEND PLATFORM_FLAGS -mfpu=fpv5-sp-d16)
      endif()
    elseif(CONFIG_ARCH_CORTEXM55)
      list(APPEND PLATFORM_FLAGS -mtune=cortex-m55)
      if(CONFIG_ARM_HAVE_MVE)
        list(APPEND PLATFORM_FLAGS
             -march=armv8.1-m.main${EXTCPUFLAGS}+mve.fp+fp.dp)
      else()
        list(APPEND PLATFORM_FLAGS -march=armv8.1-m.main${EXTCPUFLAGS})
      endif()
      if(CONFIG_ARCH_FPU)
        list(APPEND PLATFORM_FLAGS -mfpu=fpv5-d16)
      endif()
    elseif(CONFIG_ARCH_CORTEXM85)
      list(APPEND PLATFORM_FLAGS -mtune=cortex-m85)
      if(CONFIG_ARM_HAVE_MVE)
        list(APPEND PLATFORM_FLAGS
             -march=armv8.1-m.main${EXTCPUFLAGS}+mve.fp+fp.dp)
      else()
        list(APPEND PLATFORM_FLAGS -march=armv8.1-m.main${EXTCPUFLAGS})
      endif()
      if(CONFIG_ARCH_FPU)
        list(APPEND PLATFORM_FLAGS -mfpu=fpv5-d16)
      endif()
    endif()

  endif() # GHS vs others

  if(CONFIG_ARMV8M_CMSE)
    list(APPEND PLATFORM_FLAGS -mcmse)
  endif()

endif()

# ============================================================================
# Common: LLVM ABI type
# ============================================================================

if(CONFIG_ARCH_FPU)
  set(LLVM_ABITYPE eabihf)
else()
  set(LLVM_ABITYPE eabi)
endif()

# ============================================================================
# Common: Float ABI
# ============================================================================

if(CONFIG_ARCH_TOOLCHAIN_GHS)
  if(CONFIG_ARCH_FPU)
    if(CONFIG_ARM_FPU_ABI_SOFT)
      list(APPEND PLATFORM_FLAGS -fsoft)
    elseif(CONFIG_ARCH_DPFPU)
      list(APPEND PLATFORM_FLAGS -fhard)
    else()
      list(APPEND PLATFORM_FLAGS -fsingle)
    endif()
  else()
    list(APPEND PLATFORM_FLAGS -fsoft)
  endif()
else()
  if(CONFIG_ARCH_FPU)
    if(CONFIG_ARM_FPU_ABI_SOFT)
      list(APPEND PLATFORM_FLAGS -mfloat-abi=softfp)
    else()
      list(APPEND PLATFORM_FLAGS -mfloat-abi=hard)
    endif()
  else()
    list(APPEND PLATFORM_FLAGS -mfloat-abi=soft)
  endif()
endif()

# ============================================================================
# Common: Stack check and instrumentation
# ============================================================================

if(CONFIG_ARMV7M_STACKCHECK OR CONFIG_ARMV8M_STACKCHECK)
  list(APPEND PLATFORM_FLAGS -finstrument-functions -ffixed-r10)
endif()

if(CONFIG_ARCH_INSTRUMENT_ALL
   AND NOT CONFIG_ARMV7M_STACKCHECK
   AND NOT CONFIG_ARMV8M_STACKCHECK)
  list(APPEND PLATFORM_FLAGS -finstrument-functions)
endif()

add_compile_options(${PLATFORM_FLAGS})
