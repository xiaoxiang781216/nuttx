# ##############################################################################
# cmake/nuttx_add_buildinfo.cmake
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

# nuttx_add_note(<source-file> [DEPENDS <target>])
#
# Register an existing or to-be-generated file to embed in the
# .note.nuttx.buildinfo ELF section. The note's owner (KEY in 'readelf -n') is
# the file's basename. DEPENDS names a custom target whose output is the source
# file; it ensures the file exists before mkbuildinfo.py runs.

function(nuttx_add_note source)
  cmake_parse_arguments(ARG "" "DEPENDS" "" ${ARGN})

  get_filename_component(abs_src "${source}" ABSOLUTE)
  get_filename_component(key "${abs_src}" NAME)

  # Duplicate-key detection happens at build time inside mkbuildinfo.py.
  set_property(GLOBAL APPEND PROPERTY NUTTX_BUILDINFO_ENTRIES "${key}"
                                      "${abs_src}")
  set_property(GLOBAL APPEND PROPERTY NUTTX_BUILDINFO_SOURCES "${abs_src}")
  if(ARG_DEPENDS)
    set_property(GLOBAL APPEND PROPERTY NUTTX_BUILDINFO_DEPENDENCIES
                                        "${ARG_DEPENDS}")
  endif()
endfunction()

# nuttx_generate_buildinfo: Generate the assembly file and attach it to the
# given target. Called once from the top-level CMakeLists.txt.

function(nuttx_generate_buildinfo target)
  get_property(entries GLOBAL PROPERTY NUTTX_BUILDINFO_ENTRIES)
  if(NOT entries)
    return()
  endif()
  get_property(src_deps GLOBAL PROPERTY NUTTX_BUILDINFO_SOURCES)
  get_property(target_deps GLOBAL PROPERTY NUTTX_BUILDINFO_DEPENDENCIES)

  set(gen_s ${CMAKE_BINARY_DIR}/buildinfo/nuttx_buildinfo.S)

  add_custom_command(
    OUTPUT ${gen_s}
    COMMAND ${Python3_EXECUTABLE} ${NUTTX_DIR}/tools/mkbuildinfo.py --output
            ${gen_s} --entries ${entries}
    DEPENDS ${src_deps} ${NUTTX_DIR}/tools/mkbuildinfo.py
    COMMAND_EXPAND_LISTS
    COMMENT "Generating nuttx_buildinfo.S")

  add_custom_target(nuttx_buildinfo_gen DEPENDS ${gen_s})
  if(target_deps)
    add_dependencies(nuttx_buildinfo_gen ${target_deps})
  endif()

  target_sources(${target} PRIVATE ${gen_s})
  add_dependencies(${target} nuttx_buildinfo_gen)
  # xt-ld (Xtensa) rejects orphan sections by default; ARM bfd/lld place them
  # automatically. Force placement only when the user has not opted into
  # --orphan-handling=warn via CONFIG_DEBUG_LINK_ORPHAN_WARN, so that the debug
  # knob continues to surface unexpected orphans.
  if(NOT CONFIG_DEBUG_LINK_ORPHAN_WARN)
    target_link_options(${target} PRIVATE -Wl,--orphan-handling=place)
  endif()
endfunction()
