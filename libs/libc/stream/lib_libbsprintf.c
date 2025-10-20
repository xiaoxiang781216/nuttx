/****************************************************************************
 * libs/libc/stream/lib_libbsprintf.c
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

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: lib_bsprintf
 *
 * Description:
 *  Implements saving sprintf parameters to buffer, which can use
 *  lib_osprintf to restore the data in buffer to a string.
 *
 ****************************************************************************/

ssize_t lib_bsprintf(FAR void *buffer, size_t size,
                     FAR const IPTR char *fmt, va_list va)
{
  begin_packed_struct union var
    {
      int i;
      long l;
#ifdef CONFIG_HAVE_LONG_LONG
      long long ll;
#endif
      intmax_t im;
      size_t sz;
      ptrdiff_t ptr;
      FAR void *p;
      FAR const char *s;
#ifdef CONFIG_HAVE_DOUBLE
      double d;
#  ifdef CONFIG_HAVE_LONG_DOUBLE
      long double ld;
#  endif
#endif
    }

  end_packed_struct *var;
  FAR char *buff = (FAR char *)buffer;
  size_t length = size - 1;
  size_t next = 0;

  FAR const char *p = fmt;
  bool infmt = false;
  char c;

  while ((c = *p++) != '\0')
    {
      if (c != '%' && !infmt)
        {
          continue;
        }

      infmt = true;
      var = (FAR union var *)&buff[next];

      if (c == 'c' || c == 'd' || c == 'i' || c == 'u' ||
          c == 'o' || c == 'x' || c == 'X')
        {
          if (*(p - 2) == 'j')
            {
              if (next + sizeof(var->im) > length)
                {
                  return -ENOMEM;
                }

              var->im = va_arg(va, intmax_t);
              next += sizeof(var->im);
            }
#ifdef CONFIG_HAVE_LONG_LONG
          else if (*(p - 2) == 'l' && *(p - 3) == 'l')
            {
              if (next + sizeof(var->ll) > length)
                {
                  return -ENOMEM;
                }

              var->ll = va_arg(va, long long);
              next += sizeof(var->ll);
            }
#endif
          else if (*(p - 2) == 'l')
            {
              if (next + sizeof(var->l) > length)
                {
                  return -ENOMEM;
                }

              var->l = va_arg(va, long);
              next += sizeof(var->l);
            }
          else if (*(p - 2) == 'z')
            {
              if (next + sizeof(var->sz) > length)
                {
                  return -ENOMEM;
                }

              var->sz = va_arg(va, size_t);
              next += sizeof(var->sz);
            }
          else if (*(p - 2) == 't')
            {
              if (next + sizeof(var->ptr) > length)
                {
                  return -ENOMEM;
                }

              var->ptr = va_arg(va, ptrdiff_t);
              next += sizeof(var->ptr);
            }
          else
            {
              if (next + sizeof(var->i) > length)
                {
                  return -ENOMEM;
                }

              var->i = va_arg(va, int);
              next += sizeof(var->i);
            }

          infmt = false;
        }
      else if (c == 'e' || c == 'f' || c == 'g' || c == 'a' ||
                c == 'A' || c == 'E' || c == 'F' || c == 'G')
        {
#ifdef CONFIG_HAVE_DOUBLE
#  ifdef CONFIG_HAVE_LONG_DOUBLE
          if (*(p - 2) == 'L')
            {
              if (next + sizeof(var->ld) > length)
                {
                  return -ENOMEM;
                }

              var->ld = va_arg(va, long double);
              next += sizeof(var->ld);
            }
          else
#  endif
            {
              if (next + sizeof(var->d) > length)
                {
                  return -ENOMEM;
                }

              var->d = va_arg(va, double);
              next += sizeof(var->d);
            }
#endif

          infmt = false;
        }
      else if (c == '*')
        {
          var->i = va_arg(va, int);
          next += sizeof(var->i);
        }
      else if (c == 's')
        {
          size_t len;
          var->s = va_arg(va, FAR char *);
          len = strlen(var->s) + 1;
          if (next + len > length)
            {
              return -ENOMEM;
            }

          strlcpy(buff + next, var->s, len);
          next += len;
          infmt = false;
        }
      else if (c == 'p')
        {
          infmt = false;
          if (*p == 'V')
            {
              FAR struct va_format *vaf = va_arg(va, FAR struct va_format *);
              size_t len;
              int ret;
              p++;

              /* Copy the format string to the buffer */

              len = strlen(vaf->fmt) + 1;
              if (next + len > length)
                {
                  return -ENOMEM;
                }

              memcpy(buff + next, vaf->fmt, len);
              next += len;

              /* copy the va_list member to the buffer */

#  ifdef va_copy
              va_list copy;
              va_copy(copy, *vaf->va);
              ret = lib_bsprintf(buff + next, length - next,
                                 vaf->fmt, copy);
              va_end(copy);
#  else
              ret = lib_bsprintf(buff + next, length - next,
                                 vaf->fmt, vaf->va);
#  endif
              if (ret < 0)
                {
                  return ret;
                }

              next += ret;
            }
          else
            {
              if (next + sizeof(var->p) > length)
                {
                  return -ENOMEM;
                }

              var->p = va_arg(va, FAR void *);
              next += sizeof(var->p);
            }
        }
    }

  return next;
}
