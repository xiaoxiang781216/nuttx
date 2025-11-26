# ##############################################################################
# cmake/nuttx_syscall_utils.cmake
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

# The parse_syscall_csv function parses a list of CSV files that contain all the
# system calls prototypes. it returns a list of expected file names with `.c` as
# the suffix. Usage: parse_syscall_csv(OUTPUT syscall1.csv syscall2.csv ...)

function(parse_syscall_csv OUTPUT_VAR)
  # Initialize empty result list
  set(RESULT_LIST)

  # Process each input CSV file
  foreach(CSV_FILE ${ARGN})
    # Read CSV file and store each line as a list item
    file(STRINGS ${CSV_FILE} SYSCALLS)

    # Extract first column (before comma) from each line
    list(TRANSFORM SYSCALLS REPLACE "^\"([^,]+)\",.+" "\\1")

    # Append .c to each item
    list(TRANSFORM SYSCALLS APPEND ".c")

    # Append to our result list
    list(APPEND RESULT_LIST ${SYSCALLS})
  endforeach()

  # Remove any potential duplicates
  list(REMOVE_DUPLICATES RESULT_LIST)

  # Store the result in the output variable
  set(${OUTPUT_VAR}
      ${RESULT_LIST}
      PARENT_SCOPE)
endfunction()

# This function parses will generate the system call files for the given
# syscalls parsed from the CSV files.
#
# MODE can be either STUB, PROXY or WRAP.
#
# OUTPATH is the output directory.
#
# ALL_SYSCALLS is a list of all the system calls to generate. They are suffixed
# by ".c"
#
# CSV_FILES is a list of all the CSV files that will feed to the mksyscall tool.
#
# Usage: generate_syscall_files(OUTPUT_VAR GENERATED_PROXIES MODE PROXY OUTPATH
# "${CMAKE_CURRENT_BINARY_DIR}" ALL_SYSCALLS ${SYSCALLS} CSV_FILES
# ${CMAKE_CURRENT_LIST_DIR}/../syscall.csv)

function(generate_syscall_files)
  # Parse function arguments
  set(OPTIONS)
  set(ONEVALUEARG OUTPUT_VAR OUTPATH MODE)
  set(MULTIVALUEARGS ALL_SYSCALLS CSV_FILES)
  cmake_parse_arguments(ARG "${OPTIONS}" "${ONEVALUEARG}" "${MULTIVALUEARGS}"
                        ${ARGN})

  # Validate arguments
  if(NOT ARG_OUTPUT_VAR)
    message(FATAL_ERROR "OUTPUT_VAR argument is required")
  endif()

  if(NOT ARG_OUTPATH)
    set(ARG_OUTPATH "${CMAKE_CURRENT_BINARY_DIR}")
  endif()

  if(NOT ARG_MODE MATCHES "^(STUB|PROXY|WRAP)$")
    message(FATAL_ERROR "MODE must be either STUB, PROXY or WRAP")
  endif()

  if(NOT ARG_CSV_FILES)
    message(FATAL_ERROR "At least one CSV file must be specified")
  endif()

  if(NOT ARG_ALL_SYSCALLS)
    message(FATAL_ERROR "At least one syscall must be specified")
  endif()

  if(ARG_MODE STREQUAL "STUB")
    list(TRANSFORM ARG_ALL_SYSCALLS PREPEND "${ARG_OUTPATH}/STUB_")
    set(MKSYSCALL_MODE "-s")
  elseif(ARG_MODE STREQUAL "PROXY")
    list(TRANSFORM ARG_ALL_SYSCALLS PREPEND "${ARG_OUTPATH}/PROXY_")
    set(MKSYSCALL_MODE "-p")
  else()
    list(TRANSFORM ARG_ALL_SYSCALLS PREPEND "${ARG_OUTPATH}/WRAP_")
    set(MKSYSCALL_MODE "-w")
  endif()

  # Generate the custom command for all files
  add_custom_command(
    OUTPUT ${ARG_ALL_SYSCALLS}
    COMMAND ${CMAKE_BINARY_DIR}/bin_host/mksyscall ${MKSYSCALL_MODE}
            ${ARG_CSV_FILES} ${ARG_TYPE_REFERENCES}
    WORKING_DIRECTORY ${ARG_OUTPATH}
    DEPENDS mksyscall
    COMMENT "Generating system call ${ARG_MODE}s")

  # Set output variable
  set(${ARG_OUTPUT_VAR}
      ${ARG_ALL_SYSCALLS}
      PARENT_SCOPE)
endfunction()
