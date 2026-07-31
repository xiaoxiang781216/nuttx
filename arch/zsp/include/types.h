/****************************************************************************
 * arch/zsp/include/types.h
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

#ifndef __ARCH_ZSP_INCLUDE_TYPES_H
#define __ARCH_ZSP_INCLUDE_TYPES_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

/****************************************************************************
 * Type Declarations
 ****************************************************************************/

#ifndef __ASSEMBLY__

typedef signed char        _int8_t;
typedef unsigned char      _uint8_t;

typedef signed short       _int16_t;
typedef unsigned short     _uint16_t;

typedef signed int         _int32_t;
typedef unsigned int       _uint32_t;

typedef signed long long   _int64_t;
typedef unsigned long long _uint64_t;
#define __INT64_DEFINED

typedef _int64_t           _intmax_t;
typedef _uint64_t          _uintmax_t;

typedef int                _wchar_t;
typedef int                _wint_t;
typedef int                _wctype_t;

#if defined(__SIZE_TYPE__)
#define unsigned signed
typedef __SIZE_TYPE__      _ssize_t;
#undef unsigned
typedef __SIZE_TYPE__      _size_t;
#else
typedef signed int         _ssize_t;
typedef unsigned int       _size_t;
#endif

/* This is the size of the interrupt state save returned by up_irq_save() */

typedef unsigned int       irqstate_t;

#endif /* __ASSEMBLY__ */

#endif /* __ARCH_ZSP_INCLUDE_TYPES_H */
