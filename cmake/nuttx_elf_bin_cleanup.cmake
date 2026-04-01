# ##############################################################################
# cmake/nuttx_elf_bin_cleanup.cmake
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

# Removes stale ELF files from bin/, bin_debug/, and link_cmd/ that are not
# listed in .elf_manifest. Invoked via: cmake -DBINARY_DIR=<dir> -P
# nuttx_elf_bin_cleanup.cmake

if(NOT DEFINED BINARY_DIR)
  message(FATAL_ERROR "BINARY_DIR must be defined")
endif()

set(MANIFEST_FILE "${BINARY_DIR}/.elf_manifest")

if(NOT EXISTS "${MANIFEST_FILE}")
  return()
endif()

file(STRINGS "${MANIFEST_FILE}" EXPECTED_ELFS)

# clean_directory: remove files not in EXPECTED_ELFS list
#
# DIR    - directory to scan SUFFIX - filename suffix to strip before matching
# (e.g. ".cmd")

macro(clean_directory DIR SUFFIX)
  if(IS_DIRECTORY "${DIR}")
    file(GLOB _existing_files "${DIR}/*")
    foreach(_fpath ${_existing_files})
      get_filename_component(_fname "${_fpath}" NAME)
      if(NOT "${SUFFIX}" STREQUAL "")
        string(REGEX REPLACE "${SUFFIX}$" "" _elf_name "${_fname}")
      else()
        set(_elf_name "${_fname}")
      endif()
      list(FIND EXPECTED_ELFS "${_elf_name}" _idx)
      if(_idx EQUAL -1)
        message(STATUS "elf_bin_cleanup: removing stale ${_fpath}")
        file(REMOVE "${_fpath}")
      endif()
    endforeach()
  endif()
endmacro()

clean_directory("${BINARY_DIR}/bin" "")
clean_directory("${BINARY_DIR}/bin_debug" "")
clean_directory("${BINARY_DIR}/link_cmd" "\\.cmd")
