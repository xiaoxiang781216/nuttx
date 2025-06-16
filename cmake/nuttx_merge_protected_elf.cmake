# ##############################################################################
# cmake/nuttx_merge_protected_elf.cmake
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

get_property(MERGE_LD GLOBAL PROPERTY PROTECTED_MERGE_LD)

# if PROTECTED_MERGE_LD is set then merge the elfs
if(MERGE_LD)
  set(K_DIR "merge_k_dir")
  set(U_DIR "merge_u_dir")

  # are we setting CMAKE_LD?
  if(NOT CMAKE_LD)
    message(
      FATAL_ERROR
        "Please check Toolchain file , Make sure CMAKE_LD is set correctly")
  endif()

  get_filename_component(LD_SCRIPT_NAME ${MERGE_LD} NAME)
  set(LD_SCRIPT_TMP "${CMAKE_BINARY_DIR}/${LD_SCRIPT_NAME}.tmp")

  nuttx_generate_preprocess_target(SOURCE_FILE ${MERGE_LD} TARGET_FILE
                                   ${LD_SCRIPT_TMP})

  add_custom_target(merge_ldscript_tmp DEPENDS ${LD_SCRIPT_TMP})

  get_property(MERGE_LINK_OPTIONS GLOBAL PROPERTY PROTECTED_MERGE_LINK_OPTIONS)
  # are we using linker or compiler?
  if(NOT "${CMAKE_LD}" MATCHES "gcc$")
    set(USE_LINKER True)
  endif()

  get_property(SKIP_SECTION GLOBAL PROPERTY PROTECTED_MERGE_SKIP_SECTION)
  if(SKIP_SECTION)
    set(K_SKIP_SECTION "--skip-section ")
    set(U_SKIP_SYMBOL "--skip-symbol ${K_DIR}/remove_prefix_symbols.txt")
    string(JOIN " " SKIP_SECTION_STR ${SKIP_SECTION})
  endif()

  add_library(nuttx_merge_kernel $<TARGET_OBJECTS:nuttx>)
  add_library(nuttx_merge_user $<TARGET_OBJECTS:nuttx_user>)

  foreach(lib ${nuttx_libs})
    list(APPEND nuttx_kernel_libs_paths $<TARGET_FILE:${lib}>)
    list(APPEND nuttx_merged_libs_paths "${K_DIR}/$<TARGET_FILE_NAME:${lib}>")
  endforeach()

  foreach(lib ${nuttx_system_libs})
    list(APPEND nuttx_user_libs_paths $<TARGET_FILE:${lib}>)
    list(APPEND nuttx_merged_libs_paths "${U_DIR}/$<TARGET_FILE_NAME:${lib}>")
  endforeach()
  foreach(lib ${nuttx_apps_libs})
    list(APPEND nuttx_user_libs_paths $<TARGET_FILE:${lib}>)
    list(APPEND nuttx_merged_libs_paths "${U_DIR}/$<TARGET_FILE_NAME:${lib}>")
  endforeach()
  foreach(lib ${nuttx_user_extra_libs})
    list(APPEND nuttx_user_libs_paths $<TARGET_FILE:${lib}>)
    list(APPEND nuttx_merged_libs_paths "${U_DIR}/$<TARGET_FILE_NAME:${lib}>")
  endforeach()

  list(APPEND nuttx_kernel_libs_paths $<TARGET_FILE:nuttx_merge_kernel>)
  list(APPEND nuttx_user_libs_paths $<TARGET_FILE:nuttx_merge_user>)

  list(APPEND nuttx_merged_libs_paths
       "$<$<NOT:$<BOOL:${USE_LINKER}>>:-Wl,>--whole-archive")
  list(APPEND nuttx_merged_libs_paths
       "${U_DIR}/$<TARGET_FILE_NAME:nuttx_merge_user>")
  list(APPEND nuttx_merged_libs_paths
       "${K_DIR}/$<TARGET_FILE_NAME:nuttx_merge_kernel>")

  string(JOIN " " kernel_libs_str ${nuttx_kernel_libs_paths})

  string(JOIN " " user_libs_str ${nuttx_user_libs_paths})

  add_custom_target(
    merge_elf_postbuild ALL
    COMMAND ${CMAKE_COMMAND} -E echo "Merge elf: kernel elf and user elf"
    COMMAND mkdir -p ${K_DIR} ${U_DIR}
    COMMAND
      env OBJCOPY=${CMAKE_OBJCOPY}; env READELF=${CMAKE_READELF}; env
      AR=${CMAKE_AR}; env K_LIBS=${kernel_libs_str}; env
      K_SKIP=${K_SKIP_SECTION};bash -c
      "${CMAKE_SOURCE_DIR}/tools/symbolprefix.sh \$K_SKIP ${SKIP_SECTION_STR} \$K_LIBS ${K_DIR} kernel_ ;"
    COMMAND
      env OBJCOPY=${CMAKE_OBJCOPY}; env READELF=${CMAKE_READELF}; env
      AR=${CMAKE_AR}; env U_LIBS=${user_libs_str}; env U_SKIP=${U_SKIP_SYMBOL}
      ;bash -c
      "${CMAKE_SOURCE_DIR}/tools/symbolprefix.sh \$U_SKIP \$U_LIBS ${U_DIR} user_ ;"
    COMMAND
      ${CMAKE_LD} $<$<NOT:$<BOOL:${USE_LINKER}>>:-Wl,>--print-memory-usage
      $<$<NOT:$<BOOL:${USE_LINKER}>>:-Wl,>--entry=kernel___start -nostdlib
      $<$<NOT:$<BOOL:${USE_LINKER}>>:-Wl,>--gc-sections
      $<$<NOT:$<BOOL:${USE_LINKER}>>:-Wl,>--cref
      $<$<NOT:$<BOOL:${USE_LINKER}>>:-Wl,>-Map=nuttx_merger.map -T
      ${LD_SCRIPT_TMP} ${MERGE_LINK_OPTIONS}
      $<$<NOT:$<BOOL:${USE_LINKER}>>:-Wl,>--start-group
      ${nuttx_merged_libs_paths} $<$<NOT:$<BOOL:${USE_LINKER}>>:-Wl,>--end-group
      -o nuttx_merger.elf
    COMMAND ${CMAKE_COMMAND} -E echo
            "Merge elf generated at ${CMAKE_BINARY_DIR}/nuttx_merger.elf"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DEPENDS nuttx_post merge_ldscript_tmp
    COMMENT "Merging kernel and user ELFs"
    VERBATIM)

  if(TARGET merger_post_build)
    add_dependencies(post_build merger_post_build)
  endif()
endif()
