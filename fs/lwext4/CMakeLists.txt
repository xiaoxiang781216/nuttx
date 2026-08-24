# ##############################################################################
# fs/lwext4/CMakeLists.txt
#
# Licensed to the Apache Software Foundation (ASF) under one or more contributor
# license agreements.  See the NOTICE file distributed with this work for
# additional information regarding copyright ownership.  The ASF licenses this
# file to you under the Apache License, Version 2.0 (the "License"); you may not
# use this file except in compliance with the License. You may obtain a copy of
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

if(CONFIG_FS_LWEXT4)
  if(NOT EXISTS ${CMAKE_CURRENT_LIST_DIR}/lwext4)
    set(LWEXT4_VERSION 58bcf89a121b72d4fb66334f1693d3b30e4cb9c5)

    FetchContent_Declare(
      lwext4
      URL https://github.com/gkostka/lwext4/archive/${LWEXT4_VERSION}.tar.gz
          SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/lwext4 BINARY_DIR
          ${CMAKE_BINARY_DIR}/fs/lwext4/lwext4)
    FetchContent_Populate(lwext4)
  endif()

  set(LWEXT4_DIR ${CMAKE_CURRENT_LIST_DIR}/lwext4)

  # Add VFS adapter and lwext4 sources
  target_sources(
    fs
    PRIVATE lwext4_vfs.c
            ${LWEXT4_DIR}/src/ext4.c
            ${LWEXT4_DIR}/src/ext4_balloc.c
            ${LWEXT4_DIR}/src/ext4_bcache.c
            ${LWEXT4_DIR}/src/ext4_bitmap.c
            ${LWEXT4_DIR}/src/ext4_block_group.c
            ${LWEXT4_DIR}/src/ext4_blockdev.c
            ${LWEXT4_DIR}/src/ext4_crc32.c
            ${LWEXT4_DIR}/src/ext4_debug.c
            ${LWEXT4_DIR}/src/ext4_dir.c
            ${LWEXT4_DIR}/src/ext4_dir_idx.c
            ${LWEXT4_DIR}/src/ext4_extent.c
            ${LWEXT4_DIR}/src/ext4_fs.c
            ${LWEXT4_DIR}/src/ext4_hash.c
            ${LWEXT4_DIR}/src/ext4_ialloc.c
            ${LWEXT4_DIR}/src/ext4_inode.c
            ${LWEXT4_DIR}/src/ext4_journal.c
            ${LWEXT4_DIR}/src/ext4_mbr.c
            ${LWEXT4_DIR}/src/ext4_mkfs.c
            ${LWEXT4_DIR}/src/ext4_super.c
            ${LWEXT4_DIR}/src/ext4_trans.c
            ${LWEXT4_DIR}/src/ext4_xattr.c)

  # Add include directories
  target_include_directories(fs PRIVATE ${CMAKE_CURRENT_LIST_DIR}
                                        ${LWEXT4_DIR}/include)

  # Pull in the lwext4 configuration through the library's own hook: with
  # CONFIG_USE_DEFAULT_CFG=0, ext4_config.h includes "generated/ext4_config.h"
  # (resolved from the fs/lwext4 include path added below to
  # fs/lwext4/generated/ext4_config.h) before applying its #ifndef defaults.
  # That header maps the NuttX Kconfig options (HAVE_OWN_OFLAGS, debug level,
  # journal, xattr, block cache, large file, ...) onto the lwext4 CONFIG_*
  # macros.  USE_DEFAULT_CFG itself must stay a compile definition because it
  # gates that include, i.e. it is tested before the header is read.
  target_compile_definitions(fs PRIVATE CONFIG_USE_DEFAULT_CFG=0)

  # Suppress upstream lwext4 warnings only on the sources that emit them.
  # -Wno-format applies where ext4_dbg() prints uint32_t with %x.
  set_source_files_properties(
    ${LWEXT4_DIR}/src/ext4.c ${LWEXT4_DIR}/src/ext4_balloc.c
    ${LWEXT4_DIR}/src/ext4_dir_idx.c ${LWEXT4_DIR}/src/ext4_fs.c DIRECTORY ..
    PROPERTIES COMPILE_FLAGS "-Wno-shadow")

  set_source_files_properties(${LWEXT4_DIR}/src/ext4_mkfs.c DIRECTORY ..
                              PROPERTIES COMPILE_FLAGS "-Wno-format")

endif()
