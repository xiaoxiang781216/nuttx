# ##############################################################################
# libs/libbuiltin/libunwind/CMakeLists.txt
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

if(CONFIG_LIB_LIBUNWIND)

  set(UNWIND_SRCDIR ${CMAKE_CURRENT_LIST_DIR}/libunwind/src)
  set(UNWIND_INCDIR ${CMAKE_CURRENT_LIST_DIR}/libunwind/include)

  set(UNWIND_CXX_SOURCES ${UNWIND_SRCDIR}/libunwind.cpp)
  set(UNWIND_ASM_SOURCES ${UNWIND_SRCDIR}/UnwindRegistersSave.S
                         ${UNWIND_SRCDIR}/UnwindRegistersRestore.S)

  # ARM32 EHABI for both backtrace and exception unwinding
  if(CONFIG_ARCH_ARM)
    list(APPEND UNWIND_CXX_SOURCES ${UNWIND_SRCDIR}/Unwind-EHABI.cpp)
  endif()

  # DWARF UnwindLevel1: only for C++ exception unwinding on non-ARM32 arches
  if(CONFIG_CXX_EXCEPTION_UNWINDER_LIBUNWIND AND NOT CONFIG_ARCH_ARM)
    list(APPEND UNWIND_C_SOURCES ${UNWIND_SRCDIR}/UnwindLevel1-gcc-ext.c)
    list(APPEND UNWIND_C_SOURCES ${UNWIND_SRCDIR}/UnwindLevel1.c)
  endif()

  set(UNWIND_DEFS _LIBUNWIND_IS_NATIVE_ONLY NDEBUG)
  set(UNWIND_OPTS -Wno-undef -fno-sanitize=all)
  # GCC false positive -Wmaybe-uninitialized in DwarfParser.hpp
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    list(APPEND UNWIND_OPTS -Wno-maybe-uninitialized)
  endif()
  set(UNWIND_CXX_FLAGS "-fno-exceptions -fno-rtti")

  if(CONFIG_ELF AND CONFIG_CXX_EXCEPTION_UNWINDER_LIBUNWIND)
    # for implicit runtime calls (abort, __stack_chk_fail)
    if(CONFIG_ARCH_ARM)
      list(APPEND UNWIND_OPTS -mlong-calls)
    endif()

    # Forces elf to link unwind symbols from libunwind instead of libgcc
    get_property(
      ELF_EXTRA_LIB
      TARGET nuttx_global
      PROPERTY NUTTX_ELF_LINK_EXTRA_LIBRARIES)
    set_property(
      TARGET nuttx_global PROPERTY NUTTX_ELF_LINK_EXTRA_LIBRARIES
                                   $<TARGET_FILE:unwind> ${ELF_EXTRA_LIB})
  endif()

  set_source_files_properties(${UNWIND_CXX_SOURCES}
                              PROPERTIES COMPILE_FLAGS ${UNWIND_CXX_FLAGS})

  # Prevent GCC from inferring noreturn on _Unwind_Resume (GCC builtin
  # ECF_NORETURN) which causes it to skip saving callee-saved registers,
  # breaking ARM EHABI unwinding through the _Unwind_Resume frame.
  if(CONFIG_ARCH_ARM AND CMAKE_C_COMPILER_ID STREQUAL "GNU")
    set_source_files_properties(
      ${UNWIND_SRCDIR}/Unwind-EHABI.cpp
      PROPERTIES COMPILE_FLAGS "${UNWIND_CXX_FLAGS} -fno-ipa-pure-const")
  endif()

  # Link libunwind via --whole-archive in LINK_FLAGS (outside the library group)
  # to guarantee it overrides libgcc's unwind symbols on all boards.
  add_library(unwind)
  add_dependencies(unwind apps_context)
  add_dependencies(apps_post unwind)
  add_dependencies(nuttx unwind)
  nuttx_add_library_internal(unwind)
  target_link_options(nuttx PRIVATE -Wl,--whole-archive $<TARGET_FILE:unwind>
                      -Wl,--no-whole-archive)

  target_include_directories(unwind PRIVATE ${UNWIND_INCDIR} ${UNWIND_SRCDIR})
  target_compile_definitions(unwind PRIVATE ${UNWIND_DEFS})
  target_compile_options(unwind PRIVATE ${UNWIND_OPTS})
  target_sources(unwind PRIVATE ${UNWIND_CXX_SOURCES} ${UNWIND_C_SOURCES}
                                ${UNWIND_ASM_SOURCES})

  # Exclude profiling/coverage instrumentation from libunwind to prevent
  # infinite recursion (profiling hooks may trigger unwind internally).
  if(NOT CONFIG_COVERAGE_NONE)
    if("${CMAKE_C_COMPILER_ID}" MATCHES "Clang")
      target_compile_options(unwind PRIVATE -fno-profile-instr-generate
                                            -fno-coverage-mapping)
    else()
      target_compile_options(unwind PRIVATE -fno-profile-arcs
                                            -fno-test-coverage)
    endif()
  endif()

endif()
