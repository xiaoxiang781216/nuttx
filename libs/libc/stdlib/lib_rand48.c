/****************************************************************************
 * libs/libc/stdlib/lib_rand48.c
 *
 * SPDX-License-Identifier: Apache-2.0
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

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>
#include <stdlib.h>
#include <string.h>

#include <nuttx/lib/lib.h>
#include <nuttx/tls.h>

/****************************************************************************
 * Private Data
 ****************************************************************************/

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#ifdef CONFIG_HAVE_LONG_LONG
static uint64_t rand48_step(FAR unsigned short int *xi,
                            FAR unsigned short int *lc)
{
  uint64_t a;
  uint64_t x;

  x = xi[0] | ((xi[1] + 0ul) << 16) | ((xi[2] + 0ull) << 32);
  a = lc[0] | ((lc[1] + 0ul) << 16) | ((lc[2] + 0ull) << 32);
  x = a * x + lc[3];

  xi[0] = x;
  xi[1] = x >> 16;
  xi[2] = x >> 32;
  return x & 0xffffffffffffull;
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: srand48
 ****************************************************************************/

void srand48(long seed)
{
  unsigned short int p[3];

  p[0] = 0x330e;
  p[1] = seed;
  p[2] = seed >> 16;
  seed48(p);
}

/****************************************************************************
 * Name: seed48
 ****************************************************************************/

FAR unsigned short int *seed48(FAR unsigned short int seed16v[3])
{
  FAR struct task_info_s *info = task_get_info();
  static unsigned short int p[3];

  memcpy(p, info->ta_seed48, sizeof(p));
  memcpy(info->ta_seed48, seed16v, sizeof(p));
  return p;
}

/****************************************************************************
 * Name: lcong48
 ****************************************************************************/

void lcong48(FAR unsigned short int p[7])
{
  FAR struct task_info_s *info = task_get_info();

  memcpy(info->ta_seed48, p, sizeof(info->ta_seed48));
}

/****************************************************************************
 * Name: jrand48
 *
 * Description:
 *   Return signed long integers uniformly distributed over the
 *   interval [-2^31, 2^31).
 *
 ****************************************************************************/

#ifdef CONFIG_HAVE_LONG_LONG
long jrand48(FAR unsigned short int s[3])
{
  FAR struct task_info_s *info = task_get_info();

  return (long)(rand48_step(s, info->ta_seed48 + 3) >> 16);
}

/****************************************************************************
 * Name: mrand48
 *
 * Description:
 *   Return signed long integers uniformly distributed over the
 *   interval [-2^31, 2^31).
 *
 ****************************************************************************/

long mrand48(void)
{
  FAR struct task_info_s *info = task_get_info();

  return jrand48(info->ta_seed48);
}

/****************************************************************************
 * Name: nrand48
 *
 * Description:
 *   Return nonnegative long integers  uniformly  distributed over the
 *   interval [0, 2^31).
 *
 ****************************************************************************/

long nrand48(FAR unsigned short int s[3])
{
  FAR struct task_info_s *info = task_get_info();

  return rand48_step(s, info->ta_seed48 + 3) >> 17;
}

/****************************************************************************
 * Name: lrand48
 *
 * Description:
 *   Return nonnegative long integers  uniformly  distributed over the
 *   interval [0, 2^31).
 *
 ****************************************************************************/

long lrand48(void)
{
  FAR struct task_info_s *info = task_get_info();

  return nrand48(info->ta_seed48);
}

/****************************************************************************
 * Name: erand48
 *
 * Description:
 *   Return nonnegative double-precision floating-point values uniformly
 *   distributed over the interval [0.0, 1.0).
 *
 ****************************************************************************/

#  ifdef CONFIG_HAVE_DOUBLE
double erand48(FAR unsigned short int s[3])
{
  FAR struct task_info_s *info = task_get_info();
  union
    {
      uint64_t u;
      double f;
    } x;

  x.u = 0x3ff0000000000000ull | rand48_step(s, info->ta_seed48 + 3) << 4;
  return x.f - 1.0;
}

/****************************************************************************
 * Name: drand48
 *
 * Description:
 *   Return nonnegative double-precision floating-point values uniformly
 *   distributed over the interval [0.0, 1.0).
 *
 ****************************************************************************/

double drand48(void)
{
  FAR struct task_info_s *info = task_get_info();

  return erand48(info->ta_seed48);
}
#  endif
#endif
