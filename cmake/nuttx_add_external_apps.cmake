# ##############################################################################
# cmake/nuttx_add_external_apps.cmake
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
# Out-of-layout application directories ("source SDK" support).
#
# Lets application source trees live at any absolute path outside the apps/
# layout while keeping their internal layout IDENTICAL to an in-tree
# application. Only the build entry point moves; nothing inside the tree
# changes, so an out-of-layout root can later be moved under apps/ (e.g. to
# apps/external/) and will build with no edits at all.
#
# Registration is a plain ";"-separated list of directories in the
# NUTTX_EXTERNAL_APPS cache variable. There is deliberately no per-root menu
# syntax: menus come from the registered tree itself, exactly as in-tree.
#
# The two shapes an in-tree application directory can have are both supported,
# with no special casing here, because _nuttx_extapps_resolve_kconfig() applies
# the very same rule that nuttx_generate_kconfig() applies to apps/
# subdirectories:
#
# * flat application -- sources + Kconfig + CMakeLists.txt in the root.
#   Equivalent to apps/external/<someapp>/. Its Kconfig is sourced directly.
#
# * aggregate directory -- several application subdirectories, with a root
#   CMakeLists.txt doing nuttx_add_subdirectory() plus
#   nuttx_generate_kconfig(MENUDESC "..."). Equivalent to apps/external/ itself.
#   Its generated menu-wrapped Kconfig in NUTTX_APPS_BINDIR is sourced.
#
# Kconfig placement: all registered roots go into ONE fixed menu written to
# ${EXTERNALDIR}/Kconfig. nuttx/Kconfig already sources "$EXTERNALDIR/Kconfig"
# as a sibling of the "Application Configuration" menu, so the fixed menu
# appears at the same level WITHOUT modifying nuttx/Kconfig and WITHOUT
# polluting the auto-generated $APPSBINDIR/Kconfig.
#
# ##############################################################################

# Title of the single fixed menu holding every registered root. Named to mirror
# its sibling "Application Configuration".

if(NOT DEFINED NUTTX_EXTERNAL_APPS_MENUDESC)
  set(NUTTX_EXTERNAL_APPS_MENUDESC "External Application Configuration")
endif()

# ~~~
# _nuttx_extapps_resolve_kconfig(<dir> <outvar>)
#
# Resolve the Kconfig contribution of an application directory and return it in
# <outvar>, or the empty string when the directory contributes nothing.
#
# A directory that generated its own menu-wrapped Kconfig into NUTTX_APPS_BINDIR
# (i.e. it called nuttx_generate_kconfig(MENUDESC ...)) wins over a plain
# <dir>/Kconfig, because the generated file already sources the plain one's
# siblings below it.
#
# ~~~
# KEEP IN SYNC with the parent side of nuttx_generate_kconfig() in
# cmake/nuttx_kconfig.cmake, which applies this rule to apps/ subdirectories.
# The two must agree, otherwise an out-of-layout tree stops being laid out like
# an in-tree one and moving it under apps/ no longer works unchanged. The
# migration-equivalence test guards this: it compares the whole symbol set and
# menu paths between a registered root and the same directory placed under
# apps/external/, so any divergence in this rule fails it immediately.
# ~~~
# ~~~
function(_nuttx_extapps_resolve_kconfig dir outvar)
  string(REPLACE "/" "_" GENERATED "${dir}/Kconfig")
  if(WIN32)
    string(REPLACE ":" "_" GENERATED "${GENERATED}")
  endif()

  if(EXISTS ${NUTTX_APPS_BINDIR}/${GENERATED})
    set(${outvar}
        "${NUTTX_APPS_BINDIR}/${GENERATED}"
        PARENT_SCOPE)
  elseif(EXISTS "${dir}/Kconfig")
    set(${outvar}
        "${dir}/Kconfig"
        PARENT_SCOPE)
  else()
    set(${outvar}
        ""
        PARENT_SCOPE)
  endif()
endfunction()

# ~~~
# _nuttx_extapps_is_within(<child> <parent> <outvar>)
#
# TRUE when <child> is <parent> or lives below it. Both must be normalized
# absolute paths.
# ~~~
function(_nuttx_extapps_is_within child parent outvar)
  set(${outvar}
      FALSE
      PARENT_SCOPE)
  if("${parent}" STREQUAL "")
    return()
  endif()
  string(FIND "${child}/" "${parent}/" POS)
  if(POS EQUAL 0)
    set(${outvar}
        TRUE
        PARENT_SCOPE)
  endif()
endfunction()

# ~~~
# nuttx_add_external_apps(<dir> [<dir> ...])
#
# Register out-of-layout application roots. Called once from
# apps/CMakeLists.txt, after the built-in subdirectories (so external code can
# depend on apps libraries) and before nuttx_generate_kconfig().
#
# Runs on BOTH passes of apps/CMakeLists.txt. The build wiring must happen on
# each pass; the Kconfig aggregation follows the same "generate once per build
# directory" contract as nuttx_generate_kconfig() and is therefore skipped once
# ${NUTTX_APPS_BINDIR}/Kconfig exists.
#
# A bad entry is skipped with a warning and never aborts the build, so one
# stale registration cannot make the tree unconfigurable.
# ~~~
function(nuttx_add_external_apps)
  get_filename_component(APPS_REAL "${NUTTX_APPS_DIR}" REALPATH)
  get_filename_component(BINDIR_REAL "${CMAKE_BINARY_DIR}" REALPATH)

  set(ROOTS)

  foreach(ENTRY ${ARGN})
    if("${ENTRY}" STREQUAL "")
      continue()
    endif()

    # REALPATH also collapses a trailing slash and resolves symlinks, so that
    # "/a", "/a/" and a link to /a are recognized as the same root.
    get_filename_component(ROOT "${ENTRY}" REALPATH BASE_DIR
                           ${CMAKE_SOURCE_DIR})

    if(NOT IS_DIRECTORY "${ROOT}")
      message(WARNING "nuttx_add_external_apps: skipping '${ENTRY}' "
                      "(not a directory)")
      continue()
    endif()

    if(NOT EXISTS "${ROOT}/CMakeLists.txt")
      message(WARNING "nuttx_add_external_apps: skipping '${ROOT}' "
                      "(no CMakeLists.txt)")
      continue()
    endif()

    # An entry inside apps/ would be traversed by apps/ itself as well: the
    # second add_subdirectory() aborts on a duplicate binary directory and the
    # Kconfig would be sourced twice. This is the trap when a root is moved
    # in-tree but left registered, so report it as a skip rather than failing.
    _nuttx_extapps_is_within("${ROOT}" "${APPS_REAL}" INSIDE_APPS)
    if(INSIDE_APPS)
      message(
        WARNING "nuttx_add_external_apps: skipping '${ROOT}' (inside the apps "
                "directory, it is already built in-tree; drop the registration)"
      )
      continue()
    endif()

    _nuttx_extapps_is_within("${ROOT}" "${BINDIR_REAL}" INSIDE_BINDIR)
    if(INSIDE_BINDIR)
      message(WARNING "nuttx_add_external_apps: skipping '${ROOT}' "
                      "(inside the build directory)")
      continue()
    endif()

    if("${ROOT}" IN_LIST ROOTS)
      continue()
    endif()
    list(APPEND ROOTS "${ROOT}")

    # The root lives outside the build tree, so an explicit binary directory is
    # required. Derive a stable, unique name from the absolute path.
    string(REGEX REPLACE "[^a-zA-Z0-9]" "_" BINDIR_NAME "${ROOT}")
    add_subdirectory(${ROOT} extapps/${BINDIR_NAME})
  endforeach()

  # Kconfig aggregation. Generated once per build directory, together with the
  # rest of the apps Kconfig tree, so that the snapshot stays consistent and no
  # timestamp is touched on later configure runs. Changing the registration
  # therefore requires a fresh build directory, exactly as for apps/.
  if(EXISTS "${NUTTX_APPS_BINDIR}/Kconfig")
    return()
  endif()

  # Nothing registered: EXTERNALDIR still points at the upstream empty "dummy"
  # tree, which must be left untouched.
  if(NOT ROOTS)
    return()
  endif()

  # Refuse a relative EXTERNALDIR: file(WRITE) would resolve it against the
  # current source directory and drop a generated file into the source tree.
  if(NOT IS_ABSOLUTE "${EXTERNALDIR}")
    message(
      WARNING "nuttx_add_external_apps: EXTERNALDIR='${EXTERNALDIR}' is not an "
              "absolute path, the external Kconfig was not written")
    return()
  endif()

  # Resolved in a second pass: each root's own nuttx_generate_kconfig() ran
  # during its add_subdirectory() above, so its generated file exists by now.
  set(SOURCES)
  foreach(ROOT ${ROOTS})
    _nuttx_extapps_resolve_kconfig(${ROOT} ROOT_SOURCE)
    if(ROOT_SOURCE)
      string(APPEND SOURCES "source \"${ROOT_SOURCE}\"\n")
    endif()
  endforeach()

  set(CONTENT "# Autogenerated by nuttx_add_external_apps(). DO NOT EDIT.\n")
  if(NOT "${SOURCES}" STREQUAL "")
    string(APPEND CONTENT "menu \"${NUTTX_EXTERNAL_APPS_MENUDESC}\"\n")
    string(APPEND CONTENT "${SOURCES}")
    string(APPEND CONTENT "endmenu # ${NUTTX_EXTERNAL_APPS_MENUDESC}\n")
  endif()

  file(WRITE "${EXTERNALDIR}/Kconfig" "${CONTENT}")
endfunction()
