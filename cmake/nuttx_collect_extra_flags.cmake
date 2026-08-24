# ##############################################################################
# cmake/nuttx_collect_extra_flags.cmake
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
# ~~~
# nuttx_collect_extra_flags
#
# Description:
#   Collect the directory-level COMPILE_OPTIONS (e.g. -m32, -march, model
#   flags) into the global NUTTX_EXTRA_FLAGS property. Components that invoke
#   the compiler directly through add_custom_command (e.g. the external/pse52
#   partial link) read this property instead of the per-target COMPILE_OPTIONS,
#   so without it they would emit objects incompatible with the rest of the
#   image.
#
#   Compile-only flags (COMPILE_ONLY_FLAGS) are dropped, and generator
#   expressions of the form $<$<COMPILE_LANGUAGE:C>:...> are unwrapped to their
#   C payload. The collected list is exported both as the GLOBAL property
#   NUTTX_EXTRA_FLAGS and as a NUTTX_EXTRA_FLAGS variable in the caller scope.
#
# Usage:
#   nuttx_collect_extra_flags([EXCLUDE <regex> ...])
#
# Parameters:
#   EXCLUDE : optional list of regexes; matching plain flags are skipped
#             (e.g. tricore excludes "-nostdlib").
# ~~~
function(nuttx_collect_extra_flags)
  cmake_parse_arguments(ARG "" "" "EXCLUDE" ${ARGN})

  get_directory_property(TOOLCHAIN_DIR_FLAGS DIRECTORY ${CMAKE_SOURCE_DIR}
                                                       COMPILE_OPTIONS)

  set(_extra_flags "")
  foreach(FLAG ${TOOLCHAIN_DIR_FLAGS})
    if(FLAG IN_LIST COMPILE_ONLY_FLAGS)
      continue()
    endif()
    if(NOT FLAG MATCHES "^\\$<.*>$")
      set(_skip FALSE)
      foreach(_re ${ARG_EXCLUDE})
        if(FLAG MATCHES "${_re}")
          set(_skip TRUE)
          break()
        endif()
      endforeach()
      if(NOT _skip)
        list(APPEND _extra_flags ${FLAG})
      endif()
    else()
      string(REGEX MATCH "\\$<\\$<COMPILE_LANGUAGE:C>:(.*)>" matched ${FLAG})
      if(matched)
        list(APPEND _extra_flags ${CMAKE_MATCH_1})
      endif()
    endif()
  endforeach()

  set_property(GLOBAL PROPERTY NUTTX_EXTRA_FLAGS ${_extra_flags})
  set(NUTTX_EXTRA_FLAGS
      ${_extra_flags}
      PARENT_SCOPE)
endfunction()
