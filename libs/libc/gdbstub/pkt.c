/****************************************************************************
 * libs/libc/gdbstub/pkt.c
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

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <nuttx/ascii.h>
#include <nuttx/memoryregion.h>

#include "internal.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define REPBIAS 29
#define REPSIZE (255 - REPBIAS)

/****************************************************************************
 * Private Data
 ****************************************************************************/

#ifdef CONFIG_BOARD_MEMORY_RANGE
static const struct memory_region_s g_memory_region[] =
{
  CONFIG_BOARD_MEMORY_RANGE
};
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: gdb_escapechar
 *
 * Description:
 *   Send out a char, do escaping if necessary.
 *   Do note that for RLE encoded data, the length byte should NOT escape.
 *   See gdb src of remote.c remote_target::read_frame
 *
 * Input Parameters:
 *   state   - The pointer to the GDB state structure.
 *   ch      - The character to be sent.
 *   csum    - The checksum.
 *
 ****************************************************************************/

static void gdb_escapechar(FAR struct gdb_state_s *state, char c,
                           FAR char *csum)
{
  if (c == '#' || c == '$' || c == '}' || c == '*')
    {
      gdb_putchar(state, '}', csum);
      gdb_putchar(state, c ^ 0x20, csum); /* See https://sourceware.org/gdb/current/onlinedocs/gdb.html/Overview.html#Binary-Data */
    }
  else
    {
      gdb_putchar(state, c, csum);
    }
}

/****************************************************************************
 * Name: gdb_count_repeat
 *
 * Description:
 *   Get how many bytes are repeated in coming data.
 *
 * Input Parameters:
 *   data     - The buffer containing the encoded data.
 *   data_len - The length of the data to decode.
 *
 * Returned Value:
 *     The number of bytes repeated, at maximum of REPSIZE.
 *
 ****************************************************************************/

static size_t gdb_count_repeat(FAR const char *data, size_t data_len)
{
  char c = data[0];
  size_t i = 1;

  while (i < data_len && data[i] == c && i < REPSIZE)
    {
      i++;
    }

  return i;
}

/****************************************************************************
 * Name: gdb_recv_ack
 *
 * Description:
 *   Receive a packet acknowledgment.
 *
 * Input Parameters:
 *   state - The pointer to the GDB state structure.
 *
 * Returned Value:
 *   0       if an ACK (+) was received
 *   1       if a NACK (-) was received
 *   -EINVAL if a bad response was received
 *
 ****************************************************************************/

static int gdb_recv_ack(FAR struct gdb_state_s *state)
{
  int response;

  switch (response = gdb_getchar(state)) /* Wait for packet ack */
    {
      case '+': /* Packet acknowledged */
        return 0;
      case '-': /* Packet negative acknowledged */
        return 1;
      default: /* Bad response! */
        GDB_DEBUG(state, "received bad packet response: 0x%2x\n", response);
        return -EINVAL;
    }
}

/****************************************************************************
 * Name: gdb_putchar_direct
 *
 * Description:
 *   Send a character to the GDB server direct.
 *
 * Input Parameters:
 *   state   - The pointer to the GDB state structure.
 *   ch      - The character to be sent.
 *   csum    - The checksum pointer (optional).
 *
 * Returned Value:
 *   The character sent to the GDB server.
 *
 ****************************************************************************/

static int gdb_putchar_direct(FAR struct gdb_state_s *state, int ch,
                              FAR char *csum)
{
  unsigned char tmp = ch & 0xff;
  ssize_t ret;

  ret = state->send(state->priv, &tmp, 1);
  if (ret < 0)
    {
      return ret;
    }

  if (csum)
    {
      *csum += tmp;
    }

  return tmp;
}

/****************************************************************************
 * Name: gdb_putchar_flush
 *
 * Description:
 *   Guarantee flushing of the tx buffer.
 *
 * Input Parameters:
 *   state   - The pointer to the GDB state structure.
 *
 * Returned Value:
 *   Error code returned by the send function.
 *
 ****************************************************************************/

static int gdb_putchar_flush(FAR struct gdb_state_s *state)
{
  int ret = 0;

  if (state->tx_pos > 0)
    {
      ret = state->send(state->priv, state->tx_buf, state->tx_pos);
      if (ret < 0)
        {
          return ret;
        }

      state->tx_pos = 0;
    }

  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: gdb_putchar
 *
 * Description:
 *   Buffer a character for the current outgoing packet.
 *
 * Input Parameters:
 *   state   - The pointer to the GDB state structure.
 *   ch      - The character to be buffered.
 *
 * Returned Value:
 *   The buffered character, or a negative value on error.
 *
 ****************************************************************************/

int gdb_putchar(FAR struct gdb_state_s *state, int ch, FAR char *csum)
{
  unsigned char tmp = ch & 0xff;

  if (!state->tx_size)
    {
      return gdb_putchar_direct(state, ch, csum);
    }

  if (state->tx_pos >= state->tx_size)
    {
#ifdef CONFIG_LIB_GDBSTUB_DEBUG
      GDB_DEBUG(state, "Tx buffer overflow\n");
#endif
      return -EOVERFLOW;
    }

  state->tx_buf[state->tx_pos++] = tmp;
  if (csum)
    {
      *csum += tmp;
    }

  return tmp;
}

/****************************************************************************
 * Name: gdb_getchar
 *
 * Description:
 *  Get a character from the GDB server.
 *
 * Input Parameters:
 *   state   - The pointer to the GDB state structure.
 *
 * Returned Value:
 *   The character read from the GDB server.
 *
 ****************************************************************************/

int gdb_getchar(FAR struct gdb_state_s *state)
{
  ssize_t ret;

  if (!state->rx_size)
    {
      char c;
      do
        {
          ret = state->recv(state->priv, &c, 1);
        }
      while (ret == 0);

      return ret > 0 ? c : ret;
    }

  if (state->rx_pos < state->rx_len)
    {
      return state->rx_buf[state->rx_pos++];
    }

  state->rx_pos = 0;
  state->rx_len = 0;

  /* NOTE: The transport may block until data is available. */

  while (state->rx_len == 0)
    {
      ret = state->recv(state->priv,
                        state->rx_buf, state->rx_size);
      if (ret < 0)
        {
          return ret;
        }

      state->rx_len = ret;
    }

  return state->rx_buf[state->rx_pos++];
}

/****************************************************************************
 * Name: gdb_send_packet
 *
 * Description:
 *   Transmits a packet of data.
 *   Packets are of the form: $<packet-data>#<checksum>
 *
 * Input Parameters:
 *   state - The pointer to the GDB state structure.
 *
 * Returned Value:
 *   0       if the packet was transmitted and acknowledged.
 *   1       if the packet was transmitted but not acknowledged.
 *   Negative value on error.
 *
 ****************************************************************************/

int gdb_send_packet(FAR struct gdb_state_s *state)
{
  FAR char *buf = state->pkt_buf;
  size_t len = state->pkt_len;
  char csum = 0;
  size_t i = 0;
  char hex[2];
  int ret;

  ret = gdb_putchar(state, '$', NULL); /* Send packet start, no checksum */
  if (ret < 0)
    {
      return ret;
    }

#ifdef CONFIG_LIB_GDBSTUB_DEBUG
    {
      size_t p;
      GDB_DEBUG(state, "-> ");
      for (p = 0; p < len; p++)
        {
          if (isprint(buf[p]))
            {
              GDB_DEBUG(state, "%c", buf[p]);
            }
          else
            {
              GDB_DEBUG(state, "\\x%02x", buf[p] & 0xff);
            }
        }

      GDB_DEBUG(state, "\n");
    }
#endif

  while (i < len)
    {
      char c = buf[i];
      size_t count = gdb_count_repeat(&buf[i], len - i);

      i += count;

      /* GDB cannot process repeated special characters. */

      if (c == '*' || c == '}' || c == '#' || c == '$')
        {
          while (count--)
            {
              gdb_escapechar(state, c, &csum);
            }

          continue;
        }

      if (count <= 3)
        {
          while (count-- > 0)
            {
              gdb_escapechar(state, c, &csum);
            }

          continue;
        }

      gdb_escapechar(state, c, &csum);  /* The data to repeat */
      count--;

      /* Avoid special repeat count. */

      count += REPBIAS;
      if (count == '$')
        {
          gdb_escapechar(state, c, &csum);
          gdb_escapechar(state, c, &csum);
          count -= 2;
        }
      else if (count == '#' || count == 126)
        {
          gdb_escapechar(state, c, &csum);
          count -= 1;
        }

      gdb_putchar(state, '*', &csum);
      gdb_putchar(state, count, &csum);
    }

  /* Append end marker and checksum */

  gdb_putchar(state, '#', NULL);
  ret = gdb_bin2hex(hex, sizeof(hex), &csum, 1);
  if (ret < 0)
    {
      return ret;
    }

  gdb_putchar(state, hex[0], NULL);
  gdb_putchar(state, hex[1], NULL);

  ret = gdb_putchar_flush(state);
  if (ret < 0)
    {
      return ret;
    }

  return gdb_recv_ack(state);
}

/****************************************************************************
 * Name: gdb_recv_packet
 *
 * Description:
 *   Receives a packet of data, assuming a 7-bit clean connection.
 *
 * Input Parameters:
 *   state   - The pointer to the GDB state structure.
 *
 * Returned Value:
 *   0          if the packet was received.
 *   -EIO       if the packet was not received.
 *   -EOVERFLOW if the packet was too long.
 *   Negative value on error.
 *
 ****************************************************************************/

int gdb_recv_packet(FAR struct gdb_state_s *state)
{
  bool escaping = false;
  char expected;
  char buf[2];
  char csum;
  int ret;

  /* Wait for packet start */

  do
    {
      while (1)
        {
          ret = gdb_getchar(state);
          if (ret < 0)
            {
              return ret;
            }
          else if (ret == '$')
            {
              /* Detected start of packet. */

              break;
            }
          else if (ret == ASCII_ETX)
            {
              state->pkt_buf[0] = ASCII_ETX;
              state->pkt_len = 1;
              return 0;
            }
        }

      /* Read until checksum */

      csum = 0;
      state->pkt_len = 0;
      while (1)
        {
          ret = gdb_getchar(state);
          if (ret < 0) /* Error receiving character */
            {
              return ret;
            }
          else if (ret == '}')
            {
              csum += ret;
              escaping = true;
              continue;
            }
          else if (escaping)
            {
              csum += ret;
              ret ^= 0x20;
              escaping = false;
            }
          else if (ret == '#') /* End of packet */
            {
              break;
            }
          else
            {
              csum += ret;
            }

          /* Normal or escaped data */

          if (state->pkt_len >= sizeof(state->pkt_buf))
            {
              GDB_DEBUG(state, "packet buffer overflow\n");
              return -EOVERFLOW;
            }

          state->pkt_buf[state->pkt_len++] = (char)ret;
        }
    }
  while (state->pkt_len == 0); /* Ignore empty packets */

  /* Receive the checksum (two hex digits). Prefer gdb_getchar() so that any
   * optional read-ahead cache is honored and transports that block until the
   * full requested length is available are not forced to read
   * 2 bytes at once.
   */

  ret = gdb_getchar(state);
  if (ret < 0)
    {
      return ret;
    }

  buf[0] = ret;

  ret = gdb_getchar(state);
  if (ret < 0)
    {
      return ret;
    }

  buf[1] = ret;

#ifdef CONFIG_LIB_GDBSTUB_DEBUG
    {
      size_t p;
      GDB_DEBUG(state, "<- ");
      for (p = 0; p < state->pkt_len; p++)
        {
          if (isprint(state->pkt_buf[p]))
            {
              GDB_DEBUG(state, "%c", state->pkt_buf[p]);
            }
          else
            {
              GDB_DEBUG(state, "\\x%02x", state->pkt_buf[p] & 0xff);
            }
        }

      GDB_DEBUG(state, "\n");
    }
#endif

  ret = gdb_hex2bin(&expected, 1, buf, 2);
  if (ret < 0)
    {
      return ret;
    }

  if (csum != expected) /* Verify */
    {
      GDB_DEBUG(state, "received packet with bad checksum\n");
      gdb_putchar_direct(state, '-', NULL); /* Send packet nack */
      return -EIO;
    }

  gdb_putchar_direct(state, '+', NULL); /* Send packet ack */
  state->pkt_next = state->pkt_buf;
  return 0;
}

/****************************************************************************
 * Name: gdb_bin2hex
 *
 * Description:
 *   Encode data to its hex-value representation in a buffer.
 *
 * Input Parameters:
 *   buf      - The buffer to encode to.
 *   buf_len  - The length of the buffer.
 *   data     - The data to encode.
 *   data_len - The length of the data to encode.
 *
 * Returned Value:
 *    The number of bytes written to buf on success.
 *    -EOVERFLOW if the buffer is too small.
 *
 ****************************************************************************/

ssize_t gdb_bin2hex(FAR void *buf, size_t buf_len,
                    FAR const void *data, size_t data_len)
{
  size_t skip = (uintptr_t)data % 4;
  size_t ret = data_len * 2;
  FAR char *out = buf;
  size_t total;
  uint32_t in;

  if (buf_len < data_len * 2)
    {
      return -EOVERFLOW; /* Buffer too small */
    }

  if (skip != 0)
    {
      data = (FAR const void *)((uintptr_t)data & ~3);
      data_len += skip;
    }

  total = data_len;

  for (size_t i = 0; i < (data_len + 3) / 4; i++)
    {
      in = *((FAR uint32_t *)data + i);

      for (size_t j = 0; j < 4; j++)
        {
          if (skip == 0)
            {
              itoa(in >> 4 & 0xf, out++, 16);
              itoa(in & 0xf, out++, 16);
            }
          else
            {
              skip--;
            }

          in >>= 8;

          if (--total == 0)
            {
              break;
            }
        }
    }

  return ret;
}

/****************************************************************************
 * Name: gdb_hex2bin
 *
 * Description:
 *   Decode data from its hex-value representation to a buffer.
 *
 * Input Parameters:
 *   buf      - The buffer containing the encoded data.
 *   buf_len  - The length of the buffer.
 *   data     - The buffer to store the decoded data.
 *   data_len - The length of the data to decode.
 *
 * Returned Value:
 *   The number of bytes written to data on success.
 *   -EOVERFLOW if the buffer is too small.
 *   Negative value on error.
 *
 ****************************************************************************/

ssize_t gdb_hex2bin(FAR void *buf, size_t buf_len,
                    FAR const void *data, size_t data_len)
{
  FAR const char *in = data;
  FAR char *out = buf;
  size_t pos;

  if (buf_len * 2 < data_len)
    {
      return -EOVERFLOW; /* Buffer too small */
    }

  for (pos = 0; pos < data_len; pos += 2)
    {
      char ch[3] =
        {
          in[pos], in[pos + 1], 0
        };

      set_errno(0);
      out[pos / 2] = strtoul(ch, NULL, 16); /* Decode high nibble */
      if (out[pos / 2] == 0 && get_errno())
        {
          GDB_ASSERT();
          return -get_errno(); /* Buffer contained junk. */
        }
    }

  return data_len / 2;
}

/****************************************************************************
 * Name: gdb_memcpy
 *
 * Description:
 *   Copy data from src to dest.
 *
 * Input Parameters:
 *   dest - The destination buffer.
 *   src  - The source buffer.
 *   len  - The length of the data to copy.
 *
 * Returned Value:
 *   The destination buffer.
 *
 ****************************************************************************/

FAR void *gdb_memcpy(FAR void *dest, FAR const void *src, size_t len)
{
#ifdef CONFIG_MM_KASAN
  FAR char *d = dest;
  FAR const char *s = src;

  while (len--)
    {
      *d++ = *s++;
    }

  return dest;
#else
  return memcpy(dest, src, len);
#endif
}

/****************************************************************************
 * Name: gdb_expect_separator
 *
 * Description:
 *   Check if the next byte in the packet is a separator.
 *
 * Input Parameters:
 *   state - The pointer to the GDB state structure.
 *   c     - The expected separator.
 *
 * Returned Value:
 *   0 on success, -EINVAL on error.
 *
 ****************************************************************************/

int gdb_expect_separator(FAR struct gdb_state_s *state, char c)
{
  if (!state->pkt_next || *state->pkt_next != c)
    {
      return -EINVAL;
    }
  else
    {
      state->pkt_next++;
    }

  return 0;
}

/****************************************************************************
 * Name: gdb_expect_integer
 *
 * Description:
 *   Parse an integer argument from the packet.
 *
 * Input Parameters:
 *   state - The pointer to the GDB state structure.
 *   arg   - The parsed integer argument.
 *
 * Returned Value:
 *   0 on success
 *
 ****************************************************************************/

int gdb_expect_integer(FAR struct gdb_state_s *state, FAR uintptr_t *arg)
{
  size_t len = gdb_remaining_len(state);
  char tmp = state->pkt_next[len];

  state->pkt_next[len] = '\0';
  *arg = strtoul(state->pkt_next, &state->pkt_next, 16);
  state->pkt_next[len] = tmp;
  return 0;
}

/****************************************************************************
 * Name: gdb_expect_addr_length
 *
 * Description:
 *   Parse an address and length argument from the packet.
 *
 * Input Parameters:
 *   state - The pointer to the GDB state structure.
 *   addr  - The parsed address.
 *   length - The parsed length.
 *
 * Returned Value:
 *   0 on success, -EINVAL on error.
 *
 ****************************************************************************/

int gdb_expect_addr_length(FAR struct gdb_state_s *state,
                           FAR uintptr_t *addr, FAR size_t *length)
{
  int ret;

  ret = gdb_expect_integer(state, addr);
  if (ret < 0)
    {
      return ret;
    }

  ret = gdb_expect_separator(state, ',');
  if (ret < 0)
    {
      return ret;
    }

  return gdb_expect_integer(state, length);
}

/****************************************************************************
 * Name: gdb_send_ok_packet
 *
 * Description:
 *   Send OK packet.
 *
 * Input Parameters:
 *   state   - The pointer to the GDB state structure.
 *
 * Returned Value:
 *    Zero on success.
 *    Negative value on error.
 *
 ****************************************************************************/

int gdb_send_ok_packet(FAR struct gdb_state_s *state)
{
  state->pkt_buf[0] = 'O';
  state->pkt_buf[1] = 'K';
  state->pkt_len = 2;

  return gdb_send_packet(state);
}

/****************************************************************************
 * Name: gdb_send_error_packet
 *
 * Description:
 *   Send a error packet (E AA).
 *
 * Input Parameters:
 *   state   - The pointer to the GDB state structure.
 *   error   - The error to send.
 *
 * Returned Value:
 *   Zero on success.
 *   Negative value on error.
 *
 ****************************************************************************/

int gdb_send_error_packet(FAR struct gdb_state_s *state, unsigned char error)
{
  int ret;

  state->pkt_buf[0] = 'E';
  ret = gdb_bin2hex(&state->pkt_buf[1], sizeof(state->pkt_buf) - 1,
                    &error, 1);
  if (ret < 0)
    {
      return ret;
    }

  state->pkt_len = 1 + ret;
  return gdb_send_packet(state);
}

/****************************************************************************
 * Name: gdb_is_valid_region
 *
 * Description:
 *   Check if the address is in the memory region.
 *
 ****************************************************************************/

bool gdb_is_valid_region(FAR struct gdb_state_s *state,
                         uintptr_t addr, size_t len, uint32_t flags)
{
#ifdef CONFIG_BOARD_MEMORY_RANGE
  FAR const struct memory_region_s *region = g_memory_region;

  while (region->start < region->end)
    {
      if (addr >= region->start &&
          addr <= region->end - len &&
          (region->flags & flags) == flags)
        {
          return true;
        }

      region++;
    }

  return false;
#else
  UNUSED(state);
  UNUSED(addr);
  UNUSED(len);
  UNUSED(flags);
  return true;
#endif
}
