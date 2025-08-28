/****************************************************************************
 * libs/libc/stream/lib_dtoa_data.c
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * SPDX-FileCopyrightText: 2018, Keith Packard. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name NuttX nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "lib_dtoa_engine.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef DBL_MAX_10_EXP
#  error DBL_MAX_10_EXP
#endif

#ifndef DBL_MIN_10_EXP
#  error DBL_MIN_10_EXP
#endif

#ifndef DBL_DIG
#  error DBL_DIG
#endif

/* Make sure the computed sizes of the arrays match the actual sizes
 * by declaring an array which is legal if the sizes match and illegal
 * if they do not
 */

#define count_of(n)             (sizeof (n) / sizeof (n[0]))
#define match(array,size)       (count_of(array) == size)
#define check_match(array,size) (match(array, size) ? 1 : -1)

/****************************************************************************
 * Public Data
 ****************************************************************************/

const double g_dtoa_scale_up[] =
{
#if DBL_MAX_10_EXP >= 1
  1e1,
#endif
#if DBL_MAX_10_EXP >= 2
  1e2,
#endif
#if DBL_MAX_10_EXP >= 4
  1e4,
#endif
#if DBL_MAX_10_EXP >= 8
  1e8,
#endif
#if DBL_MAX_10_EXP >= 16
  1e16,
#endif
#if DBL_MAX_10_EXP >= 32
  1e32,
#endif
#if DBL_MAX_10_EXP >= 64
  1e64,
#endif
#if DBL_MAX_10_EXP >= 128
  1e128,
#endif
#if DBL_MAX_10_EXP >= 256
  1e256,
#endif
#if DBL_MAX_10_EXP >= 512
  1e512,
#endif
#if DBL_MAX_10_EXP >= 1024
  1e1024,
#endif
#if DBL_MAX_10_EXP >= 2048
  1e2048,
#endif
#if DBL_MAX_10_EXP >= 4096
  1e4096,
#endif
#if DBL_MAX_10_EXP >= 8192
  1e8192,
#endif
#if DBL_MAX_10_EXP >= 16384
  1e16384,
#endif
#if DBL_MAX_10_EXP >= 32768
  1e32768,
#endif
#if DBL_MAX_10_EXP >= 65536
  1e65536,
#endif
};

const double g_dtoa_scale_down[] =
{
#if DBL_MIN_10_EXP <= -1
  1e-1,
#endif
#if DBL_MIN_10_EXP <= -2
  1e-2,
#endif
#if DBL_MIN_10_EXP <= -4
  1e-4,
#endif
#if DBL_MIN_10_EXP <= -8
  1e-8,
#endif
#if DBL_MIN_10_EXP <= -16
  1e-16,
#endif
#if DBL_MIN_10_EXP <= -32
  1e-32,
#endif
#if DBL_MIN_10_EXP <= -64
  1e-64,
#endif
#if DBL_MIN_10_EXP <= -128
  1e-128,
#endif
#if DBL_MIN_10_EXP <= -256
  1e-256,
#endif
#if DBL_MIN_10_EXP <= -512
  1e-512,
#endif
#if DBL_MIN_10_EXP <= -1024
  1e-1024,
#endif
#if DBL_MIN_10_EXP <= -2048
  1e-2048,
#endif
#if DBL_MIN_10_EXP <= -4096
  1e-4096,
#endif
#if DBL_MIN_10_EXP <= -8192
  1e-8192,
#endif
#if DBL_MIN_10_EXP <= -16384
  1e-16384,
#endif
#if DBL_MIN_10_EXP <= -32768
  1e-32768,
#endif
#if DBL_MIN_10_EXP <= -65536
  1e-65536,
#endif
};

const double g_dtoa_round[] =
{
#if DBL_DIG >= 30
  5e30,
#endif
#if DBL_DIG >= 29
  5e29,
#endif
#if DBL_DIG >= 28
  5e28,
#endif
#if DBL_DIG >= 27
  5e27,
#endif
#if DBL_DIG >= 26
  5e26,
#endif
#if DBL_DIG >= 25
  5e25,
#endif
#if DBL_DIG >= 24
  5e24,
#endif
#if DBL_DIG >= 23
  5e23,
#endif
#if DBL_DIG >= 22
  5e22,
#endif
#if DBL_DIG >= 21
  5e21,
#endif
#if DBL_DIG >= 20
  5e20,
#endif
#if DBL_DIG >= 19
  5e19,
#endif
#if DBL_DIG >= 18
  5e18,
#endif
#if DBL_DIG >= 17
  5e17,
#endif
#if DBL_DIG >= 16
  5e16,
#endif
#if DBL_DIG >= 15
  5e15,
#endif
#if DBL_DIG >= 14
  5e14,
#endif
#if DBL_DIG >= 13
  5e13,
#endif
#if DBL_DIG >= 12
  5e12,
#endif
#if DBL_DIG >= 11
  5e11,
#endif
#if DBL_DIG >= 10
  5e10,
#endif
#if DBL_DIG >= 9
  5e9,
#endif
#if DBL_DIG >= 8
  5e8,
#endif
#if DBL_DIG >= 7
  5e7,
#endif
#if DBL_DIG >= 6
  5e6,
#endif
#if DBL_DIG >= 5
  5e5,
#endif
#if DBL_DIG >= 4
  5e4,
#endif
#if DBL_DIG >= 3
  5e3,
#endif
#if DBL_DIG >= 2
  5e2,
#endif
#if DBL_DIG >= 1
  5e1,
#endif
#if DBL_DIG >= 0
  5e0,
#endif
};

const double g_dtoa_hex_scale_up[] =
{
#if DBL_MAX_EXP - 1 >= 1
  0x1p+1,
#endif
#if DBL_MAX_EXP - 1 >= 2
  0x1p+2,
#endif
#if DBL_MAX_EXP - 1 >= 4
  0x1p+4,
#endif
#if DBL_MAX_EXP - 1 >= 8
  0x1p+8,
#endif
#if DBL_MAX_EXP - 1 >= 16
  0x1p+16,
#endif
#if DBL_MAX_EXP - 1 >= 32
  0x1p+32,
#endif
#if DBL_MAX_EXP - 1 >= 64
  0x1p+64,
#endif
#if DBL_MAX_EXP - 1 >= 128
  0x1p+128,
#endif
#if DBL_MAX_EXP - 1 >= 256
  0x1p+256,
#endif
#if DBL_MAX_EXP - 1 >= 512
  0x1p+512,
#endif
#if DBL_MAX_EXP - 1 >= 1024
  0x1p+1024,
#endif
#if DBL_MAX_EXP - 1 >= 2048
  0x1p+2048,
#endif
#if DBL_MAX_EXP - 1 >= 4096
  0x1p+4096,
#endif
#if DBL_MAX_EXP - 1 >= 8192
  0x1p+8192,
#endif
#if DBL_MAX_EXP - 1 >= 16384
  0x1p+16384,
#endif
#if DBL_MAX_EXP - 1 >= 32768
  0x1p+32768,
#endif
#if DBL_MAX_EXP - 1 >= 65536
  0x1p+65536,
#endif
};

const double g_dtoa_hex_scale_down[] =
{
#if DBL_MIN_EXP <= -1
  0x1p-1,
#endif
#if DBL_MIN_EXP <= -2
  0x1p-2,
#endif
#if DBL_MIN_EXP <= -4
  0x1p-4,
#endif
#if DBL_MIN_EXP <= -8
  0x1p-8,
#endif
#if DBL_MIN_EXP <= -16
  0x1p-16,
#endif
#if DBL_MIN_EXP <= -32
  0x1p-32,
#endif
#if DBL_MIN_EXP <= -64
  0x1p-64,
#endif
#if DBL_MIN_EXP <= -128
  0x1p-128,
#endif
#if DBL_MIN_EXP <= -256
  0x1p-256,
#endif
#if DBL_MIN_EXP <= -512
  0x1p-512,
#endif
#if DBL_MIN_EXP <= -1024
  0x1p-1024,
#endif
#if DBL_MIN_EXP <= -2048
  0x1p-2048,
#endif
#if DBL_MIN_EXP <= -4096
  0x1p-4096,
#endif
#if DBL_MIN_EXP <= -8192
  0x1p-8192,
#endif
#if DBL_MIN_EXP <= -16384
  0x1p-16384
#endif
#if DBL_MIN_EXP <= -32768
  0x1p-32768
#endif
#if DBL_MIN_EXP <= -65536
  0x1p-65536,
#endif
};
const double g_dtoa_hex_round[] =
{
#if DBL_MANT_DIG >= 129
  0x1p+127,
#endif
#if DBL_MANT_DIG >= 125
  0x1p+123,
#endif
#if DBL_MANT_DIG >= 121
  0x1p+119,
#endif
#if DBL_MANT_DIG >= 117
  0x1p+115,
#endif
#if DBL_MANT_DIG >= 113
  0x1p+111,
#endif
#if DBL_MANT_DIG >= 109
  0x1p+107,
#endif
#if DBL_MANT_DIG >= 105
  0x1p+103,
#endif
#if DBL_MANT_DIG >= 101
  0x1p+99,
#endif
#if DBL_MANT_DIG >= 97
  0x1p+95,
#endif
#if DBL_MANT_DIG >= 93
  0x1p+91,
#endif
#if DBL_MANT_DIG >= 89
  0x1p+87,
#endif
#if DBL_MANT_DIG >= 85
  0x1p+83,
#endif
#if DBL_MANT_DIG >= 81
  0x1p+79,
#endif
#if DBL_MANT_DIG >= 77
  0x1p+75,
#endif
#if DBL_MANT_DIG >= 73
  0x1p+71,
#endif
#if DBL_MANT_DIG >= 69
  0x1p+67,
#endif
#if DBL_MANT_DIG >= 65
  0x1p+63,
#endif
#if DBL_MANT_DIG >= 61
  0x1p+59,
#endif
#if DBL_MANT_DIG >= 57
  0x1p+55,
#endif
#if DBL_MANT_DIG >= 53
  0x1p+51,
#endif
#if DBL_MANT_DIG >= 49
  0x1p+47,
#endif
#if DBL_MANT_DIG >= 45
  0x1p+43,
#endif
#if DBL_MANT_DIG >= 41
  0x1p+39,
#endif
#if DBL_MANT_DIG >= 37
  0x1p+35,
#endif
#if DBL_MANT_DIG >= 33
  0x1p+31,
#endif
#if DBL_MANT_DIG >= 29
  0x1p+27,
#endif
#if DBL_MANT_DIG >= 25
  0x1p+23,
#endif
#if DBL_MANT_DIG >= 21
  0x1p+19,
#endif
#if DBL_MANT_DIG >= 17
  0x1p+15,
#endif
#if DBL_MANT_DIG >= 13
  0x1p+11,
#endif
#if DBL_MANT_DIG >= 9
  0x1p+7,
#endif
#if DBL_MANT_DIG >= 5
  0x1p+3,
#endif
};

const char g_dtoa_hex_table[16] =
{
  '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b',
  'c', 'd', 'e', 'f'
};
/****************************************************************************
 * Public Functions
 ****************************************************************************/
