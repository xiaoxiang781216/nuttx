/****************************************************************************
 * include/crypto/asn1.h
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

#ifndef __INCLUDE_CRYPTO_ASN1_H
#define __INCLUDE_CRYPTO_ASN1_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <errno.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define ASN1_INTEGER           0x02
#define ASN1_SEQUENCE          0x10
#define ASN1_CONSTRUCTED       0x20

#define BYTE_0(x)              ((uint8_t)((x)        & 0xff))
#define BYTE_1(x)              ((uint8_t)(((x) >> 8) & 0xff))

/****************************************************************************
 * Inline Functions
 ****************************************************************************/

static inline_function
int asn1_write_tag(FAR unsigned char **p,
                   FAR const unsigned char *start,
                   unsigned char tag)
{
  if (*p - start < 1)
    {
      return -EINVAL;
    }

  *--(*p) = tag;

  return 1;
}

static inline_function
int asn1_write_len(FAR unsigned char **p,
                   FAR const unsigned char *start,
                   size_t len)
{
  if (len < 0x80)
    {
      if (*p - start < 1)
        {
          return -EINVAL;
        }

      *--(*p) = (unsigned char)len;
      return 1;
    }

  if (len <= 0xff)
    {
      if (*p - start < 2)
        {
          return -EINVAL;
        }

      *--(*p) = (unsigned char)len;
      *--(*p) = 0x81;
      return 2;
    }

  if (len <= 0xffff)
    {
      if (*p - start < 3)
        {
          return -EINVAL;
        }

      *--(*p) = BYTE_0(len);
      *--(*p) = BYTE_1(len);
      *--(*p) = 0x82;
      return 3;
    }

  return -EINVAL;
}

static inline_function
int asn1_write_int(FAR unsigned char **p,
                   FAR const unsigned char *start,
                   int val)
{
  int len = 0;

  do
    {
      if (*p - start < 1)
        {
          return -EINVAL;
        }

      len += 1;
      *--(*p) = val & 0xff;
      val >>= 8;
    }
  while (val > 0);

  if (**p & 0x80)
    {
      if (*p - start < 1)
        {
          return -EINVAL;
        }

      *--(*p) = 0x00;
      len += 1;
    }

  len += asn1_write_len(p, start, len);
  len += asn1_write_tag(p, start, ASN1_INTEGER);
  return len;
}

static inline_function
int asn1_write_mpi(FAR unsigned char **p,
                   FAR const unsigned char *start,
                   FAR unsigned char *mpi, size_t mpilen)
{
  int len = mpilen;
  int i;

  /* DER represents 0 with a sign bit (0=nonnegative) and 7 value bits, not
   * as 0 digits. We need to end up with 020100, not with 0200.
   */

  if (len == 0)
    {
      len = 1;
    }

  if (*p < start || (size_t)(*p - start) < len)
    {
      return -ENOMEM;
    }

  (*p) -= len;

  /* convert to big-endian */

  for (i = 0; i < len; i++)
    {
      (*p)[len - i - 1] = mpi[i];
    }

  if (**p & 0x80)
    {
      if (*p - start < 1)
        {
          return -ENOMEM;
        }

      (*p)--;
      *(*p) = 0x00;
      len++;
    }

  len += asn1_write_len(p, start, len);
  len += asn1_write_tag(p, start, ASN1_INTEGER);
  return len;
}

#endif /* __INCLUDE_CRYPTO_ASN1_H */
