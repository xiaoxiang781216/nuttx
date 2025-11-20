# ##############################################################################
# syscall/ppcs/CMakeLists.txt
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
nuttx_add_kernel_library(ppcs SPLIT)

generate_syscall_files(
  OUTPUT_VAR
  GENERATED_PPCS
  MODE
  PPC
  OUTPATH
  ${CMAKE_CURRENT_BINARY_DIR}
  ALL_SYSCALLS
  ${PPCCALLS}
  CSV_FILES
  ${PPCCALLFILES})

add_custom_command(
  OUTPUT ${PPCLOOKUPFILE}
  COMMAND cp ${CMAKE_CURRENT_BINARY_DIR}/ppccall_lookup.h ${PPCLOOKUPFILE}
  DEPENDS ${GENERATED_PPCS}
  COMMENT "Copying ppccall_lookup.h to include/nuttx"
  VERBATIM)

set(PPCCALL_PRIVATE_HEADER "${CMAKE_CURRENT_BINARY_DIR}/ppccall.h")
add_custom_command(
  OUTPUT ${PPCCALL_PRIVATE_HEADER}
  COMMAND cp ${NUTTX_DIR}/syscall/ppccall.h ${PPCCALL_PRIVATE_HEADER}
  DEPENDS ${NUTTX_DIR}/syscall/ppccall.h
  COMMENT "Copying ppccall.h to include/nuttx"
  VERBATIM)

add_custom_target(ppccall_headers DEPENDS ${PPCLOOKUPFILE}
                                          ${PPCCALL_PRIVATE_HEADER})
add_dependencies(nuttx_context ppccall_headers)

# add sources to target
target_sources(ppcs PRIVATE ${GENERATED_PPCS})
