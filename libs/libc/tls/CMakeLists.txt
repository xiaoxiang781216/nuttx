# ##############################################################################
# libs/libc/tls/CMakeLists.txt
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

set(SRCS tls_getinfo.c)

if(NOT CONFIG_TLS_TASK_NELEM EQUAL 0)
  list(APPEND SRCS task_tls.c task_tls_destruct.c)
endif()

if(NOT CONFIG_TLS_NELEM EQUAL 0)
  list(APPEND SRCS tls_destruct.c)
endif()

if(NOT CONFIG_TLS_NCLEANUP EQUAL 0)
  list(APPEND SRCS tls_cleanup.c)
endif()

# Override libgcc's single-threaded __emutls_get_address for GCC toolchains
# compiled with --disable-tls (e.g. bare-metal RISC-V/ARM GCC). Use --wrap to
# redirect calls to our multi-thread-safe implementation without causing
# multiple definition errors with libgcc's strong symbols.
if("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU"
   AND NOT CONFIG_SCHED_THREAD_LOCAL
   AND CONFIG_TLS_NELEM GREATER 0
   AND CONFIG_TLS_TASK_NELEM GREATER 0)
  list(APPEND SRCS tls_emutls.c)
  target_link_options(nuttx PRIVATE -Wl,--wrap=__emutls_get_address
                      -Wl,--wrap=__emutls_register_common)
endif()

target_sources(c PRIVATE ${SRCS})
