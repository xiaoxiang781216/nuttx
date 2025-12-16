# ##############################################################################
# drivers/segger/CMakeLists.txt
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

if(CONFIG_TRACE32_FDX)
  list(APPEND CSRCS ${CMAKE_CURRENT_LIST_DIR}/t32fdx/t32fdx.c
       ${CMAKE_CURRENT_LIST_DIR}/t32fdx_gettime.c)
endif()

if(CONFIG_DRIVERS_NOTET32FDX)
  list(APPEND CSRCS ${CMAKE_CURRENT_LIST_DIR}/note_fdx.c)
  if(NOT "${CONFIG_DRIVERS_NOTET32FDX_SECTION}" STREQUAL "")
    target_compile_definitions(drivers PRIVATE -DDRIVERS_NOTET32FDX_SECTION)
  endif()
endif()

if(CONFIG_STREAM_FDX)
  list(APPEND CSRCS ${CMAKE_CURRENT_LIST_DIR}/stream_fdx.c)
endif()

if(CONFIG_SYSLOG_FDX)
  list(APPEND CSRCS ${CMAKE_CURRENT_LIST_DIR}/syslog_fdx.c)
endif()

if(CONFIG_TRACE32_TERM)
  list(APPEND CSRCS ${CMAKE_CURRENT_LIST_DIR}/t32term/t32term.c
       ${CMAKE_CURRENT_LIST_DIR}/t32term/t32term_memory.c)
  target_compile_definitions(
    drivers
    PRIVATE
      -DT32_TERM_MEMORY_BLOCKED_SIZE=${CONFIG_TRACE32_TERM_MEMORY_BLOCKED_SIZE}
      -DT32_TERM_BLOCKED)
endif()

if(CONFIG_SERIAL_T32TERM)
  list(APPEND CSRCS ${CMAKE_CURRENT_LIST_DIR}/serial_t32term.c)
endif()

target_sources(drivers PRIVATE ${CSRCS})
