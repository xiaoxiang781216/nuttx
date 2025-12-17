# ##############################################################################
# syscall/CMakeLists.txt
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
include(nuttx_syscall_utils)

# Get all the system call CSV files
set(SYSCALLFILES)
set(LOOKUPFILE "${CMAKE_BINARY_DIR}/include/syscall_lookup.h")
list(APPEND SYSCALLFILES ${NUTTX_DIR}/syscall/syscall.csv)
if(NOT CONFIG_LIB_SYSCALL_FILES STREQUAL "")
  foreach(file IN LISTS CONFIG_LIB_SYSCALL_FILES)
    list(APPEND SYSCALLFILES "${NUTTX_DIR}/${file}")
  endforeach()
endif()

# Parse CSV file to get expected output source files for each target
parse_syscall_csv(SYSCALLS ${SYSCALLFILES})

# Parse ppccall CSV file
set(PPCCALLFILES)
set(PPCLOOKUPFILE)
if(CONFIG_LIB_PPCCALL)
  foreach(file IN LISTS CONFIG_LIB_PPCCALL_FILES)
    list(APPEND PPCCALLFILES "${NUTTX_DIR}/${file}")
  endforeach()
  message(STATUS "PPCCALLFILES:" ${PPCCALLFILES})
  parse_syscall_csv(PPCCALLS ${PPCCALLFILES})
  set(PPCLOOKUPFILE "${CMAKE_BINARY_DIR}/include/ppccall_lookup.h")
endif()

if(CONFIG_LIB_SYSCALL_WRAPPER
   OR CONFIG_LIB_SYSCALL
   OR CONFIG_LIB_PPCCALL)
  # generate the host tool
  nuttx_build_host_target(mksyscall ${CMAKE_SOURCE_DIR}/tools/mksyscall.c)
endif()

if(CONFIG_LIB_SYSCALL_WRAPPER OR CONFIG_LIB_SYSCALL)
  # generate the syscall lookup table
  generate_syscall_files(
    OUTPUT_VAR
    GENERATED_LOOKUP
    MODE
    LOOKUP
    OUTPATH
    ${CMAKE_CURRENT_BINARY_DIR}
    ALL_SYSCALLS
    ${SYSCALLS}
    CSV_FILES
    ${SYSCALLFILES})

  add_custom_command(
    OUTPUT ${LOOKUPFILE}
    COMMAND cp ${CMAKE_CURRENT_BINARY_DIR}/syscall_lookup.h ${LOOKUPFILE}
    DEPENDS ${GENERATED_LOOKUP}
    COMMENT "Copying syscall_lookup.h to include"
    VERBATIM)

  add_custom_target(syscall_lookup_header DEPENDS ${LOOKUPFILE})
  add_dependencies(nuttx_context syscall_lookup_header)
endif()

if(CONFIG_LIB_SYSCALL)
  add_subdirectory(proxies)
  add_subdirectory(stubs)

  target_sources(stubs PRIVATE syscall_stublookup.c)
endif()

if(CONFIG_LIB_PPCCALL)
  add_subdirectory(ppcs)
endif()

if(CONFIG_LIB_SYSCALL_WRAPPER)
  add_subdirectory(wraps)
  target_sources(wraps PRIVATE syscall_names.c)
endif()
