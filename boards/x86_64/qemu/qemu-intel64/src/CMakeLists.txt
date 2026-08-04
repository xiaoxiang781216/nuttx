# ##############################################################################
# boards/x86_64/qemu/qemu-intel64/src/CMakeLists.txt
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

set(SRCS qemu_boot.c qemu_bringup.c qemu_net.c)

if(NOT CONFIG_BUILD_KERNEL)
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/romfs_boot.c)
    list(APPEND SRCS romfs_boot.c)
  else()
    list(APPEND SRCS romfs_stub.c)
  endif()
endif()

if(CONFIG_BOARDCTL)
  list(APPEND SRCS qemu_appinit.c)
endif()

if(CONFIG_BOARDCTL_RESET)
  list(APPEND SRCS qemu_reset.c)
endif()

if(CONFIG_BUILD_KERNEL)
  # Pack the standalone application binaries into the ROMFS image registered by
  # qemu_boardinit.c. The application targets are not known yet when this
  # directory is configured, so the dependency is resolved at generation time
  # from the nuttx target property. The nuttx_add_romfs helper cannot be used
  # here: its library is added to apps_post, which the application targets
  # depend on (cycle).
  add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/romfs_boot.c
    COMMAND genromfs -f romfs.img -d ${CMAKE_BINARY_DIR}/bin -V NuttXBootVol
    COMMAND xxd -i romfs.img romfs_boot.c
    DEPENDS $<TARGET_PROPERTY:nuttx,NUTTX_LOADABLE_APPS>
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
  # The board target is defined in a parent directory and cannot consume a
  # source generated here (GENERATED is directory scoped at this CMake policy
  # level), so the image goes into its own kernel library next to board.
  # nuttx_add_kernel_library() does not register the target as an apps_post
  # dependency, which would close a cycle with the application targets.
  nuttx_add_kernel_library(board_romfs ${CMAKE_CURRENT_BINARY_DIR}/romfs_boot.c)
endif()

target_sources(board PRIVATE ${SRCS})

set_property(GLOBAL PROPERTY LD_SCRIPT "${NUTTX_BOARD_DIR}/scripts/qemu.ld")
