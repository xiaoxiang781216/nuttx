# ##############################################################################
# libs/libxx/libcxx/libcxx_test.cmake
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

list(
  APPEND
  FLAGS
  -fconstexpr-steps=5000000
  -Wno-character-conversion
  -Wno-dangling
  -Wno-deprecated-builtins
  -Wno-duplicate-decl-specifier
  -Wno-extern-c-compat
  -Wno-nontrivial-memcall
  -Wno-return-type
  -Wno-self-assign-overloaded
  -Wno-undefined-internal
  -Wno-uninitialized-const-pointer
  -Wno-unused-function
  -Wno-unused-local-typedef
  -Wno-unused-result
  -Wno-unused-variable)

add_compile_options(-UNDEBUG)

add_compile_definitions(
  DISABLE_NEW_COUNT
  _LIBCPP_ABI_ENABLE_SHARED_PTR_TRIVIAL_ABI
  _LIBCPP_ABI_ENABLE_UNIQUE_PTR_TRIVIAL_ABI
  _LIBCPP_DEBUG_RANDOMIZE_UNSPECIFIED_STABILITY
  _LIBCPP_DISABLE_DEPRECATION_WARNINGS
  _LIBCPP_ENABLE_EXPERIMENTAL
  _LIBCPP_ENABLE_THREAD_SAFETY_ANNOTATIONS)

set(TEST_DIR "${CMAKE_CURRENT_LIST_DIR}/libcxx/test")
file(GLOB_RECURSE TESTS "${TEST_DIR}/*.pass.cpp")

# Remove deprecated c++ features and lit selftests
file(
  GLOB_RECURSE
  SKIP_DIRS
  ${TEST_DIR}/std/depr/*.pass.cpp
  ${TEST_DIR}/libcxx/depr/*.pass.cpp
  # hardcoded return 1 in main()
  ${TEST_DIR}/libcxx/selftest/*.pass.cpp)
list(REMOVE_ITEM TESTS ${SKIP_DIRS})

set(UNSUPPORTED_TESTS "")

# Search files whose content matches a regex pattern
function(search_files_by_regex OUT_VAR FILE_LIST PATTERN)
  set(MATCHED "")
  foreach(F ${FILE_LIST})
    file(
      STRINGS ${F} HIT
      REGEX "${PATTERN}"
      LIMIT_COUNT 1)
    if(HIT)
      list(APPEND MATCHED ${F})
    endif()
  endforeach()
  set(${OUT_VAR}
      "${MATCHED}"
      PARENT_SCOPE)
endfunction()

# Skip tests that require specific C++ standards
search_files_by_regex(REQUIRES_STD_FILES "${TESTS}" "REQUIRES:.*c\\+\\+[0-9]")
search_files_by_regex(HAS_OUR_STD "${REQUIRES_STD_FILES}"
                      "REQUIRES:.*c\\+\\+${CPP_STD_VER}")
list(REMOVE_ITEM REQUIRES_STD_FILES ${HAS_OUR_STD})
list(APPEND UNSUPPORTED_TESTS ${REQUIRES_STD_FILES})

# XFAIL/UNSUPPORTED for our C++ standard version and other annotations
search_files_by_regex(
  SKIP
  "${TESTS}"
  "XFAIL:.*c\\+\\+${CPP_STD_VER}|XFAIL: [*]|UNSUPPORTED:.*c\\+\\+${CPP_STD_VER}|UNSUPPORTED:.*!libcpp-has-legacy-debug-mode|REQUIRES:.*libcpp-abi-version=2"
)
list(APPEND UNSUPPORTED_TESTS ${SKIP})

# Skip tests C++20-removed features
if(${CPP_STD_VER} GREATER_EQUAL 20)
  search_files_by_regex(
    SKIP
    "${TESTS}"
    "_LIBCPP_ENABLE_CXX20_REMOVED_TYPE_TRAITS|_LIBCPP_ENABLE_CXX20_REMOVED_NEGATORS|_LIBCPP_ENABLE_CXX20_REMOVED_BINDER_TYPEDEFS|_LIBCPP_ENABLE_CXX20_REMOVED_ALLOCATOR_MEMBERS|_LIBCPP_ENABLE_CXX20_REMOVED_RAW_STORAGE_ITERATOR"
  )
  list(APPEND UNSUPPORTED_TESTS ${SKIP})
endif()

# clang 21 rejects construct_at on const object in constexpr (libcxx-17 compat)
# TODO: to be double checked when libcxx is upgraded to llvm-21.1.8
list(
  APPEND
  UNSUPPORTED_TESTS
  ${TEST_DIR}/std/utilities/memory/specialized.algorithms/specialized.construct/construct_at.pass.cpp
  ${TEST_DIR}/std/utilities/optional/optional.object/optional.object.assign/emplace.pass.cpp
  # clang 21 + libcxx-17: stricter segmented iterator trait detection
  ${TEST_DIR}/libcxx/containers/sequences/deque/segmented_iterator.compile.pass.cpp
  # clang 21 + libcxx-17: stricter segmented iterator trait detection
  ${TEST_DIR}/libcxx/ranges/range.adaptors/range.join/segmented_iterator.compile.pass.cpp
  # Apple-specific RTTI test, not applicable to NuttX
  ${TEST_DIR}/libcxx/language.support/support.rtti/type.info/type_info.comparison.apple.compile.pass.cpp
  # clang 21 -Werror,-Wdeprecated-volatile rejects volatile return type
  ${TEST_DIR}/std/utilities/meta/meta.unary/dependent_return_type.compile.pass.cpp
)

if(CONFIG_CXX_NO_LOCALIZATION OR CONFIG_CXX_MINI_LOCALIZATION)
  file(GLOB_RECURSE LOCALIZATION_SKIP_TESTS
       ${TEST_DIR}/std/localization/*.pass.cpp
       ${TEST_DIR}/libcxx/localization/*.pass.cpp)
  list(APPEND UNSUPPORTED_TESTS ${LOCALIZATION_SKIP_TESTS})
  search_files_by_regex(
    SKIP
    "${TESTS}"
    "UNSUPPORTED:.*no-localization|#include <(ios|istream|ostream|iostream|fstream|sstream|streambuf|locale|regex|strstream)>"
  )
  list(APPEND UNSUPPORTED_TESTS ${SKIP})
else()
  # NuttX only supports C/POSIX locale, skip tests requiring named locales
  search_files_by_regex(
    SKIP
    "${TESTS}"
    "locale\\(\"en_US|locale\\(\"fr_FR|locale\\(\"ru_RU|locale\\(\"zh_CN|locale\\(\"ja_JP|locale\\(\"de_DE|_byname|REQUIRES:.*locale\\."
  )
  list(APPEND UNSUPPORTED_TESTS ${SKIP})
endif()

if(NOT CONFIG_CXX_WCHAR)
  search_files_by_regex(
    SKIP
    "${TESTS}"
    "UNSUPPORTED:.*no-wide-characters|XFAIL:.*no-wide-characters|wchar_t|std::wstring|wformat_args|make_wformat_args"
  )
  list(APPEND UNSUPPORTED_TESTS ${SKIP})
else()
  list(
    APPEND
    UNSUPPORTED_TESTS
    # Windows-specific _wremove/_wfopen APIs not available on NuttX
    ${TEST_DIR}/libcxx/input.output/file.streams/fstreams/fstream.cons/wchar_pointer.pass.cpp
    ${TEST_DIR}/libcxx/input.output/file.streams/fstreams/fstream.members/open_wchar_pointer.pass.cpp
    ${TEST_DIR}/libcxx/input.output/file.streams/fstreams/ofstream.cons/wchar_pointer.pass.cpp
    ${TEST_DIR}/libcxx/input.output/file.streams/fstreams/ofstream.members/open_wchar_pointer.pass.cpp
    # NuttX wchar_t codecvt returns encoding()=0 (UTF-8, variable-width) not 1
    # (fixed-width)
    ${TEST_DIR}/std/localization/locale.categories/category.ctype/locale.codecvt/locale.codecvt.members/wchar_t_encoding.pass.cpp
    # NuttX wchar_t codecvt returns max_length()=4 (UTF-8) not 1
    ${TEST_DIR}/std/localization/locale.categories/category.ctype/locale.codecvt/locale.codecvt.members/wchar_t_max_length.pass.cpp
    # NuttX std::to_wstring (swprintf) output differs from glibc
    ${TEST_DIR}/std/strings/string.conversions/to_wstring.pass.cpp
    # NuttX wfilebuf seekoff position calculation differs from glibc
    ${TEST_DIR}/std/input.output/file.streams/fstreams/filebuf.virtuals/seekoff.pass.cpp
    # NuttX wfilebuf seekoff broken, assert(f.sgetc() == L'2') fails after
    # pubseekoff
    ${TEST_DIR}/std/input.output/file.streams/fstreams/filebuf.assign/member_swap.pass.cpp
    ${TEST_DIR}/std/input.output/file.streams/fstreams/filebuf.assign/move_assign.pass.cpp
    ${TEST_DIR}/std/input.output/file.streams/fstreams/filebuf.assign/nonmember_swap.pass.cpp
    ${TEST_DIR}/std/input.output/file.streams/fstreams/filebuf.cons/move.pass.cpp
    # C++20 str() && rvalue overload: moved-from stringstream not empty on NuttX
    ${TEST_DIR}/std/input.output/string.streams/istringstream/istringstream.members/str.move.pass.cpp
    ${TEST_DIR}/std/input.output/string.streams/ostringstream/ostringstream.members/str.move.pass.cpp
    ${TEST_DIR}/std/input.output/string.streams/stringbuf/stringbuf.members/str.move.pass.cpp
    ${TEST_DIR}/std/input.output/string.streams/stringstream/stringstream.members/str.move.pass.cpp
    # NuttX wchar seekoff position calculation differs from glibc
    ${TEST_DIR}/std/localization/locales/locale.convenience/conversions/conversions.buffer/seekoff.pass.cpp
    # NuttX std::format Unicode column width handling differs from glibc
    ${TEST_DIR}/std/utilities/format/format.functions/ascii.pass.cpp)

  # WINT_MIN/WINT_MAX not defined on NuttX
  search_files_by_regex(SKIP "${TESTS}" "WINT_MIN")
  list(APPEND UNSUPPORTED_TESTS ${SKIP})
endif()

if(NOT CONFIG_CXX_RTTI)
  search_files_by_regex(
    SKIP
    "${TESTS}"
    "UNSUPPORTED:.*no-rtti|XFAIL:.*no-rtti|#include <experimental/memory_resource>"
  )
  list(APPEND UNSUPPORTED_TESTS ${SKIP})
  file(GLOB_RECURSE RTTI_SKIP_TESTS
       ${TEST_DIR}/std/language.support/support.rtti/*.pass.cpp)
  list(APPEND UNSUPPORTED_TESTS ${RTTI_SKIP_TESTS})
endif()

if(CONFIG_CXX_EXCEPTION)
  search_files_by_regex(SKIP "${TESTS}"
                        "ADDITIONAL_COMPILE_FLAGS:.*-fno-exceptions")
  list(APPEND UNSUPPORTED_TESTS ${SKIP})
else()
  search_files_by_regex(SKIP "${TESTS}" "UNSUPPORTED:.*no-exceptions")
  list(APPEND UNSUPPORTED_TESTS ${SKIP})
  # exception_ptr is unimplemented without CXX_EXCEPTION
  file(
    GLOB_RECURSE EXCEPTION_PTR_SKIP
    ${TEST_DIR}/std/language.support/support.exception/except.nested/*.pass.cpp
    ${TEST_DIR}/std/language.support/support.exception/propagation/*.pass.cpp)
  list(APPEND UNSUPPORTED_TESTS ${EXCEPTION_PTR_SKIP})
endif()

# TODO: no sim config built with clang now, to be verified later
if(NOT CONFIG_SIM_ASAN)
  search_files_by_regex(SKIP_ASAN "${TESTS}" "REQUIRES:.*asan")
  list(APPEND UNSUPPORTED_TESTS ${SKIP_ASAN})
endif()
list(APPEND UNSUPPORTED_TESTS ${SKIP})

# Tests requiring ADDITIONAL_COMPILE_FLAGS with -D that we don't apply These
# tests customize abort handlers or assertion modes via compile flags
search_files_by_regex(
  SKIP
  "${TESTS}"
  "ADDITIONAL_COMPILE_FLAGS:.*-D_LIBCPP_VERBOSE_ABORT|ADDITIONAL_COMPILE_FLAGS:.*-D_LIBCPP_AVAILABILITY_CUSTOM_VERBOSE_ABORT|ADDITIONAL_COMPILE_FLAGS:.*-D_LIBCPP_ENABLE_ASSERTIONS=|ADDITIONAL_COMPILE_FLAGS:.*-D_LIBCPP_ENABLE_DEBUG_MODE=|ADDITIONAL_COMPILE_FLAGS:.*-D_LIBCPP_ENABLE_HARDENED_MODE="
)
list(APPEND UNSUPPORTED_TESTS ${SKIP})

# Large types (>4 bytes) of __atomic_* runtime functions are not provided by
# libclang_rt.builtins.a for this ARM target
search_files_by_regex(SKIP_ATOMIC "${TESTS}" "#include \"atomic_helpers\\.h\"")
list(APPEND UNSUPPORTED_TESTS ${SKIP_ATOMIC})

# Skip tests requiring 64-bit pointers or large memory on 32-bit architectures
if(NOT CONFIG_ARCH_64BIT)
  search_files_by_regex(SKIP "${TESTS}"
                        "UNSUPPORTED:.*32-bit-pointer|REQUIRES:.*large_tests")
  list(APPEND UNSUPPORTED_TESTS ${SKIP})
endif()

# Filesystem tests using filesystem_test_helper.h crash on NuttX:
# scoped_test_env calls std::system("mkdir -p"), "chmod -R 777", "rm -rf" and
# fs::canonical() — none work reliably on NuttX (no symlinks, limited shell, no
# chmod on FAT/tmpfs)
search_files_by_regex(SKIP "${TESTS}" "#include \"filesystem_test_helper\\.h\"")
list(APPEND UNSUPPORTED_TESTS ${SKIP})

# Death tests (check_assertion.h) are incompatible with NuttX flat build.
search_files_by_regex(SKIP "${TESTS}" "#include \"check_assertion\\.h\"")
list(APPEND UNSUPPORTED_TESTS ${SKIP})

# Unsupported toolchain features (-fblocks, libdispatch) and ABI flags not set
search_files_by_regex(
  SKIP
  "${TESTS}"
  "REQUIRES:.*has-fblocks|REQUIRES:.*is-lockfree-runtime-function|libdispatch|_LIBCPP_ABI_INCOMPLETE_TYPES_IN_DEQUE|_LIBCPP_DEPRECATED_ABI_DISABLE_PAIR_TRIVIAL_COPY_CTOR"
)
list(APPEND UNSUPPORTED_TESTS ${SKIP})

# Tests with FILE_DEPENDENCIES need test data files not available on NuttX
search_files_by_regex(SKIP "${TESTS}" "FILE_DEPENDENCIES:")
list(APPEND UNSUPPORTED_TESTS ${SKIP})

# Custom operator new/delete in ELF removed by --gc-sections
search_files_by_regex(SKIP "${TESTS}"
                      "void\\* operator new|void operator delete")
list(APPEND UNSUPPORTED_TESTS ${SKIP})

list(
  APPEND
  UNSUPPORTED_TESTS
  # NuttX libc strchr returns char* not const char*
  ${TEST_DIR}/std/strings/c.strings/cstring.pass.cpp
  # 'this' in static member function — GCC extension, clang rejects
  ${TEST_DIR}/std/input.output/string.streams/stringbuf/stringbuf.cons/default.pass.cpp
  # newlib/NuttX stddef.h include chain conflict
  ${TEST_DIR}/std/numerics/cfenv/cfenv.syn/cfenv.pass.cpp
  # newlib lgamma() crashes, _tls_gamma_signgam is _Thread_local but NuttX TLS
  # doesn't relocate it, so lgamma_r dereferences address 0x0
  ${TEST_DIR}/std/numerics/c.math/cmath.pass.cpp
  ${TEST_DIR}/std/numerics/c.math/ctgmath.pass.cpp
  # setitimer(ITIMER_REAL) + SIGALRM not fully supported
  ${TEST_DIR}/libcxx/thread/thread.threads/thread.thread.this/sleep_for.signals.pass.cpp
  # set_new_handler sets global state that persists after ELF unload; second run
  # sees stale handler pointer from previous ELF → crash
  ${TEST_DIR}/std/language.support/support.dynamic/alloc.errors/set.new.handler/set_new_handler.pass.cpp
  # deadlock in single-cpu env e.g. qemu
  ${TEST_DIR}/libcxx/utilities/memory/util.smartptr/race_condition.pass.cpp
  # Timing-sensitive mutex tests fail on slow QEMU emulation (< 200ms
  # thresholds) Still flaky even when run at the begining
  ${TEST_DIR}/std/thread/thread.mutex/thread.mutex.requirements/thread.timedmutex.requirements/thread.timedmutex.recursive/try_lock.pass.cpp
  ${TEST_DIR}/std/thread/thread.mutex/thread.mutex.requirements/thread.timedmutex.requirements/thread.timedmutex.class/try_lock.pass.cpp
  ${TEST_DIR}/std/thread/thread.mutex/thread.mutex.requirements/thread.timedmutex.requirements/thread.timedmutex.class/lock.pass.cpp
  # NuttX printf %a (hex float) precision handling differs from glibc
  ${TEST_DIR}/std/localization/locale.categories/category.numeric/locale.nm.put/facet.num.put.members/put_double.hex.pass.cpp
  ${TEST_DIR}/std/localization/locale.categories/category.numeric/locale.nm.put/facet.num.put.members/put_long_double.hex.pass.cpp
  # NuttX dtoa engine limited to DBL_DIG+2 (17) significant digits
  ${TEST_DIR}/std/localization/locale.categories/category.numeric/locale.nm.put/facet.num.put.members/put_double.pass.cpp
  ${TEST_DIR}/std/localization/locale.categories/category.numeric/locale.nm.put/facet.num.put.members/put_long_double.pass.cpp
  # rand_dist io: float serialization round-trip fails due to NuttX printf
  # precision
  ${TEST_DIR}/std/numerics/rand/rand.dist/rand.dist.samp/rand.dist.samp.discrete/io.pass.cpp
  ${TEST_DIR}/std/numerics/rand/rand.dist/rand.dist.samp/rand.dist.samp.plinear/io.pass.cpp
  # /dev/random not available on QEMU (virtio-rng not probed)
  ${TEST_DIR}/std/numerics/rand/rand.device/ctor.pass.cpp
  # MSVC charconv precision test - platform-specific floating-point formatting
  ${TEST_DIR}/std/utilities/charconv/charconv.msvc/test.pass.cpp
  # NuttX wcstol/wcstod cast wchar_t* to char* and call strtol/strtod
  # (lib_wcstol.c), which breaks for multi-char wide strings on little-endian
  # (e.g. L"-10" → "-\0\0\0")
  ${TEST_DIR}/std/strings/string.conversions/stod.pass.cpp
  ${TEST_DIR}/std/strings/string.conversions/stof.pass.cpp
  ${TEST_DIR}/std/strings/string.conversions/stoi.pass.cpp
  ${TEST_DIR}/std/strings/string.conversions/stol.pass.cpp
  ${TEST_DIR}/std/strings/string.conversions/stold.pass.cpp
  ${TEST_DIR}/std/strings/string.conversions/stoll.pass.cpp
  ${TEST_DIR}/std/strings/string.conversions/stoul.pass.cpp
  ${TEST_DIR}/std/strings/string.conversions/stoull.pass.cpp
  # std::visit generates 67K+ ELF sections which exceeds NuttX ELF loader limit
  ${TEST_DIR}/std/utilities/variant/variant.visit/visit.pass.cpp
  ${TEST_DIR}/std/utilities/variant/variant.visit/visit_return_type.pass.cpp
  # Link-time override of __libcpp_verbose_abort doesn't work in MODULE m
  ${TEST_DIR}/libcxx/assertions/customize_verbose_abort.link-time.pass.cpp
  # NuttX strerror returns "Error N" and error category mapping differs from
  # glibc
  ${TEST_DIR}/std/diagnostics/syserr/syserr.errcat/syserr.errcat.objects/*.pass.cpp
  ${TEST_DIR}/std/diagnostics/syserr/syserr.syserr/syserr.syserr.members/*.pass.cpp
)

# set_default_resource sets global state (kernel-side static) that persists
# after ELF unload; same issue as set_new_handler — dangling pointer to unloaded
# ELF memory
search_files_by_regex(SKIP "${TESTS}" "set_default_resource")
list(APPEND UNSUPPORTED_TESTS ${SKIP})

list(REMOVE_DUPLICATES UNSUPPORTED_TESTS)
list(REMOVE_ITEM TESTS ${UNSUPPORTED_TESTS})

# Extract compile-only tests and remove from TESTS
set(COMPILE_ONLY_TESTS ${TESTS})
list(FILTER COMPILE_ONLY_TESTS INCLUDE REGEX ".*\\.compile\\.pass\\.cpp$")
list(FILTER TESTS EXCLUDE REGEX ".*\\.compile\\.pass\\.cpp$")

set(i 0)
set(TEST_NAME_LIST "")
set(RUNNABLE_TEST_LIST "")

foreach(TEST ${TESTS})
  if(EXISTS ${TEST})
    get_filename_component(TEST_NAME ${TEST} NAME_WE)
    get_filename_component(TEST_PARENT_DIR ${TEST} DIRECTORY)
    get_filename_component(TEST_PARENT_NAME ${TEST_PARENT_DIR} NAME)
    foreach(CHARACTER "+" "-" "=" "[]" "." "!")
      string(REPLACE "${CHARACTER}" "_" TEST_NAME "${TEST_NAME}")
      string(REPLACE "${CHARACTER}" "_" TEST_PARENT_NAME "${TEST_PARENT_NAME}")
    endforeach()
    set(FULL_TEST_NAME "${TEST_PARENT_NAME}__${TEST_NAME}_${i}")
    string(LENGTH "${FULL_TEST_NAME}" _NAME_LEN)
    if(_NAME_LEN GREATER CONFIG_NAME_MAX)
      string(LENGTH "_${i}" _SUFFIX_LEN)
      math(EXPR _TRUNC_LEN "${CONFIG_NAME_MAX} - ${_SUFFIX_LEN}")
      string(SUBSTRING "${TEST_PARENT_NAME}__${TEST_NAME}" 0 ${_TRUNC_LEN}
                       _TRUNCATED)
      set(FULL_TEST_NAME "${_TRUNCATED}_${i}")
    endif()

    nuttx_add_application(
      NAME
      ${FULL_TEST_NAME}
      SRCS
      ${TEST}
      STACKSIZE
      204800
      MODULE
      m
      COMPILE_FLAGS
      ${FLAGS})
    list(APPEND TEST_NAME_LIST "${FULL_TEST_NAME}")
    list(APPEND RUNNABLE_TEST_LIST "${FULL_TEST_NAME}")
    math(EXPR i "${i}+1")
  endif()
endforeach()

# Write test names to file for test runner (exclude compile-only tests: no main)
set(TEST_LIST_FILE "${CMAKE_BINARY_DIR}/libcxx_test_list.txt")
list(JOIN RUNNABLE_TEST_LIST "\n" TEST_LIST_CONTENT)
file(WRITE ${TEST_LIST_FILE} "${TEST_LIST_CONTENT}\n")
list(LENGTH RUNNABLE_TEST_LIST TEST_COUNT)
list(LENGTH COMPILE_ONLY_TESTS COMPILE_COUNT)
message(
  STATUS
    "libcxx: ${TEST_COUNT} runnable + ${COMPILE_COUNT} compile-only -> ${TEST_LIST_FILE}"
)

# Compile-only tests: verify compilation without linking into kernel
if(COMPILE_ONLY_TESTS)
  nuttx_add_system_library(libcxx_compile_tests)
  target_sources(libcxx_compile_tests PRIVATE ${COMPILE_ONLY_TESTS})
  target_compile_options(libcxx_compile_tests PRIVATE ${FLAGS})
  # Remove from kernel link (compile-only, no main, would cause duplicate
  # symbols)
  get_property(_sys_libs GLOBAL PROPERTY NUTTX_SYSTEM_LIBRARIES)
  list(REMOVE_ITEM _sys_libs libcxx_compile_tests)
  set_property(GLOBAL PROPERTY NUTTX_SYSTEM_LIBRARIES ${_sys_libs})
  get_property(
    _elf_libs
    TARGET nuttx_global
    PROPERTY NUTTX_ELF_LINK_LIBRARIES)
  list(REMOVE_ITEM _elf_libs $<TARGET_FILE:libcxx_compile_tests>)
  set_property(TARGET nuttx_global PROPERTY NUTTX_ELF_LINK_LIBRARIES
                                            ${_elf_libs})
endif()
