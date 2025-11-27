# ##############################################################################
# drivers/aie/ethosu/CMakeLists.txt
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

if(CONFIG_ETHOSU)
  # Keep version/path handling aligned with Make.defs
  set(ETHOSU_VERSION
      "25.05"
      CACHE STRING "Ethos-U core driver version")
  set(ETHOSU_CORE_DRIVER_PATH
      ${CMAKE_CURRENT_SOURCE_DIR}/ethos-u-core-driver-${ETHOSU_VERSION})
  set(ETHOSU_EXTERNAL_GIT ${ETHOSU_CORE_DRIVER_PATH}/.git)

  # Auto-download and extract if driver sources are missing and no local git
  # checkout
  if(NOT EXISTS ${ETHOSU_EXTERNAL_GIT})
    set(ETHOSU_ZIP ethos-u-core-driver-${ETHOSU_VERSION}.zip)
    set(ETHOSU_ZIP_URL
        https://gitlab.arm.com/artificial-intelligence/ethos-u/ethos-u-core-driver/-/archive/${ETHOSU_VERSION}/${ETHOSU_ZIP}
    )
    set(ETHOSU_ZIP_PATH ${CMAKE_CURRENT_SOURCE_DIR}/${ETHOSU_ZIP})

    message(
      STATUS "Ethos-U core driver missing. Downloading: ${ETHOSU_ZIP_URL}")
    file(DOWNLOAD ${ETHOSU_ZIP_URL} ${ETHOSU_ZIP_PATH} SHOW_PROGRESS)

    # Use system unzip to extract, mirroring Make.defs behavior
    find_program(UNZIP_EXECUTABLE unzip)
    if(NOT UNZIP_EXECUTABLE)
      message(
        FATAL_ERROR
          "'unzip' not found. Please install unzip or pre-populate ${ETHOSU_CORE_DRIVER_PATH}."
      )
    endif()
    execute_process(
      COMMAND ${UNZIP_EXECUTABLE} -o ${ETHOSU_ZIP_PATH} -d
              ${CMAKE_CURRENT_SOURCE_DIR} RESULT_VARIABLE UNZIP_RESULT)
    if(NOT UNZIP_RESULT EQUAL 0)
      message(FATAL_ERROR "Failed to unzip ${ETHOSU_ZIP_PATH}")
    endif()

    # Remove the downloaded archive after successful extraction
    file(REMOVE ${ETHOSU_ZIP_PATH})
  endif()

  # Include directories
  target_include_directories(drivers PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

  # CMSIS includes
  target_include_directories(
    drivers
    PRIVATE
      ${CMAKE_SOURCE_DIR}/../apps/mlearning/cmsis/CMSIS_5/CMSIS/Core/Include)

  # External library includes
  target_include_directories(drivers PRIVATE ${ETHOSU_CORE_DRIVER_PATH}/include)

  # Undefine __KERNEL__ if it exists
  target_compile_options(drivers PRIVATE -U__KERNEL__)

  # External library specific definitions
  target_compile_definitions(drivers PRIVATE __CORTEX_M)

  # NPU architecture
  if(CONFIG_ETHOSU_U55)
    target_compile_definitions(drivers PRIVATE ETHOSU55=1 ETHOSU_ARCH=u55)
    target_sources(
      drivers PRIVATE ${ETHOSU_CORE_DRIVER_PATH}/src/ethosu_device_u55_u65.c)
  elseif(CONFIG_ETHOSU_U65)
    target_compile_definitions(drivers PRIVATE ETHOSU65=1 ETHOSU_ARCH=u65)
    target_sources(
      drivers PRIVATE ${ETHOSU_CORE_DRIVER_PATH}/src/ethosu_device_u55_u65.c)
  elseif(CONFIG_ETHOSU_U85)
    target_compile_definitions(drivers PRIVATE ETHOSU85=1 ETHOSU_ARCH=u85)
    target_sources(drivers
                   PRIVATE ${ETHOSU_CORE_DRIVER_PATH}/src/ethosu_device_u85.c)
  endif()

  # NuttX driver sources
  target_sources(drivers PRIVATE ethosu_platform.c ethosu_lowerhalf.c)

  # External driver sources
  target_sources(drivers PRIVATE ${ETHOSU_CORE_DRIVER_PATH}/src/ethosu_driver.c)
  target_sources(drivers PRIVATE ${ETHOSU_CORE_DRIVER_PATH}/src/ethosu_pmu.c)

  # Suppress warnings for the Arm driver translation unit only
  set_source_files_properties(${ETHOSU_CORE_DRIVER_PATH}/src/ethosu_driver.c
                              PROPERTIES COMPILE_FLAGS "-w")

endif()
