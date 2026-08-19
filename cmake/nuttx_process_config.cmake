# ##############################################################################
# cmake/nuttx_process_config.cmake
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

# save preprocess defconfig as orig by default
set(NUTTX_ORIG_DEFCONFIG ${NUTTX_DEFCONFIG})

function(process_config OUTPUT INPUT TREE_FILE)
  set(options)
  set(oneValueArgs)
  set(multiValueArgs INCLUDE_PATHS)
  cmake_parse_arguments(PARSE_ARGV 2 PROCESS_INCLUDES "${options}"
                        "${oneValueArgs}" "${multiValueArgs}")

  set(include_args "")
  foreach(path IN LISTS PROCESS_INCLUDES_INCLUDE_PATHS)
    list(APPEND include_args "${path}")
  endforeach()

  if(TREE_FILE)
    set(TREE_OPTION --tree)
  endif()
  message(STATUS "Processing includes: ${INPUT} → ${OUTPUT}")
  execute_process(
    COMMAND
      ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/process_config.py
      preprocess ${OUTPUT} ${INPUT} ${include_args} ${TREE_OPTION} ${TREE_FILE}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err)

  if(NOT result EQUAL 0)
    message(FATAL_ERROR "Failed to process includes:\n${err}")
  endif()
endfunction()

# fetch defconfig content
file(READ "${NUTTX_DEFCONFIG}" FILE_CONTENTS)
string(FIND "${FILE_CONTENTS}" "#include" INCLUDE_FOUND)

if(NOT EXISTS ${CMAKE_BINARY_DIR}/.defconfig.processed)
  set(TREE_FILE ${CMAKE_BINARY_DIR}/config_tree.json)
else()
  set(TREE_FILE ${CMAKE_BINARY_DIR}/config_tree_dirty.json)
endif()
# Should we preprocess defconfig?
if(INCLUDE_FOUND GREATER -1)
  get_filename_component(NUTTX_DEFCONFIG_DIR "${NUTTX_DEFCONFIG}" DIRECTORY)
  process_config(
    ${CMAKE_BINARY_DIR}/.defconfig.processed
    ${NUTTX_DEFCONFIG}
    ${TREE_FILE}
    INCLUDE_PATHS
    ${NUTTX_DEFCONFIG_DIR}/../../common/configs
    ${NUTTX_DEFCONFIG_DIR}/../common
    ${NUTTX_DEFCONFIG_DIR}
    ${NUTTX_DIR}/../apps
    ${NUTTX_DIR}/../nuttx-apps)
  set(NUTTX_DEFCONFIG ${CMAKE_BINARY_DIR}/.defconfig.processed)
endif()

# Kconfig value fragments ######################################################
#
# Overlay the fragments named by -DKCONFIG_FRAGMENTS onto the defconfig selected
# above, by appending them to a copy of it. Everything below this point in the
# configure is unchanged: NUTTX_DEFCONFIG simply names the assembled file.
#
# Deliberately a separate block rather than an extension of the preprocess step
# above: .defconfig.processed keeps meaning "the #include expansion of the board
# defconfig", which is what the config_tree.json decision above relies on, and
# what makes the pure base expansion available for comparison when something
# looks wrong.
#
# NUTTX_ORIG_DEFCONFIG was captured at the top of this file, so it still names
# the in-tree board defconfig -- the fragments never leak into it.

if(KCONFIG_FRAGMENTS)
  include(nuttx_kconfig_fragments)

  # A bare fragment name resolves next to the board defconfig and nowhere else.
  # NUTTX_BOARD_DEFCONFIG_DIR was captured in CMakeLists.txt before the
  # preprocess step above could rewrite NUTTX_DEFCONFIG, so it still names the
  # board's own config directory rather than the build directory.
  nuttx_kconfig_fragments_resolve(
    NUTTX_KCONFIG_FRAGMENT_PATHS NUTTX_KCONFIG_FRAGMENT_ERROR TOKENS
    ${KCONFIG_FRAGMENTS} DIR "${NUTTX_BOARD_DEFCONFIG_DIR}")

  if(NOT NUTTX_KCONFIG_FRAGMENT_ERROR STREQUAL "")
    message(FATAL_ERROR "Kconfig fragments could not be resolved:\n"
                        "${NUTTX_KCONFIG_FRAGMENT_ERROR}")
  endif()

  if(NUTTX_KCONFIG_FRAGMENT_PATHS)
    # The digest rides in the assembled file name, which is how a changed
    # fragment reaches the .config re-expansion gate in CMakeLists.txt: that
    # gate already compares NUTTX_DEFCONFIG against the cached
    # NUTTX_DEFCONFIG_SAVED, so no new condition is needed there. With no
    # fragments the name stays .defconfig.processed, and an existing build
    # directory therefore sees no change at all.
    nuttx_kconfig_fragments_signature(NUTTX_KCONFIG_FRAGMENT_SIG PATHS
                                      ${NUTTX_KCONFIG_FRAGMENT_PATHS})

    nuttx_kconfig_fragments_assemble(
      NUTTX_DEFCONFIG
      BASE
      "${NUTTX_DEFCONFIG}"
      PATHS
      ${NUTTX_KCONFIG_FRAGMENT_PATHS}
      SIGNATURE
      "${NUTTX_KCONFIG_FRAGMENT_SIG}"
      OUTPUT_DIR
      "${CMAKE_BINARY_DIR}")

    message(STATUS "Kconfig fragments (${NUTTX_KCONFIG_FRAGMENT_SIG}):")
    foreach(FRAGMENT ${NUTTX_KCONFIG_FRAGMENT_PATHS})
      message(STATUS "  ${FRAGMENT}")
    endforeach()

    # Editing a fragment has to re-run cmake, otherwise the digest above is
    # never recomputed and the stale .config survives
    set_property(
      DIRECTORY
      APPEND
      PROPERTY CMAKE_CONFIGURE_DEPENDS ${NUTTX_KCONFIG_FRAGMENT_PATHS})
  endif()
endif()
