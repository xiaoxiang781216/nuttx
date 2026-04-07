/****************************************************************************
 * libs/libc/misc/lib_crc16xor.c
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

/* CRC16 XOR checksum with automatic alignment and loop unrolling.
 * Uses uint64_t reads with 8x unrolling (64 bytes per iteration).
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <sys/types.h>
#include <stdint.h>

#include <nuttx/crc16.h>

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: crc16xor
 *
 * Description:
 *   Return a 16-bit XOR checksum. Uses uint64_t reads with automatic
 *   alignment handling and 8x loop unrolling for maximum throughput.
 *
 ****************************************************************************/

uint16_t crc16xor(FAR const uint8_t *src, size_t len)
{
  uint64_t sum = 0;
  FAR const uint64_t *p;

  /* Align to 8-byte boundary */

  while (((uintptr_t)src & 7) && len)
    {
      sum ^= *src++;
      len--;
    }

  /* Main loop: uint64_t reads, 64 bytes per iteration */

  p = (FAR const uint64_t *)src;
  while (len >= 64)
    {
      sum ^= p[0]; sum ^= p[1]; sum ^= p[2]; sum ^= p[3];
      sum ^= p[4]; sum ^= p[5]; sum ^= p[6]; sum ^= p[7];
      p += 8;
      len -= 64;
    }

  while (len >= 8)
    {
      sum ^= *p++;
      len -= 8;
    }

  /* Handle remaining bytes */

  src = (FAR const uint8_t *)p;
  while (len--)
    {
      sum ^= *src++;
    }

  sum ^= (sum >> 32);
  return (uint16_t)(sum ^ (sum >> 16));
}
