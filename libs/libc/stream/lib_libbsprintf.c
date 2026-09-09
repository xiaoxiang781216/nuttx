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
 * Pre-processor Definitions
 ****************************************************************************/

#ifdef CONFIG_STREAM_BSPRINTF_PREFIX
#  define BSPRINTF_PREFIX "[" CONFIG_STREAM_BSPRINTF_PREFIX "]"
#else
#  define BSPRINTF_PREFIX ""
#endif

/* The prefix is stored the way it is printed, so a reader copies that field
 * out as is.  An image that does not name itself stores an empty field
 * rather than an empty pair of brackets.  The sizeof() folds at compile
 * time, so only one of the two literals survives.
 */

#define BSPRINTF_PREFIX_LEN sizeof(BSPRINTF_PREFIX)

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: lib_bsprintf_args
 *
 * Description:
 *  Pack the arguments only, either by the per-argument codes of the type
 *  word or by parsing the conversions of fmt when the type word is zero.
 *
 ****************************************************************************/

static ssize_t lib_bsprintf_args(FAR void *buffer, size_t size,
                                 uint64_t type, FAR const IPTR char *fmt,
                                 va_list va)
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
  int prec = -1;
  char c;

  if (type != 0)
    {
      size_t i;

      for (i = 0; i < PRINTF_MAX_ARGS; i++)
        {
          uint8_t argtype = PRINTF_GET_TYPE(type, i);

          if (argtype == PRINTF_TERM)
            {
              break;
            }

          var = (FAR union var *)&buff[next];
          switch (argtype)
            {
              case PRINTF_32BIT:
                if (next + sizeof(var->i) > length)
                  {
                    return -ENOMEM;
                  }

                var->i = va_arg(va, int);
                next += sizeof(var->i);
                break;

              case PRINTF_64BIT:
                if (next + sizeof(var->ll) > length)
                  {
                    return -ENOMEM;
                  }

                var->ll = va_arg(va, long long);
                next += sizeof(var->ll);
                break;

              case PRINTF_STRING:
                {
                  FAR const char *s = va_arg(va, FAR const char *);
                  size_t len;

                  if (s == NULL)
                    {
                      s = "(null)";
                    }

                  len = strlen(s) + 1;
                  if (next + len > length)
                    {
                      return -ENOMEM;
                    }

                  strlcpy(buff + next, s, len);
                  next += len;
                }
                break;
            }
        }

      return next;
    }

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
      else if (c == '.')
        {
          prec = 0;
        }
      else if (c >= '0' && c <= '9')
        {
          if (prec >= 0)
            {
              prec = prec * 10 + (c - '0');
            }
        }
      else if (c == '*')
        {
          var->i = va_arg(va, int);
          if (prec >= 0)
            {
              prec = var->i;
            }

          next += sizeof(var->i);
        }
      else if (c == 's')
        {
          size_t len;
          var->s = va_arg(va, FAR char *);
          if (var->s == NULL)
            {
              var->s = "null";
            }

          /* A precision (".", either "*" or a literal count) bounds the
           * string, so the source need not be NUL terminated; strnlen then
           * avoids reading past the caller's buffer for a raw chunk.
           */

          if (prec >= 0)
            {
              len = strnlen(var->s, prec) + 1;
            }
          else
            {
              len = strlen(var->s) + 1;
            }

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

              if (next >= length)
                {
                  return -ENOMEM;
                }

              va_list copy;
              va_copy(copy, *vaf->va);
              ret = lib_bsprintf_args(buff + next, length - next,
                                      0, vaf->fmt, copy);
              va_end(copy);
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

      /* Precision applies to a single directive, so drop it once the
       * conversion has been consumed (infmt back to false).
       */

      if (!infmt)
        {
          prec = -1;
        }
    }

  return next;
}

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
 *  By default the arguments are packed alone: the caller keeps the format
 *  string addressable and hands its address to the reader.  Under
 *  STREAM_BSPRINTF_EMBED_FORMAT the record is made self contained instead:
 *
 *    buffer: [prefix\0][fmt\0][arguments...]
 *
 *  and a reader is told so by a NULL format string passed to lib_osprintf().
 *  Enable it whenever the reader cannot resolve an address of this image:
 *  another core (noterpmsg) or another image that inherits a retained buffer
 *  (a boot stage handing its records to the image it jumps to).  The prefix
 *  comes from STREAM_BSPRINTF_PREFIX and is stored the way it is printed; an
 *  image that does not name itself stores an empty prefix, so a record
 *  always decodes the same way whether or not it names its source.
 *
 *  A self contained record is always packed by parsing fmt, so its encoding
 *  does not depend on the type word the caller derived at compile time.
 *
 * Input Parameters:
 *   buffer - Where to pack the record
 *   size   - Size of buffer
 *   type   - Packed per-argument type codes, 0 to parse fmt instead
 *   fmt    - Format string
 *   va     - Format arguments
 *
 * Returned Value:
 *   Number of bytes packed on success; negated errno on failure.
 *
 ****************************************************************************/

ssize_t lib_bsprintf(FAR void *buffer, size_t size, uint64_t type,
                     FAR const IPTR char *fmt, va_list va)
{
  FAR char *buf = buffer;
  size_t hdrlen = 0;
  ssize_t ret;

  /* When the caller already supplied a compile-time type word (strip format
   * mode), the arguments are packed by that word and the reader recovers
   * fmt from npt_fmt, so only the prefix is embedded.  When type is zero,
   * the reader has no type word, so fmt must be embedded and parsed.
   */

  if (size < BSPRINTF_PREFIX_LEN + 1)
    {
      return -ENOMEM;
    }

  memcpy(buf, BSPRINTF_PREFIX, BSPRINTF_PREFIX_LEN);
  hdrlen = BSPRINTF_PREFIX_LEN;

#ifdef CONFIG_STREAM_BSPRINTF_EMBED_FORMAT
  if (type == 0)
    {
      size_t fmtlen;

      /* Truncate fmt instead of rejecting the record: this may run from the
       * syslog path, where losing the tail of a message is better than
       * losing all of it, and an assert would recurse.
       */

      fmtlen = strnlen(fmt, size - BSPRINTF_PREFIX_LEN - 1) + 1;
      memcpy(buf + BSPRINTF_PREFIX_LEN, fmt, fmtlen - 1);
      buf[BSPRINTF_PREFIX_LEN + fmtlen - 1] = '\0';

      hdrlen = BSPRINTF_PREFIX_LEN + fmtlen;

      /* The reader recovers fmt from the record and parses it, so pack the
       * arguments by parsing the very same copy: a truncated format string
       * then simply drops its trailing arguments on both sides.
       */

      fmt = buf + BSPRINTF_PREFIX_LEN;
    }
#endif

  ret = lib_bsprintf_args(buf + hdrlen, size - hdrlen, type, fmt, va);
  if (ret < 0)
    {
      return ret;
    }

  return hdrlen + ret;
}
