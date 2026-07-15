# ##############################################################################
# arch/x86_64/src/cmake/Toolchain.cmake
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

set(ARCH_SUBDIR intel64)

# cross-development toolchain selection

if(CONFIG_X86_64_TOOLCHAIN_GNU_NONE)
  set(CROSSDEV x86_64-none-elf-)
elseif(CONFIG_WINDOWS_CYGWIN)
  set(CROSSDEV i486-nuttx-elf-)
elseif(CONFIG_HOST_MACOS)
  set(CROSSDEV x86_64-elf-)
else()
  set(CROSSDEV)
endif()

if(CONFIG_ARCH_TOOLCHAIN_CLANG)
  set(CMAKE_ASM_COMPILER clang)
  set(CMAKE_C_COMPILER clang)
  set(CMAKE_CXX_COMPILER clang++)
  set(CMAKE_PREPROCESSOR clang -E -P -x c)
  set(CMAKE_STRIP llvm-strip --strip-unneeded)
  set(CMAKE_OBJCOPY llvm-objcopy)
  set(CMAKE_OBJDUMP llvm-objdump)
  set(CMAKE_LINKER ld.lld)
  set(CMAKE_LD ld.lld)
  set(CMAKE_AR llvm-ar)
  set(CMAKE_NM llvm-nm)
  set(CMAKE_RANLIB llvm-ranlib)
else()
  set(CMAKE_C_COMPILER ${CROSSDEV}gcc)
  set(CMAKE_CXX_COMPILER ${CROSSDEV}g++)
  set(CMAKE_ASM_COMPILER ${CMAKE_C_COMPILER})
  set(CMAKE_PREPROCESSOR ${CROSSDEV}gcc -E -P -x c)
  set(CMAKE_STRIP ${CROSSDEV}strip --strip-unneeded)
  set(CMAKE_OBJCOPY ${CROSSDEV}objcopy)
  set(CMAKE_OBJDUMP ${CROSSDEV}objdump)
  set(CMAKE_LINKER ${CROSSDEV}ld)
  set(CMAKE_LD ${CROSSDEV}ld)
  set(CMAKE_AR ${CROSSDEV}ar)
  set(CMAKE_NM ${CROSSDEV}nm)
  set(CMAKE_AS ${CROSSDEV}as)
  set(CMAKE_RANLIB ${CROSSDEV}ranlib)
endif()

# override the ARCHIVE command
set(CMAKE_ARCHIVE_COMMAND "<CMAKE_AR> rcs <TARGET> <LINK_FLAGS> <OBJECTS>")
set(CMAKE_RANLIB_COMMAND "<CMAKE_RANLIB> <TARGET>")
set(CMAKE_C_ARCHIVE_CREATE ${CMAKE_ARCHIVE_COMMAND})
set(CMAKE_CXX_ARCHIVE_CREATE ${CMAKE_ARCHIVE_COMMAND})
set(CMAKE_ASM_ARCHIVE_CREATE ${CMAKE_ARCHIVE_COMMAND})

set(CMAKE_C_ARCHIVE_APPEND ${CMAKE_ARCHIVE_COMMAND})
set(CMAKE_CXX_ARCHIVE_APPEND ${CMAKE_ARCHIVE_COMMAND})
set(CMAKE_ASM_ARCHIVE_APPEND ${CMAKE_ARCHIVE_COMMAND})

set(CMAKE_C_ARCHIVE_FINISH ${CMAKE_RANLIB_COMMAND})
set(CMAKE_CXX_ARCHIVE_FINISH ${CMAKE_RANLIB_COMMAND})
set(CMAKE_ASM_ARCHIVE_FINISH ${CMAKE_RANLIB_COMMAND})

set(NO_LTO "-fno-lto")

if(CONFIG_LTO_THIN)
  add_compile_options(-flto=thin)
elseif(CONFIG_LTO_FULL)
  add_compile_options(-flto)
  if(NOT CONFIG_ARCH_TOOLCHAIN_CLANG)
    add_compile_options(-fuse-linker-plugin)
    add_compile_options(-fno-builtin)
    set(CMAKE_LINKER ${CROSSDEV}gcc)
    set(CMAKE_LD ${CROSSDEV}gcc)
    set(CMAKE_AR ${CROSSDEV}gcc-ar)
    set(CMAKE_NM ${CROSSDEV}gcc-nm)
    set(CMAKE_RANLIB ${CROSSDEV}gcc-ranlib)
  endif()
elseif(CONFIG_LTO_FAT)
  add_compile_options(-flto -ffat-lto-objects)
  if(NOT CONFIG_ARCH_TOOLCHAIN_CLANG)
    add_compile_options(-fuse-linker-plugin)
    add_compile_options(-fno-builtin)
    set(CMAKE_LINKER ${CROSSDEV}gcc)
    set(CMAKE_LD ${CROSSDEV}gcc)
    set(CMAKE_AR ${CROSSDEV}gcc-ar)
    set(CMAKE_NM ${CROSSDEV}gcc-nm)
    set(CMAKE_RANLIB ${CROSSDEV}gcc-ranlib)
  endif()
endif()

# Suppress GCC LTO false positive: cross-module inlining into union-overlaid
# buffers triggers spurious -Wstringop-overflow (GCC bug 105329 et al.) Only
# affects lto1 link phase, not per-file compilation.

if(NOT CONFIG_LTO_NONE AND NOT CONFIG_ARCH_TOOLCHAIN_CLANG)
  add_link_options(-Wno-error=stringop-overflow)
endif()

if(CONFIG_DEBUG_SYMBOLS)
  add_compile_options(${CONFIG_DEBUG_SYMBOLS_LEVEL})
  # Generate .debug_macro section for GDB macro support (Clang only) Use DWARF
  # v4 for better readelf compatibility
  if(CONFIG_ARCH_TOOLCHAIN_CLANG)
    add_compile_options(-gdwarf-4 -fdebug-macro)
  endif()
endif()

if(CONFIG_DEBUG_NOOPT)
  add_compile_options(-O0)
elseif(CONFIG_DEBUG_CUSTOMOPT)
  add_compile_options(${CONFIG_DEBUG_OPTLEVEL})
elseif(CONFIG_DEBUG_FULLOPT)
  add_compile_options(-Os)
endif()

if(CONFIG_FRAME_POINTER)
  add_compile_options(-fno-omit-frame-pointer)
endif()

if(CONFIG_ARCH_INTEL64_DISABLE_CET)
  add_compile_options(-fcf-protection=none)
endif()

if(CONFIG_ARCH_INTEL64_DISABLE_CET)
  add_compile_options(-fno-tree-vectorize)
endif()

if(CONFIG_STACK_USAGE)
  add_compile_options(-fstack-usage)
endif()

if(${CONFIG_STACK_USAGE_WARNING})
  if(NOT ${CONFIG_STACK_USAGE_WARNING} STREQUAL 0)
    add_compile_options(-Wstack-usage=${CONFIG_STACK_USAGE_WARNING})
  endif()
endif()

if(CONFIG_ARCH_INSTRUMENT_ALL)
  add_compile_options(-finstrument-functions)
endif()

if(CONFIG_PROFILE_ALL)
  add_compile_options(-pg)
endif()

add_compile_options(-fno-pic -mcmodel=large -mno-red-zone -mrdrnd)

if(CONFIG_COVERAGE_ALL)
  if(CONFIG_COVERAGE_SOURCE)
    if(CONFIG_ARCH_TOOLCHAIN_GCC)
      add_compile_options(-fprofile-arcs -ftest-coverage -fno-inline)
    else()
      add_compile_options(-fprofile-instr-generate -fcoverage-mapping)
    endif()
  elseif(CONFIG_COVERAGE_PGO)
    if(CONFIG_ARCH_TOOLCHAIN_CLANG)
      if(EXISTS "${CMAKE_BINARY_DIR}/default.profdata")
        add_compile_options(-fprofile-use)
      else()
        add_compile_options(-fprofile-generate -fprofile-update=atomic)
      endif()
    elseif(CONFIG_ARCH_TOOLCHAIN_GCC)
      file(GLOB_RECURSE GCDA_FILES "${CMAKE_BINARY_DIR}/*.gcda")
      if(GCDA_FILES)
        add_compile_options(-fprofile-use)
      else()
        add_compile_options(-fprofile-generate -fprofile-update=atomic)
      endif()
    endif()
  endif()
endif()

if(CONFIG_ARCH_SAFE_STACK)
  add_compile_options(-fsanitize=safe-stack)
  list(APPEND COMPILE_ONLY_FLAGS -fsanitize=safe-stack)
endif()

if(CONFIG_HOST_LINUX)
  add_link_options(-Wl,-z,noexecstack)
endif()

# Architecture flags

add_link_options(-z max-page-size=0x1000)
add_link_options(-no-pie -nostdlib)
add_link_options(-Wl,--no-relax)

add_compile_options(
  -U_AIX
  -U_WIN32
  -U__APPLE__
  -U__FreeBSD__
  -U__NetBSD__
  -U__linux__
  -U__sun__
  -U__unix__
  -U__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__)

if(CONFIG_DEBUG_LINK_MAP)
  add_link_options(-Wl,--cref -Wl,-Map=nuttx.map)
endif()

add_compile_options(
  -fno-common
  -Wall
  -Wshadow
  -Wundef
  -Wno-attributes
  -Wno-unknown-pragmas
  $<$<COMPILE_LANGUAGE:C>:-Wstrict-prototypes>)

if(CONFIG_ARCH_TOOLCHAIN_CLANG)
  add_compile_options($<$<COMPILE_LANGUAGE:ASM>:-fno-integrated-as>)
else()
  add_compile_options($<$<COMPILE_LANGUAGE:ASM>:-Wa,--divide>)
endif()

# LLVM target definitions
set(LLVM_ARCHTYPE "x86_64")
set(LLVM_CPUTYPE "x86-64")
set(LLVM_ABITYPE "sysv")

if(CONFIG_CXX_STANDARD)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-std=${CONFIG_CXX_STANDARD}>)
endif()

if(CONFIG_STACK_CANARIES)
  add_compile_options(-fstack-protector-all)
else()
  add_compile_options(-fno-stack-protector)
endif()

if(NOT CONFIG_CXX_EXCEPTION)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
                      $<$<COMPILE_LANGUAGE:CXX>:-fcheck-new>)
endif()

if(NOT CONFIG_CXX_RTTI)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>)
endif()

if(CONFIG_LIBCXX)
  add_compile_options(-D_LIBCPP_DISABLE_AVAILABILITY)
endif()

if(CONFIG_DEBUG_OPT_UNUSED_SECTIONS)
  add_link_options(-Wl,--gc-sections)
  add_compile_options(-ffunction-sections -fdata-sections)
endif()

if(NOT CONFIG_LIBCXXTOOLCHAIN)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-nostdinc++>)
endif()

if(CONFIG_MM_KASAN_INSTRUMENT_ALL)
  add_compile_options(-fsanitize=kernel-address)
endif()

if(CONFIG_ARCH_TOOLCHAIN_CLANG)
  set(KASAN_PARAM "")
  list(APPEND KASAN_PARAM "asan-stack=0")
  list(APPEND KASAN_PARAM "asan-instrumentation-with-call-threshold=0")

  if(CONFIG_MM_KASAN_GLOBAL)
    list(APPEND KASAN_PARAM "asan-globals=1")
  else()
    list(APPEND KASAN_PARAM "asan-globals=0")
  endif()

  if(CONFIG_MM_KASAN_DISABLE_READS_CHECK)
    list(APPEND KASAN_PARAM "asan-instrument-reads=0")
  endif()

  if(CONFIG_MM_KASAN_DISABLE_WRITES_CHECK)
    list(APPEND KASAN_PARAM "asan-instrument-writes=0")
  endif()

  set(KASAN_PARAM_STR "")
  foreach(param IN LISTS KASAN_PARAM)
    string(APPEND KASAN_PARAM_STR "-mllvm -${param} ")
  endforeach()

  file(WRITE ${CMAKE_BINARY_DIR}/kasan_params.cmd "${KASAN_PARAM_STR}")
  add_compile_options("@${CMAKE_BINARY_DIR}/kasan_params.cmd")
  list(APPEND COMPILE_ONLY_FLAGS "@${CMAKE_BINARY_DIR}/kasan_params.cmd")
else()
  if(CONFIG_MM_KASAN_GLOBAL)
    add_compile_options(--param asan-globals=1)
  endif()

  if(CONFIG_MM_KASAN_DISABLE_READS_CHECK)
    add_compile_options(--param asan-instrument-reads=0)
  endif()

  if(CONFIG_MM_KASAN_DISABLE_WRITES_CHECK)
    add_compile_options(--param asan-instrument-writes=0)
  endif()
endif()

if(CONFIG_ARCH_X86_64_SSE3)
  add_compile_options(-msse3)
endif()

if(CONFIG_ARCH_X86_64_SSSE3)
  add_compile_options(-mssse3)
endif()

if(CONFIG_ARCH_X86_64_SSE41)
  add_compile_options(-msse4.1)
endif()

if(CONFIG_ARCH_X86_64_SSE42)
  add_compile_options(-msse4.2)
endif()

if(CONFIG_ARCH_X86_64_SSE4A)
  add_compile_options(-msse4a)
endif()

if(CONFIG_ARCH_X86_64_FMA)
  add_compile_options(-mfma)
endif()

if(CONFIG_ARCH_X86_64_AVX)
  add_compile_options(-mavx)
endif()

if(CONFIG_ARCH_X86_64_AVX512)
  add_compile_options(-mavx512f)
endif()

if(CONFIG_ARCH_X86_64_AVX512PF)
  add_compile_options(-mavx512pf)
endif()

if(CONFIG_ARCH_X86_64_AVX512ER)
  add_compile_options(-mavx512er)
endif()

if(CONFIG_ARCH_X86_64_AVX512CD)
  add_compile_options(-mavx512cd)
endif()

if(CONFIG_ARCH_X86_64_AVX512VL)
  add_compile_options(-mavx512vl)
endif()

if(CONFIG_ARCH_X86_64_AVX512BW)
  add_compile_options(-mavx512bw)
endif()

if(CONFIG_ARCH_X86_64_AVX512DQ)
  add_compile_options(-mavx512dq)
endif()

if(CONFIG_ARCH_X86_64_AVX512IFMA)
  add_compile_options(-mavx512ifma)
endif()

if(CONFIG_ARCH_X86_64_AVX512VBMI)
  add_compile_options(-mavx512vbmi)
endif()

if(CONFIG_ARCH_TOOLCHAIN_GNU AND NOT CONFIG_ARCH_TOOLCHAIN_CLANG)
  if(NOT GCCVER)
    execute_process(COMMAND ${CMAKE_C_COMPILER} --version
                    OUTPUT_VARIABLE GCC_VERSION_OUTPUT)
    string(REGEX MATCH "([0-9]+)\\.([0-9]+)\\.([0-9]+)" _
                 "${GCC_VERSION_OUTPUT}")
    set(GCCVER ${CMAKE_MATCH_1})
    if(GCCVER GREATER_EQUAL 12)
      add_compile_options(-Wno-alloc-size-larger-than)
    endif()
  endif()
endif()

if(CONFIG_LIB_LIBUNWIND)
  add_compile_options(-funwind-tables -fasynchronous-unwind-tables)
  add_link_options(-Wl,--eh-frame-hdr)
endif()
