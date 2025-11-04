# ##############################################################################
# cmake/nuttx_remove_lines.cmake
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

set(filename ${CMAKE_ARGV1})
set(search_string ${CMAKE_ARGV2})
message(FATAL_INFO ${filename})
message(FATAL_INFO ${search_string})

file(STRINGS "${filename}" lines)
set(filtered_lines "")

foreach(line IN LISTS lines)
  if(NOT line MATCHES "${search_string}" AND NOT line MATCHES
                                             "^[ \t]*;?[ \t]*$")
    string(REPLACE ";" "__SEP__" escaped_line "${line}")
    list(APPEND filtered_lines "${escaped_line}")
  endif()
endforeach()

string(JOIN "\n" new_file_contents ${filtered_lines})
string(REPLACE "__SEP__" ";" new_file_contents "${new_file_contents}")
file(WRITE "${filename}" "${new_file_contents}\n")
