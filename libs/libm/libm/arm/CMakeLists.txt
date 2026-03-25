# ##############################################################################
# libs/libm/libm/arm/CMakeLists.txt
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

if(CONFIG_ARM_LIBM)
  set(SRCS)

  if(CONFIG_LIBM_ARCH_FABSF)
    list(APPEND SRCS arch_fabsf.c)
  endif()

  if(CONFIG_LIBM_ARCH_SQRTF)
    list(APPEND SRCS arch_sqrtf.c)
  endif()

  if(CONFIG_LIBM_ARCH_FABS)
    list(APPEND SRCS arch_fabs.c)
  endif()

  if(CONFIG_LIBM_ARCH_SQRT)
    list(APPEND SRCS arch_sqrt.c)
  endif()

  if(CONFIG_LIBM_ARCH_CEIL)
    list(APPEND SRCS arch_ceil.c)
  endif()

  if(CONFIG_LIBM_ARCH_CEILF)
    list(APPEND SRCS arch_ceilf.c)
  endif()

  if(CONFIG_LIBM_ARCH_FLOOR)
    list(APPEND SRCS arch_floor.c)
  endif()

  if(CONFIG_LIBM_ARCH_FLOORF)
    list(APPEND SRCS arch_floorf.c)
  endif()

  if(CONFIG_LIBM_ARCH_NEARBYINT)
    list(APPEND SRCS arch_nearbyint.c)
  endif()

  if(CONFIG_LIBM_ARCH_NEARBYINTF)
    list(APPEND SRCS arch_nearbyintf.c)
  endif()

  if(CONFIG_LIBM_ARCH_RINTF)
    list(APPEND SRCS arch_rintf.c)
  endif()

  if(CONFIG_LIBM_ARCH_ROUNDF)
    list(APPEND SRCS arch_roundf.c)
  endif()

  if(CONFIG_LIBM_ARCH_TRUNCF)
    list(APPEND SRCS arch_truncf.c)
  endif()

  if(CONFIG_LIBM_ARCH_RINT)
    list(APPEND SRCS arch_rint.c)
  endif()

  if(CONFIG_LIBM_ARCH_ROUND)
    list(APPEND SRCS arch_round.c)
  endif()

  if(CONFIG_LIBM_ARCH_TRUNC)
    list(APPEND SRCS arch_trunc.c)
  endif()

  if(CONFIG_LIBM_ARCH_FMINF)
    list(APPEND SRCS arch_fminf.c)
  endif()

  if(CONFIG_LIBM_ARCH_FMIN)
    list(APPEND SRCS arch_fmin.c)
  endif()

  if(CONFIG_LIBM_ARCH_FMAXF)
    list(APPEND SRCS arch_fmaxf.c)
  endif()

  if(CONFIG_LIBM_ARCH_FMAX)
    list(APPEND SRCS arch_fmax.c)
  endif()

  target_sources(c PRIVATE ${SRCS})
endif()
