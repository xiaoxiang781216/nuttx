# ##############################################################################
# drivers/note/CMakeLists.txt
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
set(SRCS)

if(CONFIG_DRIVERS_NOTE)
  list(APPEND SRCS note_driver.c)
  list(APPEND SRCS note_initialize.c)
endif()

if(CONFIG_DRIVERS_NOTEFILE OR CONFIG_DRIVERS_NOTELOWEROUT)
  list(APPEND SRCS notestream_driver.c)
endif()

if(CONFIG_DRIVERS_NOTERAM)
  if(NOT "${CONFIG_DRIVERS_NOTERAM_SECTION}" STREQUAL "")
    target_compile_definitions(drivers PRIVATE -DDRIVERS_NOTERAM_SECTION)
  endif()

  list(APPEND SRCS noteram_driver.c)
endif()

if(CONFIG_DRIVERS_NOTELOG)
  list(APPEND SRCS notelog_driver.c)
endif()

if(CONFIG_DRIVERS_NOTECTL)
  list(APPEND SRCS notectl_driver.c)
endif()

if(CONFIG_DRIVERS_NOTESNAP)
  list(APPEND SRCS notesnap_driver.c)
endif()

if(CONFIG_DRIVERS_NOTEITM)
  list(APPEND SRCS noteitm_driver.c)
endif()

if(CONFIG_DRIVERS_NOTERPMSG_SERVER)
  list(APPEND SRCS noterpmsg_server.c)
endif()

if(CONFIG_DRIVERS_NOTERPMSG)
  list(APPEND SRCS noterpmsg_driver.c)
endif()

# Disable instrumentation for note driver to avoid infinite recursion.
# sched_note_event_ip is called by __cyg_profile_func_enter/exit callbacks.
if(CONFIG_ARCH_INSTRUMENT_ALL AND SRCS)
  nuttx_add_aux_library(note_aux ${SRCS})
  target_include_directories(note_aux PRIVATE ${NUTTX_DIR}/sched)
  target_compile_options(note_aux PRIVATE -fno-instrument-functions)
  target_link_libraries(drivers PRIVATE note_aux)
else()
  target_sources(drivers PRIVATE ${SRCS})
endif()

target_include_directories(drivers PRIVATE ${NUTTX_DIR}/sched)
