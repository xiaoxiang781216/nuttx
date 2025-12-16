/****************************************************************************
 * libs/libc/stream/lib_libosprintf.c
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

#include <nuttx/streams.h>

#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef enum
{
  ARCH_32BIT,   /* Source data from 32-bit architecture */
  ARCH_64BIT,   /* Source data from 64-bit architecture */
  ARCH_NATIVE   /* Source data from native architecture */
} osprintf_arch_e;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* Helper functions to read architecture-dependent types */

static long read_long_value(FAR const void *ptr, osprintf_arch_e arch)
{
  if (arch == ARCH_32BIT)
    {
      int32_t val;
      memcpy(&val, ptr, sizeof(val));
      return (long)val;
    }
  else if (arch == ARCH_64BIT)
    {
      int64_t val;
      memcpy(&val, ptr, sizeof(val));
      return (long)val;
    }
  else /* ARCH_NATIVE */
    {
      long val;
      memcpy(&val, ptr, sizeof(val));
      return val;
    }
}

static size_t read_sizet_value(FAR const void *ptr, osprintf_arch_e arch)
{
  if (arch == ARCH_32BIT)
    {
      uint32_t val;
      memcpy(&val, ptr, sizeof(val));
      return (size_t)val;
    }
  else if (arch == ARCH_64BIT)
    {
      uint64_t val;
      memcpy(&val, ptr, sizeof(val));
      return (size_t)val;
    }
  else /* ARCH_NATIVE */
    {
      size_t val;
      memcpy(&val, ptr, sizeof(val));
      return val;
    }
}

static ptrdiff_t read_ptrdiff_value(FAR const void *ptr,
                                    osprintf_arch_e arch)
{
  if (arch == ARCH_32BIT)
    {
      int32_t val;
      memcpy(&val, ptr, sizeof(val));
      return (ptrdiff_t)val;
    }
  else if (arch == ARCH_64BIT)
    {
      int64_t val;
      memcpy(&val, ptr, sizeof(val));
      return (ptrdiff_t)val;
    }
  else /* ARCH_NATIVE */
    {
      ptrdiff_t val;
      memcpy(&val, ptr, sizeof(val));
      return val;
    }
}

static FAR void *read_pointer_value(FAR const void *ptr,
                                    osprintf_arch_e arch)
{
  if (arch == ARCH_32BIT)
    {
      uint32_t val;
      memcpy(&val, ptr, sizeof(val));
      return (FAR void *)(uintptr_t)val;
    }
  else if (arch == ARCH_64BIT)
    {
      uint64_t val;
      memcpy(&val, ptr, sizeof(val));
      return (FAR void *)(uintptr_t)val;
    }
  else /* ARCH_NATIVE */
    {
      FAR void *val;
      memcpy(&val, ptr, sizeof(val));
      return val;
    }
}

static size_t get_arch_dependent_size(char type, osprintf_arch_e arch)
{
  /* Returns size of architecture-dependent types based on source arch
   * type: 'l' for long, 'z' for size_t, 't' for ptrdiff_t, 'p' for ptr
   */

  if (arch == ARCH_32BIT)
    {
      return 4;  /* 32-bit architecture */
    }
  else if (arch == ARCH_64BIT)
    {
      return 8;  /* 64-bit architecture */
    }
  else /* ARCH_NATIVE */
    {
      switch (type)
        {
          case 'l':
            return sizeof(long);
          case 'z':
            return sizeof(size_t);
          case 't':
            return sizeof(ptrdiff_t);
          case 'p':
            return sizeof(uintptr_t);
          default:
            return sizeof(int);
        }
    }
}

static int lib_osprintf_internal(FAR struct lib_outstream_s *s,
                                 FAR const IPTR char *fmt,
                                 FAR const void *buf,
                                 FAR size_t *offset,
                                 osprintf_arch_e arch)
{
  begin_packed_struct union
    {
      char c;
      char s[1];
      short int si;
      int i;
      long l;
#ifdef CONFIG_HAVE_LONG_LONG
      long long ll;
#endif
      intmax_t im;
      size_t sz;
      ptrdiff_t pd;
      uintptr_t p;
#ifdef CONFIG_HAVE_DOUBLE
      float f;
      double d;
#  ifdef CONFIG_HAVE_LONG_DOUBLE
      long double ld;
#  endif
#endif
    }

  end_packed_struct *var;
  FAR const char *prec = NULL;
  FAR const char *data = buf;
  char fmtstr[64];
  bool infmt = false;
  size_t ret = 0;
  size_t len = 0;
  char c;

  while ((c = *fmt++) != '\0')
    {
      if (c != '%' && !infmt)
        {
          lib_stream_putc(s, c);
          ret++;
          continue;
        }

      if (!infmt)
        {
          len = 0;
          infmt = true;
          memset(fmtstr, 0, sizeof(fmtstr));
        }

      var = (FAR void *)(data + *offset);
      fmtstr[len++] = c;

      if (c == 'c' || c == 'd' || c == 'i' || c == 'u' ||
          c == 'o' || c == 'x' || c == 'X')
        {
          if (*(fmt - 2) == 'j')
            {
              *offset += sizeof(var->im);
              ret += lib_sprintf(s, fmtstr, var->im);
            }
#ifdef CONFIG_HAVE_LONG_LONG
          else if (*(fmt - 2) == 'l' && *(fmt - 3) == 'l')
            {
              *offset += sizeof(var->ll);
              ret += lib_sprintf(s, fmtstr, var->ll);
            }
#endif
          else if (*(fmt - 2) == 'l')
            {
              long long_val = read_long_value(data + *offset, arch);
              *offset += get_arch_dependent_size('l', arch);
              ret += lib_sprintf(s, fmtstr, long_val);
            }
          else if (*(fmt - 2) == 'z')
            {
              size_t sizet_val = read_sizet_value(data + *offset, arch);
              *offset += get_arch_dependent_size('z', arch);
              ret += lib_sprintf(s, fmtstr, sizet_val);
            }
          else if (*(fmt - 2) == 't')
            {
              ptrdiff_t ptrdiff_val = read_ptrdiff_value(data + *offset,
                                                         arch);
              *offset += get_arch_dependent_size('t', arch);
              ret += lib_sprintf(s, fmtstr, ptrdiff_val);
            }
          else if (*(fmt - 2) == 'h' && *(fmt - 3) == 'h')
            {
              *offset += sizeof(var->c);
              ret += lib_sprintf(s, fmtstr, var->c);
            }
          else if (*(fmt - 2) == 'h')
            {
              *offset += sizeof(var->si);
              ret += lib_sprintf(s, fmtstr, var->si);
            }
          else
            {
              *offset += sizeof(var->i);
              ret += lib_sprintf(s, fmtstr, var->i);
            }

          infmt = false;
        }
      else if (c == 'e' || c == 'f' || c == 'g' || c == 'a' ||
               c == 'A' || c == 'E' || c == 'F' || c == 'G')
        {
#ifdef CONFIG_HAVE_DOUBLE
          if (*(fmt - 2) == 'h')
            {
              *offset += sizeof(var->f);
              ret += lib_sprintf(s, fmtstr, var->f);
            }
#  ifdef CONFIG_HAVE_LONG_DOUBLE
          else if (*(fmt - 2) == 'L')
            {
              *offset += sizeof(var->ld);
              ret += lib_sprintf(s, fmtstr, var->ld);
            }
#  endif
          else
            {
              *offset += sizeof(var->d);
              ret += lib_sprintf(s, fmtstr, var->d);
            }

          infmt = false;
#endif
        }
      else if (c == '*')
        {
          sprintf(fmtstr + len - 1, "%d", var->i);
          len = strlen(fmtstr);
          *offset += sizeof(var->i);
        }
      else if (c == 's')
        {
          if (prec != NULL)
            {
              *offset += strtol(prec, NULL, 10);
              prec = NULL;
            }
          else
            {
              *offset += strlen(var->s) + 1;
            }

          ret += lib_sprintf(s, fmtstr, var->s);
          infmt = false;
        }
      else if (c == 'p')
        {
          infmt = false;
          if (*fmt == 'V')
            {
              size_t off = 0;
              len = strlen(var->s) + 1;
              ret += lib_osprintf_internal(s, var->s, var->s + len, &off,
                                           arch);
              *offset += len + off;
              fmt++;
            }
          else
            {
              FAR void *ptr_val = read_pointer_value(data + *offset,
                                                     arch);
              *offset += get_arch_dependent_size('p', arch);
              ret += lib_sprintf(s, fmtstr, ptr_val);
            }
        }
      else if (c == '.')
        {
          prec = fmt;
        }
    }

  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: lib_osprintf
 *
 * Description:
 *   Parse buffer using native architecture type sizes.
 *
 ****************************************************************************/

int lib_osprintf(FAR struct lib_outstream_s *s,
                 FAR const IPTR char *fmt,
                 FAR const void *buf)
{
  size_t offset = 0;
  return lib_osprintf_internal(s, fmt, buf, &offset, ARCH_NATIVE);
}

/****************************************************************************
 * Name: lib_osprintf_32
 *
 * Description:
 *   Parse buffer assuming data came from a 32-bit CPU architecture.
 *   Architecture-dependent types (long, size_t, ptrdiff_t, pointers)
 *   are treated as 32-bit values.
 *
 ****************************************************************************/

int lib_osprintf_32(FAR struct lib_outstream_s *s,
                    FAR const IPTR char *fmt,
                    FAR const void *buf)
{
  size_t offset = 0;
  return lib_osprintf_internal(s, fmt, buf, &offset, ARCH_32BIT);
}

/****************************************************************************
 * Name: lib_osprintf_64
 *
 * Description:
 *   Parse buffer assuming data came from a 64-bit CPU architecture.
 *   Architecture-dependent types (long, size_t, ptrdiff_t, pointers)
 *   are treated as 64-bit values.
 *
 ****************************************************************************/

int lib_osprintf_64(FAR struct lib_outstream_s *s,
                    FAR const IPTR char *fmt,
                    FAR const void *buf)
{
  size_t offset = 0;
  return lib_osprintf_internal(s, fmt, buf, &offset, ARCH_64BIT);
}
