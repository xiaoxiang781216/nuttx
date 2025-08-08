# ##############################################################################
# cmake/nuttx_protected_symbol_share.cmake
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

get_property(SYMBOL_LD GLOBAL PROPERTY SHARE_SYMBOL_LD)
if(SYMBOL_LD)
  get_property(SHARE_SECTIONS GLOBAL PROPERTY SHARE_SYMBOL_SECTION)
  separate_arguments(SHARE_SECTIONS_LIST NATIVE_COMMAND "${SHARE_SECTIONS}")
  set(SHARE_SECTION_ARGS "")
  foreach(section ${SHARE_SECTIONS_LIST})
    list(APPEND SHARE_SECTION_ARGS ${section})
  endforeach()

  if(CONFIG_ARCH_TOOLCHAIN_TASKING)
    list(APPEND SHARE_SECTION_ARGS "--tasking")
  endif()

  set(NUTTX_ELF $<TARGET_FILE:nuttx>)
  add_custom_target(
    share_symbols COMMAND ${CMAKE_SOURCE_DIR}/tools/symbolshare.py -e
                          ${NUTTX_ELF} -l ${SYMBOL_LD} -s ${SHARE_SECTION_ARGS})
  if(NOT CONFIG_ARCH_TOOLCHAIN_TASKING)
    add_dependencies(user_ldscript_tmp nuttx)
    add_dependencies(share_symbols user_ldscript_tmp)
  else()
    add_dependencies(share_symbols nuttx)
  endif()
  add_dependencies(nuttx_user share_symbols)
endif()
