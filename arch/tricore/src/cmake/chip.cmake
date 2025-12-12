# ##############################################################################
# arch/tricore/src/cmake/tc3xx.cmake
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

set(PLATFORM_FLAGS)

if(CONFIG_ARCH_CHIP_AURIX_TC39X)
  if(CONFIG_TRICORE_TOOLCHAIN_TASKING)
    add_compile_options(--cpu=tc39xb)
    add_link_options(-Ctc39xb)
  else()
    list(APPEND PLATFORM_FLAGS -mcpu=tc39xx -mtc162)
  endif()
elseif(CONFIG_ARCH_CHIP_AURIX_TC4DX)
  if(CONFIG_TRICORE_TOOLCHAIN_TASKING)
    add_compile_options(--misrac-version=2004 --user-mode=hypervisor)
    add_compile_options(--default-near-size=0)
    add_link_options(-Ctc4dx)
  else()
    list(APPEND PLATFORM_FLAGS -mcpu=tc4DAx -mtc18)
  endif()
elseif(CONFIG_ARCH_CHIP_AURIX_TC48X)
  if(CONFIG_TRICORE_TOOLCHAIN_TASKING)
    add_compile_options(--misrac-version=2004 --user-mode=hypervisor)
    add_compile_options(--default-near-size=0)
    add_link_options(-Ctc48x)
  else()
    list(APPEND PLATFORM_FLAGS -mcpu=tc48xx -mtc18)
  endif()
elseif(CONFIG_ARCH_CHIP_AURIX_TC4ZX)
  if(CONFIG_TRICORE_TOOLCHAIN_TASKING)
    add_compile_options(--misrac-version=2004 --user-mode=hypervisor)
    add_compile_options(--default-near-size=0)
    add_link_options(-Ctc4zx)
  else()
    list(APPEND PLATFORM_FLAGS -mcpu=tc4Zxx -mtc18)
  endif()
endif()

add_compile_options(${PLATFORM_FLAGS})
add_link_options(${PLATFORM_FLAGS})
set_property(DIRECTORY PROPERTY TC_PLATFORM_FLAGS "${PLATFORM_FLAGS}")
