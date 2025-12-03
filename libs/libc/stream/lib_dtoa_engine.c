/****************************************************************************
 * libs/libc/stream/lib_dtoa_engine.c
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

#include <math.h>

#include <sys/param.h>

#include "lib_dtoa_engine.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* A bit of CPP trickery -- construct the floating-point value 10 ** DBL_DIG
 * by pasting the value of DBL_DIG onto '1e' to
 */

/* Green hills #define DBL_DIG (6) or #define DBL_DIG (15)
 * we need remove "()" here
 */

#if DBL_DIG == 6
#  undef DBL_DIG
#  define DBL_DIG 6
#endif

#if DBL_DIG == 15
#  undef DBL_DIG
#  define DBL_DIG 15
#endif

#if DBL_MANT_DIG == 24
#  undef DBL_MANT_DIG
#  define DBL_MANT_DIG 24
#endif

#if DBL_MANT_DIG == 53
#  undef DBL_MANT_DIG
#  define DBL_MANT_DIG 53
#endif

#define PASTE(a)          1e##a
#define SUBSTITUTE(a)     PASTE(a)
#define MIN_MANT          (SUBSTITUTE(DBL_DIG))
#define MAX_MANT          (10.0 * MIN_MANT)
#define MIN_MANT_INT      ((uint64_t)MIN_MANT)
#define MIN_MANT_EXP      DBL_DIG

#define HEX_PASTE(a)      0x1p+##a
#define HEX_SUBSTITUTE(a) HEX_PASTE(a)
#define HEX_MAX_MANT      (HEX_SUBSTITUTE(DBL_MANT_DIG) / 2.0)
#define HEX_MIN_MANT      (HEX_MAX_MANT / 16.0)
#define HEX_MIN_MANT_INT  ((uint64_t)HEX_MIN_MANT)
#define HEX_MIN_MANT_EXP  (DBL_MANT_DIG - 5)

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int __dtoa_engine(double x, FAR struct dtoa_s *dtoa, int max_digits,
                  int max_decimals, int radix)
{
  int32_t exp = 0;
  uint8_t flags = 0;
  int i;

  if (x < 0)
    {
      flags |= DTOA_MINUS;
      x = -x;
    }

  if (x == 0)
    {
      flags |= DTOA_ZERO;
      for (i = 0; i < max_digits; i++)
        dtoa->digits[i] = '0';
    }
  else if (isnan(x))
    {
      flags |= DTOA_NAN;
    }
  else if (isinf(x))
    {
      flags |= DTOA_INF;
    }
  else
    {
      double y;
      int scale_up_num = (radix == 10 ?
                          DTOA_SCALE_UP_NUM : DTOA_HEX_SCALE_UP_NUM);
      int scale_down_num = (radix == 10 ?
                            DTOA_SCALE_DOWN_NUM : DTOA_HEX_SCALE_DOWN_NUM);
      double max_mant = (radix == 10 ? MAX_MANT : HEX_MAX_MANT);
      double min_mant = (radix == 10 ? MIN_MANT : HEX_MIN_MANT);
      uint64_t min_mant_int = (radix == 10 ?
                               MIN_MANT_INT : HEX_MIN_MANT_INT);
      const double *scale_up = (radix == 10 ?
                                g_dtoa_scale_up : g_dtoa_hex_scale_up);
      const double *scale_down = (radix == 10 ?
                                  g_dtoa_scale_down : g_dtoa_hex_scale_down);
      const double *round = (radix == 10 ? g_dtoa_round : g_dtoa_hex_round);
      int32_t exp_step = (radix == 10 ? 1 : 4);

      exp = radix == 10 ? MIN_MANT_EXP : HEX_MIN_MANT_EXP;

      /* Bring x within range MIN_MANT <= x < MAX_MANT or
       * HEX_MIN_MANT <= x < HEX_MAX_MANT while computing
       * exponent value
       */

      if (x < min_mant)
        {
          for (i = scale_up_num - 1; i >= 0; i--)
            {
              y = x * scale_up[i];
              if (y < max_mant)
                {
                  x = y;
                  exp -= (1 << i);
                }
            }
        }
      else
        {
          for (i = scale_down_num - 1; i >= 0; i--)
            {
              y = x * scale_down[i];
              if (y >= min_mant)
                {
                  x = y;
                  exp += (1 << i);
                }
            }
        }

      /* If limiting decimals, then limit the max digits to no more than the
       * number of digits left of the decimal plus the number of digits right
       * of the decimal. If the integer value is 0, there are only values to
       * the right of the decimal point in dtoa->digits.
       */

      if (max_decimals != 0 && radix == 10)
        {
          max_digits = MIN(max_digits, max_decimals + MAX(exp + 1, 0));
        }

      /* Only for float in fixed-point form, If limiting decimals, and
       * precision is 0. Keep the integer part of floating-point numbers
       * or one significant digit.
       */

      if (max_decimals == 0 && max_digits == 0)
        {
          max_digits = MAX(exp + 1, 1);
        }

      /* Round nearest by adding 1/2 of the last digit before converting to
       * int. Check for overflow and adjust mantissa and exponent values
       */

      x = x + round[max_digits];

      if (x >= max_mant)
        {
          x /= (double)radix;
          exp += exp_step;
        }

      /* Now convert mantissa to decimal. */

      uint64_t mant = (uint64_t)x;
      uint64_t decimal = min_mant_int;

      /* Compute digits */

      for (i = 0; i < max_digits; i++)
        {
          dtoa->digits[i] = g_dtoa_hex_table[mant / decimal];
          mant %= decimal;
          decimal /= radix;
        }
    }

  dtoa->digits[max_digits] = '\0';
  dtoa->flags = flags;
  dtoa->exp = exp;
  return max_digits;
}
