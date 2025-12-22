# ##############################################################################
# syscall/wraps/CMakeLists.txt
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
nuttx_add_kernel_library(wraps)

generate_syscall_files(
  OUTPUT_VAR
  GENERATED_WRAPS
  MODE
  WRAP
  OUTPATH
  ${CMAKE_CURRENT_BINARY_DIR}
  ALL_SYSCALLS
  ${SYSCALLS}
  CSV_FILES
  ${CMAKE_CURRENT_LIST_DIR}/../syscall.csv)

set(SYSCALLWRAPS "${CMAKE_BINARY_DIR}/wrapsymbol.cmd")

nuttx_generate_preprocess_target(
  SOURCE_FILE ${CMAKE_CURRENT_LIST_DIR}/../syscall_wraps.h TARGET_FILE
  ${SYSCALLWRAPS}tmp)

add_custom_command(
  OUTPUT ${SYSCALLWRAPS}
  COMMAND
    bash -c
    "sed -e '1,/WRAPOPTSTARTS/d' -e '/^#/d' ${SYSCALLWRAPS}tmp > ${SYSCALLWRAPS}"
  DEPENDS ${SYSCALLWRAPS}tmp wrapsymbol
  VERBATIM)

add_custom_target(syscallwraps DEPENDS ${SYSCALLWRAPS})
add_dependencies(nuttx_context syscallwraps)
nuttx_build_host_target(wrapsymbol ${CMAKE_SOURCE_DIR}/tools/wrapsymbol.c)

# add sources to target
target_sources(wraps PRIVATE ${GENERATED_WRAPS})
