/****************************************************************************
 * libs/libc/gdbstub/internal.h
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

#ifndef __LIBS_LIBC_GDBSTUB_INTERNAL_H
#define __LIBS_LIBC_GDBSTUB_INTERNAL_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <assert.h>
#include <stdint.h>
#include <sys/types.h>

#include <nuttx/gdbstub.h>
#include <nuttx/streams.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifdef CONFIG_LIB_GDBSTUB_DEBUG
#  define GDB_DEBUG(state, ...) \
          lib_sprintf((FAR struct lib_outstream_s *)&(state)->stream, \
                      ##__VA_ARGS__)
#  define GDB_ASSERT() __assert(__FILE__, __LINE__, 0)
#else
#  define GDB_DEBUG(...)
#  define GDB_ASSERT() do {} while (0)
#endif

#define GDB_PKTBUF_SIZE CONFIG_LIB_GDBSTUB_PKTSIZE

/* Calculate remaining space in packet from ptr_next position. */

#define gdb_remaining_len(state) ((state)->pkt_len - \
                                  ((state)->pkt_next - (state)->pkt_buf))

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct gdb_state_s
{
  uint8_t running_regs[XCPTCONTEXT_SIZE]; /* Registers of running thread */
#ifdef CONFIG_LIB_GDBSTUB_DEBUG
  struct lib_syslograwstream_s stream;
#endif
  gdb_send_func_t send;                   /* Send buffer to gdb */
  gdb_recv_func_t recv;                   /* Recv buffer from gdb */
  FAR void *priv;                         /* Private data for transport */
  gdb_monitor_func_t monitor;             /* Monitor will be called when gdb
                                           * receive a monitor command
                                           */
  int last_stopreason;                    /* Last stop reason */
  FAR void *last_stopaddr;                /* Last stop address */
  pid_t pid;                              /* Current NuttX thread pid */
  FAR char *pkt_next;                     /* Pointer to next byte in packet */
  char pkt_buf[GDB_PKTBUF_SIZE];          /* Packet buffer */
  size_t pkt_len;                         /* Packet send and receive length */
  FAR void *xcpregs;                      /* Pointer to the registers in context. */
};

struct gdb_debugpoint_s
{
  int type;
  FAR void *addr;
  size_t size;
  debug_callback_t callback;
  FAR void *arg;
};

typedef CODE ssize_t (*gdb_format_func_t)(FAR void *buf, size_t buf_len,
                                          FAR const void *data,
                                          size_t data_len);

/****************************************************************************
 * Public Function Prototypes - Packet Functions (pkt.c)
 ****************************************************************************/

/****************************************************************************
 * Name: gdb_putchar
 *
 * Description:
 *   Put a character to the GDB server.
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

int gdb_putchar(FAR struct gdb_state_s *state, int ch, FAR char *csum);

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

int gdb_getchar(FAR struct gdb_state_s *state);

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

int gdb_send_packet(FAR struct gdb_state_s *state);

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

int gdb_recv_packet(FAR struct gdb_state_s *state);

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
                    FAR const void *data, size_t data_len);

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
                    FAR const void *data, size_t data_len);

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

FAR void *gdb_memcpy(FAR void *dest, FAR const void *src, size_t len);

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

int gdb_expect_separator(FAR struct gdb_state_s *state, char c);

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

int gdb_expect_integer(FAR struct gdb_state_s *state, FAR uintptr_t *arg);

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
                           FAR uintptr_t *addr, FAR size_t *length);

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

int gdb_send_ok_packet(FAR struct gdb_state_s *state);

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

int gdb_send_error_packet(FAR struct gdb_state_s *state,
                          unsigned char error);

/****************************************************************************
 * Name: gdb_is_valid_region
 *
 * Description:
 *   Check if the address is in the memory region.
 *
 * Input Parameters:
 *   state - The pointer to the GDB state structure.
 *   addr  - The address to check.
 *   len   - The length of the region.
 *   flags - The flags for the region (PF_R, PF_W, etc.).
 *
 * Returned Value:
 *   true if the region is valid, false otherwise.
 *
 ****************************************************************************/

bool gdb_is_valid_region(FAR struct gdb_state_s *state,
                         uintptr_t addr, size_t len, uint32_t flags);

/****************************************************************************
 * Public Function Prototypes - Command Functions (cmd.c)
 ****************************************************************************/

/****************************************************************************
 * Name: gdb_update_regcache
 *
 * Description:
 *   Get the registers of the specified task.
 *
 * Input Parameters:
 *   state   - The pointer to the GDB state structure.
 *
 ****************************************************************************/

void gdb_update_regcache(FAR struct gdb_state_s *state);

/****************************************************************************
 * Name: gdb_read_registers
 *
 * Description:
 *   Read Registers Command Format: g.
 *
 ****************************************************************************/

int gdb_read_registers(FAR struct gdb_state_s *state);

/****************************************************************************
 * Name: gdb_write_registers
 *
 * Description:
 *   Write Registers Command Format: G XX...
 *
 ****************************************************************************/

int gdb_write_registers(FAR struct gdb_state_s *state);

/****************************************************************************
 * Name: gdb_read_register
 *
 * Description:
 *   Read a Register Command Format: p n.
 *
 ****************************************************************************/

int gdb_read_register(FAR struct gdb_state_s *state);

/****************************************************************************
 * Name: gdb_write_register
 *
 * Description:
 *   Write a Register Command Format: P n.
 *
 ****************************************************************************/

int gdb_write_register(FAR struct gdb_state_s *state);

/****************************************************************************
 * Name: gdb_read_memory
 *
 * Description:
 *   Read Memory Command Format: m addr,length
 *
 ****************************************************************************/

int gdb_read_memory(FAR struct gdb_state_s *state);

/****************************************************************************
 * Name: gdb_read_bin_memory
 *
 * Description:
 *   Read Memory (Binary) Command Format: x addr,length
 *
 ****************************************************************************/

int gdb_read_bin_memory(FAR struct gdb_state_s *state);

/****************************************************************************
 * Name: gdb_write_memory
 *
 * Description:
 *   Write Memory Command Format: M addr,length:XX..
 *
 ****************************************************************************/

int gdb_write_memory(FAR struct gdb_state_s *state);

/****************************************************************************
 * Name: gdb_write_bin_memory
 *
 * Description:
 *   Write Memory (Binary) Command Format: X addr,length:XX..
 *
 ****************************************************************************/

int gdb_write_bin_memory(FAR struct gdb_state_s *state);

/****************************************************************************
 * Name: gdb_query
 *
 * Description:
 *   The query packet is used by GDB to request information from the stub.
 *
 ****************************************************************************/

int gdb_query(FAR struct gdb_state_s *state);

/****************************************************************************
 * Name: gdb_is_thread_active
 *
 * Description:
 *   The is thread active packet is used by GDB to check if thread is alive.
 *
 ****************************************************************************/

int gdb_is_thread_active(FAR struct gdb_state_s *state);

/****************************************************************************
 * Name: gdb_thread_context
 *
 * Description:
 *   The thread context packet is used by GDB to set current thread.
 *
 ****************************************************************************/

int gdb_thread_context(FAR struct gdb_state_s *state);

/****************************************************************************
 * Name: gdb_send_signal_packet
 *
 * Description:
 *   Send a signal packet (S AA).
 *
 ****************************************************************************/

int gdb_send_signal_packet(FAR struct gdb_state_s *state,
                           unsigned char signal);

/****************************************************************************
 * Name: gdb_send_stop
 *
 * Description:
 *   Send stop reply packet.
 *
 ****************************************************************************/

int gdb_send_stop(FAR struct gdb_state_s *state, int stopreason,
                  FAR void *stopaddr);

#ifdef CONFIG_ARCH_HAVE_DEBUG

/****************************************************************************
 * Name: gdb_debugpoint
 *
 * Description:
 *   Handle breakpoint/watchpoint commands.
 *
 ****************************************************************************/

int gdb_debugpoint(FAR struct gdb_state_s *state, bool enable);

/****************************************************************************
 * Name: gdb_step
 *
 * Description:
 *   Handle single step command.
 *
 ****************************************************************************/

int gdb_step(FAR struct gdb_state_s *state);

/****************************************************************************
 * Name: gdb_continue
 *
 * Description:
 *   Handle continue command.
 *
 ****************************************************************************/

int gdb_continue(FAR struct gdb_state_s *state);

#endif /* CONFIG_ARCH_HAVE_DEBUG */

#endif /* __LIBS_LIBC_GDBSTUB_INTERNAL_H */
