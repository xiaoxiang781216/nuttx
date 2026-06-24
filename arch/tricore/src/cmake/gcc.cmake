# ##############################################################################
# arch/tricore/src/cmake/Toolchain.cmake
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

# Toolchain

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_VERSION 1)

set(ARCH_SUBDIR chip)
include(${ARCH_SUBDIR})

set(CROSSDEV tricore-elf-)
string(REGEX REPLACE "-$" "" CROSSDEV_TARGET ${CROSSDEV})
set(CMAKE_LIBRARY_ARCHITECTURE ${CROSSDEV_TARGET})
set(CMAKE_C_COMPILER_TARGET ${CROSSDEV_TARGET})
set(CMAKE_CXX_COMPILER_TARGET ${CROSSDEV_TARGET})

set(CMAKE_ASM_COMPILER ${CROSSDEV}gcc)
set(CMAKE_C_COMPILER ${CMAKE_ASM_COMPILER})
set(CMAKE_CXX_COMPILER ${CROSSDEV}g++)
set(CMAKE_STRIP ${CROSSDEV}strip --strip-unneeded)
set(CMAKE_OBJCOPY ${CROSSDEV}objcopy)
set(CMAKE_OBJDUMP ${CROSSDEV}objdump)
set(CMAKE_ASM_GENERATION_TOOL hldumparm)

set(CMAKE_LINKER ${CROSSDEV}gcc)
set(CMAKE_LD ${CROSSDEV}gcc)
set(CMAKE_AR ${CROSSDEV}gcc-ar)
set(CMAKE_NM ${CROSSDEV}gcc-nm)
set(CMAKE_RANLIB ${CROSSDEV}gcc-ranlib)

# override the ARCHIVE command

set(CMAKE_C_ARCHIVE_CREATE "<CMAKE_AR> rcs <TARGET> <LINK_FLAGS> <OBJECTS>")
set(CMAKE_CXX_ARCHIVE_CREATE "<CMAKE_AR> rcs <TARGET> <LINK_FLAGS> <OBJECTS>")
set(CMAKE_ASM_ARCHIVE_CREATE "<CMAKE_AR> rcs <TARGET> <LINK_FLAGS> <OBJECTS>")

# Architecture flags

add_compile_options(-fno-common)
add_compile_options(-Wall -Wshadow -Wundef)
add_compile_options(-nostdlib)
add_link_options(-nostdlib)

if(CONFIG_DEBUG_CUSTOMOPT)
  add_compile_options(${CONFIG_DEBUG_OPTLEVEL})
elseif(CONFIG_DEBUG_FULLOPT)
  add_compile_options(-Os)
endif()

if(NOT CONFIG_DEBUG_NOOPT)
  add_compile_options(-fno-strict-aliasing)
endif()

if(CONFIG_FRAME_POINTER)
  add_compile_options(-fno-omit-frame-pointer)
else()
  add_compile_options(-fomit-frame-pointer)
endif()

if(CONFIG_STACK_CANARIES)
  add_compile_options(-fstack-protector-all)
endif()

if(CONFIG_STACK_USAGE)
  add_compile_options(-fstack-usage)
endif()

if(CONFIG_STACK_USAGE_WARNING AND NOT "${CONFIG_STACK_USAGE_WARNING}" STREQUAL
                                  "0")
  add_compile_options(-Wstack-usage=${CONFIG_STACK_USAGE_WARNING})
endif()

if(CONFIG_COVERAGE_ALL)
  if(CONFIG_COVERAGE_SOURCE)
    add_compile_options(-fprofile-arcs -ftest-coverage -fno-inline)
  elseif(CONFIG_COVERAGE_PGO)
    file(GLOB_RECURSE GCDA_FILES "${CMAKE_BINARY_DIR}/*.gcda")
    if(GCDA_FILES)
      add_compile_options(-fprofile-use)
    else()
      add_compile_options(-fprofile-generate -fprofile-update=atomic)
    endif()
  endif()
endif()

# Instrumentation options

if(CONFIG_ARCH_INSTRUMENT_ALL)
  add_compile_options(-finstrument-functions)
endif()

# Optimization of unused sections

if(CONFIG_DEBUG_OPT_UNUSED_SECTIONS)
  add_link_options(-Wl,--gc-sections)
  add_compile_options(-ffunction-sections -fdata-sections)
endif()

if(CONFIG_ENDIAN_BIG)
  add_compile_options(-mbig-endian)
endif()

# Link Time Optimization

if(CONFIG_LTO_THIN)
  add_compile_options(-flto=thin)
elseif(CONFIG_LTO_FULL)
  add_compile_options(-flto)
  if(CONFIG_ARCH_TOOLCHAIN_GNU)
    add_compile_options(-fno-builtin)
    add_compile_options(-fuse-linker-plugin)
  endif()
endif()

set(NO_LTO "-fno-lto")

# Debug link map

if(CONFIG_DEBUG_LINK_MAP)
  add_link_options(-Wl,--cref -Wl,-Map=nuttx.map)
endif()

if(CONFIG_DEBUG_SYMBOLS)
  add_compile_options(${CONFIG_DEBUG_SYMBOLS_LEVEL})
endif()

add_link_options(-Wl,--no-warn-rwx-segments)

if(CONFIG_MM_UBSAN_ALL)
  add_compile_options(${CONFIG_MM_UBSAN_OPTION})
endif()

if(CONFIG_MM_UBSAN_TRAP_ON_ERROR)
  add_compile_options(-fsanitize-undefined-trap-on-error)
endif()

if(CONFIG_MM_KASAN_INSTRUMENT_ALL)
  add_compile_options(-fsanitize=kernel-address)
  add_compile_options(--param=asan-stack=0)
  add_compile_options(--param=asan-instrumentation-with-call-threshold=0)
endif()

if(CONFIG_MM_KASAN_GLOBAL)
  add_compile_options(--param=asan-globals=1)
endif()

if(CONFIG_MM_KASAN_DISABLE_READS_CHECK)
  add_compile_options(--param=asan-instrument-reads=0)
endif()

if(CONFIG_MM_KASAN_DISABLE_WRITES_CHECK)
  add_compile_options(--param=asan-instrument-writes=0)
endif()

add_compile_options(-Wno-attributes -Wno-unknown-pragmas
                    $<$<COMPILE_LANGUAGE:C>:-Wstrict-prototypes>)

if(CONFIG_CXX_STANDARD)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-std=${CONFIG_CXX_STANDARD}>)
endif()

if(NOT CONFIG_LIBCXXTOOLCHAIN)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-nostdinc++>)
else()
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-D_STDLIB_H_>)
endif()

if(NOT CONFIG_CXX_EXCEPTION)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
                      $<$<COMPILE_LANGUAGE:CXX>:-fcheck-new>)
endif()

if(NOT CONFIG_CXX_RTTI)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>)
endif()

if(NOT CONFIG_ARCH_HAVE_FPU)
  list(APPEND PLATFORM_FLAGS -msoft-sp-float -msoft-dp-float)
endif()

if(CONFIG_DEBUG_LINK_ORPHAN_WARN)
  add_link_options(-Wl,--orphan-handling=warn)
endif()
