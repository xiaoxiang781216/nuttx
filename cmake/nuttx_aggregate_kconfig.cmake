# ##############################################################################
# cmake/nuttx_aggregate_kconfig.cmake
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
#
# Layered "Kconfig.defaults" merge, driven by the build system instead of
# hard-coded osource lines in nuttx/Kconfig.
#
# The generated aggregate root osources the per-layer fragments (highest
# priority first), then sources the untouched nuttx/Kconfig. kconfiglib merges
# same-named config blocks into one Symbol and concatenates their defaults in
# parse order, so the first satisfied default wins:
#
# ~~~
#   APPLICATION  (100) nuttx_add_kconfig_default(), from application CMakeLists
#   BOARD_CONFIG (200) <board defconfig dir>/Kconfig.defaults
#   BOARD        (300) NUTTX_BOARD_ABS_DIR/Kconfig.defaults
#   SOC          (400) NUTTX_CHIP_ABS_DIR/Kconfig.defaults
#   nuttx/Kconfig      sourced last, provides symbol bodies and the mainmenu
# ~~~
#
# A minimal defconfig is therefore no longer self-contained: its expansion
# depends on the fragments in effect, and changing a fragment changes every
# defconfig relying on it.
#
# The Kconfig tools run with the nuttx directory as working directory
# ($srctree), so the trailing `source "Kconfig"` resolves to nuttx/Kconfig.
#
# ##############################################################################

# Priority baseline per layer, smaller value = osourced earlier = higher
# priority. Application callers may pass any PRIORITY, e.g. to sit between two
# layers.

set(NUTTX_KCONFIG_DEFAULT_PRIORITY_APP 100)
set(NUTTX_KCONFIG_DEFAULT_PRIORITY_BOARD_CONFIG 200)
set(NUTTX_KCONFIG_DEFAULT_PRIORITY_BOARD 300)
set(NUTTX_KCONFIG_DEFAULT_PRIORITY_SOC 400)

# Zero-pad width for the sort key, so that a plain string sort is also the
# numeric priority order

set(NUTTX_KCONFIG_DEFAULT_PRIORITY_WIDTH 6)

set(NUTTX_AGGREGATE_KCONFIG_FILE ${CMAKE_BINARY_DIR}/Kconfig.aggregate)

# nuttx_kconfig_top(<out-var>)
#
# Return the top-level Kconfig for the Kconfig tools: the aggregate root when it
# has been generated, the plain nuttx/Kconfig otherwise. Every entry point that
# parses Kconfig must use it, since a tool parsing a different root recomputes
# the defaults of all invisible symbols from the base tree alone and thereby
# drops the values contributed by the layers.

function(nuttx_kconfig_top outvar)
  if(EXISTS "${NUTTX_AGGREGATE_KCONFIG_FILE}")
    set(${outvar}
        "${NUTTX_AGGREGATE_KCONFIG_FILE}"
        PARENT_SCOPE)
  else()
    set(${outvar}
        "${NUTTX_DIR}/Kconfig"
        PARENT_SCOPE)
  endif()
endfunction()

# nuttx_kconfig_default_record(<priority> <abspath>)
#
# Append a "<zero-padded-priority>|<abs-path>" entry to the global fragment
# collection. PRIORITY must be a non-negative integer, anything else would break
# the padded string ordering.

function(nuttx_kconfig_default_record priority abspath)
  if(NOT "${priority}" MATCHES "^[0-9]+$")
    message(
      FATAL_ERROR
        "nuttx_kconfig_default_record: PRIORITY must be a non-negative "
        "integer, got '${priority}' (fragment: ${abspath})")
  endif()

  # Osourcing the same fragment twice would append its defaults twice
  get_property(KNOWN GLOBAL PROPERTY NUTTX_KCONFIG_DEFAULT_PATHS)
  if("${abspath}" IN_LIST KNOWN)
    return()
  endif()

  string(LENGTH "${priority}" LEN)
  math(EXPR PAD "${NUTTX_KCONFIG_DEFAULT_PRIORITY_WIDTH} - ${LEN}")
  set(KEY "${priority}")
  while(PAD GREATER 0)
    set(KEY "0${KEY}")
    math(EXPR PAD "${PAD} - 1")
  endwhile()

  set_property(GLOBAL APPEND PROPERTY NUTTX_KCONFIG_DEFAULT_PATHS "${abspath}")
  set_property(GLOBAL APPEND PROPERTY NUTTX_KCONFIG_DEFAULT_FRAGS
                                      "${KEY}|${abspath}")
endfunction()

# nuttx_add_kconfig_default(FILE <path> [PRIORITY <int>])
#
# Register an application-level Kconfig.defaults fragment. May be called
# multiple times. PRIORITY defaults to NUTTX_KCONFIG_DEFAULT_PRIORITY_APP.
#
# Must be called before nuttx_generate_kconfig_aggregate(), i.e. from the early
# apps pass. Do not guard the call with if(CONFIG_...): the CONFIG_ variables
# are not exported yet during that pass.

function(nuttx_add_kconfig_default)
  cmake_parse_arguments(ARG "" "FILE;PRIORITY" "" ${ARGN})

  if(NOT ARG_FILE)
    message(FATAL_ERROR "nuttx_add_kconfig_default: FILE is required")
  endif()

  if("${ARG_PRIORITY}" STREQUAL "")
    set(ARG_PRIORITY ${NUTTX_KCONFIG_DEFAULT_PRIORITY_APP})
  endif()

  get_filename_component(FRAGMENT "${ARG_FILE}" ABSOLUTE)

  # The caller named this file explicitly, so a missing file is more likely a
  # typo than an opt-out. The conventional per-layer scan stays silent instead.
  if(NOT EXISTS "${FRAGMENT}")
    message(
      WARNING
        "nuttx_add_kconfig_default: fragment not found, ignored: ${FRAGMENT}")
    return()
  endif()

  # An application CMakeLists is processed twice (early pass, then build pass),
  # so a repeated registration must stay silent rather than be reported below
  get_property(KNOWN GLOBAL PROPERTY NUTTX_KCONFIG_DEFAULT_PATHS)
  if("${FRAGMENT}" IN_LIST KNOWN)
    return()
  endif()

  get_property(GENERATED GLOBAL PROPERTY NUTTX_KCONFIG_DEFAULT_GENERATED)
  if(GENERATED)
    message(
      WARNING
        "nuttx_add_kconfig_default: called after the Kconfig aggregate was "
        "generated, ignored: ${FRAGMENT}. Register the fragment earlier "
        "(before the initial olddefconfig).")
    return()
  endif()

  nuttx_kconfig_default_record(${ARG_PRIORITY} "${FRAGMENT}")
endfunction()

# nuttx_kconfig_scan_layer(<dir> <priority>)
#
# Register <dir>/Kconfig.defaults if it exists. CONFIGURE_DEPENDS re-evaluates
# the glob before every build, which is what makes a fragment that does not
# exist yet detectable: without it a Kconfig.defaults appearing in the tree
# would leave the build directory with an aggregate root that disagrees with the
# sources.

function(nuttx_kconfig_scan_layer dir priority)
  if(NOT dir)
    return()
  endif()

  file(GLOB FOUND CONFIGURE_DEPENDS "${dir}/Kconfig.defaults")
  if(FOUND)
    nuttx_kconfig_default_record(${priority} "${dir}/Kconfig.defaults")
  endif()
endfunction()

# nuttx_kconfig_default_paths(<out-var>)
#
# Return the registered fragments as absolute paths in priority order. Entries
# sharing a priority end up ordered by path.

function(nuttx_kconfig_default_paths outvar)
  get_property(FRAGS GLOBAL PROPERTY NUTTX_KCONFIG_DEFAULT_FRAGS)
  if(FRAGS)
    list(SORT FRAGS)
  endif()

  set(PATHS)
  foreach(ENTRY ${FRAGS})
    string(REGEX REPLACE "^[0-9]+\\|" "" FRAGMENT "${ENTRY}")
    list(APPEND PATHS "${FRAGMENT}")
  endforeach()

  set(${outvar}
      "${PATHS}"
      PARENT_SCOPE)
endfunction()

# nuttx_aggregate_kconfig_signature(<out-var>)
#
# Return a digest of the layer list AND of every fragment's content. The
# aggregate root only references fragments by path, so this is what detects that
# .config has to be re-expanded: an expanded .config holds a user value for
# every symbol, and a changed default can no longer propagate into it.

function(nuttx_aggregate_kconfig_signature outvar)
  nuttx_kconfig_default_paths(PATHS)

  # Newline separated to keep the material free of CMake list separators
  set(MATERIAL "")
  foreach(FRAGMENT ${PATHS})
    set(HASH "missing")
    if(EXISTS "${FRAGMENT}")
      file(SHA256 "${FRAGMENT}" HASH)
    endif()
    string(APPEND MATERIAL "${FRAGMENT}\n${HASH}\n")
  endforeach()

  string(SHA256 DIGEST "${MATERIAL}")
  set(${outvar}
      "${DIGEST}"
      PARENT_SCOPE)
endfunction()

# nuttx_generate_kconfig_aggregate([OUTPUT <file>] [BOARD_CONFIG_DIR <dir>]
# [BOARD_DIR <dir>] [SOC_DIR <dir>])
#
# Register the conventional BOARD_CONFIG/BOARD/SOC fragments, merge them with
# the application fragments and write the aggregate root. All arguments are
# optional and default to NUTTX_AGGREGATE_KCONFIG_FILE,
# NUTTX_BOARD_DEFCONFIG_DIR, NUTTX_BOARD_ABS_DIR and NUTTX_CHIP_ABS_DIR.
#
# Must be called on every configure run, because the aggregate root is also the
# top-level Kconfig of menuconfig / resetconfig / savedefconfig / setconfig. The
# write is idempotent so that an unchanged layer set leaves the timestamp alone.

function(nuttx_generate_kconfig_aggregate)
  cmake_parse_arguments(ARG "" "OUTPUT;BOARD_CONFIG_DIR;BOARD_DIR;SOC_DIR" ""
                        ${ARGN})

  if(NOT ARG_OUTPUT)
    set(ARG_OUTPUT ${NUTTX_AGGREGATE_KCONFIG_FILE})
  endif()
  if(NOT ARG_BOARD_CONFIG_DIR)
    set(ARG_BOARD_CONFIG_DIR ${NUTTX_BOARD_DEFCONFIG_DIR})
  endif()
  if(NOT ARG_BOARD_DIR)
    set(ARG_BOARD_DIR ${NUTTX_BOARD_ABS_DIR})
  endif()
  if(NOT ARG_SOC_DIR)
    set(ARG_SOC_DIR ${NUTTX_CHIP_ABS_DIR})
  endif()

  nuttx_kconfig_scan_layer("${ARG_BOARD_CONFIG_DIR}"
                           ${NUTTX_KCONFIG_DEFAULT_PRIORITY_BOARD_CONFIG})
  nuttx_kconfig_scan_layer("${ARG_BOARD_DIR}"
                           ${NUTTX_KCONFIG_DEFAULT_PRIORITY_BOARD})
  nuttx_kconfig_scan_layer("${ARG_SOC_DIR}"
                           ${NUTTX_KCONFIG_DEFAULT_PRIORITY_SOC})

  nuttx_kconfig_default_paths(PATHS)

  set(CONTENT
      "# Autogenerated by nuttx_generate_kconfig_aggregate(). DO NOT EDIT.\n")
  string(
    APPEND CONTENT
    "# Layered Kconfig.defaults (application > board-config > board > soc),\n")
  string(APPEND CONTENT "# then the base tree.\n\n")
  foreach(FRAGMENT ${PATHS})
    string(APPEND CONTENT "osource \"${FRAGMENT}\"\n")
  endforeach()

  # Base tree last, relative to $srctree
  string(APPEND CONTENT "\nsource \"Kconfig\"\n")

  set(PREVIOUS "")
  if(EXISTS "${ARG_OUTPUT}")
    file(READ "${ARG_OUTPUT}" PREVIOUS)
  endif()
  if(NOT "${PREVIOUS}" STREQUAL "${CONTENT}")
    file(WRITE "${ARG_OUTPUT}" "${CONTENT}")
    message(STATUS "Kconfig aggregate root generated: ${ARG_OUTPUT}")
    foreach(FRAGMENT ${PATHS})
      message(STATUS "  Kconfig.defaults layer: ${FRAGMENT}")
    endforeach()
  endif()

  # Editing a fragment re-triggers configure. Only existing files can be listed,
  # creation and deletion are covered by the glob in nuttx_kconfig_scan_layer().
  if(PATHS)
    set_property(
      DIRECTORY
      APPEND
      PROPERTY CMAKE_CONFIGURE_DEPENDS ${PATHS})
  endif()

  set_property(GLOBAL PROPERTY NUTTX_KCONFIG_DEFAULT_GENERATED TRUE)
endfunction()
