/****************************************************************************
 * arch/zsp/include/float.h
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

#ifndef __ARCH_ZSP_INCLUDE_FLOAT_H
#define __ARCH_ZSP_INCLUDE_FLOAT_H

/* ZSP NanoPlus: long double == double (64-bit) */

#define LDBL_MANT_DIG   __LDBL_MANT_DIG__
#define LDBL_DIG        __LDBL_DIG__
#define LDBL_MIN_EXP    __LDBL_MIN_EXP__
#define LDBL_MIN_10_EXP __LDBL_MIN_10_EXP__
#define LDBL_MAX_EXP    __LDBL_MAX_EXP__
#define LDBL_MAX_10_EXP __LDBL_MAX_10_EXP__
#define LDBL_MIN        __LDBL_MIN__
#define LDBL_MAX        __LDBL_MAX__
#define LDBL_EPSILON    __LDBL_EPSILON__

#endif /* __ARCH_ZSP_INCLUDE_FLOAT_H */
