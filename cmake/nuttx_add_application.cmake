# ##############################################################################
# cmake/nuttx_add_application.cmake
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

include(nuttx_parse_function_args)

define_property(
  GLOBAL
  PROPERTY NUTTX_APPS_LIBRARIES
  BRIEF_DOCS "NuttX application libs"
  FULL_DOCS "List of all NuttX application libraries")

# Create a directories for the application binaries `bin` for stripped binaries
# `bin_debug` for debug binaries if(NOT EXISTS ${CMAKE_BINARY_DIR}/bin) Make
# sure the binary directory always rebuild
add_custom_target(
  gen_apps_bin_dir ALL
  COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/bin
  COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/bin_debug
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/bin
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/bin_debug
  COMMENT "Generating bin output directories")

add_dependencies(apps_context gen_apps_bin_dir)

# ~~~
# Allow external toolchain to provide custom nuttx_add_elf_app implementation
#
# External toolchains can define their own nuttx_add_elf_app by:
# 1. Defining the function before this file is included
# 2. The custom function MUST set RESULT_TARGET variable in parent scope
#
# Example in toolchain file:
#   function(nuttx_add_elf_app)
#     # ...  parse arguments
#     # ...  custom implementation
#     set(RESULT_TARGET "custom_target_name" PARENT_SCOPE)
#   endfunction()
#   include(nuttx_add_application. cmake)
# ~~~

if(NOT COMMAND nuttx_add_elf_app)
  # ~~~
  # nuttx_add_elf_app
  #
  # Description:
  #   Creates a standalone executable application (loadable module, kernel ELF,
  #   or dynamic library) as opposed to a static library.
  #
  #   This function can be overridden by external toolchains to provide custom
  #   ELF building behavior.
  #
  # Usage:
  #   nuttx_add_elf_app(
  #     NAME <string>
  #     MODULE <string>
  #     DYNLIB <string>
  #     SRCS <list>
  #     [ PRIORITY <string> ]
  #     [ STACKSIZE <string> ]
  #     [ HEAPSIZE <string> ]
  #     [ UID <string> ]
  #     [ GID <string> ]
  #     [ MODE <string> ]
  #     [ COMPILE_FLAGS <list> ]
  #     [ LINK_FLAGS <list> ]
  #     [ INCLUDE_DIRECTORIES <list> ]
  #     [ DEPENDS <list> ]
  #     [ DEFINITIONS <list> ])
  #
  # Parameters:
  #   NAME       : name of the application
  #   MODULE     : module configuration ("m" for loadable)
  #   DYNLIB     : dynamic library flag ("y" for dynlib)
  #   SRCS       : source files
  #   Other parameters: same as nuttx_add_application
  #
  # Returns:
  #   Sets RESULT_TARGET variable in parent scope
  # ~~~

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

    # Use ELF capable toolchain, by building manually and overwriting the
    # non-elf output
    if(NOT CMAKE_C_ELF_COMPILER)
      set(ELF_NAME "${NAME}")
      set(ELF_TARGET "ELF_${NAME}")
      add_library(${ELF_TARGET} ${SRCS})
      add_dependencies(${ELF_TARGET} apps_post)
      if(TARGET STARTUP_OBJS)
        add_dependencies(${ELF_TARGET} STARTUP_OBJS)
      endif()
      if(NOT "${CMAKE_C_COMPILER}" MATCHES "gcc$|clang$")
        set(USE_LINKER True)
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
          ${CMAKE_C_COMPILER} ${NUTTX_EXTRA_FLAGS} -T
          ${CMAKE_BINARY_DIR}/gnu-elf.ld
          # add global MOD link option if dynlib link
          $<$<BOOL:${DYNLIB_ELF_MODE}>:$<TARGET_PROPERTY:nuttx_global,NUTTX_MOD_APP_LINK_OPTIONS>>
          # add global ELF link option if m&kernel link
          $<$<OR:$<BOOL:${KERNEL_ELF_MODE}>,$<BOOL:${LOADABLE_ELF_MODE}>>:$<TARGET_PROPERTY:nuttx_global,NUTTX_ELF_APP_LINK_OPTIONS>>
          # add local link option lastest
          ${LINK_FLAGS}
          # link startup obj if m&kernel link
          $<$<AND:$<TARGET_EXISTS:STARTUP_OBJS>,$<NOT:$<BOOL:${DYNLIB_ELF_MODE}>>>:$<TARGET_OBJECTS:STARTUP_OBJS>>
          $<$<NOT:$<BOOL:${USE_LINKER}>>:-Wl,>--start-group
          # link user lib if kernel link
          $<$<BOOL:${KERNEL_ELF_MODE}>:$<GENEX_EVAL:$<TARGET_PROPERTY:nuttx_global,NUTTX_ELF_LINK_LIBRARIES>>>
          # always link extra libs
          $<GENEX_EVAL:$<TARGET_PROPERTY:nuttx_global,NUTTX_ELF_LINK_EXTRA_LIBRARIES>>
          $<$<NOT:$<BOOL:${USE_LINKER}>>:-Wl,>--whole-archive
          $<TARGET_FILE:${ELF_TARGET}>
          $<$<NOT:$<BOOL:${USE_LINKER}>>:-Wl,>--no-whole-archive
          $<$<NOT:$<BOOL:${USE_LINKER}>>:-Wl,>--end-group -o
          ${CMAKE_BINARY_DIR}/bin_debug/${ELF_NAME}
        COMMAND
          ${CMAKE_OBJCOPY} ${SYMBOL_STACKSIZE} ${SYMBOL_PRIORITY} ${SYMBOL_UID}
          ${SYMBOL_GID} ${SYMBOL_MODE} ${SYMBOL_HEAPSIZE}
          ${CMAKE_BINARY_DIR}/bin_debug/${ELF_NAME}
        COMMAND
          ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/bin_debug/${ELF_NAME}
          ${CMAKE_BINARY_DIR}/bin/${ELF_NAME}
        COMMAND ${CMAKE_STRIP} ${CMAKE_BINARY_DIR}/bin/${ELF_NAME}
        COMMENT "Building ELF:${ELF_NAME}"
        COMMAND_EXPAND_LISTS)
    else()
      add_executable(${ELF_TARGET} ${SRCS})
      target_link_options(
        ${ELF_TARGET} PRIVATE
        $<GENEX_EVAL:$<TARGET_PROPERTY:nuttx_global,NUTTX_ELF_APP_LINK_OPTIONS>>
      )
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

  # Mark that the default implementation has been defined
  set(NUTTX_DEFAULT_ELF_BUILD_DEFINED
      TRUE
      CACHE INTERNAL "Default nuttx_add_elf_app is defined")
else()
  message(STATUS "Using custom nuttx_add_elf_app implementation")
endif()

# ~~~
# nuttx_add_application
#
# Description:
#   Declares a NuttX application as a static library. The corresponding target
#   will be named apps_<NAME>. The first entry into the source list is assumed
#   to be the one containing main() and will thus receive a -Dmain=app_main
#   definition during build.
#
# Usage:
#   nuttx_add_application( NAME <string> [ PRIORITY <string> ]
#     [ STACKSIZE <string> ] [ COMPILE_FLAGS <list> ]
#     [ INCLUDE_DIRECTORIES <list> ] [ DEPENDS <string> ]
#     [ DEFINITIONS <string> ] [ MODULE <string> ] [ SRCS <list> ] )
#
# Parameters:
#   NAME                : unique name of application
#   PRIORITY            : priority
#   STACKSIZE           : stack size
#   HEAPSIZE            : heap size
#   COMPILE_FLAGS       : compile flags
#   INCLUDE_DIRECTORIES : include directories
#   DEPENDS             : targets which this module depends on
#   DEFINITIONS         : optional compile definitions
#   MODULE              : if "m", build module (designed to received
#                         CONFIG_<app> value)
#   SRCS                : source files
#   NO_MAIN_ALIAS       : do not add a main=<app>_main alias(*)
#   DYNLIB              : if "y", build as dynamic loadable library
#   LINK_FLAGS          : link flags only for elf or loadable link
#
# (*) This is only really needed in convoluted cases where a single .c file
# contains differently named <app>_main() entries for different <app>. This
# situation should really be changed into a separate main file per actual app
# using a shared user library.
#
# Example:
#   nuttx_add_application(
#     NAME test
#     SRCS file.cpp
#     STACKSIZE 1024
#     HEAPSIZE 65536
#     DEPENDS nshlib
#     MODULE ${CONFIG_EXAMPLES_TEST})
# ~~~

function(nuttx_add_application)

  # parse arguments into variables

  nuttx_parse_function_args(
    FUNC
    nuttx_add_application
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
    NO_MAIN_ALIAS
    ARGN
    ${ARGN})

  if(NOT NAME)
    return()
  endif()

  # check if SRCS exist
  if(SRCS)
    file(GLOB SRCS_EXIST ${SRCS})
  endif()

  # Initialize TARGET variable
  set(TARGET "")
  set(NO_COMPILABLE_TARGET FALSE)

  # create target

  if(SRCS_EXIST)
    if(MODULE
       AND ("${MODULE}" STREQUAL "m")
       OR CONFIG_BUILD_KERNEL
       OR "${DYNLIB}" STREQUAL "y")
      # Build as elf using the extracted function
      nuttx_add_elf_app(
        NAME
        ${NAME}
        MODULE
        ${MODULE}
        DYNLIB
        ${DYNLIB}
        SRCS
        ${SRCS}
        PRIORITY
        ${PRIORITY}
        STACKSIZE
        ${STACKSIZE}
        UID
        ${UID}
        GID
        ${GID}
        MODE
        ${MODE}
        HEAPSIZE
        ${HEAPSIZE}
        COMPILE_FLAGS
        ${COMPILE_FLAGS}
        LINK_FLAGS
        ${LINK_FLAGS}
        INCLUDE_DIRECTORIES
        ${INCLUDE_DIRECTORIES}
        DEPENDS
        ${DEPENDS}
        DEFINITIONS
        ${DEFINITIONS})
      # Get the target name returned by nuttx_add_elf_app
      set(TARGET ${RESULT_TARGET})
    else()
      # create as library to be archived into libapps.a
      set(TARGET "apps_${NAME}")
      add_library(${TARGET} ${SRCS})

      nuttx_add_library_internal(${TARGET})
      # add to list of application libraries

      set_property(GLOBAL APPEND PROPERTY NUTTX_APPS_LIBRARIES ${TARGET})

      if(NOT NO_MAIN_ALIAS)
        # provide main() alias
        list(GET SRCS 0 MAIN_SRC)
        set_property(
          SOURCE ${MAIN_SRC}
          APPEND
          PROPERTY COMPILE_DEFINITIONS main=${NAME}_main)
      endif()
    endif()

  else()
    set(TARGET "apps_${NAME}")
    add_custom_target(${TARGET})
    set_property(GLOBAL APPEND PROPERTY NUTTX_APPS_ONLY_REGISTER ${TARGET})
    set_target_properties(${TARGET} PROPERTIES NO_COMPILABLE_TARGET True)
    set(NO_COMPILABLE_TARGET True)
  endif()

  # apps applications need to depends on apps_context by default

  add_dependencies(${TARGET} apps_context)

  # store parameters into properties (used during builtin list generation)

  set_target_properties(${TARGET} PROPERTIES APP_MAIN ${NAME}_main)
  set_target_properties(${TARGET} PROPERTIES APP_NAME ${NAME})

  if(PRIORITY)
    set_target_properties(${TARGET} PROPERTIES APP_PRIORITY ${PRIORITY})
  else()
    set_target_properties(${TARGET} PROPERTIES APP_PRIORITY
                                               SCHED_PRIORITY_DEFAULT)
  endif()

  if(STACKSIZE)
    set_target_properties(${TARGET} PROPERTIES APP_STACK ${STACKSIZE})
  else()
    set_target_properties(${TARGET} PROPERTIES APP_STACK
                                               ${CONFIG_DEFAULT_TASK_STACKSIZE})
  endif()

  if(CONFIG_SCHED_USER_IDENTITY)
    if(UID)
      set_target_properties(${TARGET} PROPERTIES APP_UID ${UID})
    else()
      set_target_properties(${TARGET} PROPERTIES APP_UID 0)
    endif()
    if(GID)
      set_target_properties(${TARGET} PROPERTIES APP_GID ${GID})
    else()
      set_target_properties(${TARGET} PROPERTIES APP_GID 0)
    endif()
    if(MODE)
      set_target_properties(${TARGET} PROPERTIES APP_MODE ${MODE})
    else()
      set_target_properties(${TARGET} PROPERTIES APP_MODE 0)
    endif()
  endif()

  if(CONFIG_MM_TASK_HEAP)
    if(HEAPSIZE)
      set_target_properties(${TARGET} PROPERTIES APP_HEAP ${HEAPSIZE})
    else()
      set_target_properties(${TARGET} PROPERTIES APP_HEAP 0)
    endif()
  endif()

  # call target_ options only target is compilable
  if(NOT NO_COMPILABLE_TARGET)
    # compile options

    if(COMPILE_FLAGS)
      target_compile_options(${TARGET} PRIVATE ${COMPILE_FLAGS})
    endif()

    # compile definitions

    if(DEFINITIONS)
      target_compile_definitions(${TARGET} PRIVATE ${DEFINITIONS})
    endif()

    if(INCLUDE_DIRECTORIES)
      target_include_directories(${TARGET} BEFORE
                                 PRIVATE ${INCLUDE_DIRECTORIES})
    endif()

    # Set apps global compile options & definitions hold by nuttx_apps_interface
    target_compile_options(
      ${TARGET}
      PRIVATE
        $<GENEX_EVAL:$<TARGET_PROPERTY:nuttx_apps_interface,APPS_COMPILE_OPTIONS>>
    )
    target_compile_definitions(
      ${TARGET}
      PRIVATE
        $<GENEX_EVAL:$<TARGET_PROPERTY:nuttx_apps_interface,APPS_COMPILE_DEFINITIONS>>
    )
    target_include_directories(
      ${TARGET}
      PRIVATE
        $<GENEX_EVAL:$<TARGET_PROPERTY:nuttx_apps_interface,APPS_INCLUDE_DIRECTORIES>>
    )
  endif()

  # add supplied dependencies

  if(DEPENDS)
    # using target_link_libraries for dependencies provides linking as well as
    # interface include and libraries
    foreach(dep ${DEPENDS})
      nuttx_add_dependencies(TARGET ${TARGET} DEPENDS ${dep})
    endforeach()
  endif()
endfunction()
