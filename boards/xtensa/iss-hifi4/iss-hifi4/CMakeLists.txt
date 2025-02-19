# ##############################################################################
# boards/xtensa/iss-hifi4/iss-hifi4/CMakeLists.txt
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

set_property(GLOBAL PROPERTY LD_SCRIPT
                             "${NUTTX_BOARD_DIR}/scripts/elf32xtensa.x")

add_subdirectory(src)

list(APPEND EXTRA_LIB libgcc.a)

get_property(NUTTX_EXTRA_FLAGS GLOBAL PROPERTY NUTTX_EXTRA_FLAGS)
execute_process(
  COMMAND ${CMAKE_C_COMPILER} ${CMAKE_C_FLAG_ARGS} ${NUTTX_EXTRA_FLAGS}
          --show-config=config
  OUTPUT_STRIP_TRAILING_WHITESPACE
  OUTPUT_VARIABLE xt_path)

foreach(lib ${EXTRA_LIB})
  file(GLOB_RECURSE find_lib ${xt_path}/xtensa-elf/lib/${lib}
       ${xt_path}/xtensa-elf/lib/*/${lib})
  nuttx_add_extra_library(${find_lib})
endforeach()
