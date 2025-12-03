# ##############################################################################
# cmake/nuttx_generate_dts.cmake
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

set(KCONFIG_CONFIG ${CMAKE_ARGV3})
set(NUTTX_DIR ${CMAKE_ARGV4})
set(CMAKE_BINARY_DIR ${CMAKE_ARGV5})

include(cmake/nuttx_kconfig.cmake)
nuttx_export_kconfig(${KCONFIG_CONFIG})

set(DTS_SRCS "")
set(CUSTOM_OVERLAY_DTS_FILE "")

# The directory containing devicetree related scripts.
set(DT_SCRIPTS_DIR ${NUTTX_DIR}/tools/devicetree)

# This parses and collects the DT information
set(GEN_EDT_SCRIPT ${DT_SCRIPTS_DIR}/gen_edt.py)
# This generates DT information needed by the C macro APIs, along with a few
# other things.
set(GEN_DEFINES_SCRIPT ${DT_SCRIPTS_DIR}/gen_defines.py)
# The generated file containing the final DTS, for debugging.
set(DTS_FILE ${CMAKE_BINARY_DIR}/dt/nuttx.dts)
# The edtlib.EDT object in pickle format.
set(EDT_PICKLE_FILE ${CMAKE_BINARY_DIR}/dt/edt.pickle)
# The generated C header needed by <nuttx/devicetree.h>
set(GEN_DEFINES ${CMAKE_BINARY_DIR}/include/nuttx/devicetree_generated.h)
# Generated build system internals.
set(DTS_PRE_FILE ${CMAKE_BINARY_DIR}/dt/nuttx.dts.pre)
set(DTS_DEPS_FILE ${CMAKE_BINARY_DIR}/dt/nuttx.dts.pre.d)

# The source files to be preprocessed.
set(DT_PREPROCESS_SOURCE_FILES "")
# The root directory of the devicetree bindings files.
set(DTS_ROOT_BINDINGS ${NUTTX_DIR}/devicetree/bindings)
# The include directories for the devicetree preprocessor.
set(DTS_PREPROCESS_INCLUDE_DIRECTORIES
    ${NUTTX_DIR}/include ${NUTTX_DIR}/devicetree ${NUTTX_DIR}/devicetree/common)

# Create a directory for the devicetree output files.
if(NOT EXISTS ${CMAKE_BINARY_DIR}/dt)
  file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/dt)
endif()

function(parse_dts_config)
  # Get .dts files from defconfig
  string(REGEX REPLACE "," ";" dirlist "${CONFIG_DTS_SOURCE_DTR}")
  foreach(dir ${dirlist})
    get_filename_component(abs_dir ${dir} ABSOLUTE)
    if(IS_DIRECTORY ${abs_dir})
      file(GLOB_RECURSE dts_files ${abs_dir}/*.dts)
      list(APPEND DTS_SRCS ${dts_files})
    endif()
  endforeach()

  # Get .overlay files from defconfig
  string(REGEX REPLACE "," ";" dirlist "${CONFIG_DTS_OVERLAY_DIR}")
  foreach(dir ${dirlist})
    get_filename_component(abs_dir ${dir} ABSOLUTE)
    if(IS_DIRECTORY ${abs_dir})
      file(GLOB_RECURSE overlay_files ${abs_dir}/*.overlay)
      list(APPEND CUSTOM_OVERLAY_DTS_FILE ${overlay_files})
    endif()
  endforeach()

  # Get .dts file by CONFIG_ARCH_BOARD name if it can not get from defconfig.
  if("${DTS_SRCS}" STREQUAL "")
    foreach(dir ${NUTTX_BOARD_ABS_DIR})
      if(EXISTS ${dir}/src/${CONFIG_ARCH_BOARD}.dts)
        list(APPEND DTS_SRCS ${dir}/src/${CONFIG_ARCH_BOARD}.dts)
      endif()
    endforeach()
  endif()
  list(APPEND DT_PREPROCESS_SOURCE_FILES ${DTS_SRCS} ${CUSTOM_OVERLAY_DTS_FILE})

  # Get custom bindings directory from defconfig
  string(REGEX REPLACE "," ";" dirlist "${CONFIG_DTS_CUSTOM_BINDINGS_DIR}")
  set(abs_dirlist "")
  foreach(dir ${dirlist})
    get_filename_component(abs_dir ${dir} ABSOLUTE)
    list(APPEND abs_dirlist ${abs_dir})
  endforeach()
  list(APPEND DTS_ROOT_BINDINGS ${abs_dirlist})

  # Get custom dt-bindings directory from defconfig
  # CONFIG_DTS_CUSTOM_DT_BINDINGS_INCLUDE_DIR
  string(REGEX REPLACE "," ";" dirlist
                       "${CONFIG_DTS_CUSTOM_DT_BINDINGS_INCLUDE_DIR}")
  set(abs_dirlist "")
  foreach(dir ${dirlist})
    get_filename_component(abs_dir ${dir} ABSOLUTE)
    list(APPEND abs_dirlist ${abs_dir})
  endforeach()
  list(APPEND DTS_PREPROCESS_INCLUDE_DIRECTORIES ${abs_dirlist})

  set(DTS_SRCS
      "${DTS_SRCS}"
      PARENT_SCOPE)
  set(DT_PREPROCESS_SOURCE_FILES
      "${DT_PREPROCESS_SOURCE_FILES}"
      PARENT_SCOPE)
  set(DTS_ROOT_BINDINGS
      "${DTS_ROOT_BINDINGS}"
      PARENT_SCOPE)
  set(DTS_PREPROCESS_INCLUDE_DIRECTORIES
      "${DTS_PREPROCESS_INCLUDE_DIRECTORIES}"
      PARENT_SCOPE)
endfunction()

# Preprocess device tree source files using the C preprocessor
function(dts_preprocess)
  set(include_opts)
  foreach(dir ${DTS_PREPROCESS_INCLUDE_DIRECTORIES})
    list(APPEND include_opts -isystem ${dir})
  endforeach()

  set(source_opts)
  foreach(file ${DT_PREPROCESS_SOURCE_FILES})
    list(APPEND source_opts -include ${file})
  endforeach()

  set(CMD_PREPROCESS
      cpp
      -x
      assembler-with-cpp
      -nostdinc
      ${include_opts}
      ${source_opts}
      -D__DTS__
      ${DT_PREPROCESS_EXTRA_CPPFLAGS}
      -E # Stop after preprocessing
      -MD
      -MF
      ${DTS_DEPS_FILE}
      -o
      ${DTS_PRE_FILE}
      ${NUTTX_DIR}/devicetree/common/empty_file.c)

  execute_process(COMMAND ${CMD_PREPROCESS} RESULT_VARIABLE PREPROCESS_RESULT)
  if(NOT "${PREPROCESS_RESULT}" STREQUAL "0")
    message(FATAL_ERROR "failed to preprocess devicetree files
                (error code ${ret}): ${DT_PREPROCESS_SOURCE_FILES}")
  endif()
  message(STATUS "Generated nuttx.dts.pre: ${DTS_PRE_FILE}")
endfunction()

# Generate edt pickle file using gen_edt.py
function(gen_edt)
  set(CMD_GEN_EDT
      ${PYTHON_EXECUTABLE}
      ${GEN_EDT_SCRIPT}
      --dts
      ${DTS_PRE_FILE}
      --dtc-flags
      '${EXTRA_DTC_FLAGS_RAW}'
      --bindings-dirs
      ${DTS_ROOT_BINDINGS}
      --dts-out
      ${DTS_FILE}.new
      --edt-pickle-out
      ${EDT_PICKLE_FILE}.new)
  execute_process(
    COMMAND ${CMD_GEN_EDT}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    RESULT_VARIABLE GEN_EDT_RESULT)
  if(NOT "${GEN_EDT_RESULT}" STREQUAL "0")
    message(FATAL_ERROR "failed to generate edt pickle file
            (error code ${GEN_EDT_RESULT})")
  endif()
  file(COPY_FILE ${DTS_FILE}.new ${DTS_FILE} ONLY_IF_DIFFERENT)
  file(COPY_FILE ${EDT_PICKLE_FILE}.new ${EDT_PICKLE_FILE} ONLY_IF_DIFFERENT)
  file(REMOVE ${DTS_FILE}.new ${EDT_PICKLE_FILE}.new)
  message(STATUS "Generated nuttx.dts: ${DTS_FILE}")
  message(STATUS "Generated pickled edt: ${EDT_PICKLE_FILE}")
endfunction(gen_edt)

# Generate devicetree_generated.h using gen_defines.py
function(gen_devicetree_header)
  set(CMD_GEN_DEFINES ${PYTHON_EXECUTABLE} ${GEN_DEFINES_SCRIPT} --edt-pickle
                      ${EDT_PICKLE_FILE} --header-out ${GEN_DEFINES}.new)
  execute_process(
    COMMAND ${CMD_GEN_DEFINES}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    RESULT_VARIABLE GEN_DEFINES_RESULT)
  file(COPY_FILE ${GEN_DEFINES}.new ${GEN_DEFINES} ONLY_IF_DIFFERENT)
  file(REMOVE ${GEN_DEFINES}.new)
  message(STATUS "Generated devicetree_generated.h: ${GEN_DEFINES}")
endfunction()

# ~~~
# nuttx_generate_dts
#
# Description:
#   Function to generate the devicetree_generated.h header and nuttx.dts files.
#
# ~~~
function(nuttx_generate_dts)
  if(NOT "${CONFIG_DEVICETREE_HEADER_GENERATION}" STREQUAL "y")
    message(STATUS "Device tree header generation disabled
                skipped device tree generation")
    return()
  endif()

  parse_dts_config()

  # Check if DTS_SRCS is empty
  if("${DTS_SRCS}" STREQUAL "")
    message(STATUS "no device tree source files specified (DTS_SRCS),
                skipped device tree generation")
    return()
  endif()

  # Preprocess device tree source files
  dts_preprocess()
  # Generate edt pickle file using gen_edt.py
  gen_edt()
  # Generate devicetree_generated.h using gen_defines.py
  gen_devicetree_header()

endfunction()

nuttx_generate_dts()
