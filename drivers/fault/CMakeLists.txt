# ##############################################################################
# drivers/fault/CMakeLists.txt
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

if(CONFIG_DRIVERS_FAULT_INJECT)
  set(SRCS fault_core.c fault_dev.c)

  target_sources(drivers PRIVATE ${SRCS})
  target_include_directories(drivers PRIVATE ${CMAKE_CURRENT_LIST_DIR})

  # Build wrapsymbol host tool (same pattern as syscall/wraps/CMakeLists.txt)

  nuttx_build_host_target(wrapsymbol ${CMAKE_SOURCE_DIR}/tools/wrapsymbol.c)

  # Generate fault_wrapsymbol.cmd by preprocessing wraps/fault_wraps.h. Output
  # to CMAKE_BINARY_DIR (same as syscall's wrapsymbol.cmd).

  set(FAULT_WRAPS "${CMAKE_BINARY_DIR}/fault_wrapsymbol.cmd")

  nuttx_generate_preprocess_target(
    SOURCE_FILE ${CMAKE_CURRENT_LIST_DIR}/wraps/fault_wraps.h TARGET_FILE
    ${FAULT_WRAPS}tmp)

  add_custom_command(
    OUTPUT ${FAULT_WRAPS}
    COMMAND
      bash -c
      "sed -e '0,/WRAPOPTSTARTS/d' -e '/^#/d' ${FAULT_WRAPS}tmp > ${FAULT_WRAPS}"
    DEPENDS ${FAULT_WRAPS}tmp wrapsymbol
    VERBATIM)

  add_custom_target(fault_wrapsymbol DEPENDS ${FAULT_WRAPS})
  add_dependencies(nuttx_context fault_wrapsymbol)
endif()
