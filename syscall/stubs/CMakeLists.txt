# ##############################################################################
# syscall/stubs/CMakeLists.txt
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
nuttx_add_kernel_library(stubs)

if(NOT (CONFIG_ARCH_64BIT) AND CONFIG_FS_LARGEFILE)
  # off_t is 64bit, so off_t need be passed as pointer on 32bit arch

  set(OPTIONS -r off_t)
endif()

generate_syscall_files(
  OUTPUT_VAR
  GENERATED_STUBS
  MODE
  STUB
  OUTPATH
  ${CMAKE_CURRENT_BINARY_DIR}
  ALL_SYSCALLS
  ${SYSCALLS}
  CSV_FILES
  ${CMAKE_CURRENT_LIST_DIR}/../syscall.csv
  TYPE_REFERENCES
  ${OPTIONS})

# add sources to target
target_sources(stubs PRIVATE ${GENERATED_STUBS})
