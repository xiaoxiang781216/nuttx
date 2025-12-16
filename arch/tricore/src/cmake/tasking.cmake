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

set(CMAKE_C_COMPILER_FORCED TRUE)
set(CMAKE_CXX_COMPILER_FORCED TRUE)

set(ARCH_SUBDIR chip)
include(${ARCH_SUBDIR})

set(CMAKE_ASM_COMPILER cctc)
set(CMAKE_C_COMPILER cctc)
set(CMAKE_CXX_COMPILER cctc)
set(CMAKE_STRIP strip --strip-unneeded)
set(CMAKE_OBJCOPY tricore-elf-objcopy)
set(CMAKE_OBJDUMP tricore-elf-objdump)

set(CMAKE_LINKER ltc)
set(CMAKE_LD ltc)
set(CMAKE_AR artc)
set(CMAKE_NM nm)
set(CMAKE_RANLIB ranlib)

set(CMAKE_C_COMPILE_OBJECT
    "<CMAKE_C_COMPILER> <DEFINES> <INCLUDES> <FLAGS> --create <SOURCE> -o <OBJECT>"
)
set(CMAKE_CXX_COMPILE_OBJECT
    "<CMAKE_CXX_COMPILER> <DEFINES> <INCLUDES> <FLAGS> --create <SOURCE> -o <OBJECT>"
)

if(CMAKE_GENERATOR MATCHES "Ninja")
  set(CMAKE_C_RESPONSE_FILE_FLAG "-f ${CMAKE_BINARY_DIR}/")
  set(CMAKE_CXX_RESPONSE_FILE_FLAG "-f ${CMAKE_BINARY_DIR}/")
  set(CMAKE_ASM_RESPONSE_FILE_FLAG "-f ${CMAKE_BINARY_DIR}/")
  set(CMAKE_C_RESPONSE_FILE_LINK_FLAG "-f ${CMAKE_BINARY_DIR}/")
  set(CMAKE_CXX_RESPONSE_FILE_LINK_FLAG "-f ${CMAKE_BINARY_DIR}/")
  set(CMAKE_ASM_RESPONSE_FILE_LINK_FLAG "-f ${CMAKE_BINARY_DIR}/")
endif()

# override the ARCHIVE command

set(CMAKE_C_ARCHIVE_CREATE "<CMAKE_AR> -r <TARGET> <LINK_FLAGS> <OBJECTS>")
set(CMAKE_CXX_ARCHIVE_CREATE "<CMAKE_AR> -r <TARGET> <LINK_FLAGS> <OBJECTS>")
set(CMAKE_ASM_ARCHIVE_CREATE "<CMAKE_AR> -r <TARGET> <LINK_FLAGS> <OBJECTS>")

# override the LINK command

add_link_options(-I${CMAKE_BINARY_DIR}/include)
set(CMAKE_C_LINK_EXECUTABLE
    "<CMAKE_LINKER> <FLAGS> <CMAKE_C_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>"
)
set(CMAKE_CXX_LINK_EXECUTABLE
    "<CMAKE_LINKER> <FLAGS> <CMAKE_CXX_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>"
)

# Architecture flags

add_compile_options(--lsl-core=vtc)
add_link_options(-v)
add_compile_options($<$<COMPILE_LANGUAGE:C>:--iso=17>)
add_compile_options(
  $<$<COMPILE_LANGUAGE:C>:--language=+gcc,+volatile,-strings,-kanji>)
add_compile_options(--no-stdinc)
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:--c++=14>)
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:--g++>)
add_compile_options(
  $<$<COMPILE_LANGUAGE:CXX>:--language=+gcc,+volatile,-strings,-kanji>)
add_compile_options(--pass-c++=-D__CTC__)

function(get_tasking_ctc_root OUT_VAR)
  set(${OUT_VAR} "")
  find_program(_CCTC_PATH cctc)
  if(_CCTC_PATH)
    get_filename_component(_BIN_DIR "${_CCTC_PATH}" DIRECTORY)
    get_filename_component(_ROOT_DIR "${_BIN_DIR}" DIRECTORY)
    set(${OUT_VAR}
        "${_ROOT_DIR}"
        PARENT_SCOPE)
  endif()
endfunction()

get_tasking_ctc_root(TASKING_ROOT_PATH)
# search limits.h in this path firstly under include.cxx/support/tasking
if(TASKING_ROOT_PATH)
  add_compile_options(-I${TASKING_ROOT_PATH}/include.cxx/support/tasking)
  add_compile_options(-I${TASKING_ROOT_PATH}/include)
endif()

if(CONFIG_DEBUG_CUSTOMOPT)
  add_compile_options(${CONFIG_DEBUG_OPTLEVEL})
elseif(CONFIG_DEBUG_FULLOPT)
  add_compile_options(-Os)
endif()

# pragma align <4> (default: 0)

add_compile_options(--align=4)

# Always use 32-bit integers for enumeration

add_compile_options(--integer-enumeration)

# tradeoff between speed (-t0) and size (-t4) (default: 4)

add_compile_options(--tradeoff=2)

# mergering of sections

add_compile_options(--pass-assembler=--concatenate-sections)

# Debug link map

if(CONFIG_DEBUG_LINK_MAP)
  add_link_options(--map-file=nuttx.map)
endif()

# enable symbolic debug information

if(CONFIG_DEBUG_SYMBOLS)
  add_compile_options(--debug-info=default)
  add_compile_options(--keep-temporary-files)
  add_compile_options(-g)
endif()

# merge source code with assembly output

add_compile_options(--source)

# generate alignment depending on assume_if hints

add_compile_options(--branch-target-align)

# cmake-format: off
# Since nuttx uses too many of GNU extensions in the implementation of
# FPU-related library functions, which is not supported in tasking, so currently
# we cannot use FPU-related configurations to manage it.
#
# Just set fp-model to Double Precision:
# --fp-model[=<flag>,...]         floating-point model (default: cFlnrSTz)
#   0                               alias for --fp-model=CFLNRStZ (strict)
#   1                               alias for --fp-model=cFLNRSTZ (precise)
#   2                               alias for --fp-model=cFlnrSTz (fast-dp)
#   3                               alias for --fp-model=cflnrSTz (fast-sp)
# cmake-format: on

add_compile_options(--fp-model=2)
add_link_options(-lc_fpu)
add_link_options(-lfp_fpu)

add_link_options(--core=mpe:vtc)
add_link_options(--hex-format=s -mcrfiklsmnoduq)
add_link_options(-lrt)

# cmake-format: off
# ctc W500: ["stdio/lib_libvsprintf.c" 884/29] expression without effect
# ctc W507: ["mm_heap/mm_malloc.c" 238/64] variable "nodesize" is possibly uninitialized
# ctc W508: ["misc/lib_impure.c" 1/1] empty source file
# ctc W525: ["getopt.c" 678/3] discarded 'const' qualifier at assignment: conversion from char const * to char *
# ctc W527: ["stdlib/lib_strtold.c" 565/23] constant of type "double" saturated
# ctc W526: ["include/sys/epoll.h" 87/5] enumeration constant shall be representable as 'int'
# ctc W529: ["wchar/lib_mbrtowc.c" 88/35] overflow in constant expression of type "unsigned long int"
# ctc W544: ["wqueue/kwork_thread.c" 210/32] unreachable code
# ctc W549: ["unistd/lib_getopt_common.c" 544/15] condition is always true
# ctc W553: ["vfs/fs_fcntl.c" 231/7] no 'break' or comment before case label
# ctc W557: ["common/tricore_main.c" 58/11] possible infinite loop
# ctc W560: ["tmpfs/fs_tmpfs.c" 232/25] possible truncation at implicit conversion to type "unsigned short int"
# ctc W562: ["mm_heap/mm_memalign.c" 70/20] unary minus applied to unsigned value
# ctc W558: ["include/nuttx/power/regulator.h" 224/36] struct/union/enum definition in parameter declaration
# ctc W587: ["stdlib/lib_strtold.c" 571/23] underflow on constant of type "double"
# ctc W588: ["misc/lib_glob.c" 150/13] dead assignment to "i" eliminated
# ctc W589: ["inode/fs_inodesearch.c" 72/8] pointer assumed to be nonzero - test removed
# cptc W0068: ["include/libcxx/string" 878] integer conversion resulted in a change of sign
# cptc W0161: ["SmartCode/ctc/include/math.h" 390] unrecognized #pragma
# cptc W0940: ["libcxx/src/include/to_chars_floating_point.h" 1073] missing return statement at end of non-void function "
# cptc W1097: ["include/libcxx/vector" 1347] unknown attribute "__visibility__"
# cptc W1105: ["libcxx/src/support/runtime/exception_pointer_unimplemented.ipp" 28] #warning directive: exception_ptr not yet implemented
# cptc W1315: ["include/libcxx/new" 183] function declared with "noreturn" does return (throw triggered)
# cptc W1858: ["libcxx/__bit/bit_cast.h" 34] attribute "__always_inline__" does not apply here
# cptc W2213: ["include/libcxx/string" 4110] GNU attributes on a template redeclaration have no effect (the attributes of the original declaration at line 656 apply instead)
# cptc W2496: ["include/libcxx/vector" 1429] the "always_inline" attribute is ignored on non-inline functions
# cptc W2529: ["libcxx/__chrono/duration.h" 559] a user-provided literal suffix must begin with "_"
# cptc W2964: ["libcxx/src/include/to_chars_floating_point.h" 994] constexpr if statements are a C++17 feature
# cptc W3085: ["include/libcxx/__charconv/tables.h" 26] inline variables are a C++17 feature
# cmake-format: on

set(TASKING_WARNINGS
    500,507,508,525,526,527,529,544,549,553,560,562,557,558,587,588,589)
set(TASKING_CXX_WARNINGS
    68,161,940,1097,1105,1315,1858,2213,2496,2529,2964,3085)

add_compile_options(--pass-c=--no-warnings=${TASKING_WARNINGS})
add_compile_options(--pass-c++=--no-warnings=${TASKING_CXX_WARNINGS})

if(NOT CONFIG_CXX_EXCEPTION)
  add_compile_options(--pass-c++=--no-exceptions)
endif()

if(CONFIG_CXX_RTTI)
  add_compile_options(--pass-c++=--rtti)
endif()

set(NUTTX_TOOLCHAIN_PREPROCESS_DEFINED true)

function(nuttx_generate_preprocess_target)

  # parse arguments into variables

  nuttx_parse_function_args(
    FUNC
    nuttx_generate_preprocess_target
    ONE_VALUE
    SOURCE_FILE
    TARGET_FILE
    MULTI_VALUE
    DEPENDS
    REQUIRED
    SOURCE_FILE
    TARGET_FILE
    ARGN
    ${ARGN})

  get_filename_component(TARGET_DIR ${TARGET_FILE} DIRECTORY)
  get_filename_component(SOURCE_FILE_NAME ${SOURCE_FILE} NAME)
  set(TARGET_FILE_TEMP "${TARGET_DIR}/${SOURCE_FILE_NAME}.cpp")
  get_target_property(NUTTX_INCLUDES nuttx NUTTX_INCLUDE_DIRECTORIES)
  list(TRANSFORM NUTTX_INCLUDES PREPEND -I)

  add_custom_command(
    OUTPUT ${TARGET_FILE}
    COMMAND ${CMAKE_COMMAND} -E copy ${SOURCE_FILE} ${TARGET_FILE_TEMP}
    COMMAND
      ${PREPROCESS} -I${CMAKE_BINARY_DIR}/include -I${NUTTX_DIR}/include
      -I${NUTTX_CHIP_ABS_DIR} ${NUTTX_INCLUDES} ${TARGET_FILE_TEMP} -o
      ${TARGET_FILE}
    COMMAND ${CMAKE_COMMAND} ${TARGET_FILE} "__builtin" -P
            ${NUTTX_DIR}/cmake/nuttx_remove_lines.cmake
    COMMAND ${CMAKE_COMMAND} -E remove ${TARGET_FILE_TEMP}
    DEPENDS ${SOURCE_FILE} ${DEPENDS})
endfunction()

set(NUTTX_FIND_TOOLCHAIN_LIB_DEFINED true)
function(nuttx_find_toolchain_lib)
  if(TASKING_ROOT_PATH AND ARGN)
    nuttx_add_extra_library(${TASKING_ROOT_PATH}/lib/tc18/${ARGN})
  endif()
endfunction()

function(nuttx_add_elf_app)
  nuttx_parse_function_args(
    FUNC
    nuttx_add_elf_app
    ONE_VALUE
    NAME
    PRIORITY
    STACKSIZE
    UID
    GID
    MODE
    HEAPSIZE
    MODULE
    DYNLIB
    MULTI_VALUE
    COMPILE_FLAGS
    LINK_FLAGS
    INCLUDE_DIRECTORIES
    SRCS
    DEPENDS
    DEFINITIONS
    OPTIONS
    ARGN
    ${ARGN})

  # create as standalone executable (loadable application or "module")
  set(ELF_TARGET "${NAME}")

  # determine the compiled elf mode
  if(CONFIG_BUILD_KERNEL)
    set(KERNEL_ELF_MODE True) # kernel elf will link all user libs
  elseif("${MODULE}" STREQUAL "m")
    set(LOADABLE_ELF_MODE True) # loadable elf only link extra libs
  elseif("${DYNLIB}" STREQUAL "y")
    set(DYNLIB_ELF_MODE True) # dynlib elf dont need start obj and other lib
  endif()

  # Use ELF capable toolchain, by building manually and overwriting the non-elf
  # output
  if(NOT CMAKE_C_ELF_COMPILER)
    set(ELF_NAME "${NAME}")
    set(ELF_TARGET "ELF_${NAME}")
    add_library(${ELF_TARGET} ${SRCS})
    add_dependencies(${ELF_TARGET} apps_post)
    if(TARGET STARTUP_OBJS)
      add_dependencies(${ELF_TARGET} STARTUP_OBJS)
    endif()

    if(STACKSIZE)
      set(SYMBOL_STACKSIZE --add-symbol nx_stacksize=${STACKSIZE})
    endif()
    if(PRIORITY)
      if(PRIORITY STREQUAL "SCHED_PRIORITY_DEFAULT")
        set(PRIORITY "0")
      endif()
      set(SYMBOL_PRIORITY --add-symbol nx_priority=${PRIORITY})
    endif()
    if(CONFIG_SCHED_USER_IDENTITY)
      if(UID)
        set(SYMBOL_UID --add-symbol nx_uid=${UID})
      endif()
      if(GID)
        set(SYMBOL_GID --add-symbol nx_gid=${GID})
      endif()
      if(MODE)
        set(SYMBOL_MODE --add-symbol nx_mode=${MODE})
      endif()
    endif()
    if(CONFIG_MM_TASK_HEAP)
      if(HEAPSIZE)
        set(SYMBOL_HEAPSIZE --add-symbol nx_heapsize=${HEAPSIZE})
      else()
        set(SYMBOL_HEAPSIZE --add-symbol
                            nx_heapsize=${CONFIG_MM_TASK_HEAP_DEFAULT_SIZE})
      endif()
    endif()
    get_property(NUTTX_EXTRA_FLAGS GLOBAL PROPERTY NUTTX_EXTRA_FLAGS)
    add_custom_command(
      TARGET ${ELF_TARGET}
      POST_BUILD
      COMMAND
        # add default link option
        ${CMAKE_LINKER} -r -e__start -I${CMAKE_BINARY_DIR}/include
        -I${NUTTX_DIR}/include
        --lsl-file=${CMAKE_SOURCE_DIR}/libs/libc/elf/tasking-elf.lsl
        # add global MOD link option if dynlib link
        $<$<BOOL:${DYNLIB_ELF_MODE}>:$<TARGET_PROPERTY:nuttx_global,NUTTX_MOD_APP_LINK_OPTIONS>>
        # add global ELF link option if m&kernel link
        $<$<OR:$<BOOL:${KERNEL_ELF_MODE}>,$<BOOL:${LOADABLE_ELF_MODE}>>:$<TARGET_PROPERTY:nuttx_global,NUTTX_ELF_APP_LINK_OPTIONS>>
        # add local link option lastest
        ${LINK_FLAGS}
        # link startup obj if m&kernel link
        $<$<AND:$<TARGET_EXISTS:STARTUP_OBJS>,$<NOT:$<BOOL:${DYNLIB_ELF_MODE}>>>:$<TARGET_OBJECTS:STARTUP_OBJS>>
        # link user lib if kernel link
        $<$<BOOL:${KERNEL_ELF_MODE}>:$<GENEX_EVAL:$<TARGET_PROPERTY:nuttx_global,NUTTX_ELF_LINK_LIBRARIES>>>
        # always link extra libs
        $<GENEX_EVAL:$<TARGET_PROPERTY:nuttx_global,NUTTX_ELF_LINK_EXTRA_LIBRARIES>>
        $<TARGET_FILE:${ELF_TARGET}> -o
        ${CMAKE_BINARY_DIR}/bin_debug/${ELF_NAME}
      COMMAND
        ${CMAKE_OBJCOPY} ${SYMBOL_STACKSIZE} ${SYMBOL_PRIORITY} ${SYMBOL_UID}
        ${SYMBOL_GID} ${SYMBOL_MODE} ${SYMBOL_HEAPSIZE}
        ${CMAKE_BINARY_DIR}/bin_debug/${ELF_NAME}
      COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/bin_debug/${ELF_NAME}
              ${CMAKE_BINARY_DIR}/bin/${ELF_NAME}
      COMMAND ${CMAKE_COMMAND} -E remove
              ${CMAKE_BINARY_DIR}/bin_debug/${ELF_NAME}.mdf
      COMMENT "Building ELF:${ELF_NAME}"
      COMMAND_EXPAND_LISTS)
  else()
    add_executable(${ELF_TARGET} ${SRCS})
    target_link_options(
      ${ELF_TARGET} PRIVATE
      $<GENEX_EVAL:$<TARGET_PROPERTY:nuttx_global,NUTTX_ELF_APP_LINK_OPTIONS>>)
  endif()

  # easy access to final ELF, regardless of how it was created
  set_property(TARGET ${ELF_TARGET}
               PROPERTY ELF_BINARY ${CMAKE_CURRENT_BINARY_DIR}/${ELF_TARGET})

  nuttx_add_library_internal(${ELF_TARGET})

  # loadable build requires applying ELF flags to all applications

  if(CONFIG_MODULES)
    add_dependencies(nuttx_apps_mksymtab ${ELF_TARGET})
    target_compile_options(
      ${ELF_TARGET}
      PRIVATE
        $<GENEX_EVAL:$<TARGET_PROPERTY:nuttx_global,NUTTX_ELF_APP_COMPILE_OPTIONS>>
    )
  endif()

  if(DYNLIB_ELF_MODE)
    add_dependencies(nuttx_apps_mksymtab ${ELF_TARGET})
    target_compile_options(
      ${ELF_TARGET}
      PRIVATE
        $<GENEX_EVAL:$<TARGET_PROPERTY:nuttx_global,NUTTX_MOD_APP_COMPILE_OPTIONS>>
    )
  endif()

  install(TARGETS ${ELF_TARGET})
  set_property(
    TARGET nuttx
    APPEND
    PROPERTY NUTTX_LOADABLE_APPS ${ELF_TARGET})

  # Return target name to parent scope
  set(RESULT_TARGET
      ${ELF_TARGET}
      PARENT_SCOPE)

endfunction()
